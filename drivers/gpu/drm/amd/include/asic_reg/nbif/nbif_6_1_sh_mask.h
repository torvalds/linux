/*
 * Copyright (C) 2017  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef _nbif_6_1_SH_MASK_HEADER
#define _nbif_6_1_SH_MASK_HEADER


// addressBlock: bif_cfg_dev0_epf0_bifcfgdecp
//VENDOR_ID
#define VENDOR_ID__VENDOR_ID__SHIFT                                                                           0x0
//DEVICE_ID
#define DEVICE_ID__DEVICE_ID__SHIFT                                                                           0x0
//COMMAND
#define COMMAND__IO_ACCESS_EN__SHIFT                                                                          0x0
#define COMMAND__MEM_ACCESS_EN__SHIFT                                                                         0x1
#define COMMAND__BUS_MASTER_EN__SHIFT                                                                         0x2
#define COMMAND__SPECIAL_CYCLE_EN__SHIFT                                                                      0x3
#define COMMAND__MEM_WRITE_INVALIDATE_EN__SHIFT                                                               0x4
#define COMMAND__PAL_SNOOP_EN__SHIFT                                                                          0x5
#define COMMAND__PARITY_ERROR_RESPONSE__SHIFT                                                                 0x6
#define COMMAND__AD_STEPPING__SHIFT                                                                           0x7
#define COMMAND__SERR_EN__SHIFT                                                                               0x8
#define COMMAND__FAST_B2B_EN__SHIFT                                                                           0x9
#define COMMAND__INT_DIS__SHIFT                                                                               0xa
//STATUS
#define STATUS__INT_STATUS__SHIFT                                                                             0x3
#define STATUS__CAP_LIST__SHIFT                                                                               0x4
#define STATUS__PCI_66_EN__SHIFT                                                                              0x5
#define STATUS__FAST_BACK_CAPABLE__SHIFT                                                                      0x7
#define STATUS__MASTER_DATA_PARITY_ERROR__SHIFT                                                               0x8
#define STATUS__DEVSEL_TIMING__SHIFT                                                                          0x9
#define STATUS__SIGNAL_TARGET_ABORT__SHIFT                                                                    0xb
#define STATUS__RECEIVED_TARGET_ABORT__SHIFT                                                                  0xc
#define STATUS__RECEIVED_MASTER_ABORT__SHIFT                                                                  0xd
#define STATUS__SIGNALED_SYSTEM_ERROR__SHIFT                                                                  0xe
#define STATUS__PARITY_ERROR_DETECTED__SHIFT                                                                  0xf
//REVISION_ID
#define REVISION_ID__MINOR_REV_ID__SHIFT                                                                      0x0
#define REVISION_ID__MAJOR_REV_ID__SHIFT                                                                      0x4
//PROG_INTERFACE
#define PROG_INTERFACE__PROG_INTERFACE__SHIFT                                                                 0x0
//SUB_CLASS
#define SUB_CLASS__SUB_CLASS__SHIFT                                                                           0x0
//BASE_CLASS
#define BASE_CLASS__BASE_CLASS__SHIFT                                                                         0x0
//CACHE_LINE
#define CACHE_LINE__CACHE_LINE_SIZE__SHIFT                                                                    0x0
//LATENCY
#define LATENCY__LATENCY_TIMER__SHIFT                                                                         0x0
//HEADER
#define HEADER__HEADER_TYPE__SHIFT                                                                            0x0
#define HEADER__DEVICE_TYPE__SHIFT                                                                            0x7
//BIST
#define BIST__BIST_COMP__SHIFT                                                                                0x0
#define BIST__BIST_STRT__SHIFT                                                                                0x6
#define BIST__BIST_CAP__SHIFT                                                                                 0x7
//BASE_ADDR_1
#define BASE_ADDR_1__BASE_ADDR__SHIFT                                                                         0x0
//BASE_ADDR_2
#define BASE_ADDR_2__BASE_ADDR__SHIFT                                                                         0x0
//BASE_ADDR_3
#define BASE_ADDR_3__BASE_ADDR__SHIFT                                                                         0x0
//BASE_ADDR_4
#define BASE_ADDR_4__BASE_ADDR__SHIFT                                                                         0x0
//BASE_ADDR_5
#define BASE_ADDR_5__BASE_ADDR__SHIFT                                                                         0x0
//BASE_ADDR_6
#define BASE_ADDR_6__BASE_ADDR__SHIFT                                                                         0x0
//ADAPTER_ID
#define ADAPTER_ID__SUBSYSTEM_VENDOR_ID__SHIFT                                                                0x0
#define ADAPTER_ID__SUBSYSTEM_ID__SHIFT                                                                       0x10
//ROM_BASE_ADDR
#define ROM_BASE_ADDR__BASE_ADDR__SHIFT                                                                       0x0
//CAP_PTR
#define CAP_PTR__CAP_PTR__SHIFT                                                                               0x0
//INTERRUPT_LINE
#define INTERRUPT_LINE__INTERRUPT_LINE__SHIFT                                                                 0x0
//INTERRUPT_PIN
#define INTERRUPT_PIN__INTERRUPT_PIN__SHIFT                                                                   0x0
//MIN_GRANT
#define MIN_GRANT__MIN_GNT__SHIFT                                                                             0x0
//MAX_LATENCY
#define MAX_LATENCY__MAX_LAT__SHIFT                                                                           0x0
//VENDOR_CAP_LIST
#define VENDOR_CAP_LIST__CAP_ID__SHIFT                                                                        0x0
#define VENDOR_CAP_LIST__NEXT_PTR__SHIFT                                                                      0x8
#define VENDOR_CAP_LIST__LENGTH__SHIFT                                                                        0x10
//ADAPTER_ID_W
#define ADAPTER_ID_W__SUBSYSTEM_VENDOR_ID__SHIFT                                                              0x0
#define ADAPTER_ID_W__SUBSYSTEM_ID__SHIFT                                                                     0x10
//PMI_CAP_LIST
#define PMI_CAP_LIST__CAP_ID__SHIFT                                                                           0x0
#define PMI_CAP_LIST__NEXT_PTR__SHIFT                                                                         0x8
//PMI_CAP
#define PMI_CAP__VERSION__SHIFT                                                                               0x0
#define PMI_CAP__PME_CLOCK__SHIFT                                                                             0x3
#define PMI_CAP__DEV_SPECIFIC_INIT__SHIFT                                                                     0x5
#define PMI_CAP__AUX_CURRENT__SHIFT                                                                           0x6
#define PMI_CAP__D1_SUPPORT__SHIFT                                                                            0x9
#define PMI_CAP__D2_SUPPORT__SHIFT                                                                            0xa
#define PMI_CAP__PME_SUPPORT__SHIFT                                                                           0xb
//PMI_STATUS_CNTL
#define PMI_STATUS_CNTL__POWER_STATE__SHIFT                                                                   0x0
#define PMI_STATUS_CNTL__NO_SOFT_RESET__SHIFT                                                                 0x3
#define PMI_STATUS_CNTL__PME_EN__SHIFT                                                                        0x8
#define PMI_STATUS_CNTL__DATA_SELECT__SHIFT                                                                   0x9
#define PMI_STATUS_CNTL__DATA_SCALE__SHIFT                                                                    0xd
#define PMI_STATUS_CNTL__PME_STATUS__SHIFT                                                                    0xf
#define PMI_STATUS_CNTL__B2_B3_SUPPORT__SHIFT                                                                 0x16
#define PMI_STATUS_CNTL__BUS_PWR_EN__SHIFT                                                                    0x17
#define PMI_STATUS_CNTL__PMI_DATA__SHIFT                                                                      0x18
//PCIE_CAP_LIST
#define PCIE_CAP_LIST__CAP_ID__SHIFT                                                                          0x0
#define PCIE_CAP_LIST__NEXT_PTR__SHIFT                                                                        0x8
//PCIE_CAP
#define PCIE_CAP__VERSION__SHIFT                                                                              0x0
#define PCIE_CAP__DEVICE_TYPE__SHIFT                                                                          0x4
#define PCIE_CAP__SLOT_IMPLEMENTED__SHIFT                                                                     0x8
#define PCIE_CAP__INT_MESSAGE_NUM__SHIFT                                                                      0x9
//DEVICE_CAP
#define DEVICE_CAP__MAX_PAYLOAD_SUPPORT__SHIFT                                                                0x0
#define DEVICE_CAP__PHANTOM_FUNC__SHIFT                                                                       0x3
#define DEVICE_CAP__EXTENDED_TAG__SHIFT                                                                       0x5
#define DEVICE_CAP__L0S_ACCEPTABLE_LATENCY__SHIFT                                                             0x6
#define DEVICE_CAP__L1_ACCEPTABLE_LATENCY__SHIFT                                                              0x9
#define DEVICE_CAP__ROLE_BASED_ERR_REPORTING__SHIFT                                                           0xf
#define DEVICE_CAP__CAPTURED_SLOT_POWER_LIMIT__SHIFT                                                          0x12
#define DEVICE_CAP__CAPTURED_SLOT_POWER_SCALE__SHIFT                                                          0x1a
#define DEVICE_CAP__FLR_CAPABLE__SHIFT                                                                        0x1c
//DEVICE_CNTL
#define DEVICE_CNTL__CORR_ERR_EN__SHIFT                                                                       0x0
#define DEVICE_CNTL__NON_FATAL_ERR_EN__SHIFT                                                                  0x1
#define DEVICE_CNTL__FATAL_ERR_EN__SHIFT                                                                      0x2
#define DEVICE_CNTL__USR_REPORT_EN__SHIFT                                                                     0x3
#define DEVICE_CNTL__RELAXED_ORD_EN__SHIFT                                                                    0x4
#define DEVICE_CNTL__MAX_PAYLOAD_SIZE__SHIFT                                                                  0x5
#define DEVICE_CNTL__EXTENDED_TAG_EN__SHIFT                                                                   0x8
#define DEVICE_CNTL__PHANTOM_FUNC_EN__SHIFT                                                                   0x9
#define DEVICE_CNTL__AUX_POWER_PM_EN__SHIFT                                                                   0xa
#define DEVICE_CNTL__NO_SNOOP_EN__SHIFT                                                                       0xb
#define DEVICE_CNTL__MAX_READ_REQUEST_SIZE__SHIFT                                                             0xc
#define DEVICE_CNTL__INITIATE_FLR__SHIFT                                                                      0xf
//DEVICE_STATUS
#define DEVICE_STATUS__CORR_ERR__SHIFT                                                                        0x0
#define DEVICE_STATUS__NON_FATAL_ERR__SHIFT                                                                   0x1
#define DEVICE_STATUS__FATAL_ERR__SHIFT                                                                       0x2
#define DEVICE_STATUS__USR_DETECTED__SHIFT                                                                    0x3
#define DEVICE_STATUS__AUX_PWR__SHIFT                                                                         0x4
#define DEVICE_STATUS__TRANSACTIONS_PEND__SHIFT                                                               0x5
//LINK_CAP
#define LINK_CAP__LINK_SPEED__SHIFT                                                                           0x0
#define LINK_CAP__LINK_WIDTH__SHIFT                                                                           0x4
#define LINK_CAP__PM_SUPPORT__SHIFT                                                                           0xa
#define LINK_CAP__L0S_EXIT_LATENCY__SHIFT                                                                     0xc
#define LINK_CAP__L1_EXIT_LATENCY__SHIFT                                                                      0xf
#define LINK_CAP__CLOCK_POWER_MANAGEMENT__SHIFT                                                               0x12
#define LINK_CAP__SURPRISE_DOWN_ERR_REPORTING__SHIFT                                                          0x13
#define LINK_CAP__DL_ACTIVE_REPORTING_CAPABLE__SHIFT                                                          0x14
#define LINK_CAP__LINK_BW_NOTIFICATION_CAP__SHIFT                                                             0x15
#define LINK_CAP__ASPM_OPTIONALITY_COMPLIANCE__SHIFT                                                          0x16
#define LINK_CAP__PORT_NUMBER__SHIFT                                                                          0x18
//LINK_CNTL
#define LINK_CNTL__PM_CONTROL__SHIFT                                                                          0x0
#define LINK_CNTL__READ_CPL_BOUNDARY__SHIFT                                                                   0x3
#define LINK_CNTL__LINK_DIS__SHIFT                                                                            0x4
#define LINK_CNTL__RETRAIN_LINK__SHIFT                                                                        0x5
#define LINK_CNTL__COMMON_CLOCK_CFG__SHIFT                                                                    0x6
#define LINK_CNTL__EXTENDED_SYNC__SHIFT                                                                       0x7
#define LINK_CNTL__CLOCK_POWER_MANAGEMENT_EN__SHIFT                                                           0x8
#define LINK_CNTL__HW_AUTONOMOUS_WIDTH_DISABLE__SHIFT                                                         0x9
#define LINK_CNTL__LINK_BW_MANAGEMENT_INT_EN__SHIFT                                                           0xa
#define LINK_CNTL__LINK_AUTONOMOUS_BW_INT_EN__SHIFT                                                           0xb
//LINK_STATUS
#define LINK_STATUS__CURRENT_LINK_SPEED__SHIFT                                                                0x0
#define LINK_STATUS__NEGOTIATED_LINK_WIDTH__SHIFT                                                             0x4
#define LINK_STATUS__LINK_TRAINING__SHIFT                                                                     0xb
#define LINK_STATUS__SLOT_CLOCK_CFG__SHIFT                                                                    0xc
#define LINK_STATUS__DL_ACTIVE__SHIFT                                                                         0xd
#define LINK_STATUS__LINK_BW_MANAGEMENT_STATUS__SHIFT                                                         0xe
#define LINK_STATUS__LINK_AUTONOMOUS_BW_STATUS__SHIFT                                                         0xf
//DEVICE_CAP2
#define DEVICE_CAP2__CPL_TIMEOUT_RANGE_SUPPORTED__SHIFT                                                       0x0
#define DEVICE_CAP2__CPL_TIMEOUT_DIS_SUPPORTED__SHIFT                                                         0x4
#define DEVICE_CAP2__ARI_FORWARDING_SUPPORTED__SHIFT                                                          0x5
#define DEVICE_CAP2__ATOMICOP_ROUTING_SUPPORTED__SHIFT                                                        0x6
#define DEVICE_CAP2__ATOMICOP_32CMPLT_SUPPORTED__SHIFT                                                        0x7
#define DEVICE_CAP2__ATOMICOP_64CMPLT_SUPPORTED__SHIFT                                                        0x8
#define DEVICE_CAP2__CAS128_CMPLT_SUPPORTED__SHIFT                                                            0x9
#define DEVICE_CAP2__NO_RO_ENABLED_P2P_PASSING__SHIFT                                                         0xa
#define DEVICE_CAP2__LTR_SUPPORTED__SHIFT                                                                     0xb
#define DEVICE_CAP2__TPH_CPLR_SUPPORTED__SHIFT                                                                0xc
#define DEVICE_CAP2__OBFF_SUPPORTED__SHIFT                                                                    0x12
#define DEVICE_CAP2__EXTENDED_FMT_FIELD_SUPPORTED__SHIFT                                                      0x14
#define DEVICE_CAP2__END_END_TLP_PREFIX_SUPPORTED__SHIFT                                                      0x15
#define DEVICE_CAP2__MAX_END_END_TLP_PREFIXES__SHIFT                                                          0x16
//DEVICE_CNTL2
#define DEVICE_CNTL2__CPL_TIMEOUT_VALUE__SHIFT                                                                0x0
#define DEVICE_CNTL2__CPL_TIMEOUT_DIS__SHIFT                                                                  0x4
#define DEVICE_CNTL2__ARI_FORWARDING_EN__SHIFT                                                                0x5
#define DEVICE_CNTL2__ATOMICOP_REQUEST_EN__SHIFT                                                              0x6
#define DEVICE_CNTL2__ATOMICOP_EGRESS_BLOCKING__SHIFT                                                         0x7
#define DEVICE_CNTL2__IDO_REQUEST_ENABLE__SHIFT                                                               0x8
#define DEVICE_CNTL2__IDO_COMPLETION_ENABLE__SHIFT                                                            0x9
#define DEVICE_CNTL2__LTR_EN__SHIFT                                                                           0xa
#define DEVICE_CNTL2__OBFF_EN__SHIFT                                                                          0xd
#define DEVICE_CNTL2__END_END_TLP_PREFIX_BLOCKING__SHIFT                                                      0xf
//DEVICE_STATUS2
#define DEVICE_STATUS2__RESERVED__SHIFT                                                                       0x0
//LINK_CAP2
#define LINK_CAP2__SUPPORTED_LINK_SPEED__SHIFT                                                                0x1
#define LINK_CAP2__CROSSLINK_SUPPORTED__SHIFT                                                                 0x8
#define LINK_CAP2__RESERVED__SHIFT                                                                            0x9
//LINK_CNTL2
#define LINK_CNTL2__TARGET_LINK_SPEED__SHIFT                                                                  0x0
#define LINK_CNTL2__ENTER_COMPLIANCE__SHIFT                                                                   0x4
#define LINK_CNTL2__HW_AUTONOMOUS_SPEED_DISABLE__SHIFT                                                        0x5
#define LINK_CNTL2__SELECTABLE_DEEMPHASIS__SHIFT                                                              0x6
#define LINK_CNTL2__XMIT_MARGIN__SHIFT                                                                        0x7
#define LINK_CNTL2__ENTER_MOD_COMPLIANCE__SHIFT                                                               0xa
#define LINK_CNTL2__COMPLIANCE_SOS__SHIFT                                                                     0xb
#define LINK_CNTL2__COMPLIANCE_DEEMPHASIS__SHIFT                                                              0xc
//LINK_STATUS2
#define LINK_STATUS2__CUR_DEEMPHASIS_LEVEL__SHIFT                                                             0x0
#define LINK_STATUS2__EQUALIZATION_COMPLETE__SHIFT                                                            0x1
#define LINK_STATUS2__EQUALIZATION_PHASE1_SUCCESS__SHIFT                                                      0x2
#define LINK_STATUS2__EQUALIZATION_PHASE2_SUCCESS__SHIFT                                                      0x3
#define LINK_STATUS2__EQUALIZATION_PHASE3_SUCCESS__SHIFT                                                      0x4
#define LINK_STATUS2__LINK_EQUALIZATION_REQUEST__SHIFT                                                        0x5
//SLOT_CAP2
#define SLOT_CAP2__RESERVED__SHIFT                                                                            0x0
//SLOT_CNTL2
#define SLOT_CNTL2__RESERVED__SHIFT                                                                           0x0
//SLOT_STATUS2
#define SLOT_STATUS2__RESERVED__SHIFT                                                                         0x0
//MSI_CAP_LIST
#define MSI_CAP_LIST__CAP_ID__SHIFT                                                                           0x0
#define MSI_CAP_LIST__NEXT_PTR__SHIFT                                                                         0x8
//MSI_MSG_CNTL
#define MSI_MSG_CNTL__MSI_EN__SHIFT                                                                           0x0
#define MSI_MSG_CNTL__MSI_MULTI_CAP__SHIFT                                                                    0x1
#define MSI_MSG_CNTL__MSI_MULTI_EN__SHIFT                                                                     0x4
#define MSI_MSG_CNTL__MSI_64BIT__SHIFT                                                                        0x7
#define MSI_MSG_CNTL__MSI_PERVECTOR_MASKING_CAP__SHIFT                                                        0x8
//MSI_MSG_ADDR_LO
#define MSI_MSG_ADDR_LO__MSI_MSG_ADDR_LO__SHIFT                                                               0x2
//MSI_MSG_ADDR_HI
#define MSI_MSG_ADDR_HI__MSI_MSG_ADDR_HI__SHIFT                                                               0x0
//MSI_MSG_DATA
#define MSI_MSG_DATA__MSI_DATA__SHIFT                                                                         0x0
//MSI_MSG_DATA_64
#define MSI_MSG_DATA_64__MSI_DATA_64__SHIFT                                                                   0x0
//MSI_MASK
#define MSI_MASK__MSI_MASK__SHIFT                                                                             0x0
//MSI_PENDING
#define MSI_PENDING__MSI_PENDING__SHIFT                                                                       0x0
//MSI_MASK_64
#define MSI_MASK_64__MSI_MASK_64__SHIFT                                                                       0x0
//MSI_PENDING_64
#define MSI_PENDING_64__MSI_PENDING_64__SHIFT                                                                 0x0
//MSIX_CAP_LIST
#define MSIX_CAP_LIST__CAP_ID__SHIFT                                                                          0x0
#define MSIX_CAP_LIST__NEXT_PTR__SHIFT                                                                        0x8
//MSIX_MSG_CNTL
#define MSIX_MSG_CNTL__MSIX_TABLE_SIZE__SHIFT                                                                 0x0
#define MSIX_MSG_CNTL__MSIX_FUNC_MASK__SHIFT                                                                  0xe
#define MSIX_MSG_CNTL__MSIX_EN__SHIFT                                                                         0xf
//MSIX_TABLE
#define MSIX_TABLE__MSIX_TABLE_BIR__SHIFT                                                                     0x0
#define MSIX_TABLE__MSIX_TABLE_OFFSET__SHIFT                                                                  0x3
//MSIX_PBA
#define MSIX_PBA__MSIX_PBA_BIR__SHIFT                                                                         0x0
#define MSIX_PBA__MSIX_PBA_OFFSET__SHIFT                                                                      0x3
//PCIE_VENDOR_SPECIFIC_ENH_CAP_LIST
#define PCIE_VENDOR_SPECIFIC_ENH_CAP_LIST__CAP_ID__SHIFT                                                      0x0
#define PCIE_VENDOR_SPECIFIC_ENH_CAP_LIST__CAP_VER__SHIFT                                                     0x10
#define PCIE_VENDOR_SPECIFIC_ENH_CAP_LIST__NEXT_PTR__SHIFT                                                    0x14
//PCIE_VENDOR_SPECIFIC_HDR
#define PCIE_VENDOR_SPECIFIC_HDR__VSEC_ID__SHIFT                                                              0x0
#define PCIE_VENDOR_SPECIFIC_HDR__VSEC_REV__SHIFT                                                             0x10
#define PCIE_VENDOR_SPECIFIC_HDR__VSEC_LENGTH__SHIFT                                                          0x14
//PCIE_VENDOR_SPECIFIC1
#define PCIE_VENDOR_SPECIFIC1__SCRATCH__SHIFT                                                                 0x0
//PCIE_VENDOR_SPECIFIC2
#define PCIE_VENDOR_SPECIFIC2__SCRATCH__SHIFT                                                                 0x0
//PCIE_VC_ENH_CAP_LIST
#define PCIE_VC_ENH_CAP_LIST__CAP_ID__SHIFT                                                                   0x0
#define PCIE_VC_ENH_CAP_LIST__CAP_VER__SHIFT                                                                  0x10
#define PCIE_VC_ENH_CAP_LIST__NEXT_PTR__SHIFT                                                                 0x14
//PCIE_PORT_VC_CAP_REG1
#define PCIE_PORT_VC_CAP_REG1__EXT_VC_COUNT__SHIFT                                                            0x0
#define PCIE_PORT_VC_CAP_REG1__LOW_PRIORITY_EXT_VC_COUNT__SHIFT                                               0x4
#define PCIE_PORT_VC_CAP_REG1__REF_CLK__SHIFT                                                                 0x8
#define PCIE_PORT_VC_CAP_REG1__PORT_ARB_TABLE_ENTRY_SIZE__SHIFT                                               0xa
//PCIE_PORT_VC_CAP_REG2
#define PCIE_PORT_VC_CAP_REG2__VC_ARB_CAP__SHIFT                                                              0x0
#define PCIE_PORT_VC_CAP_REG2__VC_ARB_TABLE_OFFSET__SHIFT                                                     0x18
//PCIE_PORT_VC_CNTL
#define PCIE_PORT_VC_CNTL__LOAD_VC_ARB_TABLE__SHIFT                                                           0x0
#define PCIE_PORT_VC_CNTL__VC_ARB_SELECT__SHIFT                                                               0x1
//PCIE_PORT_VC_STATUS
#define PCIE_PORT_VC_STATUS__VC_ARB_TABLE_STATUS__SHIFT                                                       0x0
//PCIE_VC0_RESOURCE_CAP
#define PCIE_VC0_RESOURCE_CAP__PORT_ARB_CAP__SHIFT                                                            0x0
#define PCIE_VC0_RESOURCE_CAP__REJECT_SNOOP_TRANS__SHIFT                                                      0xf
#define PCIE_VC0_RESOURCE_CAP__MAX_TIME_SLOTS__SHIFT                                                          0x10
#define PCIE_VC0_RESOURCE_CAP__PORT_ARB_TABLE_OFFSET__SHIFT                                                   0x18
//PCIE_VC0_RESOURCE_CNTL
#define PCIE_VC0_RESOURCE_CNTL__TC_VC_MAP_TC0__SHIFT                                                          0x0
#define PCIE_VC0_RESOURCE_CNTL__TC_VC_MAP_TC1_7__SHIFT                                                        0x1
#define PCIE_VC0_RESOURCE_CNTL__LOAD_PORT_ARB_TABLE__SHIFT                                                    0x10
#define PCIE_VC0_RESOURCE_CNTL__PORT_ARB_SELECT__SHIFT                                                        0x11
#define PCIE_VC0_RESOURCE_CNTL__VC_ID__SHIFT                                                                  0x18
#define PCIE_VC0_RESOURCE_CNTL__VC_ENABLE__SHIFT                                                              0x1f
//PCIE_VC0_RESOURCE_STATUS
#define PCIE_VC0_RESOURCE_STATUS__PORT_ARB_TABLE_STATUS__SHIFT                                                0x0
#define PCIE_VC0_RESOURCE_STATUS__VC_NEGOTIATION_PENDING__SHIFT                                               0x1
//PCIE_VC1_RESOURCE_CAP
#define PCIE_VC1_RESOURCE_CAP__PORT_ARB_CAP__SHIFT                                                            0x0
#define PCIE_VC1_RESOURCE_CAP__REJECT_SNOOP_TRANS__SHIFT                                                      0xf
#define PCIE_VC1_RESOURCE_CAP__MAX_TIME_SLOTS__SHIFT                                                          0x10
#define PCIE_VC1_RESOURCE_CAP__PORT_ARB_TABLE_OFFSET__SHIFT                                                   0x18
//PCIE_VC1_RESOURCE_CNTL
#define PCIE_VC1_RESOURCE_CNTL__TC_VC_MAP_TC0__SHIFT                                                          0x0
#define PCIE_VC1_RESOURCE_CNTL__TC_VC_MAP_TC1_7__SHIFT                                                        0x1
#define PCIE_VC1_RESOURCE_CNTL__LOAD_PORT_ARB_TABLE__SHIFT                                                    0x10
#define PCIE_VC1_RESOURCE_CNTL__PORT_ARB_SELECT__SHIFT                                                        0x11
#define PCIE_VC1_RESOURCE_CNTL__VC_ID__SHIFT                                                                  0x18
#define PCIE_VC1_RESOURCE_CNTL__VC_ENABLE__SHIFT                                                              0x1f
//PCIE_VC1_RESOURCE_STATUS
#define PCIE_VC1_RESOURCE_STATUS__PORT_ARB_TABLE_STATUS__SHIFT                                                0x0
#define PCIE_VC1_RESOURCE_STATUS__VC_NEGOTIATION_PENDING__SHIFT                                               0x1
//PCIE_DEV_SERIAL_NUM_ENH_CAP_LIST
#define PCIE_DEV_SERIAL_NUM_ENH_CAP_LIST__CAP_ID__SHIFT                                                       0x0
#define PCIE_DEV_SERIAL_NUM_ENH_CAP_LIST__CAP_VER__SHIFT                                                      0x10
#define PCIE_DEV_SERIAL_NUM_ENH_CAP_LIST__NEXT_PTR__SHIFT                                                     0x14
//PCIE_DEV_SERIAL_NUM_DW1
#define PCIE_DEV_SERIAL_NUM_DW1__SERIAL_NUMBER_LO__SHIFT                                                      0x0
//PCIE_DEV_SERIAL_NUM_DW2
#define PCIE_DEV_SERIAL_NUM_DW2__SERIAL_NUMBER_HI__SHIFT                                                      0x0
//PCIE_ADV_ERR_RPT_ENH_CAP_LIST
#define PCIE_ADV_ERR_RPT_ENH_CAP_LIST__CAP_ID__SHIFT                                                          0x0
#define PCIE_ADV_ERR_RPT_ENH_CAP_LIST__CAP_VER__SHIFT                                                         0x10
#define PCIE_ADV_ERR_RPT_ENH_CAP_LIST__NEXT_PTR__SHIFT                                                        0x14
//PCIE_UNCORR_ERR_STATUS
#define PCIE_UNCORR_ERR_STATUS__DLP_ERR_STATUS__SHIFT                                                         0x4
#define PCIE_UNCORR_ERR_STATUS__SURPDN_ERR_STATUS__SHIFT                                                      0x5
#define PCIE_UNCORR_ERR_STATUS__PSN_ERR_STATUS__SHIFT                                                         0xc
#define PCIE_UNCORR_ERR_STATUS__FC_ERR_STATUS__SHIFT                                                          0xd
#define PCIE_UNCORR_ERR_STATUS__CPL_TIMEOUT_STATUS__SHIFT                                                     0xe
#define PCIE_UNCORR_ERR_STATUS__CPL_ABORT_ERR_STATUS__SHIFT                                                   0xf
#define PCIE_UNCORR_ERR_STATUS__UNEXP_CPL_STATUS__SHIFT                                                       0x10
#define PCIE_UNCORR_ERR_STATUS__RCV_OVFL_STATUS__SHIFT                                                        0x11
#define PCIE_UNCORR_ERR_STATUS__MAL_TLP_STATUS__SHIFT                                                         0x12
#define PCIE_UNCORR_ERR_STATUS__ECRC_ERR_STATUS__SHIFT                                                        0x13
#define PCIE_UNCORR_ERR_STATUS__UNSUPP_REQ_ERR_STATUS__SHIFT                                                  0x14
#define PCIE_UNCORR_ERR_STATUS__ACS_VIOLATION_STATUS__SHIFT                                                   0x15
#define PCIE_UNCORR_ERR_STATUS__UNCORR_INT_ERR_STATUS__SHIFT                                                  0x16
#define PCIE_UNCORR_ERR_STATUS__MC_BLOCKED_TLP_STATUS__SHIFT                                                  0x17
#define PCIE_UNCORR_ERR_STATUS__ATOMICOP_EGRESS_BLOCKED_STATUS__SHIFT                                         0x18
#define PCIE_UNCORR_ERR_STATUS__TLP_PREFIX_BLOCKED_ERR_STATUS__SHIFT                                          0x19
//PCIE_UNCORR_ERR_MASK
#define PCIE_UNCORR_ERR_MASK__DLP_ERR_MASK__SHIFT                                                             0x4
#define PCIE_UNCORR_ERR_MASK__SURPDN_ERR_MASK__SHIFT                                                          0x5
#define PCIE_UNCORR_ERR_MASK__PSN_ERR_MASK__SHIFT                                                             0xc
#define PCIE_UNCORR_ERR_MASK__FC_ERR_MASK__SHIFT                                                              0xd
#define PCIE_UNCORR_ERR_MASK__CPL_TIMEOUT_MASK__SHIFT                                                         0xe
#define PCIE_UNCORR_ERR_MASK__CPL_ABORT_ERR_MASK__SHIFT                                                       0xf
#define PCIE_UNCORR_ERR_MASK__UNEXP_CPL_MASK__SHIFT                                                           0x10
#define PCIE_UNCORR_ERR_MASK__RCV_OVFL_MASK__SHIFT                                                            0x11
#define PCIE_UNCORR_ERR_MASK__MAL_TLP_MASK__SHIFT                                                             0x12
#define PCIE_UNCORR_ERR_MASK__ECRC_ERR_MASK__SHIFT                                                            0x13
#define PCIE_UNCORR_ERR_MASK__UNSUPP_REQ_ERR_MASK__SHIFT                                                      0x14
#define PCIE_UNCORR_ERR_MASK__ACS_VIOLATION_MASK__SHIFT                                                       0x15
#define PCIE_UNCORR_ERR_MASK__UNCORR_INT_ERR_MASK__SHIFT                                                      0x16
#define PCIE_UNCORR_ERR_MASK__MC_BLOCKED_TLP_MASK__SHIFT                                                      0x17
#define PCIE_UNCORR_ERR_MASK__ATOMICOP_EGRESS_BLOCKED_MASK__SHIFT                                             0x18
#define PCIE_UNCORR_ERR_MASK__TLP_PREFIX_BLOCKED_ERR_MASK__SHIFT                                              0x19
//PCIE_UNCORR_ERR_SEVERITY
#define PCIE_UNCORR_ERR_SEVERITY__DLP_ERR_SEVERITY__SHIFT                                                     0x4
#define PCIE_UNCORR_ERR_SEVERITY__SURPDN_ERR_SEVERITY__SHIFT                                                  0x5
#define PCIE_UNCORR_ERR_SEVERITY__PSN_ERR_SEVERITY__SHIFT                                                     0xc
#define PCIE_UNCORR_ERR_SEVERITY__FC_ERR_SEVERITY__SHIFT                                                      0xd
#define PCIE_UNCORR_ERR_SEVERITY__CPL_TIMEOUT_SEVERITY__SHIFT                                                 0xe
#define PCIE_UNCORR_ERR_SEVERITY__CPL_ABORT_ERR_SEVERITY__SHIFT                                               0xf
#define PCIE_UNCORR_ERR_SEVERITY__UNEXP_CPL_SEVERITY__SHIFT                                                   0x10
#define PCIE_UNCORR_ERR_SEVERITY__RCV_OVFL_SEVERITY__SHIFT                                                    0x11
#define PCIE_UNCORR_ERR_SEVERITY__MAL_TLP_SEVERITY__SHIFT                                                     0x12
#define PCIE_UNCORR_ERR_SEVERITY__ECRC_ERR_SEVERITY__SHIFT                                                    0x13
#define PCIE_UNCORR_ERR_SEVERITY__UNSUPP_REQ_ERR_SEVERITY__SHIFT                                              0x14
#define PCIE_UNCORR_ERR_SEVERITY__ACS_VIOLATION_SEVERITY__SHIFT                                               0x15
#define PCIE_UNCORR_ERR_SEVERITY__UNCORR_INT_ERR_SEVERITY__SHIFT                                              0x16
#define PCIE_UNCORR_ERR_SEVERITY__MC_BLOCKED_TLP_SEVERITY__SHIFT                                              0x17
#define PCIE_UNCORR_ERR_SEVERITY__ATOMICOP_EGRESS_BLOCKED_SEVERITY__SHIFT                                     0x18
#define PCIE_UNCORR_ERR_SEVERITY__TLP_PREFIX_BLOCKED_ERR_SEVERITY__SHIFT                                      0x19
//PCIE_CORR_ERR_STATUS
#define PCIE_CORR_ERR_STATUS__RCV_ERR_STATUS__SHIFT                                                           0x0
#define PCIE_CORR_ERR_STATUS__BAD_TLP_STATUS__SHIFT                                                           0x6
#define PCIE_CORR_ERR_STATUS__BAD_DLLP_STATUS__SHIFT                                                          0x7
#define PCIE_CORR_ERR_STATUS__REPLAY_NUM_ROLLOVER_STATUS__SHIFT                                               0x8
#define PCIE_CORR_ERR_STATUS__REPLAY_TIMER_TIMEOUT_STATUS__SHIFT                                              0xc
#define PCIE_CORR_ERR_STATUS__ADVISORY_NONFATAL_ERR_STATUS__SHIFT                                             0xd
#define PCIE_CORR_ERR_STATUS__CORR_INT_ERR_STATUS__SHIFT                                                      0xe
#define PCIE_CORR_ERR_STATUS__HDR_LOG_OVFL_STATUS__SHIFT                                                      0xf
//PCIE_CORR_ERR_MASK
#define PCIE_CORR_ERR_MASK__RCV_ERR_MASK__SHIFT                                                               0x0
#define PCIE_CORR_ERR_MASK__BAD_TLP_MASK__SHIFT                                                               0x6
#define PCIE_CORR_ERR_MASK__BAD_DLLP_MASK__SHIFT                                                              0x7
#define PCIE_CORR_ERR_MASK__REPLAY_NUM_ROLLOVER_MASK__SHIFT                                                   0x8
#define PCIE_CORR_ERR_MASK__REPLAY_TIMER_TIMEOUT_MASK__SHIFT                                                  0xc
#define PCIE_CORR_ERR_MASK__ADVISORY_NONFATAL_ERR_MASK__SHIFT                                                 0xd
#define PCIE_CORR_ERR_MASK__CORR_INT_ERR_MASK__SHIFT                                                          0xe
#define PCIE_CORR_ERR_MASK__HDR_LOG_OVFL_MASK__SHIFT                                                          0xf
//PCIE_ADV_ERR_CAP_CNTL
#define PCIE_ADV_ERR_CAP_CNTL__FIRST_ERR_PTR__SHIFT                                                           0x0
#define PCIE_ADV_ERR_CAP_CNTL__ECRC_GEN_CAP__SHIFT                                                            0x5
#define PCIE_ADV_ERR_CAP_CNTL__ECRC_GEN_EN__SHIFT                                                             0x6
#define PCIE_ADV_ERR_CAP_CNTL__ECRC_CHECK_CAP__SHIFT                                                          0x7
#define PCIE_ADV_ERR_CAP_CNTL__ECRC_CHECK_EN__SHIFT                                                           0x8
#define PCIE_ADV_ERR_CAP_CNTL__MULTI_HDR_RECD_CAP__SHIFT                                                      0x9
#define PCIE_ADV_ERR_CAP_CNTL__MULTI_HDR_RECD_EN__SHIFT                                                       0xa
#define PCIE_ADV_ERR_CAP_CNTL__TLP_PREFIX_LOG_PRESENT__SHIFT                                                  0xb
//PCIE_HDR_LOG0
#define PCIE_HDR_LOG0__TLP_HDR__SHIFT                                                                         0x0
//PCIE_HDR_LOG1
#define PCIE_HDR_LOG1__TLP_HDR__SHIFT                                                                         0x0
//PCIE_HDR_LOG2
#define PCIE_HDR_LOG2__TLP_HDR__SHIFT                                                                         0x0
//PCIE_HDR_LOG3
#define PCIE_HDR_LOG3__TLP_HDR__SHIFT                                                                         0x0
//PCIE_ROOT_ERR_CMD
#define PCIE_ROOT_ERR_CMD__CORR_ERR_REP_EN__SHIFT                                                             0x0
#define PCIE_ROOT_ERR_CMD__NONFATAL_ERR_REP_EN__SHIFT                                                         0x1
#define PCIE_ROOT_ERR_CMD__FATAL_ERR_REP_EN__SHIFT                                                            0x2
//PCIE_ROOT_ERR_STATUS
#define PCIE_ROOT_ERR_STATUS__ERR_CORR_RCVD__SHIFT                                                            0x0
#define PCIE_ROOT_ERR_STATUS__MULT_ERR_CORR_RCVD__SHIFT                                                       0x1
#define PCIE_ROOT_ERR_STATUS__ERR_FATAL_NONFATAL_RCVD__SHIFT                                                  0x2
#define PCIE_ROOT_ERR_STATUS__MULT_ERR_FATAL_NONFATAL_RCVD__SHIFT                                             0x3
#define PCIE_ROOT_ERR_STATUS__FIRST_UNCORRECTABLE_FATAL__SHIFT                                                0x4
#define PCIE_ROOT_ERR_STATUS__NONFATAL_ERROR_MSG_RCVD__SHIFT                                                  0x5
#define PCIE_ROOT_ERR_STATUS__FATAL_ERROR_MSG_RCVD__SHIFT                                                     0x6
#define PCIE_ROOT_ERR_STATUS__ADV_ERR_INT_MSG_NUM__SHIFT                                                      0x1b
//PCIE_ERR_SRC_ID
#define PCIE_ERR_SRC_ID__ERR_CORR_SRC_ID__SHIFT                                                               0x0
#define PCIE_ERR_SRC_ID__ERR_FATAL_NONFATAL_SRC_ID__SHIFT                                                     0x10
//PCIE_TLP_PREFIX_LOG0
#define PCIE_TLP_PREFIX_LOG0__TLP_PREFIX__SHIFT                                                               0x0
//PCIE_TLP_PREFIX_LOG1
#define PCIE_TLP_PREFIX_LOG1__TLP_PREFIX__SHIFT                                                               0x0
//PCIE_TLP_PREFIX_LOG2
#define PCIE_TLP_PREFIX_LOG2__TLP_PREFIX__SHIFT                                                               0x0
//PCIE_TLP_PREFIX_LOG3
#define PCIE_TLP_PREFIX_LOG3__TLP_PREFIX__SHIFT                                                               0x0
//PCIE_BAR_ENH_CAP_LIST
#define PCIE_BAR_ENH_CAP_LIST__CAP_ID__SHIFT                                                                  0x0
#define PCIE_BAR_ENH_CAP_LIST__CAP_VER__SHIFT                                                                 0x10
#define PCIE_BAR_ENH_CAP_LIST__NEXT_PTR__SHIFT                                                                0x14
//PCIE_BAR1_CAP
#define PCIE_BAR1_CAP__BAR_SIZE_SUPPORTED__SHIFT                                                              0x4
//PCIE_BAR1_CNTL
#define PCIE_BAR1_CNTL__BAR_INDEX__SHIFT                                                                      0x0
#define PCIE_BAR1_CNTL__BAR_TOTAL_NUM__SHIFT                                                                  0x5
#define PCIE_BAR1_CNTL__BAR_SIZE__SHIFT                                                                       0x8
//PCIE_BAR2_CAP
#define PCIE_BAR2_CAP__BAR_SIZE_SUPPORTED__SHIFT                                                              0x4
//PCIE_BAR2_CNTL
#define PCIE_BAR2_CNTL__BAR_INDEX__SHIFT                                                                      0x0
#define PCIE_BAR2_CNTL__BAR_TOTAL_NUM__SHIFT                                                                  0x5
#define PCIE_BAR2_CNTL__BAR_SIZE__SHIFT                                                                       0x8
//PCIE_BAR3_CAP
#define PCIE_BAR3_CAP__BAR_SIZE_SUPPORTED__SHIFT                                                              0x4
//PCIE_BAR3_CNTL
#define PCIE_BAR3_CNTL__BAR_INDEX__SHIFT                                                                      0x0
#define PCIE_BAR3_CNTL__BAR_TOTAL_NUM__SHIFT                                                                  0x5
#define PCIE_BAR3_CNTL__BAR_SIZE__SHIFT                                                                       0x8
//PCIE_BAR4_CAP
#define PCIE_BAR4_CAP__BAR_SIZE_SUPPORTED__SHIFT                                                              0x4
//PCIE_BAR4_CNTL
#define PCIE_BAR4_CNTL__BAR_INDEX__SHIFT                                                                      0x0
#define PCIE_BAR4_CNTL__BAR_TOTAL_NUM__SHIFT                                                                  0x5
#define PCIE_BAR4_CNTL__BAR_SIZE__SHIFT                                                                       0x8
//PCIE_BAR5_CAP
#define PCIE_BAR5_CAP__BAR_SIZE_SUPPORTED__SHIFT                                                              0x4
//PCIE_BAR5_CNTL
#define PCIE_BAR5_CNTL__BAR_INDEX__SHIFT                                                                      0x0
#define PCIE_BAR5_CNTL__BAR_TOTAL_NUM__SHIFT                                                                  0x5
#define PCIE_BAR5_CNTL__BAR_SIZE__SHIFT                                                                       0x8
//PCIE_BAR6_CAP
#define PCIE_BAR6_CAP__BAR_SIZE_SUPPORTED__SHIFT                                                              0x4
//PCIE_BAR6_CNTL
#define PCIE_BAR6_CNTL__BAR_INDEX__SHIFT                                                                      0x0
#define PCIE_BAR6_CNTL__BAR_TOTAL_NUM__SHIFT                                                                  0x5
#define PCIE_BAR6_CNTL__BAR_SIZE__SHIFT                                                                       0x8
//PCIE_PWR_BUDGET_ENH_CAP_LIST
#define PCIE_PWR_BUDGET_ENH_CAP_LIST__CAP_ID__SHIFT                                                           0x0
#define PCIE_PWR_BUDGET_ENH_CAP_LIST__CAP_VER__SHIFT                                                          0x10
#define PCIE_PWR_BUDGET_ENH_CAP_LIST__NEXT_PTR__SHIFT                                                         0x14
//PCIE_PWR_BUDGET_DATA_SELECT
#define PCIE_PWR_BUDGET_DATA_SELECT__DATA_SELECT__SHIFT                                                       0x0
//PCIE_PWR_BUDGET_DATA
#define PCIE_PWR_BUDGET_DATA__BASE_POWER__SHIFT                                                               0x0
#define PCIE_PWR_BUDGET_DATA__DATA_SCALE__SHIFT                                                               0x8
#define PCIE_PWR_BUDGET_DATA__PM_SUB_STATE__SHIFT                                                             0xa
#define PCIE_PWR_BUDGET_DATA__PM_STATE__SHIFT                                                                 0xd
#define PCIE_PWR_BUDGET_DATA__TYPE__SHIFT                                                                     0xf
#define PCIE_PWR_BUDGET_DATA__POWER_RAIL__SHIFT                                                               0x12
//PCIE_PWR_BUDGET_CAP
#define PCIE_PWR_BUDGET_CAP__SYSTEM_ALLOCATED__SHIFT                                                          0x0
//PCIE_DPA_ENH_CAP_LIST
#define PCIE_DPA_ENH_CAP_LIST__CAP_ID__SHIFT                                                                  0x0
#define PCIE_DPA_ENH_CAP_LIST__CAP_VER__SHIFT                                                                 0x10
#define PCIE_DPA_ENH_CAP_LIST__NEXT_PTR__SHIFT                                                                0x14
//PCIE_DPA_CAP
#define PCIE_DPA_CAP__SUBSTATE_MAX__SHIFT                                                                     0x0
#define PCIE_DPA_CAP__TRANS_LAT_UNIT__SHIFT                                                                   0x8
#define PCIE_DPA_CAP__PWR_ALLOC_SCALE__SHIFT                                                                  0xc
#define PCIE_DPA_CAP__TRANS_LAT_VAL_0__SHIFT                                                                  0x10
#define PCIE_DPA_CAP__TRANS_LAT_VAL_1__SHIFT                                                                  0x18
//PCIE_DPA_LATENCY_INDICATOR
#define PCIE_DPA_LATENCY_INDICATOR__TRANS_LAT_INDICATOR_BITS__SHIFT                                           0x0
//PCIE_DPA_STATUS
#define PCIE_DPA_STATUS__SUBSTATE_STATUS__SHIFT                                                               0x0
#define PCIE_DPA_STATUS__SUBSTATE_CNTL_ENABLED__SHIFT                                                         0x8
//PCIE_DPA_CNTL
#define PCIE_DPA_CNTL__SUBSTATE_CNTL__SHIFT                                                                   0x0
//PCIE_DPA_SUBSTATE_PWR_ALLOC_0
#define PCIE_DPA_SUBSTATE_PWR_ALLOC_0__SUBSTATE_PWR_ALLOC__SHIFT                                              0x0
//PCIE_DPA_SUBSTATE_PWR_ALLOC_1
#define PCIE_DPA_SUBSTATE_PWR_ALLOC_1__SUBSTATE_PWR_ALLOC__SHIFT                                              0x0
//PCIE_DPA_SUBSTATE_PWR_ALLOC_2
#define PCIE_DPA_SUBSTATE_PWR_ALLOC_2__SUBSTATE_PWR_ALLOC__SHIFT                                              0x0
//PCIE_DPA_SUBSTATE_PWR_ALLOC_3
#define PCIE_DPA_SUBSTATE_PWR_ALLOC_3__SUBSTATE_PWR_ALLOC__SHIFT                                              0x0
//PCIE_DPA_SUBSTATE_PWR_ALLOC_4
#define PCIE_DPA_SUBSTATE_PWR_ALLOC_4__SUBSTATE_PWR_ALLOC__SHIFT                                              0x0
//PCIE_DPA_SUBSTATE_PWR_ALLOC_5
#define PCIE_DPA_SUBSTATE_PWR_ALLOC_5__SUBSTATE_PWR_ALLOC__SHIFT                                              0x0
//PCIE_DPA_SUBSTATE_PWR_ALLOC_6
#define PCIE_DPA_SUBSTATE_PWR_ALLOC_6__SUBSTATE_PWR_ALLOC__SHIFT                                              0x0
//PCIE_DPA_SUBSTATE_PWR_ALLOC_7
#define PCIE_DPA_SUBSTATE_PWR_ALLOC_7__SUBSTATE_PWR_ALLOC__SHIFT                                              0x0
//PCIE_SECONDARY_ENH_CAP_LIST
#define PCIE_SECONDARY_ENH_CAP_LIST__CAP_ID__SHIFT                                                            0x0
#define PCIE_SECONDARY_ENH_CAP_LIST__CAP_VER__SHIFT                                                           0x10
#define PCIE_SECONDARY_ENH_CAP_LIST__NEXT_PTR__SHIFT                                                          0x14
//PCIE_LINK_CNTL3
#define PCIE_LINK_CNTL3__PERFORM_EQUALIZATION__SHIFT                                                          0x0
#define PCIE_LINK_CNTL3__LINK_EQUALIZATION_REQ_INT_EN__SHIFT                                                  0x1
#define PCIE_LINK_CNTL3__RESERVED__SHIFT                                                                      0x2
//PCIE_LANE_ERROR_STATUS
#define PCIE_LANE_ERROR_STATUS__LANE_ERROR_STATUS_BITS__SHIFT                                                 0x0
#define PCIE_LANE_ERROR_STATUS__RESERVED__SHIFT                                                               0x10
//PCIE_LANE_0_EQUALIZATION_CNTL
#define PCIE_LANE_0_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__SHIFT                                       0x0
#define PCIE_LANE_0_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__SHIFT                                  0x4
#define PCIE_LANE_0_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__SHIFT                                         0x8
#define PCIE_LANE_0_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__SHIFT                                    0xc
#define PCIE_LANE_0_EQUALIZATION_CNTL__RESERVED__SHIFT                                                        0xf
//PCIE_LANE_1_EQUALIZATION_CNTL
#define PCIE_LANE_1_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__SHIFT                                       0x0
#define PCIE_LANE_1_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__SHIFT                                  0x4
#define PCIE_LANE_1_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__SHIFT                                         0x8
#define PCIE_LANE_1_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__SHIFT                                    0xc
#define PCIE_LANE_1_EQUALIZATION_CNTL__RESERVED__SHIFT                                                        0xf
//PCIE_LANE_2_EQUALIZATION_CNTL
#define PCIE_LANE_2_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__SHIFT                                       0x0
#define PCIE_LANE_2_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__SHIFT                                  0x4
#define PCIE_LANE_2_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__SHIFT                                         0x8
#define PCIE_LANE_2_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__SHIFT                                    0xc
#define PCIE_LANE_2_EQUALIZATION_CNTL__RESERVED__SHIFT                                                        0xf
//PCIE_LANE_3_EQUALIZATION_CNTL
#define PCIE_LANE_3_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__SHIFT                                       0x0
#define PCIE_LANE_3_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__SHIFT                                  0x4
#define PCIE_LANE_3_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__SHIFT                                         0x8
#define PCIE_LANE_3_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__SHIFT                                    0xc
#define PCIE_LANE_3_EQUALIZATION_CNTL__RESERVED__SHIFT                                                        0xf
//PCIE_LANE_4_EQUALIZATION_CNTL
#define PCIE_LANE_4_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__SHIFT                                       0x0
#define PCIE_LANE_4_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__SHIFT                                  0x4
#define PCIE_LANE_4_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__SHIFT                                         0x8
#define PCIE_LANE_4_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__SHIFT                                    0xc
#define PCIE_LANE_4_EQUALIZATION_CNTL__RESERVED__SHIFT                                                        0xf
//PCIE_LANE_5_EQUALIZATION_CNTL
#define PCIE_LANE_5_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__SHIFT                                       0x0
#define PCIE_LANE_5_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__SHIFT                                  0x4
#define PCIE_LANE_5_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__SHIFT                                         0x8
#define PCIE_LANE_5_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__SHIFT                                    0xc
#define PCIE_LANE_5_EQUALIZATION_CNTL__RESERVED__SHIFT                                                        0xf
//PCIE_LANE_6_EQUALIZATION_CNTL
#define PCIE_LANE_6_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__SHIFT                                       0x0
#define PCIE_LANE_6_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__SHIFT                                  0x4
#define PCIE_LANE_6_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__SHIFT                                         0x8
#define PCIE_LANE_6_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__SHIFT                                    0xc
#define PCIE_LANE_6_EQUALIZATION_CNTL__RESERVED__SHIFT                                                        0xf
//PCIE_LANE_7_EQUALIZATION_CNTL
#define PCIE_LANE_7_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__SHIFT                                       0x0
#define PCIE_LANE_7_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__SHIFT                                  0x4
#define PCIE_LANE_7_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__SHIFT                                         0x8
#define PCIE_LANE_7_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__SHIFT                                    0xc
#define PCIE_LANE_7_EQUALIZATION_CNTL__RESERVED__SHIFT                                                        0xf
//PCIE_LANE_8_EQUALIZATION_CNTL
#define PCIE_LANE_8_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__SHIFT                                       0x0
#define PCIE_LANE_8_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__SHIFT                                  0x4
#define PCIE_LANE_8_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__SHIFT                                         0x8
#define PCIE_LANE_8_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__SHIFT                                    0xc
#define PCIE_LANE_8_EQUALIZATION_CNTL__RESERVED__SHIFT                                                        0xf
//PCIE_LANE_9_EQUALIZATION_CNTL
#define PCIE_LANE_9_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__SHIFT                                       0x0
#define PCIE_LANE_9_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__SHIFT                                  0x4
#define PCIE_LANE_9_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__SHIFT                                         0x8
#define PCIE_LANE_9_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__SHIFT                                    0xc
#define PCIE_LANE_9_EQUALIZATION_CNTL__RESERVED__SHIFT                                                        0xf
//PCIE_LANE_10_EQUALIZATION_CNTL
#define PCIE_LANE_10_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__SHIFT                                      0x0
#define PCIE_LANE_10_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__SHIFT                                 0x4
#define PCIE_LANE_10_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__SHIFT                                        0x8
#define PCIE_LANE_10_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__SHIFT                                   0xc
#define PCIE_LANE_10_EQUALIZATION_CNTL__RESERVED__SHIFT                                                       0xf
//PCIE_LANE_11_EQUALIZATION_CNTL
#define PCIE_LANE_11_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__SHIFT                                      0x0
#define PCIE_LANE_11_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__SHIFT                                 0x4
#define PCIE_LANE_11_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__SHIFT                                        0x8
#define PCIE_LANE_11_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__SHIFT                                   0xc
#define PCIE_LANE_11_EQUALIZATION_CNTL__RESERVED__SHIFT                                                       0xf
//PCIE_LANE_12_EQUALIZATION_CNTL
#define PCIE_LANE_12_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__SHIFT                                      0x0
#define PCIE_LANE_12_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__SHIFT                                 0x4
#define PCIE_LANE_12_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__SHIFT                                        0x8
#define PCIE_LANE_12_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__SHIFT                                   0xc
#define PCIE_LANE_12_EQUALIZATION_CNTL__RESERVED__SHIFT                                                       0xf
//PCIE_LANE_13_EQUALIZATION_CNTL
#define PCIE_LANE_13_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__SHIFT                                      0x0
#define PCIE_LANE_13_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__SHIFT                                 0x4
#define PCIE_LANE_13_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__SHIFT                                        0x8
#define PCIE_LANE_13_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__SHIFT                                   0xc
#define PCIE_LANE_13_EQUALIZATION_CNTL__RESERVED__SHIFT                                                       0xf
//PCIE_LANE_14_EQUALIZATION_CNTL
#define PCIE_LANE_14_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__SHIFT                                      0x0
#define PCIE_LANE_14_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__SHIFT                                 0x4
#define PCIE_LANE_14_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__SHIFT                                        0x8
#define PCIE_LANE_14_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__SHIFT                                   0xc
#define PCIE_LANE_14_EQUALIZATION_CNTL__RESERVED__SHIFT                                                       0xf
//PCIE_LANE_15_EQUALIZATION_CNTL
#define PCIE_LANE_15_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__SHIFT                                      0x0
#define PCIE_LANE_15_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__SHIFT                                 0x4
#define PCIE_LANE_15_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__SHIFT                                        0x8
#define PCIE_LANE_15_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__SHIFT                                   0xc
#define PCIE_LANE_15_EQUALIZATION_CNTL__RESERVED__SHIFT                                                       0xf
//PCIE_ACS_ENH_CAP_LIST
#define PCIE_ACS_ENH_CAP_LIST__CAP_ID__SHIFT                                                                  0x0
#define PCIE_ACS_ENH_CAP_LIST__CAP_VER__SHIFT                                                                 0x10
#define PCIE_ACS_ENH_CAP_LIST__NEXT_PTR__SHIFT                                                                0x14
//PCIE_ACS_CAP
#define PCIE_ACS_CAP__SOURCE_VALIDATION__SHIFT                                                                0x0
#define PCIE_ACS_CAP__TRANSLATION_BLOCKING__SHIFT                                                             0x1
#define PCIE_ACS_CAP__P2P_REQUEST_REDIRECT__SHIFT                                                             0x2
#define PCIE_ACS_CAP__P2P_COMPLETION_REDIRECT__SHIFT                                                          0x3
#define PCIE_ACS_CAP__UPSTREAM_FORWARDING__SHIFT                                                              0x4
#define PCIE_ACS_CAP__P2P_EGRESS_CONTROL__SHIFT                                                               0x5
#define PCIE_ACS_CAP__DIRECT_TRANSLATED_P2P__SHIFT                                                            0x6
#define PCIE_ACS_CAP__EGRESS_CONTROL_VECTOR_SIZE__SHIFT                                                       0x8
//PCIE_ACS_CNTL
#define PCIE_ACS_CNTL__SOURCE_VALIDATION_EN__SHIFT                                                            0x0
#define PCIE_ACS_CNTL__TRANSLATION_BLOCKING_EN__SHIFT                                                         0x1
#define PCIE_ACS_CNTL__P2P_REQUEST_REDIRECT_EN__SHIFT                                                         0x2
#define PCIE_ACS_CNTL__P2P_COMPLETION_REDIRECT_EN__SHIFT                                                      0x3
#define PCIE_ACS_CNTL__UPSTREAM_FORWARDING_EN__SHIFT                                                          0x4
#define PCIE_ACS_CNTL__P2P_EGRESS_CONTROL_EN__SHIFT                                                           0x5
#define PCIE_ACS_CNTL__DIRECT_TRANSLATED_P2P_EN__SHIFT                                                        0x6
//PCIE_ATS_ENH_CAP_LIST
#define PCIE_ATS_ENH_CAP_LIST__CAP_ID__SHIFT                                                                  0x0
#define PCIE_ATS_ENH_CAP_LIST__CAP_VER__SHIFT                                                                 0x10
#define PCIE_ATS_ENH_CAP_LIST__NEXT_PTR__SHIFT                                                                0x14
//PCIE_ATS_CAP
#define PCIE_ATS_CAP__INVALIDATE_Q_DEPTH__SHIFT                                                               0x0
#define PCIE_ATS_CAP__PAGE_ALIGNED_REQUEST__SHIFT                                                             0x5
#define PCIE_ATS_CAP__GLOBAL_INVALIDATE_SUPPORTED__SHIFT                                                      0x6
//PCIE_ATS_CNTL
#define PCIE_ATS_CNTL__STU__SHIFT                                                                             0x0
#define PCIE_ATS_CNTL__ATC_ENABLE__SHIFT                                                                      0xf
//PCIE_PAGE_REQ_ENH_CAP_LIST
#define PCIE_PAGE_REQ_ENH_CAP_LIST__CAP_ID__SHIFT                                                             0x0
#define PCIE_PAGE_REQ_ENH_CAP_LIST__CAP_VER__SHIFT                                                            0x10
#define PCIE_PAGE_REQ_ENH_CAP_LIST__NEXT_PTR__SHIFT                                                           0x14
//PCIE_PAGE_REQ_CNTL
#define PCIE_PAGE_REQ_CNTL__PRI_ENABLE__SHIFT                                                                 0x0
#define PCIE_PAGE_REQ_CNTL__PRI_RESET__SHIFT                                                                  0x1
//PCIE_PAGE_REQ_STATUS
#define PCIE_PAGE_REQ_STATUS__RESPONSE_FAILURE__SHIFT                                                         0x0
#define PCIE_PAGE_REQ_STATUS__UNEXPECTED_PAGE_REQ_GRP_INDEX__SHIFT                                            0x1
#define PCIE_PAGE_REQ_STATUS__STOPPED__SHIFT                                                                  0x8
#define PCIE_PAGE_REQ_STATUS__PRG_RESPONSE_PASID_REQUIRED__SHIFT                                              0xf
//PCIE_OUTSTAND_PAGE_REQ_CAPACITY
#define PCIE_OUTSTAND_PAGE_REQ_CAPACITY__OUTSTAND_PAGE_REQ_CAPACITY__SHIFT                                    0x0
//PCIE_OUTSTAND_PAGE_REQ_ALLOC
#define PCIE_OUTSTAND_PAGE_REQ_ALLOC__OUTSTAND_PAGE_REQ_ALLOC__SHIFT                                          0x0
//PCIE_PASID_ENH_CAP_LIST
#define PCIE_PASID_ENH_CAP_LIST__CAP_ID__SHIFT                                                                0x0
#define PCIE_PASID_ENH_CAP_LIST__CAP_VER__SHIFT                                                               0x10
#define PCIE_PASID_ENH_CAP_LIST__NEXT_PTR__SHIFT                                                              0x14
//PCIE_PASID_CAP
#define PCIE_PASID_CAP__PASID_EXE_PERMISSION_SUPPORTED__SHIFT                                                 0x1
#define PCIE_PASID_CAP__PASID_PRIV_MODE_SUPPORTED__SHIFT                                                      0x2
#define PCIE_PASID_CAP__MAX_PASID_WIDTH__SHIFT                                                                0x8
//PCIE_PASID_CNTL
#define PCIE_PASID_CNTL__PASID_ENABLE__SHIFT                                                                  0x0
#define PCIE_PASID_CNTL__PASID_EXE_PERMISSION_ENABLE__SHIFT                                                   0x1
#define PCIE_PASID_CNTL__PASID_PRIV_MODE_SUPPORTED_ENABLE__SHIFT                                              0x2
//PCIE_TPH_REQR_ENH_CAP_LIST
#define PCIE_TPH_REQR_ENH_CAP_LIST__CAP_ID__SHIFT                                                             0x0
#define PCIE_TPH_REQR_ENH_CAP_LIST__CAP_VER__SHIFT                                                            0x10
#define PCIE_TPH_REQR_ENH_CAP_LIST__NEXT_PTR__SHIFT                                                           0x14
//PCIE_TPH_REQR_CAP
#define PCIE_TPH_REQR_CAP__TPH_REQR_NO_ST_MODE_SUPPORTED__SHIFT                                               0x0
#define PCIE_TPH_REQR_CAP__TPH_REQR_INT_VEC_MODE_SUPPORTED__SHIFT                                             0x1
#define PCIE_TPH_REQR_CAP__TPH_REQR_DEV_SPC_MODE_SUPPORTED__SHIFT                                             0x2
#define PCIE_TPH_REQR_CAP__TPH_REQR_EXTND_TPH_REQR_SUPPORED__SHIFT                                            0x8
#define PCIE_TPH_REQR_CAP__TPH_REQR_ST_TABLE_LOCATION__SHIFT                                                  0x9
#define PCIE_TPH_REQR_CAP__TPH_REQR_ST_TABLE_SIZE__SHIFT                                                      0x10
//PCIE_TPH_REQR_CNTL
#define PCIE_TPH_REQR_CNTL__TPH_REQR_ST_MODE_SEL__SHIFT                                                       0x0
#define PCIE_TPH_REQR_CNTL__TPH_REQR_EN__SHIFT                                                                0x8
//PCIE_MC_ENH_CAP_LIST
#define PCIE_MC_ENH_CAP_LIST__CAP_ID__SHIFT                                                                   0x0
#define PCIE_MC_ENH_CAP_LIST__CAP_VER__SHIFT                                                                  0x10
#define PCIE_MC_ENH_CAP_LIST__NEXT_PTR__SHIFT                                                                 0x14
//PCIE_MC_CAP
#define PCIE_MC_CAP__MC_MAX_GROUP__SHIFT                                                                      0x0
#define PCIE_MC_CAP__MC_WIN_SIZE_REQ__SHIFT                                                                   0x8
#define PCIE_MC_CAP__MC_ECRC_REGEN_SUPP__SHIFT                                                                0xf
//PCIE_MC_CNTL
#define PCIE_MC_CNTL__MC_NUM_GROUP__SHIFT                                                                     0x0
#define PCIE_MC_CNTL__MC_ENABLE__SHIFT                                                                        0xf
//PCIE_MC_ADDR0
#define PCIE_MC_ADDR0__MC_INDEX_POS__SHIFT                                                                    0x0
#define PCIE_MC_ADDR0__MC_BASE_ADDR_0__SHIFT                                                                  0xc
//PCIE_MC_ADDR1
#define PCIE_MC_ADDR1__MC_BASE_ADDR_1__SHIFT                                                                  0x0
//PCIE_MC_RCV0
#define PCIE_MC_RCV0__MC_RECEIVE_0__SHIFT                                                                     0x0
//PCIE_MC_RCV1
#define PCIE_MC_RCV1__MC_RECEIVE_1__SHIFT                                                                     0x0
//PCIE_MC_BLOCK_ALL0
#define PCIE_MC_BLOCK_ALL0__MC_BLOCK_ALL_0__SHIFT                                                             0x0
//PCIE_MC_BLOCK_ALL1
#define PCIE_MC_BLOCK_ALL1__MC_BLOCK_ALL_1__SHIFT                                                             0x0
//PCIE_MC_BLOCK_UNTRANSLATED_0
#define PCIE_MC_BLOCK_UNTRANSLATED_0__MC_BLOCK_UNTRANSLATED_0__SHIFT                                          0x0
//PCIE_MC_BLOCK_UNTRANSLATED_1
#define PCIE_MC_BLOCK_UNTRANSLATED_1__MC_BLOCK_UNTRANSLATED_1__SHIFT                                          0x0
//PCIE_LTR_ENH_CAP_LIST
#define PCIE_LTR_ENH_CAP_LIST__CAP_ID__SHIFT                                                                  0x0
#define PCIE_LTR_ENH_CAP_LIST__CAP_VER__SHIFT                                                                 0x10
#define PCIE_LTR_ENH_CAP_LIST__NEXT_PTR__SHIFT                                                                0x14
//PCIE_LTR_CAP
#define PCIE_LTR_CAP__LTR_MAX_S_LATENCY_VALUE__SHIFT                                                          0x0
#define PCIE_LTR_CAP__LTR_MAX_S_LATENCY_SCALE__SHIFT                                                          0xa
#define PCIE_LTR_CAP__LTR_MAX_NS_LATENCY_VALUE__SHIFT                                                         0x10
#define PCIE_LTR_CAP__LTR_MAX_NS_LATENCY_SCALE__SHIFT                                                         0x1a
//PCIE_ARI_ENH_CAP_LIST
#define PCIE_ARI_ENH_CAP_LIST__CAP_ID__SHIFT                                                                  0x0
#define PCIE_ARI_ENH_CAP_LIST__CAP_VER__SHIFT                                                                 0x10
#define PCIE_ARI_ENH_CAP_LIST__NEXT_PTR__SHIFT                                                                0x14
//PCIE_ARI_CAP
#define PCIE_ARI_CAP__ARI_MFVC_FUNC_GROUPS_CAP__SHIFT                                                         0x0
#define PCIE_ARI_CAP__ARI_ACS_FUNC_GROUPS_CAP__SHIFT                                                          0x1
#define PCIE_ARI_CAP__ARI_NEXT_FUNC_NUM__SHIFT                                                                0x8
//PCIE_ARI_CNTL
#define PCIE_ARI_CNTL__ARI_MFVC_FUNC_GROUPS_EN__SHIFT                                                         0x0
#define PCIE_ARI_CNTL__ARI_ACS_FUNC_GROUPS_EN__SHIFT                                                          0x1
#define PCIE_ARI_CNTL__ARI_FUNCTION_GROUP__SHIFT                                                              0x4
//PCIE_SRIOV_ENH_CAP_LIST
#define PCIE_SRIOV_ENH_CAP_LIST__CAP_ID__SHIFT                                                                0x0
#define PCIE_SRIOV_ENH_CAP_LIST__CAP_VER__SHIFT                                                               0x10
#define PCIE_SRIOV_ENH_CAP_LIST__NEXT_PTR__SHIFT                                                              0x14
//PCIE_SRIOV_CAP
#define PCIE_SRIOV_CAP__SRIOV_VF_MIGRATION_CAP__SHIFT                                                         0x0
#define PCIE_SRIOV_CAP__SRIOV_ARI_CAP_HIERARCHY_PRESERVED__SHIFT                                              0x1
#define PCIE_SRIOV_CAP__SRIOV_VF_MIGRATION_INTR_MSG_NUM__SHIFT                                                0x15
//PCIE_SRIOV_CONTROL
#define PCIE_SRIOV_CONTROL__SRIOV_VF_ENABLE__SHIFT                                                            0x0
#define PCIE_SRIOV_CONTROL__SRIOV_VF_MIGRATION_ENABLE__SHIFT                                                  0x1
#define PCIE_SRIOV_CONTROL__SRIOV_VF_MIGRATION_INTR_ENABLE__SHIFT                                             0x2
#define PCIE_SRIOV_CONTROL__SRIOV_VF_MSE__SHIFT                                                               0x3
#define PCIE_SRIOV_CONTROL__SRIOV_ARI_CAP_HIERARCHY__SHIFT                                                    0x4
//PCIE_SRIOV_STATUS
#define PCIE_SRIOV_STATUS__SRIOV_VF_MIGRATION_STATUS__SHIFT                                                   0x0
//PCIE_SRIOV_INITIAL_VFS
#define PCIE_SRIOV_INITIAL_VFS__SRIOV_INITIAL_VFS__SHIFT                                                      0x0
//PCIE_SRIOV_TOTAL_VFS
#define PCIE_SRIOV_TOTAL_VFS__SRIOV_TOTAL_VFS__SHIFT                                                          0x0
//PCIE_SRIOV_NUM_VFS
#define PCIE_SRIOV_NUM_VFS__SRIOV_NUM_VFS__SHIFT                                                              0x0
//PCIE_SRIOV_FUNC_DEP_LINK
#define PCIE_SRIOV_FUNC_DEP_LINK__SRIOV_FUNC_DEP_LINK__SHIFT                                                  0x0
//PCIE_SRIOV_FIRST_VF_OFFSET
#define PCIE_SRIOV_FIRST_VF_OFFSET__SRIOV_FIRST_VF_OFFSET__SHIFT                                              0x0
//PCIE_SRIOV_VF_STRIDE
#define PCIE_SRIOV_VF_STRIDE__SRIOV_VF_STRIDE__SHIFT                                                          0x0
//PCIE_SRIOV_VF_DEVICE_ID
#define PCIE_SRIOV_VF_DEVICE_ID__SRIOV_VF_DEVICE_ID__SHIFT                                                    0x0
//PCIE_SRIOV_SUPPORTED_PAGE_SIZE
#define PCIE_SRIOV_SUPPORTED_PAGE_SIZE__SRIOV_SUPPORTED_PAGE_SIZE__SHIFT                                      0x0
//PCIE_SRIOV_SYSTEM_PAGE_SIZE
#define PCIE_SRIOV_SYSTEM_PAGE_SIZE__SRIOV_SYSTEM_PAGE_SIZE__SHIFT                                            0x0
//PCIE_SRIOV_VF_BASE_ADDR_0
#define PCIE_SRIOV_VF_BASE_ADDR_0__VF_BASE_ADDR__SHIFT                                                        0x0
//PCIE_SRIOV_VF_BASE_ADDR_1
#define PCIE_SRIOV_VF_BASE_ADDR_1__VF_BASE_ADDR__SHIFT                                                        0x0
//PCIE_SRIOV_VF_BASE_ADDR_2
#define PCIE_SRIOV_VF_BASE_ADDR_2__VF_BASE_ADDR__SHIFT                                                        0x0
//PCIE_SRIOV_VF_BASE_ADDR_3
#define PCIE_SRIOV_VF_BASE_ADDR_3__VF_BASE_ADDR__SHIFT                                                        0x0
//PCIE_SRIOV_VF_BASE_ADDR_4
#define PCIE_SRIOV_VF_BASE_ADDR_4__VF_BASE_ADDR__SHIFT                                                        0x0
//PCIE_SRIOV_VF_BASE_ADDR_5
#define PCIE_SRIOV_VF_BASE_ADDR_5__VF_BASE_ADDR__SHIFT                                                        0x0
//PCIE_SRIOV_VF_MIGRATION_STATE_ARRAY_OFFSET
#define PCIE_SRIOV_VF_MIGRATION_STATE_ARRAY_OFFSET__SRIOV_VF_MIGRATION_STATE_BIF__SHIFT                       0x0
#define PCIE_SRIOV_VF_MIGRATION_STATE_ARRAY_OFFSET__SRIOV_VF_MIGRATION_STATE_ARRAY_OFFSET__SHIFT              0x3
//PCIE_VENDOR_SPECIFIC_ENH_CAP_LIST_GPUIOV
#define PCIE_VENDOR_SPECIFIC_ENH_CAP_LIST_GPUIOV__CAP_ID__SHIFT                                               0x0
#define PCIE_VENDOR_SPECIFIC_ENH_CAP_LIST_GPUIOV__CAP_VER__SHIFT                                              0x10
#define PCIE_VENDOR_SPECIFIC_ENH_CAP_LIST_GPUIOV__NEXT_PTR__SHIFT                                             0x14
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV__VSEC_ID__SHIFT                                                       0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV__VSEC_REV__SHIFT                                                      0x10
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV__VSEC_LENGTH__SHIFT                                                   0x14
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_SRIOV_SHADOW
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_SRIOV_SHADOW__VF_EN__SHIFT                                            0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_SRIOV_SHADOW__VF_NUM__SHIFT                                           0x10
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__GFX_CMD_COMPLETE_INTR_EN__SHIFT                          0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__GFX_HANG_SELF_RECOVERED_INTR_EN__SHIFT                   0x1
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__GFX_HANG_NEED_FLR_INTR_EN__SHIFT                         0x2
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__GFX_VM_BUSY_TRANSITION_INTR_EN__SHIFT                    0x3
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__UVD_CMD_COMPLETE_INTR_EN__SHIFT                          0x8
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__UVD_HANG_SELF_RECOVERED_INTR_EN__SHIFT                   0x9
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__UVD_HANG_NEED_FLR_INTR_EN__SHIFT                         0xa
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__UVD_VM_BUSY_TRANSITION_INTR_EN__SHIFT                    0xb
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__VCE_CMD_COMPLETE_INTR_EN__SHIFT                          0x10
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__VCE_HANG_SELF_RECOVERED_INTR_EN__SHIFT                   0x11
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__VCE_HANG_NEED_FLR_INTR_EN__SHIFT                         0x12
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__VCE_VM_BUSY_TRANSITION_INTR_EN__SHIFT                    0x13
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__HVVM_MAILBOX_TRN_ACK_INTR_EN__SHIFT                      0x18
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__HVVM_MAILBOX_RCV_VALID_INTR_EN__SHIFT                    0x19
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__GFX_CMD_COMPLETE_INTR_STATUS__SHIFT                      0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__GFX_HANG_SELF_RECOVERED_INTR_STATUS__SHIFT               0x1
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__GFX_HANG_NEED_FLR_INTR_STATUS__SHIFT                     0x2
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__GFX_VM_BUSY_TRANSITION_INTR_STATUS__SHIFT                0x3
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__UVD_CMD_COMPLETE_INTR_STATUS__SHIFT                      0x8
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__UVD_HANG_SELF_RECOVERED_INTR_STATUS__SHIFT               0x9
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__UVD_HANG_NEED_FLR_INTR_STATUS__SHIFT                     0xa
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__UVD_VM_BUSY_TRANSITION_INTR_STATUS__SHIFT                0xb
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__VCE_CMD_COMPLETE_INTR_STATUS__SHIFT                      0x10
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__VCE_HANG_SELF_RECOVERED_INTR_STATUS__SHIFT               0x11
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__VCE_HANG_NEED_FLR_INTR_STATUS__SHIFT                     0x12
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__VCE_VM_BUSY_TRANSITION_INTR_STATUS__SHIFT                0x13
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__HVVM_MAILBOX_TRN_ACK_INTR_STATUS__SHIFT                  0x18
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__HVVM_MAILBOX_RCV_VALID_INTR_STATUS__SHIFT                0x19
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_RESET_CONTROL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_RESET_CONTROL__SOFT_PF_FLR__SHIFT                                     0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW0__VF_INDEX__SHIFT                                        0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW0__TRN_MSG_DATA__SHIFT                                    0x8
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW0__TRN_MSG_VALID__SHIFT                                   0xf
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW0__RCV_MSG_DATA__SHIFT                                    0x10
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW0__RCV_MSG_ACK__SHIFT                                     0x18
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF0_TRN_ACK__SHIFT                                     0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF0_RCV_VALID__SHIFT                                   0x1
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF1_TRN_ACK__SHIFT                                     0x2
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF1_RCV_VALID__SHIFT                                   0x3
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF2_TRN_ACK__SHIFT                                     0x4
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF2_RCV_VALID__SHIFT                                   0x5
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF3_TRN_ACK__SHIFT                                     0x6
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF3_RCV_VALID__SHIFT                                   0x7
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF4_TRN_ACK__SHIFT                                     0x8
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF4_RCV_VALID__SHIFT                                   0x9
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF5_TRN_ACK__SHIFT                                     0xa
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF5_RCV_VALID__SHIFT                                   0xb
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF6_TRN_ACK__SHIFT                                     0xc
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF6_RCV_VALID__SHIFT                                   0xd
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF7_TRN_ACK__SHIFT                                     0xe
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF7_RCV_VALID__SHIFT                                   0xf
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF8_TRN_ACK__SHIFT                                     0x10
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF8_RCV_VALID__SHIFT                                   0x11
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF9_TRN_ACK__SHIFT                                     0x12
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF9_RCV_VALID__SHIFT                                   0x13
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF10_TRN_ACK__SHIFT                                    0x14
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF10_RCV_VALID__SHIFT                                  0x15
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF11_TRN_ACK__SHIFT                                    0x16
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF11_RCV_VALID__SHIFT                                  0x17
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF12_TRN_ACK__SHIFT                                    0x18
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF12_RCV_VALID__SHIFT                                  0x19
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF13_TRN_ACK__SHIFT                                    0x1a
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF13_RCV_VALID__SHIFT                                  0x1b
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF14_TRN_ACK__SHIFT                                    0x1c
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF14_RCV_VALID__SHIFT                                  0x1d
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF15_TRN_ACK__SHIFT                                    0x1e
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF15_RCV_VALID__SHIFT                                  0x1f
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW2
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW2__PF_TRN_ACK__SHIFT                                      0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW2__PF_RCV_VALID__SHIFT                                    0x1
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_CONTEXT
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_CONTEXT__CONTEXT_SIZE__SHIFT                                          0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_CONTEXT__LOC__SHIFT                                                   0x7
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_CONTEXT__CONTEXT_OFFSET__SHIFT                                        0xa
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_TOTAL_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_TOTAL_FB__TOTAL_FB_AVAILABLE__SHIFT                                   0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_TOTAL_FB__TOTAL_FB_CONSUMED__SHIFT                                    0x10
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_OFFSETS
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_OFFSETS__UVDSCH_OFFSET__SHIFT                                         0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_OFFSETS__VCESCH_OFFSET__SHIFT                                         0x8
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_OFFSETS__GFXSCH_OFFSET__SHIFT                                         0x10
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF0_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF0_FB__VF0_FB_SIZE__SHIFT                                            0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF0_FB__VF0_FB_OFFSET__SHIFT                                          0x10
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF1_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF1_FB__VF1_FB_SIZE__SHIFT                                            0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF1_FB__VF1_FB_OFFSET__SHIFT                                          0x10
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF2_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF2_FB__VF2_FB_SIZE__SHIFT                                            0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF2_FB__VF2_FB_OFFSET__SHIFT                                          0x10
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF3_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF3_FB__VF3_FB_SIZE__SHIFT                                            0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF3_FB__VF3_FB_OFFSET__SHIFT                                          0x10
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF4_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF4_FB__VF4_FB_SIZE__SHIFT                                            0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF4_FB__VF4_FB_OFFSET__SHIFT                                          0x10
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF5_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF5_FB__VF5_FB_SIZE__SHIFT                                            0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF5_FB__VF5_FB_OFFSET__SHIFT                                          0x10
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF6_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF6_FB__VF6_FB_SIZE__SHIFT                                            0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF6_FB__VF6_FB_OFFSET__SHIFT                                          0x10
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF7_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF7_FB__VF7_FB_SIZE__SHIFT                                            0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF7_FB__VF7_FB_OFFSET__SHIFT                                          0x10
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF8_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF8_FB__VF8_FB_SIZE__SHIFT                                            0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF8_FB__VF8_FB_OFFSET__SHIFT                                          0x10
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF9_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF9_FB__VF9_FB_SIZE__SHIFT                                            0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF9_FB__VF9_FB_OFFSET__SHIFT                                          0x10
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF10_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF10_FB__VF10_FB_SIZE__SHIFT                                          0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF10_FB__VF10_FB_OFFSET__SHIFT                                        0x10
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF11_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF11_FB__VF11_FB_SIZE__SHIFT                                          0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF11_FB__VF11_FB_OFFSET__SHIFT                                        0x10
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF12_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF12_FB__VF12_FB_SIZE__SHIFT                                          0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF12_FB__VF12_FB_OFFSET__SHIFT                                        0x10
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF13_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF13_FB__VF13_FB_SIZE__SHIFT                                          0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF13_FB__VF13_FB_OFFSET__SHIFT                                        0x10
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF14_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF14_FB__VF14_FB_SIZE__SHIFT                                          0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF14_FB__VF14_FB_OFFSET__SHIFT                                        0x10
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF15_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF15_FB__VF15_FB_SIZE__SHIFT                                          0x0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF15_FB__VF15_FB_OFFSET__SHIFT                                        0x10
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW0__DW0__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW1
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW1__DW1__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW2
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW2__DW2__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW3
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW3__DW3__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW4
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW4__DW4__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW5
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW5__DW5__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW6
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW6__DW6__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW7
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW7__DW7__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW0__DW0__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW1
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW1__DW1__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW2
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW2__DW2__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW3
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW3__DW3__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW4
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW4__DW4__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW5
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW5__DW5__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW6
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW6__DW6__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW7
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW7__DW7__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW0__DW0__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW1
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW1__DW1__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW2
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW2__DW2__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW3
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW3__DW3__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW4
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW4__DW4__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW5
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW5__DW5__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW6
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW6__DW6__SHIFT                                                0x0
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW7
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW7__DW7__SHIFT                                                0x0


// addressBlock: bif_cfg_dev0_swds_bifcfgdecp
//SUB_BUS_NUMBER_LATENCY
#define SUB_BUS_NUMBER_LATENCY__PRIMARY_BUS__SHIFT                                                            0x0
#define SUB_BUS_NUMBER_LATENCY__SECONDARY_BUS__SHIFT                                                          0x8
#define SUB_BUS_NUMBER_LATENCY__SUB_BUS_NUM__SHIFT                                                            0x10
#define SUB_BUS_NUMBER_LATENCY__SECONDARY_LATENCY_TIMER__SHIFT                                                0x18
//IO_BASE_LIMIT
#define IO_BASE_LIMIT__IO_BASE_TYPE__SHIFT                                                                    0x0
#define IO_BASE_LIMIT__IO_BASE__SHIFT                                                                         0x4
#define IO_BASE_LIMIT__IO_LIMIT_TYPE__SHIFT                                                                   0x8
#define IO_BASE_LIMIT__IO_LIMIT__SHIFT                                                                        0xc
//SECONDARY_STATUS
#define SECONDARY_STATUS__CAP_LIST__SHIFT                                                                     0x4
#define SECONDARY_STATUS__PCI_66_EN__SHIFT                                                                    0x5
#define SECONDARY_STATUS__FAST_BACK_CAPABLE__SHIFT                                                            0x7
#define SECONDARY_STATUS__MASTER_DATA_PARITY_ERROR__SHIFT                                                     0x8
#define SECONDARY_STATUS__DEVSEL_TIMING__SHIFT                                                                0x9
#define SECONDARY_STATUS__SIGNAL_TARGET_ABORT__SHIFT                                                          0xb
#define SECONDARY_STATUS__RECEIVED_TARGET_ABORT__SHIFT                                                        0xc
#define SECONDARY_STATUS__RECEIVED_MASTER_ABORT__SHIFT                                                        0xd
#define SECONDARY_STATUS__RECEIVED_SYSTEM_ERROR__SHIFT                                                        0xe
#define SECONDARY_STATUS__PARITY_ERROR_DETECTED__SHIFT                                                        0xf
//MEM_BASE_LIMIT
#define MEM_BASE_LIMIT__MEM_BASE_TYPE__SHIFT                                                                  0x0
#define MEM_BASE_LIMIT__MEM_BASE_31_20__SHIFT                                                                 0x4
#define MEM_BASE_LIMIT__MEM_LIMIT_TYPE__SHIFT                                                                 0x10
#define MEM_BASE_LIMIT__MEM_LIMIT_31_20__SHIFT                                                                0x14
//PREF_BASE_LIMIT
#define PREF_BASE_LIMIT__PREF_MEM_BASE_TYPE__SHIFT                                                            0x0
#define PREF_BASE_LIMIT__PREF_MEM_BASE_31_20__SHIFT                                                           0x4
#define PREF_BASE_LIMIT__PREF_MEM_LIMIT_TYPE__SHIFT                                                           0x10
#define PREF_BASE_LIMIT__PREF_MEM_LIMIT_31_20__SHIFT                                                          0x14
//PREF_BASE_UPPER
#define PREF_BASE_UPPER__PREF_BASE_UPPER__SHIFT                                                               0x0
//PREF_LIMIT_UPPER
#define PREF_LIMIT_UPPER__PREF_LIMIT_UPPER__SHIFT                                                             0x0
//IO_BASE_LIMIT_HI
#define IO_BASE_LIMIT_HI__IO_BASE_31_16__SHIFT                                                                0x0
#define IO_BASE_LIMIT_HI__IO_LIMIT_31_16__SHIFT                                                               0x10
//IRQ_BRIDGE_CNTL
#define IRQ_BRIDGE_CNTL__PARITY_RESPONSE_EN__SHIFT                                                            0x0
#define IRQ_BRIDGE_CNTL__SERR_EN__SHIFT                                                                       0x1
#define IRQ_BRIDGE_CNTL__ISA_EN__SHIFT                                                                        0x2
#define IRQ_BRIDGE_CNTL__VGA_EN__SHIFT                                                                        0x3
#define IRQ_BRIDGE_CNTL__VGA_DEC__SHIFT                                                                       0x4
#define IRQ_BRIDGE_CNTL__MASTER_ABORT_MODE__SHIFT                                                             0x5
#define IRQ_BRIDGE_CNTL__SECONDARY_BUS_RESET__SHIFT                                                           0x6
#define IRQ_BRIDGE_CNTL__FAST_B2B_EN__SHIFT                                                                   0x7
//SLOT_CAP
#define SLOT_CAP__ATTN_BUTTON_PRESENT__SHIFT                                                                  0x0
#define SLOT_CAP__PWR_CONTROLLER_PRESENT__SHIFT                                                               0x1
#define SLOT_CAP__MRL_SENSOR_PRESENT__SHIFT                                                                   0x2
#define SLOT_CAP__ATTN_INDICATOR_PRESENT__SHIFT                                                               0x3
#define SLOT_CAP__PWR_INDICATOR_PRESENT__SHIFT                                                                0x4
#define SLOT_CAP__HOTPLUG_SURPRISE__SHIFT                                                                     0x5
#define SLOT_CAP__HOTPLUG_CAPABLE__SHIFT                                                                      0x6
#define SLOT_CAP__SLOT_PWR_LIMIT_VALUE__SHIFT                                                                 0x7
#define SLOT_CAP__SLOT_PWR_LIMIT_SCALE__SHIFT                                                                 0xf
#define SLOT_CAP__ELECTROMECH_INTERLOCK_PRESENT__SHIFT                                                        0x11
#define SLOT_CAP__NO_COMMAND_COMPLETED_SUPPORTED__SHIFT                                                       0x12
#define SLOT_CAP__PHYSICAL_SLOT_NUM__SHIFT                                                                    0x13
//SLOT_CNTL
#define SLOT_CNTL__ATTN_BUTTON_PRESSED_EN__SHIFT                                                              0x0
#define SLOT_CNTL__PWR_FAULT_DETECTED_EN__SHIFT                                                               0x1
#define SLOT_CNTL__MRL_SENSOR_CHANGED_EN__SHIFT                                                               0x2
#define SLOT_CNTL__PRESENCE_DETECT_CHANGED_EN__SHIFT                                                          0x3
#define SLOT_CNTL__COMMAND_COMPLETED_INTR_EN__SHIFT                                                           0x4
#define SLOT_CNTL__HOTPLUG_INTR_EN__SHIFT                                                                     0x5
#define SLOT_CNTL__ATTN_INDICATOR_CNTL__SHIFT                                                                 0x6
#define SLOT_CNTL__PWR_INDICATOR_CNTL__SHIFT                                                                  0x8
#define SLOT_CNTL__PWR_CONTROLLER_CNTL__SHIFT                                                                 0xa
#define SLOT_CNTL__ELECTROMECH_INTERLOCK_CNTL__SHIFT                                                          0xb
#define SLOT_CNTL__DL_STATE_CHANGED_EN__SHIFT                                                                 0xc
//SLOT_STATUS
#define SLOT_STATUS__ATTN_BUTTON_PRESSED__SHIFT                                                               0x0
#define SLOT_STATUS__PWR_FAULT_DETECTED__SHIFT                                                                0x1
#define SLOT_STATUS__MRL_SENSOR_CHANGED__SHIFT                                                                0x2
#define SLOT_STATUS__PRESENCE_DETECT_CHANGED__SHIFT                                                           0x3
#define SLOT_STATUS__COMMAND_COMPLETED__SHIFT                                                                 0x4
#define SLOT_STATUS__MRL_SENSOR_STATE__SHIFT                                                                  0x5
#define SLOT_STATUS__PRESENCE_DETECT_STATE__SHIFT                                                             0x6
#define SLOT_STATUS__ELECTROMECH_INTERLOCK_STATUS__SHIFT                                                      0x7
#define SLOT_STATUS__DL_STATE_CHANGED__SHIFT                                                                  0x8
//SSID_CAP_LIST
#define SSID_CAP_LIST__CAP_ID__SHIFT                                                                          0x0
#define SSID_CAP_LIST__NEXT_PTR__SHIFT                                                                        0x8
//SSID_CAP
#define SSID_CAP__SUBSYSTEM_VENDOR_ID__SHIFT                                                                  0x0
#define SSID_CAP__SUBSYSTEM_ID__SHIFT                                                                         0x10


// addressBlock: rcc_shadow_reg_shadowdec
//SHADOW_COMMAND
#define SHADOW_COMMAND__IOEN_UP__SHIFT                                                                        0x0
#define SHADOW_COMMAND__MEMEN_UP__SHIFT                                                                       0x1
//SHADOW_BASE_ADDR_1
#define SHADOW_BASE_ADDR_1__BAR1_UP__SHIFT                                                                    0x0
//SHADOW_BASE_ADDR_2
#define SHADOW_BASE_ADDR_2__BAR2_UP__SHIFT                                                                    0x0
//SHADOW_SUB_BUS_NUMBER_LATENCY
#define SHADOW_SUB_BUS_NUMBER_LATENCY__SECONDARY_BUS_UP__SHIFT                                                0x8
#define SHADOW_SUB_BUS_NUMBER_LATENCY__SUB_BUS_NUM_UP__SHIFT                                                  0x10
//SHADOW_IO_BASE_LIMIT
#define SHADOW_IO_BASE_LIMIT__IO_BASE_UP__SHIFT                                                               0x4
#define SHADOW_IO_BASE_LIMIT__IO_LIMIT_UP__SHIFT                                                              0xc
//SHADOW_MEM_BASE_LIMIT
#define SHADOW_MEM_BASE_LIMIT__MEM_BASE_TYPE__SHIFT                                                           0x0
#define SHADOW_MEM_BASE_LIMIT__MEM_BASE_31_20_UP__SHIFT                                                       0x4
#define SHADOW_MEM_BASE_LIMIT__MEM_LIMIT_TYPE__SHIFT                                                          0x10
#define SHADOW_MEM_BASE_LIMIT__MEM_LIMIT_31_20_UP__SHIFT                                                      0x14
//SHADOW_PREF_BASE_LIMIT
#define SHADOW_PREF_BASE_LIMIT__PREF_MEM_BASE_TYPE__SHIFT                                                     0x0
#define SHADOW_PREF_BASE_LIMIT__PREF_MEM_BASE_31_20_UP__SHIFT                                                 0x4
#define SHADOW_PREF_BASE_LIMIT__PREF_MEM_LIMIT_TYPE__SHIFT                                                    0x10
#define SHADOW_PREF_BASE_LIMIT__PREF_MEM_LIMIT_31_20_UP__SHIFT                                                0x14
//SHADOW_PREF_BASE_UPPER
#define SHADOW_PREF_BASE_UPPER__PREF_BASE_UPPER_UP__SHIFT                                                     0x0
//SHADOW_PREF_LIMIT_UPPER
#define SHADOW_PREF_LIMIT_UPPER__PREF_LIMIT_UPPER_UP__SHIFT                                                   0x0
//SHADOW_IO_BASE_LIMIT_HI
#define SHADOW_IO_BASE_LIMIT_HI__IO_BASE_31_16_UP__SHIFT                                                      0x0
#define SHADOW_IO_BASE_LIMIT_HI__IO_LIMIT_31_16_UP__SHIFT                                                     0x10
//SHADOW_IRQ_BRIDGE_CNTL
#define SHADOW_IRQ_BRIDGE_CNTL__ISA_EN_UP__SHIFT                                                              0x2
#define SHADOW_IRQ_BRIDGE_CNTL__VGA_EN_UP__SHIFT                                                              0x3
#define SHADOW_IRQ_BRIDGE_CNTL__VGA_DEC_UP__SHIFT                                                             0x4
#define SHADOW_IRQ_BRIDGE_CNTL__SECONDARY_BUS_RESET_UP__SHIFT                                                 0x6
//SUC_INDEX
#define SUC_INDEX__SUC_INDEX__SHIFT                                                                           0x0
//SUC_DATA
#define SUC_DATA__SUC_DATA__SHIFT                                                                             0x0


// addressBlock: bif_bx_pf_SUMDEC
//SUM_INDEX
#define SUM_INDEX__SUM_INDEX__SHIFT                                                                           0x0
//SUM_DATA
#define SUM_DATA__SUM_DATA__SHIFT                                                                             0x0


// addressBlock: gdc_GDCDEC
//A2S_CNTL_CL0
#define A2S_CNTL_CL0__NSNOOP_MAP__SHIFT                                                                       0x0
#define A2S_CNTL_CL0__REQPASSPW_VC0_MAP__SHIFT                                                                0x2
#define A2S_CNTL_CL0__REQPASSPW_NVC0_MAP__SHIFT                                                               0x4
#define A2S_CNTL_CL0__REQRSPPASSPW_VC0_MAP__SHIFT                                                             0x6
#define A2S_CNTL_CL0__REQRSPPASSPW_NVC0_MAP__SHIFT                                                            0x8
#define A2S_CNTL_CL0__BLKLVL_MAP__SHIFT                                                                       0xa
#define A2S_CNTL_CL0__DATERR_MAP__SHIFT                                                                       0xc
#define A2S_CNTL_CL0__EXOKAY_WR_MAP__SHIFT                                                                    0xe
#define A2S_CNTL_CL0__EXOKAY_RD_MAP__SHIFT                                                                    0x10
#define A2S_CNTL_CL0__RESP_WR_MAP__SHIFT                                                                      0x12
#define A2S_CNTL_CL0__RESP_RD_MAP__SHIFT                                                                      0x14
//A2S_CNTL_CL1
#define A2S_CNTL_CL1__NSNOOP_MAP__SHIFT                                                                       0x0
#define A2S_CNTL_CL1__REQPASSPW_VC0_MAP__SHIFT                                                                0x2
#define A2S_CNTL_CL1__REQPASSPW_NVC0_MAP__SHIFT                                                               0x4
#define A2S_CNTL_CL1__REQRSPPASSPW_VC0_MAP__SHIFT                                                             0x6
#define A2S_CNTL_CL1__REQRSPPASSPW_NVC0_MAP__SHIFT                                                            0x8
#define A2S_CNTL_CL1__BLKLVL_MAP__SHIFT                                                                       0xa
#define A2S_CNTL_CL1__DATERR_MAP__SHIFT                                                                       0xc
#define A2S_CNTL_CL1__EXOKAY_WR_MAP__SHIFT                                                                    0xe
#define A2S_CNTL_CL1__EXOKAY_RD_MAP__SHIFT                                                                    0x10
#define A2S_CNTL_CL1__RESP_WR_MAP__SHIFT                                                                      0x12
#define A2S_CNTL_CL1__RESP_RD_MAP__SHIFT                                                                      0x14
//A2S_CNTL_CL2
#define A2S_CNTL_CL2__NSNOOP_MAP__SHIFT                                                                       0x0
#define A2S_CNTL_CL2__REQPASSPW_VC0_MAP__SHIFT                                                                0x2
#define A2S_CNTL_CL2__REQPASSPW_NVC0_MAP__SHIFT                                                               0x4
#define A2S_CNTL_CL2__REQRSPPASSPW_VC0_MAP__SHIFT                                                             0x6
#define A2S_CNTL_CL2__REQRSPPASSPW_NVC0_MAP__SHIFT                                                            0x8
#define A2S_CNTL_CL2__BLKLVL_MAP__SHIFT                                                                       0xa
#define A2S_CNTL_CL2__DATERR_MAP__SHIFT                                                                       0xc
#define A2S_CNTL_CL2__EXOKAY_WR_MAP__SHIFT                                                                    0xe
#define A2S_CNTL_CL2__EXOKAY_RD_MAP__SHIFT                                                                    0x10
#define A2S_CNTL_CL2__RESP_WR_MAP__SHIFT                                                                      0x12
#define A2S_CNTL_CL2__RESP_RD_MAP__SHIFT                                                                      0x14
//A2S_CNTL_CL3
#define A2S_CNTL_CL3__NSNOOP_MAP__SHIFT                                                                       0x0
#define A2S_CNTL_CL3__REQPASSPW_VC0_MAP__SHIFT                                                                0x2
#define A2S_CNTL_CL3__REQPASSPW_NVC0_MAP__SHIFT                                                               0x4
#define A2S_CNTL_CL3__REQRSPPASSPW_VC0_MAP__SHIFT                                                             0x6
#define A2S_CNTL_CL3__REQRSPPASSPW_NVC0_MAP__SHIFT                                                            0x8
#define A2S_CNTL_CL3__BLKLVL_MAP__SHIFT                                                                       0xa
#define A2S_CNTL_CL3__DATERR_MAP__SHIFT                                                                       0xc
#define A2S_CNTL_CL3__EXOKAY_WR_MAP__SHIFT                                                                    0xe
#define A2S_CNTL_CL3__EXOKAY_RD_MAP__SHIFT                                                                    0x10
#define A2S_CNTL_CL3__RESP_WR_MAP__SHIFT                                                                      0x12
#define A2S_CNTL_CL3__RESP_RD_MAP__SHIFT                                                                      0x14
//A2S_CNTL_CL4
#define A2S_CNTL_CL4__NSNOOP_MAP__SHIFT                                                                       0x0
#define A2S_CNTL_CL4__REQPASSPW_VC0_MAP__SHIFT                                                                0x2
#define A2S_CNTL_CL4__REQPASSPW_NVC0_MAP__SHIFT                                                               0x4
#define A2S_CNTL_CL4__REQRSPPASSPW_VC0_MAP__SHIFT                                                             0x6
#define A2S_CNTL_CL4__REQRSPPASSPW_NVC0_MAP__SHIFT                                                            0x8
#define A2S_CNTL_CL4__BLKLVL_MAP__SHIFT                                                                       0xa
#define A2S_CNTL_CL4__DATERR_MAP__SHIFT                                                                       0xc
#define A2S_CNTL_CL4__EXOKAY_WR_MAP__SHIFT                                                                    0xe
#define A2S_CNTL_CL4__EXOKAY_RD_MAP__SHIFT                                                                    0x10
#define A2S_CNTL_CL4__RESP_WR_MAP__SHIFT                                                                      0x12
#define A2S_CNTL_CL4__RESP_RD_MAP__SHIFT                                                                      0x14
//A2S_CNTL_SW0
#define A2S_CNTL_SW0__WR_TAG_SET_MIN__SHIFT                                                                   0x0
#define A2S_CNTL_SW0__RD_TAG_SET_MIN__SHIFT                                                                   0x3
#define A2S_CNTL_SW0__FORCE_RSP_REORDER_EN__SHIFT                                                             0x6
#define A2S_CNTL_SW0__RSP_REORDER_DIS__SHIFT                                                                  0x7
#define A2S_CNTL_SW0__WRRSP_ACCUM_SEL__SHIFT                                                                  0x8
#define A2S_CNTL_SW0__SDP_WR_CHAIN_DIS__SHIFT                                                                 0x9
#define A2S_CNTL_SW0__WRRSP_TAGFIFO_CONT_RD_DIS__SHIFT                                                        0xa
#define A2S_CNTL_SW0__RDRSP_TAGFIFO_CONT_RD_DIS__SHIFT                                                        0xb
#define A2S_CNTL_SW0__RDRSP_STS_DATSTS_PRIORITY__SHIFT                                                        0xc
#define A2S_CNTL_SW0__WRR_RD_WEIGHT__SHIFT                                                                    0x10
#define A2S_CNTL_SW0__WRR_WR_WEIGHT__SHIFT                                                                    0x18
//A2S_CNTL_SW1
#define A2S_CNTL_SW1__WR_TAG_SET_MIN__SHIFT                                                                   0x0
#define A2S_CNTL_SW1__RD_TAG_SET_MIN__SHIFT                                                                   0x3
#define A2S_CNTL_SW1__FORCE_RSP_REORDER_EN__SHIFT                                                             0x6
#define A2S_CNTL_SW1__RSP_REORDER_DIS__SHIFT                                                                  0x7
#define A2S_CNTL_SW1__WRRSP_ACCUM_SEL__SHIFT                                                                  0x8
#define A2S_CNTL_SW1__SDP_WR_CHAIN_DIS__SHIFT                                                                 0x9
#define A2S_CNTL_SW1__WRRSP_TAGFIFO_CONT_RD_DIS__SHIFT                                                        0xa
#define A2S_CNTL_SW1__RDRSP_TAGFIFO_CONT_RD_DIS__SHIFT                                                        0xb
#define A2S_CNTL_SW1__RDRSP_STS_DATSTS_PRIORITY__SHIFT                                                        0xc
#define A2S_CNTL_SW1__WRR_RD_WEIGHT__SHIFT                                                                    0x10
#define A2S_CNTL_SW1__WRR_WR_WEIGHT__SHIFT                                                                    0x18
//A2S_CNTL_SW2
#define A2S_CNTL_SW2__WR_TAG_SET_MIN__SHIFT                                                                   0x0
#define A2S_CNTL_SW2__RD_TAG_SET_MIN__SHIFT                                                                   0x3
#define A2S_CNTL_SW2__FORCE_RSP_REORDER_EN__SHIFT                                                             0x6
#define A2S_CNTL_SW2__RSP_REORDER_DIS__SHIFT                                                                  0x7
#define A2S_CNTL_SW2__WRRSP_ACCUM_SEL__SHIFT                                                                  0x8
#define A2S_CNTL_SW2__SDP_WR_CHAIN_DIS__SHIFT                                                                 0x9
#define A2S_CNTL_SW2__WRRSP_TAGFIFO_CONT_RD_DIS__SHIFT                                                        0xa
#define A2S_CNTL_SW2__RDRSP_TAGFIFO_CONT_RD_DIS__SHIFT                                                        0xb
#define A2S_CNTL_SW2__RDRSP_STS_DATSTS_PRIORITY__SHIFT                                                        0xc
#define A2S_CNTL_SW2__WRR_RD_WEIGHT__SHIFT                                                                    0x10
#define A2S_CNTL_SW2__WRR_WR_WEIGHT__SHIFT                                                                    0x18
//NGDC_MGCG_CTRL
#define NGDC_MGCG_CTRL__NGDC_MGCG_EN__SHIFT                                                                   0x0
#define NGDC_MGCG_CTRL__NGDC_MGCG_MODE__SHIFT                                                                 0x1
#define NGDC_MGCG_CTRL__NGDC_MGCG_HYSTERESIS__SHIFT                                                           0x2
//A2S_MISC_CNTL
#define A2S_MISC_CNTL__BLKLVL_FOR_MSG__SHIFT                                                                  0x0
#define A2S_MISC_CNTL__RESERVE_2_CRED_FOR_NPWR_REQ_DIS__SHIFT                                                 0x2
//NGDC_SDP_PORT_CTRL
#define NGDC_SDP_PORT_CTRL__SDP_DISCON_HYSTERESIS__SHIFT                                                      0x0
//NGDC_RESERVED_0
#define NGDC_RESERVED_0__RESERVED__SHIFT                                                                      0x0
//NGDC_RESERVED_1
#define NGDC_RESERVED_1__RESERVED__SHIFT                                                                      0x0
//BIF_SDMA0_DOORBELL_RANGE
#define BIF_SDMA0_DOORBELL_RANGE__OFFSET__SHIFT                                                               0x2
#define BIF_SDMA0_DOORBELL_RANGE__SIZE__SHIFT                                                                 0x10
//BIF_SDMA1_DOORBELL_RANGE
#define BIF_SDMA1_DOORBELL_RANGE__OFFSET__SHIFT                                                               0x2
#define BIF_SDMA1_DOORBELL_RANGE__SIZE__SHIFT                                                                 0x10
//BIF_IH_DOORBELL_RANGE
#define BIF_IH_DOORBELL_RANGE__OFFSET__SHIFT                                                                  0x2
#define BIF_IH_DOORBELL_RANGE__SIZE__SHIFT                                                                    0x10
//BIF_MMSCH0_DOORBELL_RANGE
#define BIF_MMSCH0_DOORBELL_RANGE__OFFSET__SHIFT                                                              0x2
#define BIF_MMSCH0_DOORBELL_RANGE__SIZE__SHIFT                                                                0x10
//BIF_DOORBELL_FENCE_CNTL
#define BIF_DOORBELL_FENCE_CNTL__DOORBELL_FENCE_ENABLE__SHIFT                                                 0x0
//S2A_MISC_CNTL
#define S2A_MISC_CNTL__DOORBELL_64BIT_SUPPORT_SDMA0_DIS__SHIFT                                                0x0
#define S2A_MISC_CNTL__DOORBELL_64BIT_SUPPORT_SDMA1_DIS__SHIFT                                                0x1
#define S2A_MISC_CNTL__DOORBELL_64BIT_SUPPORT_CP_DIS__SHIFT                                                   0x2
//A2S_CNTL2_SEC_CL0
#define A2S_CNTL2_SEC_CL0__SECLVL_MAP__SHIFT                                                                  0x0
//A2S_CNTL2_SEC_CL1
#define A2S_CNTL2_SEC_CL1__SECLVL_MAP__SHIFT                                                                  0x0
//A2S_CNTL2_SEC_CL2
#define A2S_CNTL2_SEC_CL2__SECLVL_MAP__SHIFT                                                                  0x0
//A2S_CNTL2_SEC_CL3
#define A2S_CNTL2_SEC_CL3__SECLVL_MAP__SHIFT                                                                  0x0
//A2S_CNTL2_SEC_CL4
#define A2S_CNTL2_SEC_CL4__SECLVL_MAP__SHIFT                                                                  0x0


// addressBlock: nbif_sion_SIONDEC
//SION_CL0_RdRsp_BurstTarget_REG0
#define SION_CL0_RdRsp_BurstTarget_REG0__RdRsp_BurstTarget_31_0__SHIFT                                        0x0
//SION_CL0_RdRsp_BurstTarget_REG1
#define SION_CL0_RdRsp_BurstTarget_REG1__RdRsp_BurstTarget_63_32__SHIFT                                       0x0
//SION_CL0_RdRsp_TimeSlot_REG0
#define SION_CL0_RdRsp_TimeSlot_REG0__RdRsp_TimeSlot_31_0__SHIFT                                              0x0
//SION_CL0_RdRsp_TimeSlot_REG1
#define SION_CL0_RdRsp_TimeSlot_REG1__RdRsp_TimeSlot_63_32__SHIFT                                             0x0
//SION_CL0_WrRsp_BurstTarget_REG0
#define SION_CL0_WrRsp_BurstTarget_REG0__WrRsp_BurstTarget_31_0__SHIFT                                        0x0
//SION_CL0_WrRsp_BurstTarget_REG1
#define SION_CL0_WrRsp_BurstTarget_REG1__WrRsp_BurstTarget_63_32__SHIFT                                       0x0
//SION_CL0_WrRsp_TimeSlot_REG0
#define SION_CL0_WrRsp_TimeSlot_REG0__WrRsp_TimeSlot_31_0__SHIFT                                              0x0
//SION_CL0_WrRsp_TimeSlot_REG1
#define SION_CL0_WrRsp_TimeSlot_REG1__WrRsp_TimeSlot_63_32__SHIFT                                             0x0
//SION_CL0_Req_BurstTarget_REG0
#define SION_CL0_Req_BurstTarget_REG0__Req_BurstTarget_31_0__SHIFT                                            0x0
//SION_CL0_Req_BurstTarget_REG1
#define SION_CL0_Req_BurstTarget_REG1__Req_BurstTarget_63_32__SHIFT                                           0x0
//SION_CL0_Req_TimeSlot_REG0
#define SION_CL0_Req_TimeSlot_REG0__Req_TimeSlot_31_0__SHIFT                                                  0x0
//SION_CL0_Req_TimeSlot_REG1
#define SION_CL0_Req_TimeSlot_REG1__Req_TimeSlot_63_32__SHIFT                                                 0x0
//SION_CL0_ReqPoolCredit_Alloc_REG0
#define SION_CL0_ReqPoolCredit_Alloc_REG0__ReqPoolCredit_Alloc_31_0__SHIFT                                    0x0
//SION_CL0_ReqPoolCredit_Alloc_REG1
#define SION_CL0_ReqPoolCredit_Alloc_REG1__ReqPoolCredit_Alloc_63_32__SHIFT                                   0x0
//SION_CL0_DataPoolCredit_Alloc_REG0
#define SION_CL0_DataPoolCredit_Alloc_REG0__DataPoolCredit_Alloc_31_0__SHIFT                                  0x0
//SION_CL0_DataPoolCredit_Alloc_REG1
#define SION_CL0_DataPoolCredit_Alloc_REG1__DataPoolCredit_Alloc_63_32__SHIFT                                 0x0
//SION_CL0_RdRspPoolCredit_Alloc_REG0
#define SION_CL0_RdRspPoolCredit_Alloc_REG0__RdRspPoolCredit_Alloc_31_0__SHIFT                                0x0
//SION_CL0_RdRspPoolCredit_Alloc_REG1
#define SION_CL0_RdRspPoolCredit_Alloc_REG1__RdRspPoolCredit_Alloc_63_32__SHIFT                               0x0
//SION_CL0_WrRspPoolCredit_Alloc_REG0
#define SION_CL0_WrRspPoolCredit_Alloc_REG0__WrRspPoolCredit_Alloc_31_0__SHIFT                                0x0
//SION_CL0_WrRspPoolCredit_Alloc_REG1
#define SION_CL0_WrRspPoolCredit_Alloc_REG1__WrRspPoolCredit_Alloc_63_32__SHIFT                               0x0
//SION_CL1_RdRsp_BurstTarget_REG0
#define SION_CL1_RdRsp_BurstTarget_REG0__RdRsp_BurstTarget_31_0__SHIFT                                        0x0
//SION_CL1_RdRsp_BurstTarget_REG1
#define SION_CL1_RdRsp_BurstTarget_REG1__RdRsp_BurstTarget_63_32__SHIFT                                       0x0
//SION_CL1_RdRsp_TimeSlot_REG0
#define SION_CL1_RdRsp_TimeSlot_REG0__RdRsp_TimeSlot_31_0__SHIFT                                              0x0
//SION_CL1_RdRsp_TimeSlot_REG1
#define SION_CL1_RdRsp_TimeSlot_REG1__RdRsp_TimeSlot_63_32__SHIFT                                             0x0
//SION_CL1_WrRsp_BurstTarget_REG0
#define SION_CL1_WrRsp_BurstTarget_REG0__WrRsp_BurstTarget_31_0__SHIFT                                        0x0
//SION_CL1_WrRsp_BurstTarget_REG1
#define SION_CL1_WrRsp_BurstTarget_REG1__WrRsp_BurstTarget_63_32__SHIFT                                       0x0
//SION_CL1_WrRsp_TimeSlot_REG0
#define SION_CL1_WrRsp_TimeSlot_REG0__WrRsp_TimeSlot_31_0__SHIFT                                              0x0
//SION_CL1_WrRsp_TimeSlot_REG1
#define SION_CL1_WrRsp_TimeSlot_REG1__WrRsp_TimeSlot_63_32__SHIFT                                             0x0
//SION_CL1_Req_BurstTarget_REG0
#define SION_CL1_Req_BurstTarget_REG0__Req_BurstTarget_31_0__SHIFT                                            0x0
//SION_CL1_Req_BurstTarget_REG1
#define SION_CL1_Req_BurstTarget_REG1__Req_BurstTarget_63_32__SHIFT                                           0x0
//SION_CL1_Req_TimeSlot_REG0
#define SION_CL1_Req_TimeSlot_REG0__Req_TimeSlot_31_0__SHIFT                                                  0x0
//SION_CL1_Req_TimeSlot_REG1
#define SION_CL1_Req_TimeSlot_REG1__Req_TimeSlot_63_32__SHIFT                                                 0x0
//SION_CL1_ReqPoolCredit_Alloc_REG0
#define SION_CL1_ReqPoolCredit_Alloc_REG0__ReqPoolCredit_Alloc_31_0__SHIFT                                    0x0
//SION_CL1_ReqPoolCredit_Alloc_REG1
#define SION_CL1_ReqPoolCredit_Alloc_REG1__ReqPoolCredit_Alloc_63_32__SHIFT                                   0x0
//SION_CL1_DataPoolCredit_Alloc_REG0
#define SION_CL1_DataPoolCredit_Alloc_REG0__DataPoolCredit_Alloc_31_0__SHIFT                                  0x0
//SION_CL1_DataPoolCredit_Alloc_REG1
#define SION_CL1_DataPoolCredit_Alloc_REG1__DataPoolCredit_Alloc_63_32__SHIFT                                 0x0
//SION_CL1_RdRspPoolCredit_Alloc_REG0
#define SION_CL1_RdRspPoolCredit_Alloc_REG0__RdRspPoolCredit_Alloc_31_0__SHIFT                                0x0
//SION_CL1_RdRspPoolCredit_Alloc_REG1
#define SION_CL1_RdRspPoolCredit_Alloc_REG1__RdRspPoolCredit_Alloc_63_32__SHIFT                               0x0
//SION_CL1_WrRspPoolCredit_Alloc_REG0
#define SION_CL1_WrRspPoolCredit_Alloc_REG0__WrRspPoolCredit_Alloc_31_0__SHIFT                                0x0
//SION_CL1_WrRspPoolCredit_Alloc_REG1
#define SION_CL1_WrRspPoolCredit_Alloc_REG1__WrRspPoolCredit_Alloc_63_32__SHIFT                               0x0
//SION_CL2_RdRsp_BurstTarget_REG0
#define SION_CL2_RdRsp_BurstTarget_REG0__RdRsp_BurstTarget_31_0__SHIFT                                        0x0
//SION_CL2_RdRsp_BurstTarget_REG1
#define SION_CL2_RdRsp_BurstTarget_REG1__RdRsp_BurstTarget_63_32__SHIFT                                       0x0
//SION_CL2_RdRsp_TimeSlot_REG0
#define SION_CL2_RdRsp_TimeSlot_REG0__RdRsp_TimeSlot_31_0__SHIFT                                              0x0
//SION_CL2_RdRsp_TimeSlot_REG1
#define SION_CL2_RdRsp_TimeSlot_REG1__RdRsp_TimeSlot_63_32__SHIFT                                             0x0
//SION_CL2_WrRsp_BurstTarget_REG0
#define SION_CL2_WrRsp_BurstTarget_REG0__WrRsp_BurstTarget_31_0__SHIFT                                        0x0
//SION_CL2_WrRsp_BurstTarget_REG1
#define SION_CL2_WrRsp_BurstTarget_REG1__WrRsp_BurstTarget_63_32__SHIFT                                       0x0
//SION_CL2_WrRsp_TimeSlot_REG0
#define SION_CL2_WrRsp_TimeSlot_REG0__WrRsp_TimeSlot_31_0__SHIFT                                              0x0
//SION_CL2_WrRsp_TimeSlot_REG1
#define SION_CL2_WrRsp_TimeSlot_REG1__WrRsp_TimeSlot_63_32__SHIFT                                             0x0
//SION_CL2_Req_BurstTarget_REG0
#define SION_CL2_Req_BurstTarget_REG0__Req_BurstTarget_31_0__SHIFT                                            0x0
//SION_CL2_Req_BurstTarget_REG1
#define SION_CL2_Req_BurstTarget_REG1__Req_BurstTarget_63_32__SHIFT                                           0x0
//SION_CL2_Req_TimeSlot_REG0
#define SION_CL2_Req_TimeSlot_REG0__Req_TimeSlot_31_0__SHIFT                                                  0x0
//SION_CL2_Req_TimeSlot_REG1
#define SION_CL2_Req_TimeSlot_REG1__Req_TimeSlot_63_32__SHIFT                                                 0x0
//SION_CL2_ReqPoolCredit_Alloc_REG0
#define SION_CL2_ReqPoolCredit_Alloc_REG0__ReqPoolCredit_Alloc_31_0__SHIFT                                    0x0
//SION_CL2_ReqPoolCredit_Alloc_REG1
#define SION_CL2_ReqPoolCredit_Alloc_REG1__ReqPoolCredit_Alloc_63_32__SHIFT                                   0x0
//SION_CL2_DataPoolCredit_Alloc_REG0
#define SION_CL2_DataPoolCredit_Alloc_REG0__DataPoolCredit_Alloc_31_0__SHIFT                                  0x0
//SION_CL2_DataPoolCredit_Alloc_REG1
#define SION_CL2_DataPoolCredit_Alloc_REG1__DataPoolCredit_Alloc_63_32__SHIFT                                 0x0
//SION_CL2_RdRspPoolCredit_Alloc_REG0
#define SION_CL2_RdRspPoolCredit_Alloc_REG0__RdRspPoolCredit_Alloc_31_0__SHIFT                                0x0
//SION_CL2_RdRspPoolCredit_Alloc_REG1
#define SION_CL2_RdRspPoolCredit_Alloc_REG1__RdRspPoolCredit_Alloc_63_32__SHIFT                               0x0
//SION_CL2_WrRspPoolCredit_Alloc_REG0
#define SION_CL2_WrRspPoolCredit_Alloc_REG0__WrRspPoolCredit_Alloc_31_0__SHIFT                                0x0
//SION_CL2_WrRspPoolCredit_Alloc_REG1
#define SION_CL2_WrRspPoolCredit_Alloc_REG1__WrRspPoolCredit_Alloc_63_32__SHIFT                               0x0
//SION_CL3_RdRsp_BurstTarget_REG0
#define SION_CL3_RdRsp_BurstTarget_REG0__RdRsp_BurstTarget_31_0__SHIFT                                        0x0
//SION_CL3_RdRsp_BurstTarget_REG1
#define SION_CL3_RdRsp_BurstTarget_REG1__RdRsp_BurstTarget_63_32__SHIFT                                       0x0
//SION_CL3_RdRsp_TimeSlot_REG0
#define SION_CL3_RdRsp_TimeSlot_REG0__RdRsp_TimeSlot_31_0__SHIFT                                              0x0
//SION_CL3_RdRsp_TimeSlot_REG1
#define SION_CL3_RdRsp_TimeSlot_REG1__RdRsp_TimeSlot_63_32__SHIFT                                             0x0
//SION_CL3_WrRsp_BurstTarget_REG0
#define SION_CL3_WrRsp_BurstTarget_REG0__WrRsp_BurstTarget_31_0__SHIFT                                        0x0
//SION_CL3_WrRsp_BurstTarget_REG1
#define SION_CL3_WrRsp_BurstTarget_REG1__WrRsp_BurstTarget_63_32__SHIFT                                       0x0
//SION_CL3_WrRsp_TimeSlot_REG0
#define SION_CL3_WrRsp_TimeSlot_REG0__WrRsp_TimeSlot_31_0__SHIFT                                              0x0
//SION_CL3_WrRsp_TimeSlot_REG1
#define SION_CL3_WrRsp_TimeSlot_REG1__WrRsp_TimeSlot_63_32__SHIFT                                             0x0
//SION_CL3_Req_BurstTarget_REG0
#define SION_CL3_Req_BurstTarget_REG0__Req_BurstTarget_31_0__SHIFT                                            0x0
//SION_CL3_Req_BurstTarget_REG1
#define SION_CL3_Req_BurstTarget_REG1__Req_BurstTarget_63_32__SHIFT                                           0x0
//SION_CL3_Req_TimeSlot_REG0
#define SION_CL3_Req_TimeSlot_REG0__Req_TimeSlot_31_0__SHIFT                                                  0x0
//SION_CL3_Req_TimeSlot_REG1
#define SION_CL3_Req_TimeSlot_REG1__Req_TimeSlot_63_32__SHIFT                                                 0x0
//SION_CL3_ReqPoolCredit_Alloc_REG0
#define SION_CL3_ReqPoolCredit_Alloc_REG0__ReqPoolCredit_Alloc_31_0__SHIFT                                    0x0
//SION_CL3_ReqPoolCredit_Alloc_REG1
#define SION_CL3_ReqPoolCredit_Alloc_REG1__ReqPoolCredit_Alloc_63_32__SHIFT                                   0x0
//SION_CL3_DataPoolCredit_Alloc_REG0
#define SION_CL3_DataPoolCredit_Alloc_REG0__DataPoolCredit_Alloc_31_0__SHIFT                                  0x0
//SION_CL3_DataPoolCredit_Alloc_REG1
#define SION_CL3_DataPoolCredit_Alloc_REG1__DataPoolCredit_Alloc_63_32__SHIFT                                 0x0
//SION_CL3_RdRspPoolCredit_Alloc_REG0
#define SION_CL3_RdRspPoolCredit_Alloc_REG0__RdRspPoolCredit_Alloc_31_0__SHIFT                                0x0
//SION_CL3_RdRspPoolCredit_Alloc_REG1
#define SION_CL3_RdRspPoolCredit_Alloc_REG1__RdRspPoolCredit_Alloc_63_32__SHIFT                               0x0
//SION_CL3_WrRspPoolCredit_Alloc_REG0
#define SION_CL3_WrRspPoolCredit_Alloc_REG0__WrRspPoolCredit_Alloc_31_0__SHIFT                                0x0
//SION_CL3_WrRspPoolCredit_Alloc_REG1
#define SION_CL3_WrRspPoolCredit_Alloc_REG1__WrRspPoolCredit_Alloc_63_32__SHIFT                               0x0
//SION_CL4_RdRsp_BurstTarget_REG0
#define SION_CL4_RdRsp_BurstTarget_REG0__RdRsp_BurstTarget_31_0__SHIFT                                        0x0
//SION_CL4_RdRsp_BurstTarget_REG1
#define SION_CL4_RdRsp_BurstTarget_REG1__RdRsp_BurstTarget_63_32__SHIFT                                       0x0
//SION_CL4_RdRsp_TimeSlot_REG0
#define SION_CL4_RdRsp_TimeSlot_REG0__RdRsp_TimeSlot_31_0__SHIFT                                              0x0
//SION_CL4_RdRsp_TimeSlot_REG1
#define SION_CL4_RdRsp_TimeSlot_REG1__RdRsp_TimeSlot_63_32__SHIFT                                             0x0
//SION_CL4_WrRsp_BurstTarget_REG0
#define SION_CL4_WrRsp_BurstTarget_REG0__WrRsp_BurstTarget_31_0__SHIFT                                        0x0
//SION_CL4_WrRsp_BurstTarget_REG1
#define SION_CL4_WrRsp_BurstTarget_REG1__WrRsp_BurstTarget_63_32__SHIFT                                       0x0
//SION_CL4_WrRsp_TimeSlot_REG0
#define SION_CL4_WrRsp_TimeSlot_REG0__WrRsp_TimeSlot_31_0__SHIFT                                              0x0
//SION_CL4_WrRsp_TimeSlot_REG1
#define SION_CL4_WrRsp_TimeSlot_REG1__WrRsp_TimeSlot_63_32__SHIFT                                             0x0
//SION_CL4_Req_BurstTarget_REG0
#define SION_CL4_Req_BurstTarget_REG0__Req_BurstTarget_31_0__SHIFT                                            0x0
//SION_CL4_Req_BurstTarget_REG1
#define SION_CL4_Req_BurstTarget_REG1__Req_BurstTarget_63_32__SHIFT                                           0x0
//SION_CL4_Req_TimeSlot_REG0
#define SION_CL4_Req_TimeSlot_REG0__Req_TimeSlot_31_0__SHIFT                                                  0x0
//SION_CL4_Req_TimeSlot_REG1
#define SION_CL4_Req_TimeSlot_REG1__Req_TimeSlot_63_32__SHIFT                                                 0x0
//SION_CL4_ReqPoolCredit_Alloc_REG0
#define SION_CL4_ReqPoolCredit_Alloc_REG0__ReqPoolCredit_Alloc_31_0__SHIFT                                    0x0
//SION_CL4_ReqPoolCredit_Alloc_REG1
#define SION_CL4_ReqPoolCredit_Alloc_REG1__ReqPoolCredit_Alloc_63_32__SHIFT                                   0x0
//SION_CL4_DataPoolCredit_Alloc_REG0
#define SION_CL4_DataPoolCredit_Alloc_REG0__DataPoolCredit_Alloc_31_0__SHIFT                                  0x0
//SION_CL4_DataPoolCredit_Alloc_REG1
#define SION_CL4_DataPoolCredit_Alloc_REG1__DataPoolCredit_Alloc_63_32__SHIFT                                 0x0
//SION_CL4_RdRspPoolCredit_Alloc_REG0
#define SION_CL4_RdRspPoolCredit_Alloc_REG0__RdRspPoolCredit_Alloc_31_0__SHIFT                                0x0
//SION_CL4_RdRspPoolCredit_Alloc_REG1
#define SION_CL4_RdRspPoolCredit_Alloc_REG1__RdRspPoolCredit_Alloc_63_32__SHIFT                               0x0
//SION_CL4_WrRspPoolCredit_Alloc_REG0
#define SION_CL4_WrRspPoolCredit_Alloc_REG0__WrRspPoolCredit_Alloc_31_0__SHIFT                                0x0
//SION_CL4_WrRspPoolCredit_Alloc_REG1
#define SION_CL4_WrRspPoolCredit_Alloc_REG1__WrRspPoolCredit_Alloc_63_32__SHIFT                               0x0
//SION_CL5_RdRsp_BurstTarget_REG0
#define SION_CL5_RdRsp_BurstTarget_REG0__RdRsp_BurstTarget_31_0__SHIFT                                        0x0
//SION_CL5_RdRsp_BurstTarget_REG1
#define SION_CL5_RdRsp_BurstTarget_REG1__RdRsp_BurstTarget_63_32__SHIFT                                       0x0
//SION_CL5_RdRsp_TimeSlot_REG0
#define SION_CL5_RdRsp_TimeSlot_REG0__RdRsp_TimeSlot_31_0__SHIFT                                              0x0
//SION_CL5_RdRsp_TimeSlot_REG1
#define SION_CL5_RdRsp_TimeSlot_REG1__RdRsp_TimeSlot_63_32__SHIFT                                             0x0
//SION_CL5_WrRsp_BurstTarget_REG0
#define SION_CL5_WrRsp_BurstTarget_REG0__WrRsp_BurstTarget_31_0__SHIFT                                        0x0
//SION_CL5_WrRsp_BurstTarget_REG1
#define SION_CL5_WrRsp_BurstTarget_REG1__WrRsp_BurstTarget_63_32__SHIFT                                       0x0
//SION_CL5_WrRsp_TimeSlot_REG0
#define SION_CL5_WrRsp_TimeSlot_REG0__WrRsp_TimeSlot_31_0__SHIFT                                              0x0
//SION_CL5_WrRsp_TimeSlot_REG1
#define SION_CL5_WrRsp_TimeSlot_REG1__WrRsp_TimeSlot_63_32__SHIFT                                             0x0
//SION_CL5_Req_BurstTarget_REG0
#define SION_CL5_Req_BurstTarget_REG0__Req_BurstTarget_31_0__SHIFT                                            0x0
//SION_CL5_Req_BurstTarget_REG1
#define SION_CL5_Req_BurstTarget_REG1__Req_BurstTarget_63_32__SHIFT                                           0x0
//SION_CL5_Req_TimeSlot_REG0
#define SION_CL5_Req_TimeSlot_REG0__Req_TimeSlot_31_0__SHIFT                                                  0x0
//SION_CL5_Req_TimeSlot_REG1
#define SION_CL5_Req_TimeSlot_REG1__Req_TimeSlot_63_32__SHIFT                                                 0x0
//SION_CL5_ReqPoolCredit_Alloc_REG0
#define SION_CL5_ReqPoolCredit_Alloc_REG0__ReqPoolCredit_Alloc_31_0__SHIFT                                    0x0
//SION_CL5_ReqPoolCredit_Alloc_REG1
#define SION_CL5_ReqPoolCredit_Alloc_REG1__ReqPoolCredit_Alloc_63_32__SHIFT                                   0x0
//SION_CL5_DataPoolCredit_Alloc_REG0
#define SION_CL5_DataPoolCredit_Alloc_REG0__DataPoolCredit_Alloc_31_0__SHIFT                                  0x0
//SION_CL5_DataPoolCredit_Alloc_REG1
#define SION_CL5_DataPoolCredit_Alloc_REG1__DataPoolCredit_Alloc_63_32__SHIFT                                 0x0
//SION_CL5_RdRspPoolCredit_Alloc_REG0
#define SION_CL5_RdRspPoolCredit_Alloc_REG0__RdRspPoolCredit_Alloc_31_0__SHIFT                                0x0
//SION_CL5_RdRspPoolCredit_Alloc_REG1
#define SION_CL5_RdRspPoolCredit_Alloc_REG1__RdRspPoolCredit_Alloc_63_32__SHIFT                               0x0
//SION_CL5_WrRspPoolCredit_Alloc_REG0
#define SION_CL5_WrRspPoolCredit_Alloc_REG0__WrRspPoolCredit_Alloc_31_0__SHIFT                                0x0
//SION_CL5_WrRspPoolCredit_Alloc_REG1
#define SION_CL5_WrRspPoolCredit_Alloc_REG1__WrRspPoolCredit_Alloc_63_32__SHIFT                               0x0
//SION_CNTL_REG0
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_0_SOFT_OVERRIDE_CLK0__SHIFT                                0x0
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_0_SOFT_OVERRIDE_CLK1__SHIFT                                0x1
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_0_SOFT_OVERRIDE_CLK2__SHIFT                                0x2
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_0_SOFT_OVERRIDE_CLK3__SHIFT                                0x3
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_0_SOFT_OVERRIDE_CLK4__SHIFT                                0x4
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_0_SOFT_OVERRIDE_CLK5__SHIFT                                0x5
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_0_SOFT_OVERRIDE_CLK6__SHIFT                                0x6
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_0_SOFT_OVERRIDE_CLK7__SHIFT                                0x7
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_0_SOFT_OVERRIDE_CLK8__SHIFT                                0x8
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_0_SOFT_OVERRIDE_CLK9__SHIFT                                0x9
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_1_SOFT_OVERRIDE_CLK0__SHIFT                                0xa
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_1_SOFT_OVERRIDE_CLK1__SHIFT                                0xb
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_1_SOFT_OVERRIDE_CLK2__SHIFT                                0xc
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_1_SOFT_OVERRIDE_CLK3__SHIFT                                0xd
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_1_SOFT_OVERRIDE_CLK4__SHIFT                                0xe
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_1_SOFT_OVERRIDE_CLK5__SHIFT                                0xf
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_1_SOFT_OVERRIDE_CLK6__SHIFT                                0x10
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_1_SOFT_OVERRIDE_CLK7__SHIFT                                0x11
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_1_SOFT_OVERRIDE_CLK8__SHIFT                                0x12
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_1_SOFT_OVERRIDE_CLK9__SHIFT                                0x13
//SION_CNTL_REG1
#define SION_CNTL_REG1__LIVELOCK_WATCHDOG_THRESHOLD__SHIFT                                                    0x0
#define SION_CNTL_REG1__CG_OFF_HYSTERESIS__SHIFT                                                              0x8


// addressBlock: syshub_mmreg_direct_syshubdirect
//SYSHUB_DS_CTRL_SOCCLK
#define SYSHUB_DS_CTRL_SOCCLK__HST_CL0_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                   0x0
#define SYSHUB_DS_CTRL_SOCCLK__HST_CL1_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                   0x1
#define SYSHUB_DS_CTRL_SOCCLK__HST_CL2_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                   0x2
#define SYSHUB_DS_CTRL_SOCCLK__HST_CL3_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                   0x3
#define SYSHUB_DS_CTRL_SOCCLK__HST_CL4_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                   0x4
#define SYSHUB_DS_CTRL_SOCCLK__HST_CL5_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                   0x5
#define SYSHUB_DS_CTRL_SOCCLK__HST_CL6_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                   0x6
#define SYSHUB_DS_CTRL_SOCCLK__HST_CL7_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                   0x7
#define SYSHUB_DS_CTRL_SOCCLK__DMA_CL0_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                   0x10
#define SYSHUB_DS_CTRL_SOCCLK__DMA_CL1_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                   0x11
#define SYSHUB_DS_CTRL_SOCCLK__DMA_CL2_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                   0x12
#define SYSHUB_DS_CTRL_SOCCLK__DMA_CL3_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                   0x13
#define SYSHUB_DS_CTRL_SOCCLK__DMA_CL4_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                   0x14
#define SYSHUB_DS_CTRL_SOCCLK__DMA_CL5_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                   0x15
#define SYSHUB_DS_CTRL_SOCCLK__DMA_CL6_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                   0x16
#define SYSHUB_DS_CTRL_SOCCLK__DMA_CL7_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                   0x17
#define SYSHUB_DS_CTRL_SOCCLK__SYSHUB_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                    0x1c
#define SYSHUB_DS_CTRL_SOCCLK__SYSHUB_SOCCLK_DS_EN__SHIFT                                                     0x1f
//SYSHUB_DS_CTRL2_SOCCLK
#define SYSHUB_DS_CTRL2_SOCCLK__SYSHUB_SOCCLK_DS_TIMER__SHIFT                                                 0x0
//SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK
#define SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK__SYSHUB_bgen_socclk_HST_SW0_bypass_en__SHIFT                 0x0
#define SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK__SYSHUB_bgen_socclk_HST_SW1_bypass_en__SHIFT                 0x1
#define SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW0_bypass_en__SHIFT                 0xf
#define SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW1_bypass_en__SHIFT                 0x10
#define SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW2_bypass_en__SHIFT                 0x11
//SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK
#define SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK__SYSHUB_bgen_socclk_HST_SW0_imm_en__SHIFT                       0x0
#define SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK__SYSHUB_bgen_socclk_HST_SW1_imm_en__SHIFT                       0x1
#define SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW0_imm_en__SHIFT                       0xf
#define SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW1_imm_en__SHIFT                       0x10
#define SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW2_imm_en__SHIFT                       0x11
//DMA_CLK0_SW0_SYSHUB_QOS_CNTL
#define DMA_CLK0_SW0_SYSHUB_QOS_CNTL__QOS_CNTL_MODE__SHIFT                                                    0x0
#define DMA_CLK0_SW0_SYSHUB_QOS_CNTL__QOS_MAX_VALUE__SHIFT                                                    0x1
#define DMA_CLK0_SW0_SYSHUB_QOS_CNTL__QOS_MIN_VALUE__SHIFT                                                    0x5
//DMA_CLK0_SW1_SYSHUB_QOS_CNTL
#define DMA_CLK0_SW1_SYSHUB_QOS_CNTL__QOS_CNTL_MODE__SHIFT                                                    0x0
#define DMA_CLK0_SW1_SYSHUB_QOS_CNTL__QOS_MAX_VALUE__SHIFT                                                    0x1
#define DMA_CLK0_SW1_SYSHUB_QOS_CNTL__QOS_MIN_VALUE__SHIFT                                                    0x5
//DMA_CLK0_SW0_CL0_CNTL
#define DMA_CLK0_SW0_CL0_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                                      0x0
#define DMA_CLK0_SW0_CL0_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                                    0x1
#define DMA_CLK0_SW0_CL0_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                                  0x8
#define DMA_CLK0_SW0_CL0_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                               0x9
#define DMA_CLK0_SW0_CL0_CNTL__READ_WRR_WEIGHT__SHIFT                                                         0x10
#define DMA_CLK0_SW0_CL0_CNTL__WRITE_WRR_WEIGHT__SHIFT                                                        0x18
//DMA_CLK0_SW0_CL1_CNTL
#define DMA_CLK0_SW0_CL1_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                                      0x0
#define DMA_CLK0_SW0_CL1_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                                    0x1
#define DMA_CLK0_SW0_CL1_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                                  0x8
#define DMA_CLK0_SW0_CL1_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                               0x9
#define DMA_CLK0_SW0_CL1_CNTL__READ_WRR_WEIGHT__SHIFT                                                         0x10
#define DMA_CLK0_SW0_CL1_CNTL__WRITE_WRR_WEIGHT__SHIFT                                                        0x18
//DMA_CLK0_SW0_CL2_CNTL
#define DMA_CLK0_SW0_CL2_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                                      0x0
#define DMA_CLK0_SW0_CL2_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                                    0x1
#define DMA_CLK0_SW0_CL2_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                                  0x8
#define DMA_CLK0_SW0_CL2_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                               0x9
#define DMA_CLK0_SW0_CL2_CNTL__READ_WRR_WEIGHT__SHIFT                                                         0x10
#define DMA_CLK0_SW0_CL2_CNTL__WRITE_WRR_WEIGHT__SHIFT                                                        0x18
//DMA_CLK0_SW0_CL3_CNTL
#define DMA_CLK0_SW0_CL3_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                                      0x0
#define DMA_CLK0_SW0_CL3_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                                    0x1
#define DMA_CLK0_SW0_CL3_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                                  0x8
#define DMA_CLK0_SW0_CL3_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                               0x9
#define DMA_CLK0_SW0_CL3_CNTL__READ_WRR_WEIGHT__SHIFT                                                         0x10
#define DMA_CLK0_SW0_CL3_CNTL__WRITE_WRR_WEIGHT__SHIFT                                                        0x18
//DMA_CLK0_SW0_CL4_CNTL
#define DMA_CLK0_SW0_CL4_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                                      0x0
#define DMA_CLK0_SW0_CL4_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                                    0x1
#define DMA_CLK0_SW0_CL4_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                                  0x8
#define DMA_CLK0_SW0_CL4_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                               0x9
#define DMA_CLK0_SW0_CL4_CNTL__READ_WRR_WEIGHT__SHIFT                                                         0x10
#define DMA_CLK0_SW0_CL4_CNTL__WRITE_WRR_WEIGHT__SHIFT                                                        0x18
//DMA_CLK0_SW0_CL5_CNTL
#define DMA_CLK0_SW0_CL5_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                                      0x0
#define DMA_CLK0_SW0_CL5_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                                    0x1
#define DMA_CLK0_SW0_CL5_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                                  0x8
#define DMA_CLK0_SW0_CL5_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                               0x9
#define DMA_CLK0_SW0_CL5_CNTL__READ_WRR_WEIGHT__SHIFT                                                         0x10
#define DMA_CLK0_SW0_CL5_CNTL__WRITE_WRR_WEIGHT__SHIFT                                                        0x18
//DMA_CLK0_SW1_CL0_CNTL
#define DMA_CLK0_SW1_CL0_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                                      0x0
#define DMA_CLK0_SW1_CL0_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                                    0x1
#define DMA_CLK0_SW1_CL0_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                                  0x8
#define DMA_CLK0_SW1_CL0_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                               0x9
#define DMA_CLK0_SW1_CL0_CNTL__READ_WRR_WEIGHT__SHIFT                                                         0x10
#define DMA_CLK0_SW1_CL0_CNTL__WRITE_WRR_WEIGHT__SHIFT                                                        0x18
//DMA_CLK0_SW2_CL0_CNTL
#define DMA_CLK0_SW2_CL0_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                                      0x0
#define DMA_CLK0_SW2_CL0_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                                    0x1
#define DMA_CLK0_SW2_CL0_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                                  0x8
#define DMA_CLK0_SW2_CL0_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                               0x9
#define DMA_CLK0_SW2_CL0_CNTL__READ_WRR_WEIGHT__SHIFT                                                         0x10
#define DMA_CLK0_SW2_CL0_CNTL__WRITE_WRR_WEIGHT__SHIFT                                                        0x18
//SYSHUB_CG_CNTL
#define SYSHUB_CG_CNTL__SYSHUB_CG_EN__SHIFT                                                                   0x0
#define SYSHUB_CG_CNTL__SYSHUB_CG_IDLE_TIMER__SHIFT                                                           0x8
#define SYSHUB_CG_CNTL__SYSHUB_CG_WAKEUP_TIMER__SHIFT                                                         0x10
//SYSHUB_TRANS_IDLE
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF0__SHIFT                                                       0x0
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF1__SHIFT                                                       0x1
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF2__SHIFT                                                       0x2
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF3__SHIFT                                                       0x3
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF4__SHIFT                                                       0x4
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF5__SHIFT                                                       0x5
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF6__SHIFT                                                       0x6
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF7__SHIFT                                                       0x7
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF8__SHIFT                                                       0x8
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF9__SHIFT                                                       0x9
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF10__SHIFT                                                      0xa
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF11__SHIFT                                                      0xb
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF12__SHIFT                                                      0xc
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF13__SHIFT                                                      0xd
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF14__SHIFT                                                      0xe
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF15__SHIFT                                                      0xf
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_PF__SHIFT                                                        0x10
//SYSHUB_HP_TIMER
#define SYSHUB_HP_TIMER__SYSHUB_HP_TIMER__SHIFT                                                               0x0
//SYSHUB_SCRATCH
#define SYSHUB_SCRATCH__SCRATCH__SHIFT                                                                        0x0
//SYSHUB_DS_CTRL_SHUBCLK
#define SYSHUB_DS_CTRL_SHUBCLK__HST_CL0_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                 0x0
#define SYSHUB_DS_CTRL_SHUBCLK__HST_CL1_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                 0x1
#define SYSHUB_DS_CTRL_SHUBCLK__HST_CL2_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                 0x2
#define SYSHUB_DS_CTRL_SHUBCLK__HST_CL3_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                 0x3
#define SYSHUB_DS_CTRL_SHUBCLK__HST_CL4_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                 0x4
#define SYSHUB_DS_CTRL_SHUBCLK__HST_CL5_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                 0x5
#define SYSHUB_DS_CTRL_SHUBCLK__HST_CL6_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                 0x6
#define SYSHUB_DS_CTRL_SHUBCLK__HST_CL7_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                 0x7
#define SYSHUB_DS_CTRL_SHUBCLK__DMA_CL0_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                 0x10
#define SYSHUB_DS_CTRL_SHUBCLK__DMA_CL1_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                 0x11
#define SYSHUB_DS_CTRL_SHUBCLK__DMA_CL2_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                 0x12
#define SYSHUB_DS_CTRL_SHUBCLK__DMA_CL3_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                 0x13
#define SYSHUB_DS_CTRL_SHUBCLK__DMA_CL4_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                 0x14
#define SYSHUB_DS_CTRL_SHUBCLK__DMA_CL5_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                 0x15
#define SYSHUB_DS_CTRL_SHUBCLK__DMA_CL6_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                 0x16
#define SYSHUB_DS_CTRL_SHUBCLK__DMA_CL7_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                 0x17
#define SYSHUB_DS_CTRL_SHUBCLK__SYSHUB_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                                  0x1c
#define SYSHUB_DS_CTRL_SHUBCLK__SYSHUB_SHUBCLK_DS_EN__SHIFT                                                   0x1f
//SYSHUB_DS_CTRL2_SHUBCLK
#define SYSHUB_DS_CTRL2_SHUBCLK__SYSHUB_SHUBCLK_DS_TIMER__SHIFT                                               0x0
//SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SHUBCLK
#define SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SHUBCLK__SYSHUB_bgen_shubclk_DMA_SW0_bypass_en__SHIFT               0xf
#define SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SHUBCLK__SYSHUB_bgen_shubclk_DMA_SW1_bypass_en__SHIFT               0x10
//SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SHUBCLK
#define SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SHUBCLK__SYSHUB_bgen_shubclk_DMA_SW0_imm_en__SHIFT                     0xf
#define SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SHUBCLK__SYSHUB_bgen_shubclk_DMA_SW1_imm_en__SHIFT                     0x10
//DMA_CLK1_SW0_SYSHUB_QOS_CNTL
#define DMA_CLK1_SW0_SYSHUB_QOS_CNTL__QOS_CNTL_MODE__SHIFT                                                    0x0
#define DMA_CLK1_SW0_SYSHUB_QOS_CNTL__QOS_MAX_VALUE__SHIFT                                                    0x1
#define DMA_CLK1_SW0_SYSHUB_QOS_CNTL__QOS_MIN_VALUE__SHIFT                                                    0x5
//DMA_CLK1_SW1_SYSHUB_QOS_CNTL
#define DMA_CLK1_SW1_SYSHUB_QOS_CNTL__QOS_CNTL_MODE__SHIFT                                                    0x0
#define DMA_CLK1_SW1_SYSHUB_QOS_CNTL__QOS_MAX_VALUE__SHIFT                                                    0x1
#define DMA_CLK1_SW1_SYSHUB_QOS_CNTL__QOS_MIN_VALUE__SHIFT                                                    0x5
//DMA_CLK1_SW0_CL0_CNTL
#define DMA_CLK1_SW0_CL0_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                                      0x0
#define DMA_CLK1_SW0_CL0_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                                    0x1
#define DMA_CLK1_SW0_CL0_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                                  0x8
#define DMA_CLK1_SW0_CL0_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                               0x9
#define DMA_CLK1_SW0_CL0_CNTL__READ_WRR_WEIGHT__SHIFT                                                         0x10
#define DMA_CLK1_SW0_CL0_CNTL__WRITE_WRR_WEIGHT__SHIFT                                                        0x18
//DMA_CLK1_SW0_CL1_CNTL
#define DMA_CLK1_SW0_CL1_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                                      0x0
#define DMA_CLK1_SW0_CL1_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                                    0x1
#define DMA_CLK1_SW0_CL1_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                                  0x8
#define DMA_CLK1_SW0_CL1_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                               0x9
#define DMA_CLK1_SW0_CL1_CNTL__READ_WRR_WEIGHT__SHIFT                                                         0x10
#define DMA_CLK1_SW0_CL1_CNTL__WRITE_WRR_WEIGHT__SHIFT                                                        0x18
//DMA_CLK1_SW0_CL2_CNTL
#define DMA_CLK1_SW0_CL2_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                                      0x0
#define DMA_CLK1_SW0_CL2_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                                    0x1
#define DMA_CLK1_SW0_CL2_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                                  0x8
#define DMA_CLK1_SW0_CL2_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                               0x9
#define DMA_CLK1_SW0_CL2_CNTL__READ_WRR_WEIGHT__SHIFT                                                         0x10
#define DMA_CLK1_SW0_CL2_CNTL__WRITE_WRR_WEIGHT__SHIFT                                                        0x18
//DMA_CLK1_SW0_CL3_CNTL
#define DMA_CLK1_SW0_CL3_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                                      0x0
#define DMA_CLK1_SW0_CL3_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                                    0x1
#define DMA_CLK1_SW0_CL3_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                                  0x8
#define DMA_CLK1_SW0_CL3_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                               0x9
#define DMA_CLK1_SW0_CL3_CNTL__READ_WRR_WEIGHT__SHIFT                                                         0x10
#define DMA_CLK1_SW0_CL3_CNTL__WRITE_WRR_WEIGHT__SHIFT                                                        0x18
//DMA_CLK1_SW0_CL4_CNTL
#define DMA_CLK1_SW0_CL4_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                                      0x0
#define DMA_CLK1_SW0_CL4_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                                    0x1
#define DMA_CLK1_SW0_CL4_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                                  0x8
#define DMA_CLK1_SW0_CL4_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                               0x9
#define DMA_CLK1_SW0_CL4_CNTL__READ_WRR_WEIGHT__SHIFT                                                         0x10
#define DMA_CLK1_SW0_CL4_CNTL__WRITE_WRR_WEIGHT__SHIFT                                                        0x18
//DMA_CLK1_SW1_CL0_CNTL
#define DMA_CLK1_SW1_CL0_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                                      0x0
#define DMA_CLK1_SW1_CL0_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                                    0x1
#define DMA_CLK1_SW1_CL0_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                                  0x8
#define DMA_CLK1_SW1_CL0_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                               0x9
#define DMA_CLK1_SW1_CL0_CNTL__READ_WRR_WEIGHT__SHIFT                                                         0x10
#define DMA_CLK1_SW1_CL0_CNTL__WRITE_WRR_WEIGHT__SHIFT                                                        0x18
//DMA_CLK1_SW1_CL1_CNTL
#define DMA_CLK1_SW1_CL1_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                                      0x0
#define DMA_CLK1_SW1_CL1_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                                    0x1
#define DMA_CLK1_SW1_CL1_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                                  0x8
#define DMA_CLK1_SW1_CL1_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                               0x9
#define DMA_CLK1_SW1_CL1_CNTL__READ_WRR_WEIGHT__SHIFT                                                         0x10
#define DMA_CLK1_SW1_CL1_CNTL__WRITE_WRR_WEIGHT__SHIFT                                                        0x18
//DMA_CLK1_SW1_CL2_CNTL
#define DMA_CLK1_SW1_CL2_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                                      0x0
#define DMA_CLK1_SW1_CL2_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                                    0x1
#define DMA_CLK1_SW1_CL2_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                                  0x8
#define DMA_CLK1_SW1_CL2_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                               0x9
#define DMA_CLK1_SW1_CL2_CNTL__READ_WRR_WEIGHT__SHIFT                                                         0x10
#define DMA_CLK1_SW1_CL2_CNTL__WRITE_WRR_WEIGHT__SHIFT                                                        0x18
//DMA_CLK1_SW1_CL3_CNTL
#define DMA_CLK1_SW1_CL3_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                                      0x0
#define DMA_CLK1_SW1_CL3_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                                    0x1
#define DMA_CLK1_SW1_CL3_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                                  0x8
#define DMA_CLK1_SW1_CL3_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                               0x9
#define DMA_CLK1_SW1_CL3_CNTL__READ_WRR_WEIGHT__SHIFT                                                         0x10
#define DMA_CLK1_SW1_CL3_CNTL__WRITE_WRR_WEIGHT__SHIFT                                                        0x18
//DMA_CLK1_SW1_CL4_CNTL
#define DMA_CLK1_SW1_CL4_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                                      0x0
#define DMA_CLK1_SW1_CL4_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                                    0x1
#define DMA_CLK1_SW1_CL4_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                                  0x8
#define DMA_CLK1_SW1_CL4_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                               0x9
#define DMA_CLK1_SW1_CL4_CNTL__READ_WRR_WEIGHT__SHIFT                                                         0x10
#define DMA_CLK1_SW1_CL4_CNTL__WRITE_WRR_WEIGHT__SHIFT                                                        0x18


// addressBlock: gdc_ras_gdc_ras_regblk
//GDC_RAS_LEAF0_CTRL
#define GDC_RAS_LEAF0_CTRL__POISON_DET_EN__SHIFT                                                              0x0
#define GDC_RAS_LEAF0_CTRL__POISON_ERREVENT_EN__SHIFT                                                         0x1
#define GDC_RAS_LEAF0_CTRL__POISON_STALL_EN__SHIFT                                                            0x2
#define GDC_RAS_LEAF0_CTRL__PARITY_DET_EN__SHIFT                                                              0x4
#define GDC_RAS_LEAF0_CTRL__PARITY_ERREVENT_EN__SHIFT                                                         0x5
#define GDC_RAS_LEAF0_CTRL__PARITY_STALL_EN__SHIFT                                                            0x6
#define GDC_RAS_LEAF0_CTRL__ERR_EVENT_RECV__SHIFT                                                             0x10
#define GDC_RAS_LEAF0_CTRL__LINK_DIS_RECV__SHIFT                                                              0x11
#define GDC_RAS_LEAF0_CTRL__POISON_ERR_DET__SHIFT                                                             0x12
#define GDC_RAS_LEAF0_CTRL__PARITY_ERR_DET__SHIFT                                                             0x13
#define GDC_RAS_LEAF0_CTRL__ERR_EVENT_SENT__SHIFT                                                             0x14
#define GDC_RAS_LEAF0_CTRL__EGRESS_STALLED__SHIFT                                                             0x15
//GDC_RAS_LEAF1_CTRL
#define GDC_RAS_LEAF1_CTRL__POISON_DET_EN__SHIFT                                                              0x0
#define GDC_RAS_LEAF1_CTRL__POISON_ERREVENT_EN__SHIFT                                                         0x1
#define GDC_RAS_LEAF1_CTRL__POISON_STALL_EN__SHIFT                                                            0x2
#define GDC_RAS_LEAF1_CTRL__PARITY_DET_EN__SHIFT                                                              0x4
#define GDC_RAS_LEAF1_CTRL__PARITY_ERREVENT_EN__SHIFT                                                         0x5
#define GDC_RAS_LEAF1_CTRL__PARITY_STALL_EN__SHIFT                                                            0x6
#define GDC_RAS_LEAF1_CTRL__ERR_EVENT_RECV__SHIFT                                                             0x10
#define GDC_RAS_LEAF1_CTRL__LINK_DIS_RECV__SHIFT                                                              0x11
#define GDC_RAS_LEAF1_CTRL__POISON_ERR_DET__SHIFT                                                             0x12
#define GDC_RAS_LEAF1_CTRL__PARITY_ERR_DET__SHIFT                                                             0x13
#define GDC_RAS_LEAF1_CTRL__ERR_EVENT_SENT__SHIFT                                                             0x14
#define GDC_RAS_LEAF1_CTRL__EGRESS_STALLED__SHIFT                                                             0x15
//GDC_RAS_LEAF2_CTRL
#define GDC_RAS_LEAF2_CTRL__POISON_DET_EN__SHIFT                                                              0x0
#define GDC_RAS_LEAF2_CTRL__POISON_ERREVENT_EN__SHIFT                                                         0x1
#define GDC_RAS_LEAF2_CTRL__POISON_STALL_EN__SHIFT                                                            0x2
#define GDC_RAS_LEAF2_CTRL__PARITY_DET_EN__SHIFT                                                              0x4
#define GDC_RAS_LEAF2_CTRL__PARITY_ERREVENT_EN__SHIFT                                                         0x5
#define GDC_RAS_LEAF2_CTRL__PARITY_STALL_EN__SHIFT                                                            0x6
#define GDC_RAS_LEAF2_CTRL__ERR_EVENT_RECV__SHIFT                                                             0x10
#define GDC_RAS_LEAF2_CTRL__LINK_DIS_RECV__SHIFT                                                              0x11
#define GDC_RAS_LEAF2_CTRL__POISON_ERR_DET__SHIFT                                                             0x12
#define GDC_RAS_LEAF2_CTRL__PARITY_ERR_DET__SHIFT                                                             0x13
#define GDC_RAS_LEAF2_CTRL__ERR_EVENT_SENT__SHIFT                                                             0x14
#define GDC_RAS_LEAF2_CTRL__EGRESS_STALLED__SHIFT                                                             0x15
//GDC_RAS_LEAF3_CTRL
#define GDC_RAS_LEAF3_CTRL__POISON_DET_EN__SHIFT                                                              0x0
#define GDC_RAS_LEAF3_CTRL__POISON_ERREVENT_EN__SHIFT                                                         0x1
#define GDC_RAS_LEAF3_CTRL__POISON_STALL_EN__SHIFT                                                            0x2
#define GDC_RAS_LEAF3_CTRL__PARITY_DET_EN__SHIFT                                                              0x4
#define GDC_RAS_LEAF3_CTRL__PARITY_ERREVENT_EN__SHIFT                                                         0x5
#define GDC_RAS_LEAF3_CTRL__PARITY_STALL_EN__SHIFT                                                            0x6
#define GDC_RAS_LEAF3_CTRL__ERR_EVENT_RECV__SHIFT                                                             0x10
#define GDC_RAS_LEAF3_CTRL__LINK_DIS_RECV__SHIFT                                                              0x11
#define GDC_RAS_LEAF3_CTRL__POISON_ERR_DET__SHIFT                                                             0x12
#define GDC_RAS_LEAF3_CTRL__PARITY_ERR_DET__SHIFT                                                             0x13
#define GDC_RAS_LEAF3_CTRL__ERR_EVENT_SENT__SHIFT                                                             0x14
#define GDC_RAS_LEAF3_CTRL__EGRESS_STALLED__SHIFT                                                             0x15
//GDC_RAS_LEAF4_CTRL
#define GDC_RAS_LEAF4_CTRL__POISON_DET_EN__SHIFT                                                              0x0
#define GDC_RAS_LEAF4_CTRL__POISON_ERREVENT_EN__SHIFT                                                         0x1
#define GDC_RAS_LEAF4_CTRL__POISON_STALL_EN__SHIFT                                                            0x2
#define GDC_RAS_LEAF4_CTRL__PARITY_DET_EN__SHIFT                                                              0x4
#define GDC_RAS_LEAF4_CTRL__PARITY_ERREVENT_EN__SHIFT                                                         0x5
#define GDC_RAS_LEAF4_CTRL__PARITY_STALL_EN__SHIFT                                                            0x6
#define GDC_RAS_LEAF4_CTRL__ERR_EVENT_RECV__SHIFT                                                             0x10
#define GDC_RAS_LEAF4_CTRL__LINK_DIS_RECV__SHIFT                                                              0x11
#define GDC_RAS_LEAF4_CTRL__POISON_ERR_DET__SHIFT                                                             0x12
#define GDC_RAS_LEAF4_CTRL__PARITY_ERR_DET__SHIFT                                                             0x13
#define GDC_RAS_LEAF4_CTRL__ERR_EVENT_SENT__SHIFT                                                             0x14
#define GDC_RAS_LEAF4_CTRL__EGRESS_STALLED__SHIFT                                                             0x15
//GDC_RAS_LEAF5_CTRL
#define GDC_RAS_LEAF5_CTRL__POISON_DET_EN__SHIFT                                                              0x0
#define GDC_RAS_LEAF5_CTRL__POISON_ERREVENT_EN__SHIFT                                                         0x1
#define GDC_RAS_LEAF5_CTRL__POISON_STALL_EN__SHIFT                                                            0x2
#define GDC_RAS_LEAF5_CTRL__PARITY_DET_EN__SHIFT                                                              0x4
#define GDC_RAS_LEAF5_CTRL__PARITY_ERREVENT_EN__SHIFT                                                         0x5
#define GDC_RAS_LEAF5_CTRL__PARITY_STALL_EN__SHIFT                                                            0x6
#define GDC_RAS_LEAF5_CTRL__ERR_EVENT_RECV__SHIFT                                                             0x10
#define GDC_RAS_LEAF5_CTRL__LINK_DIS_RECV__SHIFT                                                              0x11
#define GDC_RAS_LEAF5_CTRL__POISON_ERR_DET__SHIFT                                                             0x12
#define GDC_RAS_LEAF5_CTRL__PARITY_ERR_DET__SHIFT                                                             0x13
#define GDC_RAS_LEAF5_CTRL__ERR_EVENT_SENT__SHIFT                                                             0x14
#define GDC_RAS_LEAF5_CTRL__EGRESS_STALLED__SHIFT                                                             0x15


// addressBlock: gdc_rst_GDCRST_DEC
//SHUB_PF_FLR_RST
#define SHUB_PF_FLR_RST__PF0_FLR_RST__SHIFT                                                                   0x0
#define SHUB_PF_FLR_RST__PF1_FLR_RST__SHIFT                                                                   0x1
#define SHUB_PF_FLR_RST__PF2_FLR_RST__SHIFT                                                                   0x2
#define SHUB_PF_FLR_RST__PF3_FLR_RST__SHIFT                                                                   0x3
#define SHUB_PF_FLR_RST__PF4_FLR_RST__SHIFT                                                                   0x4
#define SHUB_PF_FLR_RST__PF5_FLR_RST__SHIFT                                                                   0x5
#define SHUB_PF_FLR_RST__PF6_FLR_RST__SHIFT                                                                   0x6
#define SHUB_PF_FLR_RST__PF7_FLR_RST__SHIFT                                                                   0x7
//SHUB_GFX_DRV_MODE1_RST
#define SHUB_GFX_DRV_MODE1_RST__GFX_DRV_MODE1_RST__SHIFT                                                      0x0
//SHUB_LINK_RESET
#define SHUB_LINK_RESET__LINK_RESET__SHIFT                                                                    0x0
//SHUB_PF0_VF_FLR_RST
#define SHUB_PF0_VF_FLR_RST__PF0_VF0_FLR_RST__SHIFT                                                           0x0
#define SHUB_PF0_VF_FLR_RST__PF0_VF1_FLR_RST__SHIFT                                                           0x1
#define SHUB_PF0_VF_FLR_RST__PF0_VF2_FLR_RST__SHIFT                                                           0x2
#define SHUB_PF0_VF_FLR_RST__PF0_VF3_FLR_RST__SHIFT                                                           0x3
#define SHUB_PF0_VF_FLR_RST__PF0_VF4_FLR_RST__SHIFT                                                           0x4
#define SHUB_PF0_VF_FLR_RST__PF0_VF5_FLR_RST__SHIFT                                                           0x5
#define SHUB_PF0_VF_FLR_RST__PF0_VF6_FLR_RST__SHIFT                                                           0x6
#define SHUB_PF0_VF_FLR_RST__PF0_VF7_FLR_RST__SHIFT                                                           0x7
#define SHUB_PF0_VF_FLR_RST__PF0_VF8_FLR_RST__SHIFT                                                           0x8
#define SHUB_PF0_VF_FLR_RST__PF0_VF9_FLR_RST__SHIFT                                                           0x9
#define SHUB_PF0_VF_FLR_RST__PF0_VF10_FLR_RST__SHIFT                                                          0xa
#define SHUB_PF0_VF_FLR_RST__PF0_VF11_FLR_RST__SHIFT                                                          0xb
#define SHUB_PF0_VF_FLR_RST__PF0_VF12_FLR_RST__SHIFT                                                          0xc
#define SHUB_PF0_VF_FLR_RST__PF0_VF13_FLR_RST__SHIFT                                                          0xd
#define SHUB_PF0_VF_FLR_RST__PF0_VF14_FLR_RST__SHIFT                                                          0xe
#define SHUB_PF0_VF_FLR_RST__PF0_VF15_FLR_RST__SHIFT                                                          0xf
#define SHUB_PF0_VF_FLR_RST__PF0_SOFTPF_FLR_RST__SHIFT                                                        0x1f
//SHUB_HARD_RST_CTRL
#define SHUB_HARD_RST_CTRL__COR_RESET_EN__SHIFT                                                               0x0
#define SHUB_HARD_RST_CTRL__REG_RESET_EN__SHIFT                                                               0x1
#define SHUB_HARD_RST_CTRL__STY_RESET_EN__SHIFT                                                               0x2
#define SHUB_HARD_RST_CTRL__NIC400_RESET_EN__SHIFT                                                            0x3
#define SHUB_HARD_RST_CTRL__SDP_PORT_RESET_EN__SHIFT                                                          0x4
//SHUB_SOFT_RST_CTRL
#define SHUB_SOFT_RST_CTRL__COR_RESET_EN__SHIFT                                                               0x0
#define SHUB_SOFT_RST_CTRL__REG_RESET_EN__SHIFT                                                               0x1
#define SHUB_SOFT_RST_CTRL__STY_RESET_EN__SHIFT                                                               0x2
#define SHUB_SOFT_RST_CTRL__NIC400_RESET_EN__SHIFT                                                            0x3
#define SHUB_SOFT_RST_CTRL__SDP_PORT_RESET_EN__SHIFT                                                          0x4
//SHUB_SDP_PORT_RST
#define SHUB_SDP_PORT_RST__SDP_PORT_RST__SHIFT                                                                0x0


// addressBlock: bif_bx_pf_SYSDEC
//SBIOS_SCRATCH_0
#define SBIOS_SCRATCH_0__SBIOS_SCRATCH_DW__SHIFT                                                              0x0
//SBIOS_SCRATCH_1
#define SBIOS_SCRATCH_1__SBIOS_SCRATCH_DW__SHIFT                                                              0x0
//SBIOS_SCRATCH_2
#define SBIOS_SCRATCH_2__SBIOS_SCRATCH_DW__SHIFT                                                              0x0
//SBIOS_SCRATCH_3
#define SBIOS_SCRATCH_3__SBIOS_SCRATCH_DW__SHIFT                                                              0x0
//BIOS_SCRATCH_0
#define BIOS_SCRATCH_0__BIOS_SCRATCH_0__SHIFT                                                                 0x0
//BIOS_SCRATCH_1
#define BIOS_SCRATCH_1__BIOS_SCRATCH_1__SHIFT                                                                 0x0
//BIOS_SCRATCH_2
#define BIOS_SCRATCH_2__BIOS_SCRATCH_2__SHIFT                                                                 0x0
//BIOS_SCRATCH_3
#define BIOS_SCRATCH_3__BIOS_SCRATCH_3__SHIFT                                                                 0x0
//BIOS_SCRATCH_4
#define BIOS_SCRATCH_4__BIOS_SCRATCH_4__SHIFT                                                                 0x0
//BIOS_SCRATCH_5
#define BIOS_SCRATCH_5__BIOS_SCRATCH_5__SHIFT                                                                 0x0
//BIOS_SCRATCH_6
#define BIOS_SCRATCH_6__BIOS_SCRATCH_6__SHIFT                                                                 0x0
//BIOS_SCRATCH_7
#define BIOS_SCRATCH_7__BIOS_SCRATCH_7__SHIFT                                                                 0x0
//BIOS_SCRATCH_8
#define BIOS_SCRATCH_8__BIOS_SCRATCH_8__SHIFT                                                                 0x0
//BIOS_SCRATCH_9
#define BIOS_SCRATCH_9__BIOS_SCRATCH_9__SHIFT                                                                 0x0
//BIOS_SCRATCH_10
#define BIOS_SCRATCH_10__BIOS_SCRATCH_10__SHIFT                                                               0x0
//BIOS_SCRATCH_11
#define BIOS_SCRATCH_11__BIOS_SCRATCH_11__SHIFT                                                               0x0
//BIOS_SCRATCH_12
#define BIOS_SCRATCH_12__BIOS_SCRATCH_12__SHIFT                                                               0x0
//BIOS_SCRATCH_13
#define BIOS_SCRATCH_13__BIOS_SCRATCH_13__SHIFT                                                               0x0
//BIOS_SCRATCH_14
#define BIOS_SCRATCH_14__BIOS_SCRATCH_14__SHIFT                                                               0x0
//BIOS_SCRATCH_15
#define BIOS_SCRATCH_15__BIOS_SCRATCH_15__SHIFT                                                               0x0
//BIF_RLC_INTR_CNTL
#define BIF_RLC_INTR_CNTL__RLC_CMD_COMPLETE__SHIFT                                                            0x0
#define BIF_RLC_INTR_CNTL__RLC_HANG_SELF_RECOVERED__SHIFT                                                     0x1
#define BIF_RLC_INTR_CNTL__RLC_HANG_NEED_FLR__SHIFT                                                           0x2
#define BIF_RLC_INTR_CNTL__RLC_VM_BUSY_TRANSITION__SHIFT                                                      0x3
//BIF_VCE_INTR_CNTL
#define BIF_VCE_INTR_CNTL__VCE_CMD_COMPLETE__SHIFT                                                            0x0
#define BIF_VCE_INTR_CNTL__VCE_HANG_SELF_RECOVERED__SHIFT                                                     0x1
#define BIF_VCE_INTR_CNTL__VCE_HANG_NEED_FLR__SHIFT                                                           0x2
#define BIF_VCE_INTR_CNTL__VCE_VM_BUSY_TRANSITION__SHIFT                                                      0x3
//BIF_UVD_INTR_CNTL
#define BIF_UVD_INTR_CNTL__UVD_CMD_COMPLETE__SHIFT                                                            0x0
#define BIF_UVD_INTR_CNTL__UVD_HANG_SELF_RECOVERED__SHIFT                                                     0x1
#define BIF_UVD_INTR_CNTL__UVD_HANG_NEED_FLR__SHIFT                                                           0x2
#define BIF_UVD_INTR_CNTL__UVD_VM_BUSY_TRANSITION__SHIFT                                                      0x3
//GFX_MMIOREG_CAM_ADDR0
#define GFX_MMIOREG_CAM_ADDR0__CAM_ADDR0__SHIFT                                                               0x0
//GFX_MMIOREG_CAM_REMAP_ADDR0
#define GFX_MMIOREG_CAM_REMAP_ADDR0__CAM_REMAP_ADDR0__SHIFT                                                   0x0
//GFX_MMIOREG_CAM_ADDR1
#define GFX_MMIOREG_CAM_ADDR1__CAM_ADDR1__SHIFT                                                               0x0
//GFX_MMIOREG_CAM_REMAP_ADDR1
#define GFX_MMIOREG_CAM_REMAP_ADDR1__CAM_REMAP_ADDR1__SHIFT                                                   0x0
//GFX_MMIOREG_CAM_ADDR2
#define GFX_MMIOREG_CAM_ADDR2__CAM_ADDR2__SHIFT                                                               0x0
//GFX_MMIOREG_CAM_REMAP_ADDR2
#define GFX_MMIOREG_CAM_REMAP_ADDR2__CAM_REMAP_ADDR2__SHIFT                                                   0x0
//GFX_MMIOREG_CAM_ADDR3
#define GFX_MMIOREG_CAM_ADDR3__CAM_ADDR3__SHIFT                                                               0x0
//GFX_MMIOREG_CAM_REMAP_ADDR3
#define GFX_MMIOREG_CAM_REMAP_ADDR3__CAM_REMAP_ADDR3__SHIFT                                                   0x0
//GFX_MMIOREG_CAM_ADDR4
#define GFX_MMIOREG_CAM_ADDR4__CAM_ADDR4__SHIFT                                                               0x0
//GFX_MMIOREG_CAM_REMAP_ADDR4
#define GFX_MMIOREG_CAM_REMAP_ADDR4__CAM_REMAP_ADDR4__SHIFT                                                   0x0
//GFX_MMIOREG_CAM_ADDR5
#define GFX_MMIOREG_CAM_ADDR5__CAM_ADDR5__SHIFT                                                               0x0
//GFX_MMIOREG_CAM_REMAP_ADDR5
#define GFX_MMIOREG_CAM_REMAP_ADDR5__CAM_REMAP_ADDR5__SHIFT                                                   0x0
//GFX_MMIOREG_CAM_ADDR6
#define GFX_MMIOREG_CAM_ADDR6__CAM_ADDR6__SHIFT                                                               0x0
//GFX_MMIOREG_CAM_REMAP_ADDR6
#define GFX_MMIOREG_CAM_REMAP_ADDR6__CAM_REMAP_ADDR6__SHIFT                                                   0x0
//GFX_MMIOREG_CAM_ADDR7
#define GFX_MMIOREG_CAM_ADDR7__CAM_ADDR7__SHIFT                                                               0x0
//GFX_MMIOREG_CAM_REMAP_ADDR7
#define GFX_MMIOREG_CAM_REMAP_ADDR7__CAM_REMAP_ADDR7__SHIFT                                                   0x0
//GFX_MMIOREG_CAM_CNTL
#define GFX_MMIOREG_CAM_CNTL__CAM_ENABLE__SHIFT                                                               0x0
//GFX_MMIOREG_CAM_ZERO_CPL
#define GFX_MMIOREG_CAM_ZERO_CPL__CAM_ZERO_CPL__SHIFT                                                         0x0
//GFX_MMIOREG_CAM_ONE_CPL
#define GFX_MMIOREG_CAM_ONE_CPL__CAM_ONE_CPL__SHIFT                                                           0x0
//GFX_MMIOREG_CAM_PROGRAMMABLE_CPL
#define GFX_MMIOREG_CAM_PROGRAMMABLE_CPL__CAM_PROGRAMMABLE_CPL__SHIFT                                         0x0


// addressBlock: bif_bx_pf_SYSPFVFDEC
//MM_INDEX
#define MM_INDEX__MM_OFFSET__SHIFT                                                                            0x0
#define MM_INDEX__MM_APER__SHIFT                                                                              0x1f
//MM_DATA
#define MM_DATA__MM_DATA__SHIFT                                                                               0x0
//MM_INDEX_HI
#define MM_INDEX_HI__MM_OFFSET_HI__SHIFT                                                                      0x0
//SYSHUB_INDEX_OVLP
#define SYSHUB_INDEX_OVLP__SYSHUB_OFFSET__SHIFT                                                               0x0
//SYSHUB_DATA_OVLP
#define SYSHUB_DATA_OVLP__SYSHUB_DATA__SHIFT                                                                  0x0
//PCIE_INDEX
#define PCIE_INDEX__PCIE_INDEX__SHIFT                                                                         0x0
//PCIE_DATA
#define PCIE_DATA__PCIE_DATA__SHIFT                                                                           0x0
//PCIE_INDEX2
#define PCIE_INDEX2__PCIE_INDEX2__SHIFT                                                                       0x0
//PCIE_DATA2
#define PCIE_DATA2__PCIE_DATA2__SHIFT                                                                         0x0


// addressBlock: rcc_dwn_BIFDEC1
//DN_PCIE_RESERVED
#define DN_PCIE_RESERVED__PCIE_RESERVED__SHIFT                                                                0x0
//DN_PCIE_SCRATCH
#define DN_PCIE_SCRATCH__PCIE_SCRATCH__SHIFT                                                                  0x0
//DN_PCIE_CNTL
#define DN_PCIE_CNTL__HWINIT_WR_LOCK__SHIFT                                                                   0x0
#define DN_PCIE_CNTL__UR_ERR_REPORT_DIS_DN__SHIFT                                                             0x7
#define DN_PCIE_CNTL__RX_IGNORE_LTR_MSG_UR__SHIFT                                                             0x1e
//DN_PCIE_CONFIG_CNTL
#define DN_PCIE_CONFIG_CNTL__CI_EXTENDED_TAG_EN_OVERRIDE__SHIFT                                               0x19
//DN_PCIE_RX_CNTL2
#define DN_PCIE_RX_CNTL2__FLR_EXTEND_MODE__SHIFT                                                              0x1c
//DN_PCIE_BUS_CNTL
#define DN_PCIE_BUS_CNTL__IMMEDIATE_PMI_DIS__SHIFT                                                            0x7
#define DN_PCIE_BUS_CNTL__AER_CPL_TIMEOUT_RO_DIS_SWDN__SHIFT                                                  0x8
//DN_PCIE_CFG_CNTL
#define DN_PCIE_CFG_CNTL__CFG_EN_DEC_TO_HIDDEN_REG__SHIFT                                                     0x0
#define DN_PCIE_CFG_CNTL__CFG_EN_DEC_TO_GEN2_HIDDEN_REG__SHIFT                                                0x1
#define DN_PCIE_CFG_CNTL__CFG_EN_DEC_TO_GEN3_HIDDEN_REG__SHIFT                                                0x2
//DN_PCIE_STRAP_F0
#define DN_PCIE_STRAP_F0__STRAP_F0_EN__SHIFT                                                                  0x0
#define DN_PCIE_STRAP_F0__STRAP_F0_MC_EN__SHIFT                                                               0x11
#define DN_PCIE_STRAP_F0__STRAP_F0_MSI_MULTI_CAP__SHIFT                                                       0x15
//DN_PCIE_STRAP_MISC
#define DN_PCIE_STRAP_MISC__STRAP_CLK_PM_EN__SHIFT                                                            0x18
#define DN_PCIE_STRAP_MISC__STRAP_MST_ADR64_EN__SHIFT                                                         0x1d
//DN_PCIE_STRAP_MISC2
#define DN_PCIE_STRAP_MISC2__STRAP_MSTCPL_TIMEOUT_EN__SHIFT                                                   0x2


// addressBlock: rcc_dwnp_BIFDEC1
//PCIEP_RESERVED
#define PCIEP_RESERVED__PCIEP_RESERVED__SHIFT                                                                 0x0
//PCIEP_SCRATCH
#define PCIEP_SCRATCH__PCIEP_SCRATCH__SHIFT                                                                   0x0
//PCIE_ERR_CNTL
#define PCIE_ERR_CNTL__ERR_REPORTING_DIS__SHIFT                                                               0x0
#define PCIE_ERR_CNTL__AER_HDR_LOG_TIMEOUT__SHIFT                                                             0x8
#define PCIE_ERR_CNTL__AER_HDR_LOG_F0_TIMER_EXPIRED__SHIFT                                                    0xb
#define PCIE_ERR_CNTL__SEND_ERR_MSG_IMMEDIATELY__SHIFT                                                        0x11
//PCIE_RX_CNTL
#define PCIE_RX_CNTL__RX_IGNORE_MAX_PAYLOAD_ERR__SHIFT                                                        0x8
#define PCIE_RX_CNTL__RX_IGNORE_TC_ERR_DN__SHIFT                                                              0x9
#define PCIE_RX_CNTL__RX_PCIE_CPL_TIMEOUT_DIS__SHIFT                                                          0x14
#define PCIE_RX_CNTL__RX_IGNORE_SHORTPREFIX_ERR_DN__SHIFT                                                     0x15
#define PCIE_RX_CNTL__RX_RCB_FLR_TIMEOUT_DIS__SHIFT                                                           0x1b
//PCIE_LC_SPEED_CNTL
#define PCIE_LC_SPEED_CNTL__LC_GEN2_EN_STRAP__SHIFT                                                           0x0
#define PCIE_LC_SPEED_CNTL__LC_GEN3_EN_STRAP__SHIFT                                                           0x1
//PCIE_LC_CNTL2
#define PCIE_LC_CNTL2__LC_LINK_BW_NOTIFICATION_DIS__SHIFT                                                     0x1b
//PCIEP_STRAP_MISC
#define PCIEP_STRAP_MISC__STRAP_MULTI_FUNC_EN__SHIFT                                                          0xa
//LTR_MSG_INFO_FROM_EP
#define LTR_MSG_INFO_FROM_EP__LTR_MSG_INFO_FROM_EP__SHIFT                                                     0x0


// addressBlock: rcc_ep_BIFDEC1
//EP_PCIE_SCRATCH
#define EP_PCIE_SCRATCH__PCIE_SCRATCH__SHIFT                                                                  0x0
//EP_PCIE_CNTL
#define EP_PCIE_CNTL__UR_ERR_REPORT_DIS__SHIFT                                                                0x7
#define EP_PCIE_CNTL__PCIE_MALFORM_ATOMIC_OPS__SHIFT                                                          0x8
#define EP_PCIE_CNTL__RX_IGNORE_LTR_MSG_UR__SHIFT                                                             0x1e
//EP_PCIE_INT_CNTL
#define EP_PCIE_INT_CNTL__CORR_ERR_INT_EN__SHIFT                                                              0x0
#define EP_PCIE_INT_CNTL__NON_FATAL_ERR_INT_EN__SHIFT                                                         0x1
#define EP_PCIE_INT_CNTL__FATAL_ERR_INT_EN__SHIFT                                                             0x2
#define EP_PCIE_INT_CNTL__USR_DETECTED_INT_EN__SHIFT                                                          0x3
#define EP_PCIE_INT_CNTL__MISC_ERR_INT_EN__SHIFT                                                              0x4
#define EP_PCIE_INT_CNTL__POWER_STATE_CHG_INT_EN__SHIFT                                                       0x6
//EP_PCIE_INT_STATUS
#define EP_PCIE_INT_STATUS__CORR_ERR_INT_STATUS__SHIFT                                                        0x0
#define EP_PCIE_INT_STATUS__NON_FATAL_ERR_INT_STATUS__SHIFT                                                   0x1
#define EP_PCIE_INT_STATUS__FATAL_ERR_INT_STATUS__SHIFT                                                       0x2
#define EP_PCIE_INT_STATUS__USR_DETECTED_INT_STATUS__SHIFT                                                    0x3
#define EP_PCIE_INT_STATUS__MISC_ERR_INT_STATUS__SHIFT                                                        0x4
#define EP_PCIE_INT_STATUS__POWER_STATE_CHG_INT_STATUS__SHIFT                                                 0x6
//EP_PCIE_RX_CNTL2
#define EP_PCIE_RX_CNTL2__RX_IGNORE_EP_INVALIDPASID_UR__SHIFT                                                 0x0
//EP_PCIE_BUS_CNTL
#define EP_PCIE_BUS_CNTL__IMMEDIATE_PMI_DIS__SHIFT                                                            0x7
//EP_PCIE_CFG_CNTL
#define EP_PCIE_CFG_CNTL__CFG_EN_DEC_TO_HIDDEN_REG__SHIFT                                                     0x0
#define EP_PCIE_CFG_CNTL__CFG_EN_DEC_TO_GEN2_HIDDEN_REG__SHIFT                                                0x1
#define EP_PCIE_CFG_CNTL__CFG_EN_DEC_TO_GEN3_HIDDEN_REG__SHIFT                                                0x2
//EP_PCIE_OBFF_CNTL
#define EP_PCIE_OBFF_CNTL__TX_OBFF_PRIV_DISABLE__SHIFT                                                        0x0
#define EP_PCIE_OBFF_CNTL__TX_OBFF_WAKE_SIMPLE_MODE_EN__SHIFT                                                 0x1
#define EP_PCIE_OBFF_CNTL__TX_OBFF_HOSTMEM_TO_ACTIVE__SHIFT                                                   0x2
#define EP_PCIE_OBFF_CNTL__TX_OBFF_SLVCPL_TO_ACTIVE__SHIFT                                                    0x3
#define EP_PCIE_OBFF_CNTL__TX_OBFF_WAKE_MAX_PULSE_WIDTH__SHIFT                                                0x4
#define EP_PCIE_OBFF_CNTL__TX_OBFF_WAKE_MAX_TWO_FALLING_WIDTH__SHIFT                                          0x8
#define EP_PCIE_OBFF_CNTL__TX_OBFF_WAKE_SAMPLING_PERIOD__SHIFT                                                0xc
#define EP_PCIE_OBFF_CNTL__TX_OBFF_INTR_TO_ACTIVE__SHIFT                                                      0x10
#define EP_PCIE_OBFF_CNTL__TX_OBFF_ERR_TO_ACTIVE__SHIFT                                                       0x11
#define EP_PCIE_OBFF_CNTL__TX_OBFF_ANY_MSG_TO_ACTIVE__SHIFT                                                   0x12
#define EP_PCIE_OBFF_CNTL__TX_OBFF_ACCEPT_IN_NOND0__SHIFT                                                     0x13
#define EP_PCIE_OBFF_CNTL__TX_OBFF_PENDING_REQ_TO_ACTIVE__SHIFT                                               0x14
//EP_PCIE_TX_LTR_CNTL
#define EP_PCIE_TX_LTR_CNTL__LTR_PRIV_S_SHORT_VALUE__SHIFT                                                    0x0
#define EP_PCIE_TX_LTR_CNTL__LTR_PRIV_S_LONG_VALUE__SHIFT                                                     0x3
#define EP_PCIE_TX_LTR_CNTL__LTR_PRIV_S_REQUIREMENT__SHIFT                                                    0x6
#define EP_PCIE_TX_LTR_CNTL__LTR_PRIV_NS_SHORT_VALUE__SHIFT                                                   0x7
#define EP_PCIE_TX_LTR_CNTL__LTR_PRIV_NS_LONG_VALUE__SHIFT                                                    0xa
#define EP_PCIE_TX_LTR_CNTL__LTR_PRIV_NS_REQUIREMENT__SHIFT                                                   0xd
#define EP_PCIE_TX_LTR_CNTL__LTR_PRIV_MSG_DIS_IN_PM_NON_D0__SHIFT                                             0xe
#define EP_PCIE_TX_LTR_CNTL__LTR_PRIV_RST_LTR_IN_DL_DOWN__SHIFT                                               0xf
#define EP_PCIE_TX_LTR_CNTL__TX_CHK_FC_FOR_L1__SHIFT                                                          0x10
//EP_PCIE_STRAP_MISC
#define EP_PCIE_STRAP_MISC__STRAP_MST_ADR64_EN__SHIFT                                                         0x1d
//EP_PCIE_STRAP_MISC2
#define EP_PCIE_STRAP_MISC2__STRAP_TPH_SUPPORTED__SHIFT                                                       0x4
//EP_PCIE_STRAP_PI
//EP_PCIE_F0_DPA_CAP
#define EP_PCIE_F0_DPA_CAP__TRANS_LAT_UNIT__SHIFT                                                             0x8
#define EP_PCIE_F0_DPA_CAP__PWR_ALLOC_SCALE__SHIFT                                                            0xc
#define EP_PCIE_F0_DPA_CAP__TRANS_LAT_VAL_0__SHIFT                                                            0x10
#define EP_PCIE_F0_DPA_CAP__TRANS_LAT_VAL_1__SHIFT                                                            0x18
//EP_PCIE_F0_DPA_LATENCY_INDICATOR
#define EP_PCIE_F0_DPA_LATENCY_INDICATOR__TRANS_LAT_INDICATOR_BITS__SHIFT                                     0x0
//EP_PCIE_F0_DPA_CNTL
#define EP_PCIE_F0_DPA_CNTL__SUBSTATE_STATUS__SHIFT                                                           0x0
#define EP_PCIE_F0_DPA_CNTL__DPA_COMPLIANCE_MODE__SHIFT                                                       0x8
//PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_0
#define PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_0__SUBSTATE_PWR_ALLOC__SHIFT                                           0x0
//PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_1
#define PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_1__SUBSTATE_PWR_ALLOC__SHIFT                                           0x0
//PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_2
#define PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_2__SUBSTATE_PWR_ALLOC__SHIFT                                           0x0
//PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_3
#define PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_3__SUBSTATE_PWR_ALLOC__SHIFT                                           0x0
//PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_4
#define PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_4__SUBSTATE_PWR_ALLOC__SHIFT                                           0x0
//PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_5
#define PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_5__SUBSTATE_PWR_ALLOC__SHIFT                                           0x0
//PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_6
#define PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_6__SUBSTATE_PWR_ALLOC__SHIFT                                           0x0
//PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_7
#define PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_7__SUBSTATE_PWR_ALLOC__SHIFT                                           0x0
//EP_PCIE_PME_CONTROL
#define EP_PCIE_PME_CONTROL__PME_SERVICE_TIMER__SHIFT                                                         0x0
//EP_PCIEP_RESERVED
#define EP_PCIEP_RESERVED__PCIEP_RESERVED__SHIFT                                                              0x0
//EP_PCIE_TX_CNTL
#define EP_PCIE_TX_CNTL__TX_SNR_OVERRIDE__SHIFT                                                               0xa
#define EP_PCIE_TX_CNTL__TX_RO_OVERRIDE__SHIFT                                                                0xc
#define EP_PCIE_TX_CNTL__TX_F0_TPH_DIS__SHIFT                                                                 0x18
#define EP_PCIE_TX_CNTL__TX_F1_TPH_DIS__SHIFT                                                                 0x19
#define EP_PCIE_TX_CNTL__TX_F2_TPH_DIS__SHIFT                                                                 0x1a
//EP_PCIE_TX_REQUESTER_ID
#define EP_PCIE_TX_REQUESTER_ID__TX_REQUESTER_ID_FUNCTION__SHIFT                                              0x0
#define EP_PCIE_TX_REQUESTER_ID__TX_REQUESTER_ID_DEVICE__SHIFT                                                0x3
#define EP_PCIE_TX_REQUESTER_ID__TX_REQUESTER_ID_BUS__SHIFT                                                   0x8
//EP_PCIE_ERR_CNTL
#define EP_PCIE_ERR_CNTL__ERR_REPORTING_DIS__SHIFT                                                            0x0
#define EP_PCIE_ERR_CNTL__AER_HDR_LOG_TIMEOUT__SHIFT                                                          0x8
#define EP_PCIE_ERR_CNTL__SEND_ERR_MSG_IMMEDIATELY__SHIFT                                                     0x11
#define EP_PCIE_ERR_CNTL__STRAP_POISONED_ADVISORY_NONFATAL__SHIFT                                             0x12
#define EP_PCIE_ERR_CNTL__AER_HDR_LOG_F0_TIMER_EXPIRED__SHIFT                                                 0x18
#define EP_PCIE_ERR_CNTL__AER_HDR_LOG_F1_TIMER_EXPIRED__SHIFT                                                 0x19
#define EP_PCIE_ERR_CNTL__AER_HDR_LOG_F2_TIMER_EXPIRED__SHIFT                                                 0x1a
#define EP_PCIE_ERR_CNTL__AER_HDR_LOG_F3_TIMER_EXPIRED__SHIFT                                                 0x1b
#define EP_PCIE_ERR_CNTL__AER_HDR_LOG_F4_TIMER_EXPIRED__SHIFT                                                 0x1c
#define EP_PCIE_ERR_CNTL__AER_HDR_LOG_F5_TIMER_EXPIRED__SHIFT                                                 0x1d
#define EP_PCIE_ERR_CNTL__AER_HDR_LOG_F6_TIMER_EXPIRED__SHIFT                                                 0x1e
#define EP_PCIE_ERR_CNTL__AER_HDR_LOG_F7_TIMER_EXPIRED__SHIFT                                                 0x1f
//EP_PCIE_RX_CNTL
#define EP_PCIE_RX_CNTL__RX_IGNORE_MAX_PAYLOAD_ERR__SHIFT                                                     0x8
#define EP_PCIE_RX_CNTL__RX_IGNORE_TC_ERR__SHIFT                                                              0x9
#define EP_PCIE_RX_CNTL__RX_PCIE_CPL_TIMEOUT_DIS__SHIFT                                                       0x14
#define EP_PCIE_RX_CNTL__RX_IGNORE_SHORTPREFIX_ERR__SHIFT                                                     0x15
#define EP_PCIE_RX_CNTL__RX_IGNORE_MAXPREFIX_ERR__SHIFT                                                       0x16
#define EP_PCIE_RX_CNTL__RX_IGNORE_INVALIDPASID_ERR__SHIFT                                                    0x18
#define EP_PCIE_RX_CNTL__RX_IGNORE_NOT_PASID_UR__SHIFT                                                        0x19
#define EP_PCIE_RX_CNTL__RX_TPH_DIS__SHIFT                                                                    0x1a
//EP_PCIE_LC_SPEED_CNTL
#define EP_PCIE_LC_SPEED_CNTL__LC_GEN2_EN_STRAP__SHIFT                                                        0x0
#define EP_PCIE_LC_SPEED_CNTL__LC_GEN3_EN_STRAP__SHIFT                                                        0x1


// addressBlock: bif_bx_pf_BIFDEC1
//BIF_MM_INDACCESS_CNTL
#define BIF_MM_INDACCESS_CNTL__MM_INDACCESS_DIS__SHIFT                                                        0x1
//BUS_CNTL
#define BUS_CNTL__PMI_INT_DIS_EP__SHIFT                                                                       0x3
#define BUS_CNTL__PMI_INT_DIS_DN__SHIFT                                                                       0x4
#define BUS_CNTL__PMI_INT_DIS_SWUS__SHIFT                                                                     0x5
#define BUS_CNTL__VGA_REG_COHERENCY_DIS__SHIFT                                                                0x6
#define BUS_CNTL__VGA_MEM_COHERENCY_DIS__SHIFT                                                                0x7
#define BUS_CNTL__SET_AZ_TC__SHIFT                                                                            0xa
#define BUS_CNTL__SET_MC_TC__SHIFT                                                                            0xd
#define BUS_CNTL__ZERO_BE_WR_EN__SHIFT                                                                        0x10
#define BUS_CNTL__ZERO_BE_RD_EN__SHIFT                                                                        0x11
#define BUS_CNTL__RD_STALL_IO_WR__SHIFT                                                                       0x12
#define BUS_CNTL__DEASRT_INTX_DSTATE_CHK_DIS_EP__SHIFT                                                        0x13
#define BUS_CNTL__DEASRT_INTX_DSTATE_CHK_DIS_DN__SHIFT                                                        0x14
#define BUS_CNTL__DEASRT_INTX_DSTATE_CHK_DIS_SWUS__SHIFT                                                      0x15
#define BUS_CNTL__DEASRT_INTX_IN_NOND0_EN_EP__SHIFT                                                           0x16
#define BUS_CNTL__DEASRT_INTX_IN_NOND0_EN_DN__SHIFT                                                           0x17
#define BUS_CNTL__UR_OVRD_FOR_ECRC_EN__SHIFT                                                                  0x18
//BIF_SCRATCH0
#define BIF_SCRATCH0__BIF_SCRATCH0__SHIFT                                                                     0x0
//BIF_SCRATCH1
#define BIF_SCRATCH1__BIF_SCRATCH1__SHIFT                                                                     0x0
//BX_RESET_EN
#define BX_RESET_EN__COR_RESET_EN__SHIFT                                                                      0x0
#define BX_RESET_EN__REG_RESET_EN__SHIFT                                                                      0x1
#define BX_RESET_EN__STY_RESET_EN__SHIFT                                                                      0x2
#define BX_RESET_EN__FLR_TWICE_EN__SHIFT                                                                      0x8
#define BX_RESET_EN__RESET_ON_VFENABLE_LOW_EN__SHIFT                                                          0x10
//MM_CFGREGS_CNTL
#define MM_CFGREGS_CNTL__MM_CFG_FUNC_SEL__SHIFT                                                               0x0
#define MM_CFGREGS_CNTL__MM_CFG_DEV_SEL__SHIFT                                                                0x6
#define MM_CFGREGS_CNTL__MM_WR_TO_CFG_EN__SHIFT                                                               0x1f
//BX_RESET_CNTL
#define BX_RESET_CNTL__LINK_TRAIN_EN__SHIFT                                                                   0x0
//INTERRUPT_CNTL
#define INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE__SHIFT                                                           0x0
#define INTERRUPT_CNTL__IH_DUMMY_RD_EN__SHIFT                                                                 0x1
#define INTERRUPT_CNTL__IH_REQ_NONSNOOP_EN__SHIFT                                                             0x3
#define INTERRUPT_CNTL__IH_INTR_DLY_CNTR__SHIFT                                                               0x4
#define INTERRUPT_CNTL__GEN_IH_INT_EN__SHIFT                                                                  0x8
#define INTERRUPT_CNTL__BIF_RB_REQ_NONSNOOP_EN__SHIFT                                                         0xf
//INTERRUPT_CNTL2
#define INTERRUPT_CNTL2__IH_DUMMY_RD_ADDR__SHIFT                                                              0x0
//CLKREQB_PAD_CNTL
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_A__SHIFT                                                                0x0
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_SEL__SHIFT                                                              0x1
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_MODE__SHIFT                                                             0x2
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_SPARE__SHIFT                                                            0x3
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_SN0__SHIFT                                                              0x5
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_SN1__SHIFT                                                              0x6
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_SN2__SHIFT                                                              0x7
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_SN3__SHIFT                                                              0x8
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_SLEWN__SHIFT                                                            0x9
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_WAKE__SHIFT                                                             0xa
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_SCHMEN__SHIFT                                                           0xb
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_CNTL_EN__SHIFT                                                          0xc
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_Y__SHIFT                                                                0xd
#define CLKREQB_PAD_CNTL__CLKREQB_PERF_COUNTER_UPPER__SHIFT                                                   0x18
//CLKREQB_PERF_COUNTER
#define CLKREQB_PERF_COUNTER__CLKREQB_PERF_COUNTER_LOWER__SHIFT                                               0x0
//BIF_CLK_CTRL
#define BIF_CLK_CTRL__BIF_XSTCLK_READY__SHIFT                                                                 0x0
#define BIF_CLK_CTRL__BACO_XSTCLK_SWITCH_BYPASS__SHIFT                                                        0x1
//BIF_FEATURES_CONTROL_MISC
#define BIF_FEATURES_CONTROL_MISC__MST_BIF_REQ_EP_DIS__SHIFT                                                  0x0
#define BIF_FEATURES_CONTROL_MISC__SLV_BIF_CPL_EP_DIS__SHIFT                                                  0x1
#define BIF_FEATURES_CONTROL_MISC__BIF_SLV_REQ_EP_DIS__SHIFT                                                  0x2
#define BIF_FEATURES_CONTROL_MISC__BIF_MST_CPL_EP_DIS__SHIFT                                                  0x3
#define BIF_FEATURES_CONTROL_MISC__MC_BIF_REQ_ID_ROUTING_DIS__SHIFT                                           0x9
#define BIF_FEATURES_CONTROL_MISC__AZ_BIF_REQ_ID_ROUTING_DIS__SHIFT                                           0xa
#define BIF_FEATURES_CONTROL_MISC__ATC_PRG_RESP_PASID_UR_EN__SHIFT                                            0xb
#define BIF_FEATURES_CONTROL_MISC__BIF_RB_SET_OVERFLOW_EN__SHIFT                                              0xc
#define BIF_FEATURES_CONTROL_MISC__ATOMIC_ERR_INT_DIS__SHIFT                                                  0xd
#define BIF_FEATURES_CONTROL_MISC__BME_HDL_NONVIR_EN__SHIFT                                                   0xf
#define BIF_FEATURES_CONTROL_MISC__FLR_MST_PEND_CHK_DIS__SHIFT                                                0x11
#define BIF_FEATURES_CONTROL_MISC__FLR_SLV_PEND_CHK_DIS__SHIFT                                                0x12
#define BIF_FEATURES_CONTROL_MISC__DOORBELL_SELFRING_GPA_APER_CHK_48BIT_ADDR__SHIFT                           0x18
//BIF_DOORBELL_CNTL
#define BIF_DOORBELL_CNTL__SELF_RING_DIS__SHIFT                                                               0x0
#define BIF_DOORBELL_CNTL__TRANS_CHECK_DIS__SHIFT                                                             0x1
#define BIF_DOORBELL_CNTL__UNTRANS_LBACK_EN__SHIFT                                                            0x2
#define BIF_DOORBELL_CNTL__NON_CONSECUTIVE_BE_ZERO_DIS__SHIFT                                                 0x3
#define BIF_DOORBELL_CNTL__DOORBELL_MONITOR_EN__SHIFT                                                         0x4
#define BIF_DOORBELL_CNTL__DB_MNTR_INTGEN_DIS__SHIFT                                                          0x18
#define BIF_DOORBELL_CNTL__DB_MNTR_INTGEN_MODE_0__SHIFT                                                       0x19
#define BIF_DOORBELL_CNTL__DB_MNTR_INTGEN_MODE_1__SHIFT                                                       0x1a
#define BIF_DOORBELL_CNTL__DB_MNTR_INTGEN_MODE_2__SHIFT                                                       0x1b
//BIF_DOORBELL_INT_CNTL
#define BIF_DOORBELL_INT_CNTL__DOORBELL_INTERRUPT_STATUS__SHIFT                                               0x0
#define BIF_DOORBELL_INT_CNTL__IOHC_RAS_INTERRUPT_STATUS__SHIFT                                               0x1
#define BIF_DOORBELL_INT_CNTL__DOORBELL_INTERRUPT_CLEAR__SHIFT                                                0x10
#define BIF_DOORBELL_INT_CNTL__IOHC_RAS_INTERRUPT_CLEAR__SHIFT                                                0x11
//BIF_SLVARB_MODE
#define BIF_SLVARB_MODE__SLVARB_MODE__SHIFT                                                                   0x0
//BIF_FB_EN
#define BIF_FB_EN__FB_READ_EN__SHIFT                                                                          0x0
#define BIF_FB_EN__FB_WRITE_EN__SHIFT                                                                         0x1
//BIF_BUSY_DELAY_CNTR
#define BIF_BUSY_DELAY_CNTR__DELAY_CNT__SHIFT                                                                 0x0
//BIF_PERFMON_CNTL
#define BIF_PERFMON_CNTL__PERFCOUNTER_EN__SHIFT                                                               0x0
#define BIF_PERFMON_CNTL__PERFCOUNTER_RESET0__SHIFT                                                           0x1
#define BIF_PERFMON_CNTL__PERFCOUNTER_RESET1__SHIFT                                                           0x2
#define BIF_PERFMON_CNTL__PERF_SEL0__SHIFT                                                                    0x8
#define BIF_PERFMON_CNTL__PERF_SEL1__SHIFT                                                                    0xd
//BIF_PERFCOUNTER0_RESULT
#define BIF_PERFCOUNTER0_RESULT__PERFCOUNTER_RESULT__SHIFT                                                    0x0
//BIF_PERFCOUNTER1_RESULT
#define BIF_PERFCOUNTER1_RESULT__PERFCOUNTER_RESULT__SHIFT                                                    0x0
//BIF_MST_TRANS_PENDING_VF
#define BIF_MST_TRANS_PENDING_VF__BIF_MST_TRANS_PENDING__SHIFT                                                0x0
//BIF_SLV_TRANS_PENDING_VF
#define BIF_SLV_TRANS_PENDING_VF__BIF_SLV_TRANS_PENDING__SHIFT                                                0x0
//BACO_CNTL
#define BACO_CNTL__BACO_EN__SHIFT                                                                             0x0
#define BACO_CNTL__BACO_BIF_LCLK_SWITCH__SHIFT                                                                0x1
#define BACO_CNTL__BACO_DUMMY_EN__SHIFT                                                                       0x2
#define BACO_CNTL__BACO_POWER_OFF__SHIFT                                                                      0x3
#define BACO_CNTL__BACO_DSTATE_BYPASS__SHIFT                                                                  0x5
#define BACO_CNTL__BACO_RST_INTR_MASK__SHIFT                                                                  0x6
#define BACO_CNTL__BACO_MODE__SHIFT                                                                           0x8
#define BACO_CNTL__RCU_BIF_CONFIG_DONE__SHIFT                                                                 0x9
#define BACO_CNTL__BACO_AUTO_EXIT__SHIFT                                                                      0x1f
//BIF_BACO_EXIT_TIME0
#define BIF_BACO_EXIT_TIME0__BACO_EXIT_PXEN_CLR_TIMER__SHIFT                                                  0x0
//BIF_BACO_EXIT_TIMER1
#define BIF_BACO_EXIT_TIMER1__BACO_EXIT_SIDEBAND_TIMER__SHIFT                                                 0x0
#define BIF_BACO_EXIT_TIMER1__BACO_HW_EXIT_DIS__SHIFT                                                         0x1a
#define BIF_BACO_EXIT_TIMER1__PX_EN_OE_IN_PX_EN_HIGH__SHIFT                                                   0x1b
#define BIF_BACO_EXIT_TIMER1__PX_EN_OE_IN_PX_EN_LOW__SHIFT                                                    0x1c
#define BIF_BACO_EXIT_TIMER1__BACO_MODE_SEL__SHIFT                                                            0x1d
#define BIF_BACO_EXIT_TIMER1__AUTO_BACO_EXIT_CLR_BY_HW_DIS__SHIFT                                             0x1f
//BIF_BACO_EXIT_TIMER2
#define BIF_BACO_EXIT_TIMER2__BACO_EXIT_LCLK_BAK_TIMER__SHIFT                                                 0x0
//BIF_BACO_EXIT_TIMER3
#define BIF_BACO_EXIT_TIMER3__BACO_EXIT_DUMMY_EN_CLR_TIMER__SHIFT                                             0x0
//BIF_BACO_EXIT_TIMER4
#define BIF_BACO_EXIT_TIMER4__BACO_EXIT_BACO_EN_CLR_TIMER__SHIFT                                              0x0
//MEM_TYPE_CNTL
#define MEM_TYPE_CNTL__BF_MEM_PHY_G5_G3__SHIFT                                                                0x0
//SMU_BIF_VDDGFX_PWR_STATUS
#define SMU_BIF_VDDGFX_PWR_STATUS__VDDGFX_GFX_PWR_OFF__SHIFT                                                  0x0
//BIF_VDDGFX_GFX0_LOWER
#define BIF_VDDGFX_GFX0_LOWER__VDDGFX_GFX0_REG_LOWER__SHIFT                                                   0x2
#define BIF_VDDGFX_GFX0_LOWER__VDDGFX_GFX0_REG_CMP_EN__SHIFT                                                  0x1e
#define BIF_VDDGFX_GFX0_LOWER__VDDGFX_GFX0_REG_STALL_EN__SHIFT                                                0x1f
//BIF_VDDGFX_GFX0_UPPER
#define BIF_VDDGFX_GFX0_UPPER__VDDGFX_GFX0_REG_UPPER__SHIFT                                                   0x2
//BIF_VDDGFX_GFX1_LOWER
#define BIF_VDDGFX_GFX1_LOWER__VDDGFX_GFX1_REG_LOWER__SHIFT                                                   0x2
#define BIF_VDDGFX_GFX1_LOWER__VDDGFX_GFX1_REG_CMP_EN__SHIFT                                                  0x1e
#define BIF_VDDGFX_GFX1_LOWER__VDDGFX_GFX1_REG_STALL_EN__SHIFT                                                0x1f
//BIF_VDDGFX_GFX1_UPPER
#define BIF_VDDGFX_GFX1_UPPER__VDDGFX_GFX1_REG_UPPER__SHIFT                                                   0x2
//BIF_VDDGFX_GFX2_LOWER
#define BIF_VDDGFX_GFX2_LOWER__VDDGFX_GFX2_REG_LOWER__SHIFT                                                   0x2
#define BIF_VDDGFX_GFX2_LOWER__VDDGFX_GFX2_REG_CMP_EN__SHIFT                                                  0x1e
#define BIF_VDDGFX_GFX2_LOWER__VDDGFX_GFX2_REG_STALL_EN__SHIFT                                                0x1f
//BIF_VDDGFX_GFX2_UPPER
#define BIF_VDDGFX_GFX2_UPPER__VDDGFX_GFX2_REG_UPPER__SHIFT                                                   0x2
//BIF_VDDGFX_GFX3_LOWER
#define BIF_VDDGFX_GFX3_LOWER__VDDGFX_GFX3_REG_LOWER__SHIFT                                                   0x2
#define BIF_VDDGFX_GFX3_LOWER__VDDGFX_GFX3_REG_CMP_EN__SHIFT                                                  0x1e
#define BIF_VDDGFX_GFX3_LOWER__VDDGFX_GFX3_REG_STALL_EN__SHIFT                                                0x1f
//BIF_VDDGFX_GFX3_UPPER
#define BIF_VDDGFX_GFX3_UPPER__VDDGFX_GFX3_REG_UPPER__SHIFT                                                   0x2
//BIF_VDDGFX_GFX4_LOWER
#define BIF_VDDGFX_GFX4_LOWER__VDDGFX_GFX4_REG_LOWER__SHIFT                                                   0x2
#define BIF_VDDGFX_GFX4_LOWER__VDDGFX_GFX4_REG_CMP_EN__SHIFT                                                  0x1e
#define BIF_VDDGFX_GFX4_LOWER__VDDGFX_GFX4_REG_STALL_EN__SHIFT                                                0x1f
//BIF_VDDGFX_GFX4_UPPER
#define BIF_VDDGFX_GFX4_UPPER__VDDGFX_GFX4_REG_UPPER__SHIFT                                                   0x2
//BIF_VDDGFX_GFX5_LOWER
#define BIF_VDDGFX_GFX5_LOWER__VDDGFX_GFX5_REG_LOWER__SHIFT                                                   0x2
#define BIF_VDDGFX_GFX5_LOWER__VDDGFX_GFX5_REG_CMP_EN__SHIFT                                                  0x1e
#define BIF_VDDGFX_GFX5_LOWER__VDDGFX_GFX5_REG_STALL_EN__SHIFT                                                0x1f
//BIF_VDDGFX_GFX5_UPPER
#define BIF_VDDGFX_GFX5_UPPER__VDDGFX_GFX5_REG_UPPER__SHIFT                                                   0x2
//BIF_VDDGFX_RSV1_LOWER
#define BIF_VDDGFX_RSV1_LOWER__VDDGFX_RSV1_REG_LOWER__SHIFT                                                   0x2
#define BIF_VDDGFX_RSV1_LOWER__VDDGFX_RSV1_REG_CMP_EN__SHIFT                                                  0x1e
#define BIF_VDDGFX_RSV1_LOWER__VDDGFX_RSV1_REG_STALL_EN__SHIFT                                                0x1f
//BIF_VDDGFX_RSV1_UPPER
#define BIF_VDDGFX_RSV1_UPPER__VDDGFX_RSV1_REG_UPPER__SHIFT                                                   0x2
//BIF_VDDGFX_RSV2_LOWER
#define BIF_VDDGFX_RSV2_LOWER__VDDGFX_RSV2_REG_LOWER__SHIFT                                                   0x2
#define BIF_VDDGFX_RSV2_LOWER__VDDGFX_RSV2_REG_CMP_EN__SHIFT                                                  0x1e
#define BIF_VDDGFX_RSV2_LOWER__VDDGFX_RSV2_REG_STALL_EN__SHIFT                                                0x1f
//BIF_VDDGFX_RSV2_UPPER
#define BIF_VDDGFX_RSV2_UPPER__VDDGFX_RSV2_REG_UPPER__SHIFT                                                   0x2
//BIF_VDDGFX_RSV3_LOWER
#define BIF_VDDGFX_RSV3_LOWER__VDDGFX_RSV3_REG_LOWER__SHIFT                                                   0x2
#define BIF_VDDGFX_RSV3_LOWER__VDDGFX_RSV3_REG_CMP_EN__SHIFT                                                  0x1e
#define BIF_VDDGFX_RSV3_LOWER__VDDGFX_RSV3_REG_STALL_EN__SHIFT                                                0x1f
//BIF_VDDGFX_RSV3_UPPER
#define BIF_VDDGFX_RSV3_UPPER__VDDGFX_RSV3_REG_UPPER__SHIFT                                                   0x2
//BIF_VDDGFX_RSV4_LOWER
#define BIF_VDDGFX_RSV4_LOWER__VDDGFX_RSV4_REG_LOWER__SHIFT                                                   0x2
#define BIF_VDDGFX_RSV4_LOWER__VDDGFX_RSV4_REG_CMP_EN__SHIFT                                                  0x1e
#define BIF_VDDGFX_RSV4_LOWER__VDDGFX_RSV4_REG_STALL_EN__SHIFT                                                0x1f
//BIF_VDDGFX_RSV4_UPPER
#define BIF_VDDGFX_RSV4_UPPER__VDDGFX_RSV4_REG_UPPER__SHIFT                                                   0x2
//BIF_VDDGFX_FB_CMP
#define BIF_VDDGFX_FB_CMP__VDDGFX_FB_HDP_CMP_EN__SHIFT                                                        0x0
#define BIF_VDDGFX_FB_CMP__VDDGFX_FB_HDP_STALL_EN__SHIFT                                                      0x1
#define BIF_VDDGFX_FB_CMP__VDDGFX_FB_XDMA_CMP_EN__SHIFT                                                       0x2
#define BIF_VDDGFX_FB_CMP__VDDGFX_FB_XDMA_STALL_EN__SHIFT                                                     0x3
#define BIF_VDDGFX_FB_CMP__VDDGFX_FB_VGA_CMP_EN__SHIFT                                                        0x4
#define BIF_VDDGFX_FB_CMP__VDDGFX_FB_VGA_STALL_EN__SHIFT                                                      0x5
//BIF_DOORBELL_GBLAPER1_LOWER
#define BIF_DOORBELL_GBLAPER1_LOWER__DOORBELL_GBLAPER1_LOWER__SHIFT                                           0x2
#define BIF_DOORBELL_GBLAPER1_LOWER__DOORBELL_GBLAPER1_EN__SHIFT                                              0x1f
//BIF_DOORBELL_GBLAPER1_UPPER
#define BIF_DOORBELL_GBLAPER1_UPPER__DOORBELL_GBLAPER1_UPPER__SHIFT                                           0x2
//BIF_DOORBELL_GBLAPER2_LOWER
#define BIF_DOORBELL_GBLAPER2_LOWER__DOORBELL_GBLAPER2_LOWER__SHIFT                                           0x2
#define BIF_DOORBELL_GBLAPER2_LOWER__DOORBELL_GBLAPER2_EN__SHIFT                                              0x1f
//BIF_DOORBELL_GBLAPER2_UPPER
#define BIF_DOORBELL_GBLAPER2_UPPER__DOORBELL_GBLAPER2_UPPER__SHIFT                                           0x2
//REMAP_HDP_MEM_FLUSH_CNTL
#define REMAP_HDP_MEM_FLUSH_CNTL__ADDRESS__SHIFT                                                              0x2
//REMAP_HDP_REG_FLUSH_CNTL
#define REMAP_HDP_REG_FLUSH_CNTL__ADDRESS__SHIFT                                                              0x2
//BIF_RB_CNTL
#define BIF_RB_CNTL__RB_ENABLE__SHIFT                                                                         0x0
#define BIF_RB_CNTL__RB_SIZE__SHIFT                                                                           0x1
#define BIF_RB_CNTL__WPTR_WRITEBACK_ENABLE__SHIFT                                                             0x8
#define BIF_RB_CNTL__WPTR_WRITEBACK_TIMER__SHIFT                                                              0x9
#define BIF_RB_CNTL__BIF_RB_TRAN__SHIFT                                                                       0x11
#define BIF_RB_CNTL__WPTR_OVERFLOW_CLEAR__SHIFT                                                               0x1f
//BIF_RB_BASE
#define BIF_RB_BASE__ADDR__SHIFT                                                                              0x0
//BIF_RB_RPTR
#define BIF_RB_RPTR__OFFSET__SHIFT                                                                            0x2
//BIF_RB_WPTR
#define BIF_RB_WPTR__BIF_RB_OVERFLOW__SHIFT                                                                   0x0
#define BIF_RB_WPTR__OFFSET__SHIFT                                                                            0x2
//BIF_RB_WPTR_ADDR_HI
#define BIF_RB_WPTR_ADDR_HI__ADDR__SHIFT                                                                      0x0
//BIF_RB_WPTR_ADDR_LO
#define BIF_RB_WPTR_ADDR_LO__ADDR__SHIFT                                                                      0x2
//MAILBOX_INDEX
#define MAILBOX_INDEX__MAILBOX_INDEX__SHIFT                                                                   0x0
//BIF_GPUIOV_RESET_NOTIFICATION
#define BIF_GPUIOV_RESET_NOTIFICATION__RESET_NOTIFICATION__SHIFT                                              0x0
//BIF_UVD_GPUIOV_CFG_SIZE
#define BIF_UVD_GPUIOV_CFG_SIZE__UVD_GPUIOV_CFG_SIZE__SHIFT                                                   0x0
//BIF_VCE_GPUIOV_CFG_SIZE
#define BIF_VCE_GPUIOV_CFG_SIZE__VCE_GPUIOV_CFG_SIZE__SHIFT                                                   0x0
//BIF_GFX_SDMA_GPUIOV_CFG_SIZE
#define BIF_GFX_SDMA_GPUIOV_CFG_SIZE__GFX_SDMA_GPUIOV_CFG_SIZE__SHIFT                                         0x0
//BIF_GMI_WRR_WEIGHT
#define BIF_GMI_WRR_WEIGHT__GMI_REQ_REALTIME_WEIGHT__SHIFT                                                    0x0
#define BIF_GMI_WRR_WEIGHT__GMI_REQ_NORM_P_WEIGHT__SHIFT                                                      0x8
#define BIF_GMI_WRR_WEIGHT__GMI_REQ_NORM_NP_WEIGHT__SHIFT                                                     0x10
//NBIF_STRAP_WRITE_CTRL
#define NBIF_STRAP_WRITE_CTRL__NBIF_STRAP_WRITE_ONCE_ENABLE__SHIFT                                            0x0
//BIF_PERSTB_PAD_CNTL
#define BIF_PERSTB_PAD_CNTL__PERSTB_PAD_CNTL__SHIFT                                                           0x0
//BIF_PX_EN_PAD_CNTL
#define BIF_PX_EN_PAD_CNTL__PX_EN_PAD_CNTL__SHIFT                                                             0x0
//BIF_REFPADKIN_PAD_CNTL
#define BIF_REFPADKIN_PAD_CNTL__REFPADKIN_PAD_CNTL__SHIFT                                                     0x0
//BIF_CLKREQB_PAD_CNTL
#define BIF_CLKREQB_PAD_CNTL__CLKREQB_PAD_CNTL__SHIFT                                                         0x0


// addressBlock: rcc_pf_0_BIFDEC1
//RCC_BACO_CNTL_MISC
#define RCC_BACO_CNTL_MISC__BIF_ROM_REQ_DIS__SHIFT                                                            0x0
#define RCC_BACO_CNTL_MISC__BIF_AZ_REQ_DIS__SHIFT                                                             0x1
//RCC_RESET_EN
#define RCC_RESET_EN__DB_APER_RESET_EN__SHIFT                                                                 0xf
//RCC_VDM_SUPPORT
#define RCC_VDM_SUPPORT__MCTP_SUPPORT__SHIFT                                                                  0x0
#define RCC_VDM_SUPPORT__AMPTP_SUPPORT__SHIFT                                                                 0x1
#define RCC_VDM_SUPPORT__OTHER_VDM_SUPPORT__SHIFT                                                             0x2
#define RCC_VDM_SUPPORT__ROUTE_TO_RC_CHECK_IN_RCMODE__SHIFT                                                   0x3
#define RCC_VDM_SUPPORT__ROUTE_BROADCAST_CHECK_IN_RCMODE__SHIFT                                               0x4
//RCC_PEER_REG_RANGE0
#define RCC_PEER_REG_RANGE0__START_ADDR__SHIFT                                                                0x0
#define RCC_PEER_REG_RANGE0__END_ADDR__SHIFT                                                                  0x10
//RCC_PEER_REG_RANGE1
#define RCC_PEER_REG_RANGE1__START_ADDR__SHIFT                                                                0x0
#define RCC_PEER_REG_RANGE1__END_ADDR__SHIFT                                                                  0x10
//RCC_BUS_CNTL
#define RCC_BUS_CNTL__PMI_IO_DIS__SHIFT                                                                       0x2
#define RCC_BUS_CNTL__PMI_MEM_DIS__SHIFT                                                                      0x3
#define RCC_BUS_CNTL__PMI_BM_DIS__SHIFT                                                                       0x4
#define RCC_BUS_CNTL__PMI_IO_DIS_DN__SHIFT                                                                    0x5
#define RCC_BUS_CNTL__PMI_MEM_DIS_DN__SHIFT                                                                   0x6
#define RCC_BUS_CNTL__PMI_IO_DIS_UP__SHIFT                                                                    0x7
#define RCC_BUS_CNTL__PMI_MEM_DIS_UP__SHIFT                                                                   0x8
#define RCC_BUS_CNTL__ROOT_ERR_LOG_ON_EVENT__SHIFT                                                            0xc
#define RCC_BUS_CNTL__HOST_CPL_POISONED_LOG_IN_RC__SHIFT                                                      0xd
#define RCC_BUS_CNTL__DN_SEC_SIG_CPLCA_WITH_EP_ERR__SHIFT                                                     0x10
#define RCC_BUS_CNTL__DN_SEC_RCV_CPLCA_WITH_EP_ERR__SHIFT                                                     0x11
#define RCC_BUS_CNTL__DN_SEC_RCV_CPLUR_WITH_EP_ERR__SHIFT                                                     0x12
#define RCC_BUS_CNTL__DN_PRI_SIG_CPLCA_WITH_EP_ERR__SHIFT                                                     0x13
#define RCC_BUS_CNTL__DN_PRI_RCV_CPLCA_WITH_EP_ERR__SHIFT                                                     0x14
#define RCC_BUS_CNTL__DN_PRI_RCV_CPLUR_WITH_EP_ERR__SHIFT                                                     0x15
#define RCC_BUS_CNTL__MAX_PAYLOAD_SIZE_MODE__SHIFT                                                            0x18
#define RCC_BUS_CNTL__PRIV_MAX_PAYLOAD_SIZE__SHIFT                                                            0x19
#define RCC_BUS_CNTL__MAX_READ_REQUEST_SIZE_MODE__SHIFT                                                       0x1c
#define RCC_BUS_CNTL__PRIV_MAX_READ_REQUEST_SIZE__SHIFT                                                       0x1d
//RCC_CONFIG_CNTL
#define RCC_CONFIG_CNTL__CFG_VGA_RAM_EN__SHIFT                                                                0x0
#define RCC_CONFIG_CNTL__GENMO_MONO_ADDRESS_B__SHIFT                                                          0x2
#define RCC_CONFIG_CNTL__GRPH_ADRSEL__SHIFT                                                                   0x3
//RCC_CONFIG_F0_BASE
#define RCC_CONFIG_F0_BASE__F0_BASE__SHIFT                                                                    0x0
//RCC_CONFIG_APER_SIZE
#define RCC_CONFIG_APER_SIZE__APER_SIZE__SHIFT                                                                0x0
//RCC_CONFIG_REG_APER_SIZE
#define RCC_CONFIG_REG_APER_SIZE__REG_APER_SIZE__SHIFT                                                        0x0
//RCC_XDMA_LO
#define RCC_XDMA_LO__BIF_XDMA_LOWER_BOUND__SHIFT                                                              0x0
#define RCC_XDMA_LO__BIF_XDMA_APER_EN__SHIFT                                                                  0x1f
//RCC_XDMA_HI
#define RCC_XDMA_HI__BIF_XDMA_UPPER_BOUND__SHIFT                                                              0x0
//RCC_FEATURES_CONTROL_MISC
#define RCC_FEATURES_CONTROL_MISC__UR_PSN_PKT_REPORT_POISON_DIS__SHIFT                                        0x4
#define RCC_FEATURES_CONTROL_MISC__POST_PSN_ONLY_PKT_REPORT_UR_ALL_DIS__SHIFT                                 0x5
#define RCC_FEATURES_CONTROL_MISC__POST_PSN_ONLY_PKT_REPORT_UR_PART_DIS__SHIFT                                0x6
#define RCC_FEATURES_CONTROL_MISC__ATC_PRG_RESP_PASID_UR_EN__SHIFT                                            0x8
#define RCC_FEATURES_CONTROL_MISC__RX_IGNORE_TRANSMRD_UR__SHIFT                                               0x9
#define RCC_FEATURES_CONTROL_MISC__RX_IGNORE_TRANSMWR_UR__SHIFT                                               0xa
#define RCC_FEATURES_CONTROL_MISC__RX_IGNORE_ATSTRANSREQ_UR__SHIFT                                            0xb
#define RCC_FEATURES_CONTROL_MISC__RX_IGNORE_PAGEREQMSG_UR__SHIFT                                             0xc
#define RCC_FEATURES_CONTROL_MISC__RX_IGNORE_INVCPL_UR__SHIFT                                                 0xd
#define RCC_FEATURES_CONTROL_MISC__CLR_MSI_X_PENDING_WHEN_DISABLED_DIS__SHIFT                                 0xe
#define RCC_FEATURES_CONTROL_MISC__CHECK_BME_ON_PENDING_PKT_GEN_DIS__SHIFT                                    0xf
#define RCC_FEATURES_CONTROL_MISC__PSN_CHECK_ON_PAYLOAD_DIS__SHIFT                                            0x10
#define RCC_FEATURES_CONTROL_MISC__CLR_MSI_PENDING_ON_MULTIEN_DIS__SHIFT                                      0x11
#define RCC_FEATURES_CONTROL_MISC__SET_DEVICE_ERR_FOR_ECRC_EN__SHIFT                                          0x12
//RCC_BUSNUM_CNTL1
#define RCC_BUSNUM_CNTL1__ID_MASK__SHIFT                                                                      0x0
//RCC_BUSNUM_LIST0
#define RCC_BUSNUM_LIST0__ID0__SHIFT                                                                          0x0
#define RCC_BUSNUM_LIST0__ID1__SHIFT                                                                          0x8
#define RCC_BUSNUM_LIST0__ID2__SHIFT                                                                          0x10
#define RCC_BUSNUM_LIST0__ID3__SHIFT                                                                          0x18
//RCC_BUSNUM_LIST1
#define RCC_BUSNUM_LIST1__ID4__SHIFT                                                                          0x0
#define RCC_BUSNUM_LIST1__ID5__SHIFT                                                                          0x8
#define RCC_BUSNUM_LIST1__ID6__SHIFT                                                                          0x10
#define RCC_BUSNUM_LIST1__ID7__SHIFT                                                                          0x18
//RCC_BUSNUM_CNTL2
#define RCC_BUSNUM_CNTL2__AUTOUPDATE_SEL__SHIFT                                                               0x0
#define RCC_BUSNUM_CNTL2__AUTOUPDATE_EN__SHIFT                                                                0x8
#define RCC_BUSNUM_CNTL2__HDPREG_CNTL__SHIFT                                                                  0x10
#define RCC_BUSNUM_CNTL2__ERROR_MULTIPLE_ID_MATCH__SHIFT                                                      0x11
//RCC_CAPTURE_HOST_BUSNUM
#define RCC_CAPTURE_HOST_BUSNUM__CHECK_EN__SHIFT                                                              0x0
//RCC_HOST_BUSNUM
#define RCC_HOST_BUSNUM__HOST_ID__SHIFT                                                                       0x0
//RCC_PEER0_FB_OFFSET_HI
#define RCC_PEER0_FB_OFFSET_HI__PEER0_FB_OFFSET_HI__SHIFT                                                     0x0
//RCC_PEER0_FB_OFFSET_LO
#define RCC_PEER0_FB_OFFSET_LO__PEER0_FB_OFFSET_LO__SHIFT                                                     0x0
#define RCC_PEER0_FB_OFFSET_LO__PEER0_FB_EN__SHIFT                                                            0x1f
//RCC_PEER1_FB_OFFSET_HI
#define RCC_PEER1_FB_OFFSET_HI__PEER1_FB_OFFSET_HI__SHIFT                                                     0x0
//RCC_PEER1_FB_OFFSET_LO
#define RCC_PEER1_FB_OFFSET_LO__PEER1_FB_OFFSET_LO__SHIFT                                                     0x0
#define RCC_PEER1_FB_OFFSET_LO__PEER1_FB_EN__SHIFT                                                            0x1f
//RCC_PEER2_FB_OFFSET_HI
#define RCC_PEER2_FB_OFFSET_HI__PEER2_FB_OFFSET_HI__SHIFT                                                     0x0
//RCC_PEER2_FB_OFFSET_LO
#define RCC_PEER2_FB_OFFSET_LO__PEER2_FB_OFFSET_LO__SHIFT                                                     0x0
#define RCC_PEER2_FB_OFFSET_LO__PEER2_FB_EN__SHIFT                                                            0x1f
//RCC_PEER3_FB_OFFSET_HI
#define RCC_PEER3_FB_OFFSET_HI__PEER3_FB_OFFSET_HI__SHIFT                                                     0x0
//RCC_PEER3_FB_OFFSET_LO
#define RCC_PEER3_FB_OFFSET_LO__PEER3_FB_OFFSET_LO__SHIFT                                                     0x0
#define RCC_PEER3_FB_OFFSET_LO__PEER3_FB_EN__SHIFT                                                            0x1f
//RCC_DEVFUNCNUM_LIST0
#define RCC_DEVFUNCNUM_LIST0__DEVFUNC_ID0__SHIFT                                                              0x0
#define RCC_DEVFUNCNUM_LIST0__DEVFUNC_ID1__SHIFT                                                              0x8
#define RCC_DEVFUNCNUM_LIST0__DEVFUNC_ID2__SHIFT                                                              0x10
#define RCC_DEVFUNCNUM_LIST0__DEVFUNC_ID3__SHIFT                                                              0x18
//RCC_DEVFUNCNUM_LIST1
#define RCC_DEVFUNCNUM_LIST1__DEVFUNC_ID4__SHIFT                                                              0x0
#define RCC_DEVFUNCNUM_LIST1__DEVFUNC_ID5__SHIFT                                                              0x8
#define RCC_DEVFUNCNUM_LIST1__DEVFUNC_ID6__SHIFT                                                              0x10
#define RCC_DEVFUNCNUM_LIST1__DEVFUNC_ID7__SHIFT                                                              0x18
//RCC_DEV0_LINK_CNTL
#define RCC_DEV0_LINK_CNTL__LINK_DOWN_EXIT__SHIFT                                                             0x0
#define RCC_DEV0_LINK_CNTL__LINK_DOWN_ENTRY__SHIFT                                                            0x8
//RCC_CMN_LINK_CNTL
#define RCC_CMN_LINK_CNTL__BLOCK_PME_ON_L0S_DIS__SHIFT                                                        0x0
#define RCC_CMN_LINK_CNTL__BLOCK_PME_ON_L1_DIS__SHIFT                                                         0x1
#define RCC_CMN_LINK_CNTL__BLOCK_PME_ON_LDN_DIS__SHIFT                                                        0x2
#define RCC_CMN_LINK_CNTL__PM_L1_IDLE_CHECK_DMA_EN__SHIFT                                                     0x3
//RCC_EP_REQUESTERID_RESTORE
#define RCC_EP_REQUESTERID_RESTORE__EP_REQID_BUS__SHIFT                                                       0x0
#define RCC_EP_REQUESTERID_RESTORE__EP_REQID_DEV__SHIFT                                                       0x8
//RCC_LTR_LSWITCH_CNTL
#define RCC_LTR_LSWITCH_CNTL__LSWITCH_LATENCY_VALUE__SHIFT                                                    0x0
//RCC_MH_ARB_CNTL
#define RCC_MH_ARB_CNTL__MH_ARB_MODE__SHIFT                                                                   0x0
#define RCC_MH_ARB_CNTL__MH_ARB_FIX_PRIORITY__SHIFT                                                           0x1


// addressBlock: rcc_pf_0_BIFDEC2
//GFXMSIX_VECT0_ADDR_LO
#define GFXMSIX_VECT0_ADDR_LO__MSG_ADDR_LO__SHIFT                                                             0x2
//GFXMSIX_VECT0_ADDR_HI
#define GFXMSIX_VECT0_ADDR_HI__MSG_ADDR_HI__SHIFT                                                             0x0
//GFXMSIX_VECT0_MSG_DATA
#define GFXMSIX_VECT0_MSG_DATA__MSG_DATA__SHIFT                                                               0x0
//GFXMSIX_VECT0_CONTROL
#define GFXMSIX_VECT0_CONTROL__MASK_BIT__SHIFT                                                                0x0
//GFXMSIX_VECT1_ADDR_LO
#define GFXMSIX_VECT1_ADDR_LO__MSG_ADDR_LO__SHIFT                                                             0x2
//GFXMSIX_VECT1_ADDR_HI
#define GFXMSIX_VECT1_ADDR_HI__MSG_ADDR_HI__SHIFT                                                             0x0
//GFXMSIX_VECT1_MSG_DATA
#define GFXMSIX_VECT1_MSG_DATA__MSG_DATA__SHIFT                                                               0x0
//GFXMSIX_VECT1_CONTROL
#define GFXMSIX_VECT1_CONTROL__MASK_BIT__SHIFT                                                                0x0
//GFXMSIX_VECT2_ADDR_LO
#define GFXMSIX_VECT2_ADDR_LO__MSG_ADDR_LO__SHIFT                                                             0x2
//GFXMSIX_VECT2_ADDR_HI
#define GFXMSIX_VECT2_ADDR_HI__MSG_ADDR_HI__SHIFT                                                             0x0
//GFXMSIX_VECT2_MSG_DATA
#define GFXMSIX_VECT2_MSG_DATA__MSG_DATA__SHIFT                                                               0x0
//GFXMSIX_VECT2_CONTROL
#define GFXMSIX_VECT2_CONTROL__MASK_BIT__SHIFT                                                                0x0
//GFXMSIX_PBA
#define GFXMSIX_PBA__MSIX_PENDING_BITS_0__SHIFT                                                               0x0
#define GFXMSIX_PBA__MSIX_PENDING_BITS_1__SHIFT                                                               0x1
#define GFXMSIX_PBA__MSIX_PENDING_BITS_2__SHIFT                                                               0x2


// addressBlock: rcc_strap_BIFDEC1
//RCC_DEV0_PORT_STRAP0
#define RCC_DEV0_PORT_STRAP0__STRAP_ARI_EN_DN_DEV0__SHIFT                                                     0x1
#define RCC_DEV0_PORT_STRAP0__STRAP_ACS_EN_DN_DEV0__SHIFT                                                     0x2
#define RCC_DEV0_PORT_STRAP0__STRAP_AER_EN_DN_DEV0__SHIFT                                                     0x3
#define RCC_DEV0_PORT_STRAP0__STRAP_CPL_ABORT_ERR_EN_DN_DEV0__SHIFT                                           0x4
#define RCC_DEV0_PORT_STRAP0__STRAP_DEVICE_ID_DN_DEV0__SHIFT                                                  0x5
#define RCC_DEV0_PORT_STRAP0__STRAP_INTERRUPT_PIN_DN_DEV0__SHIFT                                              0x15
#define RCC_DEV0_PORT_STRAP0__STRAP_IGNORE_E2E_PREFIX_UR_DN_DEV0__SHIFT                                       0x18
#define RCC_DEV0_PORT_STRAP0__STRAP_MAX_PAYLOAD_SUPPORT_DN_DEV0__SHIFT                                        0x19
#define RCC_DEV0_PORT_STRAP0__STRAP_MAX_LINK_WIDTH_SUPPORT_DEV0__SHIFT                                        0x1c
#define RCC_DEV0_PORT_STRAP0__STRAP_EPF0_DUMMY_EN_DEV0__SHIFT                                                 0x1f
//RCC_DEV0_PORT_STRAP1
#define RCC_DEV0_PORT_STRAP1__STRAP_SUBSYS_ID_DN_DEV0__SHIFT                                                  0x0
#define RCC_DEV0_PORT_STRAP1__STRAP_SUBSYS_VEN_ID_DN_DEV0__SHIFT                                              0x10
//RCC_DEV0_PORT_STRAP2
#define RCC_DEV0_PORT_STRAP2__STRAP_DE_EMPHASIS_SEL_DN_DEV0__SHIFT                                            0x0
#define RCC_DEV0_PORT_STRAP2__STRAP_DSN_EN_DN_DEV0__SHIFT                                                     0x1
#define RCC_DEV0_PORT_STRAP2__STRAP_E2E_PREFIX_EN_DEV0__SHIFT                                                 0x2
#define RCC_DEV0_PORT_STRAP2__STRAP_ECN1P1_EN_DEV0__SHIFT                                                     0x3
#define RCC_DEV0_PORT_STRAP2__STRAP_ECRC_CHECK_EN_DEV0__SHIFT                                                 0x4
#define RCC_DEV0_PORT_STRAP2__STRAP_ECRC_GEN_EN_DEV0__SHIFT                                                   0x5
#define RCC_DEV0_PORT_STRAP2__STRAP_ERR_REPORTING_DIS_DEV0__SHIFT                                             0x6
#define RCC_DEV0_PORT_STRAP2__STRAP_EXTENDED_FMT_SUPPORTED_DEV0__SHIFT                                        0x7
#define RCC_DEV0_PORT_STRAP2__STRAP_EXTENDED_TAG_ECN_EN_DEV0__SHIFT                                           0x8
#define RCC_DEV0_PORT_STRAP2__STRAP_EXT_VC_COUNT_DN_DEV0__SHIFT                                               0x9
#define RCC_DEV0_PORT_STRAP2__STRAP_FIRST_RCVD_ERR_LOG_DN_DEV0__SHIFT                                         0xc
#define RCC_DEV0_PORT_STRAP2__STRAP_POISONED_ADVISORY_NONFATAL_DN_DEV0__SHIFT                                 0xd
#define RCC_DEV0_PORT_STRAP2__STRAP_GEN2_COMPLIANCE_DEV0__SHIFT                                               0xe
#define RCC_DEV0_PORT_STRAP2__STRAP_GEN2_EN_DEV0__SHIFT                                                       0xf
#define RCC_DEV0_PORT_STRAP2__STRAP_GEN3_COMPLIANCE_DEV0__SHIFT                                               0x10
#define RCC_DEV0_PORT_STRAP2__STRAP_TARGET_LINK_SPEED_DEV0__SHIFT                                             0x11
#define RCC_DEV0_PORT_STRAP2__STRAP_INTERNAL_ERR_EN_DEV0__SHIFT                                               0x13
#define RCC_DEV0_PORT_STRAP2__STRAP_L0S_ACCEPTABLE_LATENCY_DEV0__SHIFT                                        0x14
#define RCC_DEV0_PORT_STRAP2__STRAP_L0S_EXIT_LATENCY_DEV0__SHIFT                                              0x17
#define RCC_DEV0_PORT_STRAP2__STRAP_L1_ACCEPTABLE_LATENCY_DEV0__SHIFT                                         0x1a
#define RCC_DEV0_PORT_STRAP2__STRAP_L1_EXIT_LATENCY_DEV0__SHIFT                                               0x1d
//RCC_DEV0_PORT_STRAP3
#define RCC_DEV0_PORT_STRAP3__STRAP_LINK_BW_NOTIFICATION_CAP_DN_EN_DEV0__SHIFT                                0x0
#define RCC_DEV0_PORT_STRAP3__STRAP_LTR_EN_DEV0__SHIFT                                                        0x1
#define RCC_DEV0_PORT_STRAP3__STRAP_LTR_EN_DN_DEV0__SHIFT                                                     0x2
#define RCC_DEV0_PORT_STRAP3__STRAP_MAX_PAYLOAD_SUPPORT_DEV0__SHIFT                                           0x3
#define RCC_DEV0_PORT_STRAP3__STRAP_MSI_EN_DN_DEV0__SHIFT                                                     0x6
#define RCC_DEV0_PORT_STRAP3__STRAP_MSTCPL_TIMEOUT_EN_DEV0__SHIFT                                             0x7
#define RCC_DEV0_PORT_STRAP3__STRAP_NO_SOFT_RESET_DN_DEV0__SHIFT                                              0x8
#define RCC_DEV0_PORT_STRAP3__STRAP_OBFF_SUPPORTED_DEV0__SHIFT                                                0x9
#define RCC_DEV0_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_DOWNSTREAM_PORT_RX_PRESET_HINT_DEV0__SHIFT    0xb
#define RCC_DEV0_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_DOWNSTREAM_PORT_TX_PRESET_DEV0__SHIFT         0xe
#define RCC_DEV0_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_UPSTREAM_PORT_RX_PRESET_HINT_DEV0__SHIFT      0x12
#define RCC_DEV0_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_UPSTREAM_PORT_TX_PRESET_DEV0__SHIFT           0x15
#define RCC_DEV0_PORT_STRAP3__STRAP_PM_SUPPORT_DEV0__SHIFT                                                    0x19
#define RCC_DEV0_PORT_STRAP3__STRAP_PM_SUPPORT_DN_DEV0__SHIFT                                                 0x1b
#define RCC_DEV0_PORT_STRAP3__STRAP_ATOMIC_EN_DN_DEV0__SHIFT                                                  0x1d
#define RCC_DEV0_PORT_STRAP3__STRAP_VENDOR_ID_BIT_DN_DEV0__SHIFT                                              0x1e
#define RCC_DEV0_PORT_STRAP3__STRAP_PMC_DSI_DN_DEV0__SHIFT                                                    0x1f
//RCC_DEV0_PORT_STRAP4
#define RCC_DEV0_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_0_DEV0__SHIFT                                         0x0
#define RCC_DEV0_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_1_DEV0__SHIFT                                         0x8
#define RCC_DEV0_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_2_DEV0__SHIFT                                         0x10
#define RCC_DEV0_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_3_DEV0__SHIFT                                         0x18
//RCC_DEV0_PORT_STRAP5
#define RCC_DEV0_PORT_STRAP5__STRAP_PWR_BUDGET_DATA_8T0_4_DEV0__SHIFT                                         0x0
#define RCC_DEV0_PORT_STRAP5__STRAP_PWR_BUDGET_DATA_8T0_5_DEV0__SHIFT                                         0x8
#define RCC_DEV0_PORT_STRAP5__STRAP_PWR_BUDGET_SYSTEM_ALLOCATED_DEV0__SHIFT                                   0x10
#define RCC_DEV0_PORT_STRAP5__STRAP_ATOMIC_64BIT_EN_DN_DEV0__SHIFT                                            0x11
#define RCC_DEV0_PORT_STRAP5__STRAP_ATOMIC_ROUTING_EN_DEV0__SHIFT                                             0x12
#define RCC_DEV0_PORT_STRAP5__STRAP_VC_EN_DN_DEV0__SHIFT                                                      0x13
#define RCC_DEV0_PORT_STRAP5__STRAP_TwoVC_EN_DEV0__SHIFT                                                      0x14
#define RCC_DEV0_PORT_STRAP5__STRAP_TwoVC_EN_DN_DEV0__SHIFT                                                   0x15
#define RCC_DEV0_PORT_STRAP5__STRAP_ACS_SOURCE_VALIDATION_DN_DEV0__SHIFT                                      0x17
#define RCC_DEV0_PORT_STRAP5__STRAP_ACS_TRANSLATION_BLOCKING_DN_DEV0__SHIFT                                   0x18
#define RCC_DEV0_PORT_STRAP5__STRAP_ACS_P2P_REQUEST_REDIRECT_DN_DEV0__SHIFT                                   0x19
#define RCC_DEV0_PORT_STRAP5__STRAP_ACS_P2P_COMPLETION_REDIRECT_DN_DEV0__SHIFT                                0x1a
#define RCC_DEV0_PORT_STRAP5__STRAP_ACS_UPSTREAM_FORWARDING_DN_DEV0__SHIFT                                    0x1b
#define RCC_DEV0_PORT_STRAP5__STRAP_ACS_P2P_EGRESS_CONTROL_DN_DEV0__SHIFT                                     0x1c
#define RCC_DEV0_PORT_STRAP5__STRAP_ACS_DIRECT_TRANSLATED_P2P_DN_DEV0__SHIFT                                  0x1d
#define RCC_DEV0_PORT_STRAP5__STRAP_MSI_MAP_EN_DEV0__SHIFT                                                    0x1e
#define RCC_DEV0_PORT_STRAP5__STRAP_SSID_EN_DEV0__SHIFT                                                       0x1f
//RCC_DEV0_PORT_STRAP6
#define RCC_DEV0_PORT_STRAP6__STRAP_CFG_CRS_EN_DEV0__SHIFT                                                    0x0
#define RCC_DEV0_PORT_STRAP6__STRAP_SMN_ERR_STATUS_MASK_EN_DNS_DEV0__SHIFT                                    0x1
//RCC_DEV0_PORT_STRAP7
#define RCC_DEV0_PORT_STRAP7__STRAP_PORT_NUMBER_DEV0__SHIFT                                                   0x0
#define RCC_DEV0_PORT_STRAP7__STRAP_MAJOR_REV_ID_DN_DEV0__SHIFT                                               0x8
#define RCC_DEV0_PORT_STRAP7__STRAP_MINOR_REV_ID_DN_DEV0__SHIFT                                               0xc
#define RCC_DEV0_PORT_STRAP7__STRAP_RP_BUSNUM_DEV0__SHIFT                                                     0x10
#define RCC_DEV0_PORT_STRAP7__STRAP_DN_DEVNUM_DEV0__SHIFT                                                     0x18
#define RCC_DEV0_PORT_STRAP7__STRAP_DN_FUNCID_DEV0__SHIFT                                                     0x1d
//RCC_DEV0_EPF0_STRAP0
#define RCC_DEV0_EPF0_STRAP0__STRAP_DEVICE_ID_DEV0_F0__SHIFT                                                  0x0
#define RCC_DEV0_EPF0_STRAP0__STRAP_MAJOR_REV_ID_DEV0_F0__SHIFT                                               0x10
#define RCC_DEV0_EPF0_STRAP0__STRAP_MINOR_REV_ID_DEV0_F0__SHIFT                                               0x14
#define RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0__SHIFT                                                 0x18
#define RCC_DEV0_EPF0_STRAP0__STRAP_FUNC_EN_DEV0_F0__SHIFT                                                    0x1c
#define RCC_DEV0_EPF0_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV0_F0__SHIFT                                      0x1d
#define RCC_DEV0_EPF0_STRAP0__STRAP_D1_SUPPORT_DEV0_F0__SHIFT                                                 0x1e
#define RCC_DEV0_EPF0_STRAP0__STRAP_D2_SUPPORT_DEV0_F0__SHIFT                                                 0x1f
//RCC_DEV0_EPF0_STRAP1
#define RCC_DEV0_EPF0_STRAP1__STRAP_SRIOV_VF_DEVICE_ID_DEV0_F0__SHIFT                                         0x0
#define RCC_DEV0_EPF0_STRAP1__STRAP_SRIOV_SUPPORTED_PAGE_SIZE_DEV0_F0__SHIFT                                  0x10
//RCC_DEV0_EPF0_STRAP13
#define RCC_DEV0_EPF0_STRAP13__STRAP_CLASS_CODE_PIF_DEV0_F0__SHIFT                                            0x0
#define RCC_DEV0_EPF0_STRAP13__STRAP_CLASS_CODE_SUB_DEV0_F0__SHIFT                                            0x8
#define RCC_DEV0_EPF0_STRAP13__STRAP_CLASS_CODE_BASE_DEV0_F0__SHIFT                                           0x10
//RCC_DEV0_EPF0_STRAP2
#define RCC_DEV0_EPF0_STRAP2__STRAP_SRIOV_EN_DEV0_F0__SHIFT                                                   0x0
#define RCC_DEV0_EPF0_STRAP2__STRAP_SRIOV_TOTAL_VFS_DEV0_F0__SHIFT                                            0x1
#define RCC_DEV0_EPF0_STRAP2__STRAP_64BAR_DIS_DEV0_F0__SHIFT                                                  0x6
#define RCC_DEV0_EPF0_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F0__SHIFT                                              0x7
#define RCC_DEV0_EPF0_STRAP2__STRAP_RESIZE_BAR_EN_DEV0_F0__SHIFT                                              0x8
#define RCC_DEV0_EPF0_STRAP2__STRAP_MAX_PASID_WIDTH_DEV0_F0__SHIFT                                            0x9
#define RCC_DEV0_EPF0_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV0_F0__SHIFT                                     0xe
#define RCC_DEV0_EPF0_STRAP2__STRAP_ARI_EN_DEV0_F0__SHIFT                                                     0xf
#define RCC_DEV0_EPF0_STRAP2__STRAP_AER_EN_DEV0_F0__SHIFT                                                     0x10
#define RCC_DEV0_EPF0_STRAP2__STRAP_ACS_EN_DEV0_F0__SHIFT                                                     0x11
#define RCC_DEV0_EPF0_STRAP2__STRAP_ATS_EN_DEV0_F0__SHIFT                                                     0x12
#define RCC_DEV0_EPF0_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV0_F0__SHIFT                                           0x14
#define RCC_DEV0_EPF0_STRAP2__STRAP_DPA_EN_DEV0_F0__SHIFT                                                     0x15
#define RCC_DEV0_EPF0_STRAP2__STRAP_DSN_EN_DEV0_F0__SHIFT                                                     0x16
#define RCC_DEV0_EPF0_STRAP2__STRAP_VC_EN_DEV0_F0__SHIFT                                                      0x17
#define RCC_DEV0_EPF0_STRAP2__STRAP_MSI_MULTI_CAP_DEV0_F0__SHIFT                                              0x18
#define RCC_DEV0_EPF0_STRAP2__STRAP_PAGE_REQ_EN_DEV0_F0__SHIFT                                                0x1b
#define RCC_DEV0_EPF0_STRAP2__STRAP_PASID_EN_DEV0_F0__SHIFT                                                   0x1c
#define RCC_DEV0_EPF0_STRAP2__STRAP_PASID_EXE_PERMISSION_SUPPORTED_DEV0_F0__SHIFT                             0x1d
#define RCC_DEV0_EPF0_STRAP2__STRAP_PASID_GLOBAL_INVALIDATE_SUPPORTED_DEV0_F0__SHIFT                          0x1e
#define RCC_DEV0_EPF0_STRAP2__STRAP_PASID_PRIV_MODE_SUPPORTED_DEV0_F0__SHIFT                                  0x1f
//RCC_DEV0_EPF0_STRAP3
#define RCC_DEV0_EPF0_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV0_F0__SHIFT                                 0x0
#define RCC_DEV0_EPF0_STRAP3__STRAP_PWR_EN_DEV0_F0__SHIFT                                                     0x1
#define RCC_DEV0_EPF0_STRAP3__STRAP_SUBSYS_ID_DEV0_F0__SHIFT                                                  0x2
#define RCC_DEV0_EPF0_STRAP3__STRAP_MSI_EN_DEV0_F0__SHIFT                                                     0x12
#define RCC_DEV0_EPF0_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV0_F0__SHIFT                                         0x13
#define RCC_DEV0_EPF0_STRAP3__STRAP_MSIX_EN_DEV0_F0__SHIFT                                                    0x14
#define RCC_DEV0_EPF0_STRAP3__STRAP_MSIX_TABLE_BIR_DEV0_F0__SHIFT                                             0x15
#define RCC_DEV0_EPF0_STRAP3__STRAP_PMC_DSI_DEV0_F0__SHIFT                                                    0x18
#define RCC_DEV0_EPF0_STRAP3__STRAP_VENDOR_ID_BIT_DEV0_F0__SHIFT                                              0x19
#define RCC_DEV0_EPF0_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV0_F0__SHIFT                                   0x1a
#define RCC_DEV0_EPF0_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV0_F0__SHIFT                                  0x1b
//RCC_DEV0_EPF0_STRAP4
#define RCC_DEV0_EPF0_STRAP4__STRAP_MSIX_TABLE_OFFSET_DEV0_F0__SHIFT                                          0x0
#define RCC_DEV0_EPF0_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV0_F0__SHIFT                                            0x14
#define RCC_DEV0_EPF0_STRAP4__STRAP_ATOMIC_EN_DEV0_F0__SHIFT                                                  0x15
#define RCC_DEV0_EPF0_STRAP4__STRAP_FLR_EN_DEV0_F0__SHIFT                                                     0x16
#define RCC_DEV0_EPF0_STRAP4__STRAP_PME_SUPPORT_DEV0_F0__SHIFT                                                0x17
#define RCC_DEV0_EPF0_STRAP4__STRAP_INTERRUPT_PIN_DEV0_F0__SHIFT                                              0x1c
#define RCC_DEV0_EPF0_STRAP4__STRAP_AUXPWR_SUPPORT_DEV0_F0__SHIFT                                             0x1f
//RCC_DEV0_EPF0_STRAP5
#define RCC_DEV0_EPF0_STRAP5__STRAP_SUBSYS_VEN_ID_DEV0_F0__SHIFT                                              0x0
//RCC_DEV0_EPF0_STRAP8
#define RCC_DEV0_EPF0_STRAP8__STRAP_BAR_COMPLIANCE_EN_DEV0_F0__SHIFT                                          0x0
#define RCC_DEV0_EPF0_STRAP8__STRAP_DOORBELL_APER_SIZE_DEV0_F0__SHIFT                                         0x1
#define RCC_DEV0_EPF0_STRAP8__STRAP_DOORBELL_BAR_DIS_DEV0_F0__SHIFT                                           0x3
#define RCC_DEV0_EPF0_STRAP8__STRAP_FB_ALWAYS_ON_DEV0_F0__SHIFT                                               0x4
#define RCC_DEV0_EPF0_STRAP8__STRAP_FB_CPL_TYPE_SEL_DEV0_F0__SHIFT                                            0x5
#define RCC_DEV0_EPF0_STRAP8__STRAP_IO_BAR_DIS_DEV0_F0__SHIFT                                                 0x7
#define RCC_DEV0_EPF0_STRAP8__STRAP_LFB_ERRMSG_EN_DEV0_F0__SHIFT                                              0x8
#define RCC_DEV0_EPF0_STRAP8__STRAP_MEM_AP_SIZE_DEV0_F0__SHIFT                                                0x9
#define RCC_DEV0_EPF0_STRAP8__STRAP_REG_AP_SIZE_DEV0_F0__SHIFT                                                0xc
#define RCC_DEV0_EPF0_STRAP8__STRAP_ROM_AP_SIZE_DEV0_F0__SHIFT                                                0xe
#define RCC_DEV0_EPF0_STRAP8__STRAP_VF_DOORBELL_APER_SIZE_DEV0_F0__SHIFT                                      0x10
#define RCC_DEV0_EPF0_STRAP8__STRAP_VF_MEM_AP_SIZE_DEV0_F0__SHIFT                                             0x13
#define RCC_DEV0_EPF0_STRAP8__STRAP_VF_REG_AP_SIZE_DEV0_F0__SHIFT                                             0x16
#define RCC_DEV0_EPF0_STRAP8__STRAP_VGA_DIS_DEV0_F0__SHIFT                                                    0x18
#define RCC_DEV0_EPF0_STRAP8__STRAP_NBIF_ROM_BAR_DIS_CHICKEN_DEV0_F0__SHIFT                                   0x19
#define RCC_DEV0_EPF0_STRAP8__STRAP_VF_REG_PROT_DIS_DEV0_F0__SHIFT                                            0x1a
#define RCC_DEV0_EPF0_STRAP8__STRAP_VF_MSI_MULTI_CAP_DEV0_F0__SHIFT                                           0x1b
#define RCC_DEV0_EPF0_STRAP8__STRAP_SRIOV_VF_MAPPING_MODE_DEV0_F0__SHIFT                                      0x1e
//RCC_DEV0_EPF0_STRAP9
//RCC_DEV0_EPF1_STRAP0
#define RCC_DEV0_EPF1_STRAP0__STRAP_DEVICE_ID_DEV0_F1__SHIFT                                                  0x0
#define RCC_DEV0_EPF1_STRAP0__STRAP_MAJOR_REV_ID_DEV0_F1__SHIFT                                               0x10
#define RCC_DEV0_EPF1_STRAP0__STRAP_MINOR_REV_ID_DEV0_F1__SHIFT                                               0x14
#define RCC_DEV0_EPF1_STRAP0__STRAP_FUNC_EN_DEV0_F1__SHIFT                                                    0x1c
#define RCC_DEV0_EPF1_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV0_F1__SHIFT                                      0x1d
#define RCC_DEV0_EPF1_STRAP0__STRAP_D1_SUPPORT_DEV0_F1__SHIFT                                                 0x1e
#define RCC_DEV0_EPF1_STRAP0__STRAP_D2_SUPPORT_DEV0_F1__SHIFT                                                 0x1f
//RCC_DEV0_EPF1_STRAP10
#define RCC_DEV0_EPF1_STRAP10__STRAP_APER1_RESIZE_EN_DEV0_F1__SHIFT                                           0x0
#define RCC_DEV0_EPF1_STRAP10__STRAP_APER1_RESIZE_SUPPORT_DEV0_F1__SHIFT                                      0x1
//RCC_DEV0_EPF1_STRAP11
#define RCC_DEV0_EPF1_STRAP11__STRAP_APER2_RESIZE_EN_DEV0_F1__SHIFT                                           0x0
#define RCC_DEV0_EPF1_STRAP11__STRAP_APER2_RESIZE_SUPPORT_DEV0_F1__SHIFT                                      0x1
//RCC_DEV0_EPF1_STRAP12
#define RCC_DEV0_EPF1_STRAP12__STRAP_APER3_RESIZE_EN_DEV0_F1__SHIFT                                           0x0
#define RCC_DEV0_EPF1_STRAP12__STRAP_APER3_RESIZE_SUPPORT_DEV0_F1__SHIFT                                      0x1
//RCC_DEV0_EPF1_STRAP13
#define RCC_DEV0_EPF1_STRAP13__STRAP_CLASS_CODE_PIF_DEV0_F1__SHIFT                                            0x0
#define RCC_DEV0_EPF1_STRAP13__STRAP_CLASS_CODE_SUB_DEV0_F1__SHIFT                                            0x8
#define RCC_DEV0_EPF1_STRAP13__STRAP_CLASS_CODE_BASE_DEV0_F1__SHIFT                                           0x10
//RCC_DEV0_EPF1_STRAP2
#define RCC_DEV0_EPF1_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F1__SHIFT                                              0x7
#define RCC_DEV0_EPF1_STRAP2__STRAP_RESIZE_BAR_EN_DEV0_F1__SHIFT                                              0x8
#define RCC_DEV0_EPF1_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV0_F1__SHIFT                                     0xe
#define RCC_DEV0_EPF1_STRAP2__STRAP_AER_EN_DEV0_F1__SHIFT                                                     0x10
#define RCC_DEV0_EPF1_STRAP2__STRAP_ACS_EN_DEV0_F1__SHIFT                                                     0x11
#define RCC_DEV0_EPF1_STRAP2__STRAP_ATS_EN_DEV0_F1__SHIFT                                                     0x12
#define RCC_DEV0_EPF1_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV0_F1__SHIFT                                           0x14
#define RCC_DEV0_EPF1_STRAP2__STRAP_DPA_EN_DEV0_F1__SHIFT                                                     0x15
#define RCC_DEV0_EPF1_STRAP2__STRAP_DSN_EN_DEV0_F1__SHIFT                                                     0x16
#define RCC_DEV0_EPF1_STRAP2__STRAP_VC_EN_DEV0_F1__SHIFT                                                      0x17
#define RCC_DEV0_EPF1_STRAP2__STRAP_MSI_MULTI_CAP_DEV0_F1__SHIFT                                              0x18
//RCC_DEV0_EPF1_STRAP3
#define RCC_DEV0_EPF1_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV0_F1__SHIFT                                 0x0
#define RCC_DEV0_EPF1_STRAP3__STRAP_PWR_EN_DEV0_F1__SHIFT                                                     0x1
#define RCC_DEV0_EPF1_STRAP3__STRAP_SUBSYS_ID_DEV0_F1__SHIFT                                                  0x2
#define RCC_DEV0_EPF1_STRAP3__STRAP_MSI_EN_DEV0_F1__SHIFT                                                     0x12
#define RCC_DEV0_EPF1_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV0_F1__SHIFT                                         0x13
#define RCC_DEV0_EPF1_STRAP3__STRAP_MSIX_EN_DEV0_F1__SHIFT                                                    0x14
#define RCC_DEV0_EPF1_STRAP3__STRAP_PMC_DSI_DEV0_F1__SHIFT                                                    0x18
#define RCC_DEV0_EPF1_STRAP3__STRAP_VENDOR_ID_BIT_DEV0_F1__SHIFT                                              0x19
#define RCC_DEV0_EPF1_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV0_F1__SHIFT                                   0x1a
#define RCC_DEV0_EPF1_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV0_F1__SHIFT                                  0x1b
//RCC_DEV0_EPF1_STRAP4
#define RCC_DEV0_EPF1_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV0_F1__SHIFT                                            0x14
#define RCC_DEV0_EPF1_STRAP4__STRAP_ATOMIC_EN_DEV0_F1__SHIFT                                                  0x15
#define RCC_DEV0_EPF1_STRAP4__STRAP_FLR_EN_DEV0_F1__SHIFT                                                     0x16
#define RCC_DEV0_EPF1_STRAP4__STRAP_PME_SUPPORT_DEV0_F1__SHIFT                                                0x17
#define RCC_DEV0_EPF1_STRAP4__STRAP_INTERRUPT_PIN_DEV0_F1__SHIFT                                              0x1c
#define RCC_DEV0_EPF1_STRAP4__STRAP_AUXPWR_SUPPORT_DEV0_F1__SHIFT                                             0x1f
//RCC_DEV0_EPF1_STRAP5
#define RCC_DEV0_EPF1_STRAP5__STRAP_SUBSYS_VEN_ID_DEV0_F1__SHIFT                                              0x0
//RCC_DEV0_EPF1_STRAP6
#define RCC_DEV0_EPF1_STRAP6__STRAP_APER0_EN_DEV0_F1__SHIFT                                                   0x0
#define RCC_DEV0_EPF1_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV0_F1__SHIFT                                      0x1
#define RCC_DEV0_EPF1_STRAP6__STRAP_APER0_64BAR_EN_DEV0_F1__SHIFT                                             0x2
#define RCC_DEV0_EPF1_STRAP6__STRAP_APER0_AP_SIZE_DEV0_F1__SHIFT                                              0x4
#define RCC_DEV0_EPF1_STRAP6__STRAP_APER1_EN_DEV0_F1__SHIFT                                                   0x8
#define RCC_DEV0_EPF1_STRAP6__STRAP_APER1_PREFETCHABLE_EN_DEV0_F1__SHIFT                                      0x9
#define RCC_DEV0_EPF1_STRAP6__STRAP_APER2_EN_DEV0_F1__SHIFT                                                   0x10
#define RCC_DEV0_EPF1_STRAP6__STRAP_APER2_PREFETCHABLE_EN_DEV0_F1__SHIFT                                      0x11
#define RCC_DEV0_EPF1_STRAP6__STRAP_APER3_EN_DEV0_F1__SHIFT                                                   0x18
#define RCC_DEV0_EPF1_STRAP6__STRAP_APER3_PREFETCHABLE_EN_DEV0_F1__SHIFT                                      0x19
//RCC_DEV0_EPF1_STRAP7
#define RCC_DEV0_EPF1_STRAP7__STRAP_ROM_APER_EN_DEV0_F1__SHIFT                                                0x0
#define RCC_DEV0_EPF1_STRAP7__STRAP_ROM_APER_SIZE_DEV0_F1__SHIFT                                              0x1


// addressBlock: bif_bx_pf_BIFPFVFDEC1
//BIF_BME_STATUS
#define BIF_BME_STATUS__DMA_ON_BME_LOW__SHIFT                                                                 0x0
#define BIF_BME_STATUS__CLEAR_DMA_ON_BME_LOW__SHIFT                                                           0x10
//BIF_ATOMIC_ERR_LOG
#define BIF_ATOMIC_ERR_LOG__UR_ATOMIC_OPCODE__SHIFT                                                           0x0
#define BIF_ATOMIC_ERR_LOG__UR_ATOMIC_REQEN_LOW__SHIFT                                                        0x1
#define BIF_ATOMIC_ERR_LOG__CLEAR_UR_ATOMIC_OPCODE__SHIFT                                                     0x10
#define BIF_ATOMIC_ERR_LOG__CLEAR_UR_ATOMIC_REQEN_LOW__SHIFT                                                  0x11
//DOORBELL_SELFRING_GPA_APER_BASE_HIGH
#define DOORBELL_SELFRING_GPA_APER_BASE_HIGH__DOORBELL_SELFRING_GPA_APER_BASE_HIGH__SHIFT                     0x0
//DOORBELL_SELFRING_GPA_APER_BASE_LOW
#define DOORBELL_SELFRING_GPA_APER_BASE_LOW__DOORBELL_SELFRING_GPA_APER_BASE_LOW__SHIFT                       0x0
//DOORBELL_SELFRING_GPA_APER_CNTL
#define DOORBELL_SELFRING_GPA_APER_CNTL__DOORBELL_SELFRING_GPA_APER_EN__SHIFT                                 0x0
#define DOORBELL_SELFRING_GPA_APER_CNTL__DOORBELL_SELFRING_GPA_APER_SIZE__SHIFT                               0x8
//HDP_REG_COHERENCY_FLUSH_CNTL
#define HDP_REG_COHERENCY_FLUSH_CNTL__HDP_REG_FLUSH_ADDR__SHIFT                                               0x0
//HDP_MEM_COHERENCY_FLUSH_CNTL
#define HDP_MEM_COHERENCY_FLUSH_CNTL__HDP_MEM_FLUSH_ADDR__SHIFT                                               0x0
//GPU_HDP_FLUSH_REQ
#define GPU_HDP_FLUSH_REQ__CP0__SHIFT                                                                         0x0
#define GPU_HDP_FLUSH_REQ__CP1__SHIFT                                                                         0x1
#define GPU_HDP_FLUSH_REQ__CP2__SHIFT                                                                         0x2
#define GPU_HDP_FLUSH_REQ__CP3__SHIFT                                                                         0x3
#define GPU_HDP_FLUSH_REQ__CP4__SHIFT                                                                         0x4
#define GPU_HDP_FLUSH_REQ__CP5__SHIFT                                                                         0x5
#define GPU_HDP_FLUSH_REQ__CP6__SHIFT                                                                         0x6
#define GPU_HDP_FLUSH_REQ__CP7__SHIFT                                                                         0x7
#define GPU_HDP_FLUSH_REQ__CP8__SHIFT                                                                         0x8
#define GPU_HDP_FLUSH_REQ__CP9__SHIFT                                                                         0x9
#define GPU_HDP_FLUSH_REQ__SDMA0__SHIFT                                                                       0xa
#define GPU_HDP_FLUSH_REQ__SDMA1__SHIFT                                                                       0xb
//GPU_HDP_FLUSH_DONE
#define GPU_HDP_FLUSH_DONE__CP0__SHIFT                                                                        0x0
#define GPU_HDP_FLUSH_DONE__CP1__SHIFT                                                                        0x1
#define GPU_HDP_FLUSH_DONE__CP2__SHIFT                                                                        0x2
#define GPU_HDP_FLUSH_DONE__CP3__SHIFT                                                                        0x3
#define GPU_HDP_FLUSH_DONE__CP4__SHIFT                                                                        0x4
#define GPU_HDP_FLUSH_DONE__CP5__SHIFT                                                                        0x5
#define GPU_HDP_FLUSH_DONE__CP6__SHIFT                                                                        0x6
#define GPU_HDP_FLUSH_DONE__CP7__SHIFT                                                                        0x7
#define GPU_HDP_FLUSH_DONE__CP8__SHIFT                                                                        0x8
#define GPU_HDP_FLUSH_DONE__CP9__SHIFT                                                                        0x9
#define GPU_HDP_FLUSH_DONE__SDMA0__SHIFT                                                                      0xa
#define GPU_HDP_FLUSH_DONE__SDMA1__SHIFT                                                                      0xb
//BIF_TRANS_PENDING
#define BIF_TRANS_PENDING__BIF_MST_TRANS_PENDING__SHIFT                                                       0x0
#define BIF_TRANS_PENDING__BIF_SLV_TRANS_PENDING__SHIFT                                                       0x1
//MAILBOX_MSGBUF_TRN_DW0
#define MAILBOX_MSGBUF_TRN_DW0__MSGBUF_DATA__SHIFT                                                            0x0
//MAILBOX_MSGBUF_TRN_DW1
#define MAILBOX_MSGBUF_TRN_DW1__MSGBUF_DATA__SHIFT                                                            0x0
//MAILBOX_MSGBUF_TRN_DW2
#define MAILBOX_MSGBUF_TRN_DW2__MSGBUF_DATA__SHIFT                                                            0x0
//MAILBOX_MSGBUF_TRN_DW3
#define MAILBOX_MSGBUF_TRN_DW3__MSGBUF_DATA__SHIFT                                                            0x0
//MAILBOX_MSGBUF_RCV_DW0
#define MAILBOX_MSGBUF_RCV_DW0__MSGBUF_DATA__SHIFT                                                            0x0
//MAILBOX_MSGBUF_RCV_DW1
#define MAILBOX_MSGBUF_RCV_DW1__MSGBUF_DATA__SHIFT                                                            0x0
//MAILBOX_MSGBUF_RCV_DW2
#define MAILBOX_MSGBUF_RCV_DW2__MSGBUF_DATA__SHIFT                                                            0x0
//MAILBOX_MSGBUF_RCV_DW3
#define MAILBOX_MSGBUF_RCV_DW3__MSGBUF_DATA__SHIFT                                                            0x0
//MAILBOX_CONTROL
#define MAILBOX_CONTROL__TRN_MSG_VALID__SHIFT                                                                 0x0
#define MAILBOX_CONTROL__TRN_MSG_ACK__SHIFT                                                                   0x1
#define MAILBOX_CONTROL__RCV_MSG_VALID__SHIFT                                                                 0x8
#define MAILBOX_CONTROL__RCV_MSG_ACK__SHIFT                                                                   0x9
//MAILBOX_INT_CNTL
#define MAILBOX_INT_CNTL__VALID_INT_EN__SHIFT                                                                 0x0
#define MAILBOX_INT_CNTL__ACK_INT_EN__SHIFT                                                                   0x1
//BIF_VMHV_MAILBOX
#define BIF_VMHV_MAILBOX__VMHV_MAILBOX_TRN_ACK_INTR_EN__SHIFT                                                 0x0
#define BIF_VMHV_MAILBOX__VMHV_MAILBOX_RCV_VALID_INTR_EN__SHIFT                                               0x1
#define BIF_VMHV_MAILBOX__VMHV_MAILBOX_TRN_MSG_DATA__SHIFT                                                    0x8
#define BIF_VMHV_MAILBOX__VMHV_MAILBOX_TRN_MSG_VALID__SHIFT                                                   0xf
#define BIF_VMHV_MAILBOX__VMHV_MAILBOX_RCV_MSG_DATA__SHIFT                                                    0x10
#define BIF_VMHV_MAILBOX__VMHV_MAILBOX_RCV_MSG_VALID__SHIFT                                                   0x17
#define BIF_VMHV_MAILBOX__VMHV_MAILBOX_TRN_MSG_ACK__SHIFT                                                     0x18
#define BIF_VMHV_MAILBOX__VMHV_MAILBOX_RCV_MSG_ACK__SHIFT                                                     0x19


// addressBlock: rcc_pf_0_BIFPFVFDEC1
//RCC_DOORBELL_APER_EN
#define RCC_DOORBELL_APER_EN__BIF_DOORBELL_APER_EN__SHIFT                                                     0x0
//RCC_CONFIG_MEMSIZE
#define RCC_CONFIG_MEMSIZE__CONFIG_MEMSIZE__SHIFT                                                             0x0
//RCC_CONFIG_RESERVED
#define RCC_CONFIG_RESERVED__CONFIG_RESERVED__SHIFT                                                           0x0
//RCC_IOV_FUNC_IDENTIFIER
#define RCC_IOV_FUNC_IDENTIFIER__FUNC_IDENTIFIER__SHIFT                                                       0x0
#define RCC_IOV_FUNC_IDENTIFIER__IOV_ENABLE__SHIFT                                                            0x1f


// addressBlock: syshub_mmreg_ind_syshubdec
//SYSHUB_INDEX
#define SYSHUB_INDEX__INDEX__SHIFT                                                                            0x0
//SYSHUB_DATA
#define SYSHUB_DATA__DATA__SHIFT                                                                              0x0


// addressBlock: rcc_strap_rcc_strap_internal
//RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0__STRAP_ARI_EN_DN_DEV0__SHIFT                                    0x1
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0__STRAP_ACS_EN_DN_DEV0__SHIFT                                    0x2
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0__STRAP_AER_EN_DN_DEV0__SHIFT                                    0x3
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0__STRAP_CPL_ABORT_ERR_EN_DN_DEV0__SHIFT                          0x4
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0__STRAP_DEVICE_ID_DN_DEV0__SHIFT                                 0x5
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0__STRAP_INTERRUPT_PIN_DN_DEV0__SHIFT                             0x15
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0__STRAP_IGNORE_E2E_PREFIX_UR_DN_DEV0__SHIFT                      0x18
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0__STRAP_MAX_PAYLOAD_SUPPORT_DN_DEV0__SHIFT                       0x19
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0__STRAP_MAX_LINK_WIDTH_SUPPORT_DEV0__SHIFT                       0x1c
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0__STRAP_EPF0_DUMMY_EN_DEV0__SHIFT                                0x1f
//RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP1
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP1__STRAP_SUBSYS_ID_DN_DEV0__SHIFT                                 0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP1__STRAP_SUBSYS_VEN_ID_DN_DEV0__SHIFT                             0x10
//RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_DE_EMPHASIS_SEL_DN_DEV0__SHIFT                           0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_DSN_EN_DN_DEV0__SHIFT                                    0x1
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_E2E_PREFIX_EN_DEV0__SHIFT                                0x2
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_ECN1P1_EN_DEV0__SHIFT                                    0x3
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_ECRC_CHECK_EN_DEV0__SHIFT                                0x4
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_ECRC_GEN_EN_DEV0__SHIFT                                  0x5
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_ERR_REPORTING_DIS_DEV0__SHIFT                            0x6
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_EXTENDED_FMT_SUPPORTED_DEV0__SHIFT                       0x7
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_EXTENDED_TAG_ECN_EN_DEV0__SHIFT                          0x8
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_EXT_VC_COUNT_DN_DEV0__SHIFT                              0x9
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_FIRST_RCVD_ERR_LOG_DN_DEV0__SHIFT                        0xc
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_POISONED_ADVISORY_NONFATAL_DN_DEV0__SHIFT                0xd
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_GEN2_COMPLIANCE_DEV0__SHIFT                              0xe
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_GEN2_EN_DEV0__SHIFT                                      0xf
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_GEN3_COMPLIANCE_DEV0__SHIFT                              0x10
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_TARGET_LINK_SPEED_DEV0__SHIFT                            0x11
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_INTERNAL_ERR_EN_DEV0__SHIFT                              0x13
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_L0S_ACCEPTABLE_LATENCY_DEV0__SHIFT                       0x14
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_L0S_EXIT_LATENCY_DEV0__SHIFT                             0x17
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_L1_ACCEPTABLE_LATENCY_DEV0__SHIFT                        0x1a
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_L1_EXIT_LATENCY_DEV0__SHIFT                              0x1d
//RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_LINK_BW_NOTIFICATION_CAP_DN_EN_DEV0__SHIFT               0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_LTR_EN_DEV0__SHIFT                                       0x1
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_LTR_EN_DN_DEV0__SHIFT                                    0x2
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_MAX_PAYLOAD_SUPPORT_DEV0__SHIFT                          0x3
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_MSI_EN_DN_DEV0__SHIFT                                    0x6
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_MSTCPL_TIMEOUT_EN_DEV0__SHIFT                            0x7
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_NO_SOFT_RESET_DN_DEV0__SHIFT                             0x8
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_OBFF_SUPPORTED_DEV0__SHIFT                               0x9
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_DOWNSTREAM_PORT_RX_PRESET_HINT_DEV0__SHIFT  0xb
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_DOWNSTREAM_PORT_TX_PRESET_DEV0__SHIFT  0xe
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_UPSTREAM_PORT_RX_PRESET_HINT_DEV0__SHIFT  0x12
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_UPSTREAM_PORT_TX_PRESET_DEV0__SHIFT  0x15
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_PM_SUPPORT_DEV0__SHIFT                                   0x19
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_PM_SUPPORT_DN_DEV0__SHIFT                                0x1b
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_ATOMIC_EN_DN_DEV0__SHIFT                                 0x1d
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_VENDOR_ID_BIT_DN_DEV0__SHIFT                             0x1e
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_PMC_DSI_DN_DEV0__SHIFT                                   0x1f
//RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP4
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_0_DEV0__SHIFT                        0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_1_DEV0__SHIFT                        0x8
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_2_DEV0__SHIFT                        0x10
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_3_DEV0__SHIFT                        0x18
//RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_PWR_BUDGET_DATA_8T0_4_DEV0__SHIFT                        0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_PWR_BUDGET_DATA_8T0_5_DEV0__SHIFT                        0x8
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_PWR_BUDGET_SYSTEM_ALLOCATED_DEV0__SHIFT                  0x10
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_ATOMIC_64BIT_EN_DN_DEV0__SHIFT                           0x11
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_ATOMIC_ROUTING_EN_DEV0__SHIFT                            0x12
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_VC_EN_DN_DEV0__SHIFT                                     0x13
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_TwoVC_EN_DEV0__SHIFT                                     0x14
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_TwoVC_EN_DN_DEV0__SHIFT                                  0x15
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_ACS_SOURCE_VALIDATION_DN_DEV0__SHIFT                     0x17
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_ACS_TRANSLATION_BLOCKING_DN_DEV0__SHIFT                  0x18
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_ACS_P2P_REQUEST_REDIRECT_DN_DEV0__SHIFT                  0x19
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_ACS_P2P_COMPLETION_REDIRECT_DN_DEV0__SHIFT               0x1a
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_ACS_UPSTREAM_FORWARDING_DN_DEV0__SHIFT                   0x1b
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_ACS_P2P_EGRESS_CONTROL_DN_DEV0__SHIFT                    0x1c
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_ACS_DIRECT_TRANSLATED_P2P_DN_DEV0__SHIFT                 0x1d
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_MSI_MAP_EN_DEV0__SHIFT                                   0x1e
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_SSID_EN_DEV0__SHIFT                                      0x1f
//RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP6
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP6__STRAP_CFG_CRS_EN_DEV0__SHIFT                                   0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP6__STRAP_SMN_ERR_STATUS_MASK_EN_DNS_DEV0__SHIFT                   0x1
//RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP7
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP7__STRAP_PORT_NUMBER_DEV0__SHIFT                                  0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP7__STRAP_MAJOR_REV_ID_DN_DEV0__SHIFT                              0x8
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP7__STRAP_MINOR_REV_ID_DN_DEV0__SHIFT                              0xc
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP7__STRAP_RP_BUSNUM_DEV0__SHIFT                                    0x10
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP7__STRAP_DN_DEVNUM_DEV0__SHIFT                                    0x18
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP7__STRAP_DN_FUNCID_DEV0__SHIFT                                    0x1d
//RCC_DEV1_PORT_STRAP0
#define RCC_DEV1_PORT_STRAP0__STRAP_ARI_EN_DN_DEV1__SHIFT                                                     0x1
#define RCC_DEV1_PORT_STRAP0__STRAP_ACS_EN_DN_DEV1__SHIFT                                                     0x2
#define RCC_DEV1_PORT_STRAP0__STRAP_AER_EN_DN_DEV1__SHIFT                                                     0x3
#define RCC_DEV1_PORT_STRAP0__STRAP_CPL_ABORT_ERR_EN_DN_DEV1__SHIFT                                           0x4
#define RCC_DEV1_PORT_STRAP0__STRAP_DEVICE_ID_DN_DEV1__SHIFT                                                  0x5
#define RCC_DEV1_PORT_STRAP0__STRAP_INTERRUPT_PIN_DN_DEV1__SHIFT                                              0x15
#define RCC_DEV1_PORT_STRAP0__STRAP_IGNORE_E2E_PREFIX_UR_DN_DEV1__SHIFT                                       0x18
#define RCC_DEV1_PORT_STRAP0__STRAP_MAX_PAYLOAD_SUPPORT_DN_DEV1__SHIFT                                        0x19
#define RCC_DEV1_PORT_STRAP0__STRAP_MAX_LINK_WIDTH_SUPPORT_DEV1__SHIFT                                        0x1c
#define RCC_DEV1_PORT_STRAP0__STRAP_EPF0_DUMMY_EN_DEV1__SHIFT                                                 0x1f
//RCC_DEV1_PORT_STRAP1
#define RCC_DEV1_PORT_STRAP1__STRAP_SUBSYS_ID_DN_DEV1__SHIFT                                                  0x0
#define RCC_DEV1_PORT_STRAP1__STRAP_SUBSYS_VEN_ID_DN_DEV1__SHIFT                                              0x10
//RCC_DEV1_PORT_STRAP2
#define RCC_DEV1_PORT_STRAP2__STRAP_DE_EMPHASIS_SEL_DN_DEV1__SHIFT                                            0x0
#define RCC_DEV1_PORT_STRAP2__STRAP_DSN_EN_DN_DEV1__SHIFT                                                     0x1
#define RCC_DEV1_PORT_STRAP2__STRAP_E2E_PREFIX_EN_DEV1__SHIFT                                                 0x2
#define RCC_DEV1_PORT_STRAP2__STRAP_ECN1P1_EN_DEV1__SHIFT                                                     0x3
#define RCC_DEV1_PORT_STRAP2__STRAP_ECRC_CHECK_EN_DEV1__SHIFT                                                 0x4
#define RCC_DEV1_PORT_STRAP2__STRAP_ECRC_GEN_EN_DEV1__SHIFT                                                   0x5
#define RCC_DEV1_PORT_STRAP2__STRAP_ERR_REPORTING_DIS_DEV1__SHIFT                                             0x6
#define RCC_DEV1_PORT_STRAP2__STRAP_EXTENDED_FMT_SUPPORTED_DEV1__SHIFT                                        0x7
#define RCC_DEV1_PORT_STRAP2__STRAP_EXTENDED_TAG_ECN_EN_DEV1__SHIFT                                           0x8
#define RCC_DEV1_PORT_STRAP2__STRAP_EXT_VC_COUNT_DN_DEV1__SHIFT                                               0x9
#define RCC_DEV1_PORT_STRAP2__STRAP_FIRST_RCVD_ERR_LOG_DN_DEV1__SHIFT                                         0xc
#define RCC_DEV1_PORT_STRAP2__STRAP_POISONED_ADVISORY_NONFATAL_DN_DEV1__SHIFT                                 0xd
#define RCC_DEV1_PORT_STRAP2__STRAP_GEN2_COMPLIANCE_DEV1__SHIFT                                               0xe
#define RCC_DEV1_PORT_STRAP2__STRAP_GEN2_EN_DEV1__SHIFT                                                       0xf
#define RCC_DEV1_PORT_STRAP2__STRAP_GEN3_COMPLIANCE_DEV1__SHIFT                                               0x10
#define RCC_DEV1_PORT_STRAP2__STRAP_TARGET_LINK_SPEED_DEV1__SHIFT                                             0x11
#define RCC_DEV1_PORT_STRAP2__STRAP_INTERNAL_ERR_EN_DEV1__SHIFT                                               0x13
#define RCC_DEV1_PORT_STRAP2__STRAP_L0S_ACCEPTABLE_LATENCY_DEV1__SHIFT                                        0x14
#define RCC_DEV1_PORT_STRAP2__STRAP_L0S_EXIT_LATENCY_DEV1__SHIFT                                              0x17
#define RCC_DEV1_PORT_STRAP2__STRAP_L1_ACCEPTABLE_LATENCY_DEV1__SHIFT                                         0x1a
#define RCC_DEV1_PORT_STRAP2__STRAP_L1_EXIT_LATENCY_DEV1__SHIFT                                               0x1d
//RCC_DEV1_PORT_STRAP3
#define RCC_DEV1_PORT_STRAP3__STRAP_LINK_BW_NOTIFICATION_CAP_DN_EN_DEV1__SHIFT                                0x0
#define RCC_DEV1_PORT_STRAP3__STRAP_LTR_EN_DEV1__SHIFT                                                        0x1
#define RCC_DEV1_PORT_STRAP3__STRAP_LTR_EN_DN_DEV1__SHIFT                                                     0x2
#define RCC_DEV1_PORT_STRAP3__STRAP_MAX_PAYLOAD_SUPPORT_DEV1__SHIFT                                           0x3
#define RCC_DEV1_PORT_STRAP3__STRAP_MSI_EN_DN_DEV1__SHIFT                                                     0x6
#define RCC_DEV1_PORT_STRAP3__STRAP_MSTCPL_TIMEOUT_EN_DEV1__SHIFT                                             0x7
#define RCC_DEV1_PORT_STRAP3__STRAP_NO_SOFT_RESET_DN_DEV1__SHIFT                                              0x8
#define RCC_DEV1_PORT_STRAP3__STRAP_OBFF_SUPPORTED_DEV1__SHIFT                                                0x9
#define RCC_DEV1_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_DOWNSTREAM_PORT_RX_PRESET_HINT_DEV1__SHIFT    0xb
#define RCC_DEV1_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_DOWNSTREAM_PORT_TX_PRESET_DEV1__SHIFT         0xe
#define RCC_DEV1_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_UPSTREAM_PORT_RX_PRESET_HINT_DEV1__SHIFT      0x12
#define RCC_DEV1_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_UPSTREAM_PORT_TX_PRESET_DEV1__SHIFT           0x15
#define RCC_DEV1_PORT_STRAP3__STRAP_PM_SUPPORT_DEV1__SHIFT                                                    0x19
#define RCC_DEV1_PORT_STRAP3__STRAP_PM_SUPPORT_DN_DEV1__SHIFT                                                 0x1b
#define RCC_DEV1_PORT_STRAP3__STRAP_ATOMIC_EN_DN_DEV1__SHIFT                                                  0x1d
#define RCC_DEV1_PORT_STRAP3__STRAP_VENDOR_ID_BIT_DN_DEV1__SHIFT                                              0x1e
#define RCC_DEV1_PORT_STRAP3__STRAP_PMC_DSI_DN_DEV1__SHIFT                                                    0x1f
//RCC_DEV1_PORT_STRAP4
#define RCC_DEV1_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_0_DEV1__SHIFT                                         0x0
#define RCC_DEV1_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_1_DEV1__SHIFT                                         0x8
#define RCC_DEV1_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_2_DEV1__SHIFT                                         0x10
#define RCC_DEV1_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_3_DEV1__SHIFT                                         0x18
//RCC_DEV1_PORT_STRAP5
#define RCC_DEV1_PORT_STRAP5__STRAP_PWR_BUDGET_DATA_8T0_4_DEV1__SHIFT                                         0x0
#define RCC_DEV1_PORT_STRAP5__STRAP_PWR_BUDGET_DATA_8T0_5_DEV1__SHIFT                                         0x8
#define RCC_DEV1_PORT_STRAP5__STRAP_PWR_BUDGET_SYSTEM_ALLOCATED_DEV1__SHIFT                                   0x10
#define RCC_DEV1_PORT_STRAP5__STRAP_ATOMIC_64BIT_EN_DN_DEV1__SHIFT                                            0x11
#define RCC_DEV1_PORT_STRAP5__STRAP_ATOMIC_ROUTING_EN_DEV1__SHIFT                                             0x12
#define RCC_DEV1_PORT_STRAP5__STRAP_VC_EN_DN_DEV1__SHIFT                                                      0x13
#define RCC_DEV1_PORT_STRAP5__STRAP_TwoVC_EN_DEV1__SHIFT                                                      0x14
#define RCC_DEV1_PORT_STRAP5__STRAP_TwoVC_EN_DN_DEV1__SHIFT                                                   0x15
#define RCC_DEV1_PORT_STRAP5__STRAP_ACS_SOURCE_VALIDATION_DN_DEV1__SHIFT                                      0x17
#define RCC_DEV1_PORT_STRAP5__STRAP_ACS_TRANSLATION_BLOCKING_DN_DEV1__SHIFT                                   0x18
#define RCC_DEV1_PORT_STRAP5__STRAP_ACS_P2P_REQUEST_REDIRECT_DN_DEV1__SHIFT                                   0x19
#define RCC_DEV1_PORT_STRAP5__STRAP_ACS_P2P_COMPLETION_REDIRECT_DN_DEV1__SHIFT                                0x1a
#define RCC_DEV1_PORT_STRAP5__STRAP_ACS_UPSTREAM_FORWARDING_DN_DEV1__SHIFT                                    0x1b
#define RCC_DEV1_PORT_STRAP5__STRAP_ACS_P2P_EGRESS_CONTROL_DN_DEV1__SHIFT                                     0x1c
#define RCC_DEV1_PORT_STRAP5__STRAP_ACS_DIRECT_TRANSLATED_P2P_DN_DEV1__SHIFT                                  0x1d
#define RCC_DEV1_PORT_STRAP5__STRAP_MSI_MAP_EN_DEV1__SHIFT                                                    0x1e
#define RCC_DEV1_PORT_STRAP5__STRAP_SSID_EN_DEV1__SHIFT                                                       0x1f
//RCC_DEV1_PORT_STRAP6
#define RCC_DEV1_PORT_STRAP6__STRAP_CFG_CRS_EN_DEV1__SHIFT                                                    0x0
#define RCC_DEV1_PORT_STRAP6__STRAP_SMN_ERR_STATUS_MASK_EN_DNS_DEV1__SHIFT                                    0x1
//RCC_DEV1_PORT_STRAP7
#define RCC_DEV1_PORT_STRAP7__STRAP_PORT_NUMBER_DEV1__SHIFT                                                   0x0
#define RCC_DEV1_PORT_STRAP7__STRAP_MAJOR_REV_ID_DN_DEV1__SHIFT                                               0x8
#define RCC_DEV1_PORT_STRAP7__STRAP_MINOR_REV_ID_DN_DEV1__SHIFT                                               0xc
#define RCC_DEV1_PORT_STRAP7__STRAP_RP_BUSNUM_DEV1__SHIFT                                                     0x10
#define RCC_DEV1_PORT_STRAP7__STRAP_DN_DEVNUM_DEV1__SHIFT                                                     0x18
#define RCC_DEV1_PORT_STRAP7__STRAP_DN_FUNCID_DEV1__SHIFT                                                     0x1d
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP0__STRAP_DEVICE_ID_DEV0_F0__SHIFT                                 0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP0__STRAP_MAJOR_REV_ID_DEV0_F0__SHIFT                              0x10
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP0__STRAP_MINOR_REV_ID_DEV0_F0__SHIFT                              0x14
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0__SHIFT                                0x18
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP0__STRAP_FUNC_EN_DEV0_F0__SHIFT                                   0x1c
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV0_F0__SHIFT                     0x1d
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP0__STRAP_D1_SUPPORT_DEV0_F0__SHIFT                                0x1e
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP0__STRAP_D2_SUPPORT_DEV0_F0__SHIFT                                0x1f
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP1
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP1__STRAP_SRIOV_VF_DEVICE_ID_DEV0_F0__SHIFT                        0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP1__STRAP_SRIOV_SUPPORTED_PAGE_SIZE_DEV0_F0__SHIFT                 0x10
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_SRIOV_EN_DEV0_F0__SHIFT                                  0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_SRIOV_TOTAL_VFS_DEV0_F0__SHIFT                           0x1
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_64BAR_DIS_DEV0_F0__SHIFT                                 0x6
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F0__SHIFT                             0x7
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_RESIZE_BAR_EN_DEV0_F0__SHIFT                             0x8
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_MAX_PASID_WIDTH_DEV0_F0__SHIFT                           0x9
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV0_F0__SHIFT                    0xe
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_ARI_EN_DEV0_F0__SHIFT                                    0xf
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_AER_EN_DEV0_F0__SHIFT                                    0x10
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_ACS_EN_DEV0_F0__SHIFT                                    0x11
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_ATS_EN_DEV0_F0__SHIFT                                    0x12
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV0_F0__SHIFT                          0x14
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_DPA_EN_DEV0_F0__SHIFT                                    0x15
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_DSN_EN_DEV0_F0__SHIFT                                    0x16
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_VC_EN_DEV0_F0__SHIFT                                     0x17
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_MSI_MULTI_CAP_DEV0_F0__SHIFT                             0x18
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_PAGE_REQ_EN_DEV0_F0__SHIFT                               0x1b
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_PASID_EN_DEV0_F0__SHIFT                                  0x1c
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_PASID_EXE_PERMISSION_SUPPORTED_DEV0_F0__SHIFT            0x1d
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_PASID_GLOBAL_INVALIDATE_SUPPORTED_DEV0_F0__SHIFT         0x1e
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_PASID_PRIV_MODE_SUPPORTED_DEV0_F0__SHIFT                 0x1f
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV0_F0__SHIFT                0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_PWR_EN_DEV0_F0__SHIFT                                    0x1
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_SUBSYS_ID_DEV0_F0__SHIFT                                 0x2
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_MSI_EN_DEV0_F0__SHIFT                                    0x12
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV0_F0__SHIFT                        0x13
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_MSIX_EN_DEV0_F0__SHIFT                                   0x14
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_MSIX_TABLE_BIR_DEV0_F0__SHIFT                            0x15
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_PMC_DSI_DEV0_F0__SHIFT                                   0x18
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_VENDOR_ID_BIT_DEV0_F0__SHIFT                             0x19
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV0_F0__SHIFT                  0x1a
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV0_F0__SHIFT                 0x1b
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP4
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP4__STRAP_MSIX_TABLE_OFFSET_DEV0_F0__SHIFT                         0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV0_F0__SHIFT                           0x14
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP4__STRAP_ATOMIC_EN_DEV0_F0__SHIFT                                 0x15
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP4__STRAP_FLR_EN_DEV0_F0__SHIFT                                    0x16
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP4__STRAP_PME_SUPPORT_DEV0_F0__SHIFT                               0x17
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP4__STRAP_INTERRUPT_PIN_DEV0_F0__SHIFT                             0x1c
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP4__STRAP_AUXPWR_SUPPORT_DEV0_F0__SHIFT                            0x1f
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP5
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP5__STRAP_SUBSYS_VEN_ID_DEV0_F0__SHIFT                             0x0
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_BAR_COMPLIANCE_EN_DEV0_F0__SHIFT                         0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_DOORBELL_APER_SIZE_DEV0_F0__SHIFT                        0x1
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_DOORBELL_BAR_DIS_DEV0_F0__SHIFT                          0x3
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_FB_ALWAYS_ON_DEV0_F0__SHIFT                              0x4
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_FB_CPL_TYPE_SEL_DEV0_F0__SHIFT                           0x5
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_IO_BAR_DIS_DEV0_F0__SHIFT                                0x7
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_LFB_ERRMSG_EN_DEV0_F0__SHIFT                             0x8
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_MEM_AP_SIZE_DEV0_F0__SHIFT                               0x9
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_REG_AP_SIZE_DEV0_F0__SHIFT                               0xc
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_ROM_AP_SIZE_DEV0_F0__SHIFT                               0xe
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_VF_DOORBELL_APER_SIZE_DEV0_F0__SHIFT                     0x10
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_VF_MEM_AP_SIZE_DEV0_F0__SHIFT                            0x13
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_VF_REG_AP_SIZE_DEV0_F0__SHIFT                            0x16
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_VGA_DIS_DEV0_F0__SHIFT                                   0x18
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_NBIF_ROM_BAR_DIS_CHICKEN_DEV0_F0__SHIFT                  0x19
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_VF_REG_PROT_DIS_DEV0_F0__SHIFT                           0x1a
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_VF_MSI_MULTI_CAP_DEV0_F0__SHIFT                          0x1b
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_SRIOV_VF_MAPPING_MODE_DEV0_F0__SHIFT                     0x1e
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP9
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP13
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP13__STRAP_CLASS_CODE_PIF_DEV0_F0__SHIFT                           0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP13__STRAP_CLASS_CODE_SUB_DEV0_F0__SHIFT                           0x8
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP13__STRAP_CLASS_CODE_BASE_DEV0_F0__SHIFT                          0x10
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP0__STRAP_DEVICE_ID_DEV0_F1__SHIFT                                 0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP0__STRAP_MAJOR_REV_ID_DEV0_F1__SHIFT                              0x10
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP0__STRAP_MINOR_REV_ID_DEV0_F1__SHIFT                              0x14
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP0__STRAP_FUNC_EN_DEV0_F1__SHIFT                                   0x1c
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV0_F1__SHIFT                     0x1d
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP0__STRAP_D1_SUPPORT_DEV0_F1__SHIFT                                0x1e
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP0__STRAP_D2_SUPPORT_DEV0_F1__SHIFT                                0x1f
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F1__SHIFT                             0x7
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_RESIZE_BAR_EN_DEV0_F1__SHIFT                             0x8
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV0_F1__SHIFT                    0xe
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_AER_EN_DEV0_F1__SHIFT                                    0x10
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_ACS_EN_DEV0_F1__SHIFT                                    0x11
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_ATS_EN_DEV0_F1__SHIFT                                    0x12
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV0_F1__SHIFT                          0x14
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_DPA_EN_DEV0_F1__SHIFT                                    0x15
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_DSN_EN_DEV0_F1__SHIFT                                    0x16
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_VC_EN_DEV0_F1__SHIFT                                     0x17
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_MSI_MULTI_CAP_DEV0_F1__SHIFT                             0x18
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV0_F1__SHIFT                0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3__STRAP_PWR_EN_DEV0_F1__SHIFT                                    0x1
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3__STRAP_SUBSYS_ID_DEV0_F1__SHIFT                                 0x2
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3__STRAP_MSI_EN_DEV0_F1__SHIFT                                    0x12
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV0_F1__SHIFT                        0x13
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3__STRAP_MSIX_EN_DEV0_F1__SHIFT                                   0x14
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3__STRAP_PMC_DSI_DEV0_F1__SHIFT                                   0x18
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3__STRAP_VENDOR_ID_BIT_DEV0_F1__SHIFT                             0x19
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV0_F1__SHIFT                  0x1a
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV0_F1__SHIFT                 0x1b
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP4
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV0_F1__SHIFT                           0x14
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP4__STRAP_ATOMIC_EN_DEV0_F1__SHIFT                                 0x15
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP4__STRAP_FLR_EN_DEV0_F1__SHIFT                                    0x16
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP4__STRAP_PME_SUPPORT_DEV0_F1__SHIFT                               0x17
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP4__STRAP_INTERRUPT_PIN_DEV0_F1__SHIFT                             0x1c
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP4__STRAP_AUXPWR_SUPPORT_DEV0_F1__SHIFT                            0x1f
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP5
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP5__STRAP_SUBSYS_VEN_ID_DEV0_F1__SHIFT                             0x0
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6__STRAP_APER0_EN_DEV0_F1__SHIFT                                  0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV0_F1__SHIFT                     0x1
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6__STRAP_APER0_64BAR_EN_DEV0_F1__SHIFT                            0x2
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6__STRAP_APER0_AP_SIZE_DEV0_F1__SHIFT                             0x4
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6__STRAP_APER1_EN_DEV0_F1__SHIFT                                  0x8
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6__STRAP_APER1_PREFETCHABLE_EN_DEV0_F1__SHIFT                     0x9
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6__STRAP_APER2_EN_DEV0_F1__SHIFT                                  0x10
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6__STRAP_APER2_PREFETCHABLE_EN_DEV0_F1__SHIFT                     0x11
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6__STRAP_APER3_EN_DEV0_F1__SHIFT                                  0x18
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6__STRAP_APER3_PREFETCHABLE_EN_DEV0_F1__SHIFT                     0x19
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP7
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP7__STRAP_ROM_APER_EN_DEV0_F1__SHIFT                               0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP7__STRAP_ROM_APER_SIZE_DEV0_F1__SHIFT                             0x1
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP10
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP10__STRAP_APER1_RESIZE_EN_DEV0_F1__SHIFT                          0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP10__STRAP_APER1_RESIZE_SUPPORT_DEV0_F1__SHIFT                     0x1
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP11
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP11__STRAP_APER2_RESIZE_EN_DEV0_F1__SHIFT                          0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP11__STRAP_APER2_RESIZE_SUPPORT_DEV0_F1__SHIFT                     0x1
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP12
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP12__STRAP_APER3_RESIZE_EN_DEV0_F1__SHIFT                          0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP12__STRAP_APER3_RESIZE_SUPPORT_DEV0_F1__SHIFT                     0x1
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP13
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP13__STRAP_CLASS_CODE_PIF_DEV0_F1__SHIFT                           0x0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP13__STRAP_CLASS_CODE_SUB_DEV0_F1__SHIFT                           0x8
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP13__STRAP_CLASS_CODE_BASE_DEV0_F1__SHIFT                          0x10
//RCC_DEV0_EPF2_STRAP0
#define RCC_DEV0_EPF2_STRAP0__STRAP_DEVICE_ID_DEV0_F2__SHIFT                                                  0x0
#define RCC_DEV0_EPF2_STRAP0__STRAP_MAJOR_REV_ID_DEV0_F2__SHIFT                                               0x10
#define RCC_DEV0_EPF2_STRAP0__STRAP_MINOR_REV_ID_DEV0_F2__SHIFT                                               0x14
#define RCC_DEV0_EPF2_STRAP0__STRAP_FUNC_EN_DEV0_F2__SHIFT                                                    0x1c
#define RCC_DEV0_EPF2_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV0_F2__SHIFT                                      0x1d
#define RCC_DEV0_EPF2_STRAP0__STRAP_D1_SUPPORT_DEV0_F2__SHIFT                                                 0x1e
#define RCC_DEV0_EPF2_STRAP0__STRAP_D2_SUPPORT_DEV0_F2__SHIFT                                                 0x1f
//RCC_DEV0_EPF2_STRAP2
#define RCC_DEV0_EPF2_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F2__SHIFT                                              0x7
#define RCC_DEV0_EPF2_STRAP2__STRAP_RESIZE_BAR_EN_DEV0_F2__SHIFT                                              0x8
#define RCC_DEV0_EPF2_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV0_F2__SHIFT                                     0xe
#define RCC_DEV0_EPF2_STRAP2__STRAP_AER_EN_DEV0_F2__SHIFT                                                     0x10
#define RCC_DEV0_EPF2_STRAP2__STRAP_ACS_EN_DEV0_F2__SHIFT                                                     0x11
#define RCC_DEV0_EPF2_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV0_F2__SHIFT                                           0x14
#define RCC_DEV0_EPF2_STRAP2__STRAP_DPA_EN_DEV0_F2__SHIFT                                                     0x15
#define RCC_DEV0_EPF2_STRAP2__STRAP_VC_EN_DEV0_F2__SHIFT                                                      0x17
#define RCC_DEV0_EPF2_STRAP2__STRAP_MSI_MULTI_CAP_DEV0_F2__SHIFT                                              0x18
//RCC_DEV0_EPF2_STRAP3
#define RCC_DEV0_EPF2_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV0_F2__SHIFT                                 0x0
#define RCC_DEV0_EPF2_STRAP3__STRAP_PWR_EN_DEV0_F2__SHIFT                                                     0x1
#define RCC_DEV0_EPF2_STRAP3__STRAP_SUBSYS_ID_DEV0_F2__SHIFT                                                  0x2
#define RCC_DEV0_EPF2_STRAP3__STRAP_MSI_EN_DEV0_F2__SHIFT                                                     0x12
#define RCC_DEV0_EPF2_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV0_F2__SHIFT                                         0x13
#define RCC_DEV0_EPF2_STRAP3__STRAP_MSIX_EN_DEV0_F2__SHIFT                                                    0x14
#define RCC_DEV0_EPF2_STRAP3__STRAP_PMC_DSI_DEV0_F2__SHIFT                                                    0x18
#define RCC_DEV0_EPF2_STRAP3__STRAP_VENDOR_ID_BIT_DEV0_F2__SHIFT                                              0x19
#define RCC_DEV0_EPF2_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV0_F2__SHIFT                                   0x1a
#define RCC_DEV0_EPF2_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV0_F2__SHIFT                                  0x1b
//RCC_DEV0_EPF2_STRAP4
#define RCC_DEV0_EPF2_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV0_F2__SHIFT                                            0x14
#define RCC_DEV0_EPF2_STRAP4__STRAP_ATOMIC_EN_DEV0_F2__SHIFT                                                  0x15
#define RCC_DEV0_EPF2_STRAP4__STRAP_FLR_EN_DEV0_F2__SHIFT                                                     0x16
#define RCC_DEV0_EPF2_STRAP4__STRAP_PME_SUPPORT_DEV0_F2__SHIFT                                                0x17
#define RCC_DEV0_EPF2_STRAP4__STRAP_INTERRUPT_PIN_DEV0_F2__SHIFT                                              0x1c
#define RCC_DEV0_EPF2_STRAP4__STRAP_AUXPWR_SUPPORT_DEV0_F2__SHIFT                                             0x1f
//RCC_DEV0_EPF2_STRAP5
#define RCC_DEV0_EPF2_STRAP5__STRAP_SUBSYS_VEN_ID_DEV0_F2__SHIFT                                              0x0
#define RCC_DEV0_EPF2_STRAP5__STRAP_SATAIDP_EN_DEV0_F2__SHIFT                                                 0x18
//RCC_DEV0_EPF2_STRAP6
#define RCC_DEV0_EPF2_STRAP6__STRAP_APER0_EN_DEV0_F2__SHIFT                                                   0x0
#define RCC_DEV0_EPF2_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV0_F2__SHIFT                                      0x1
#define RCC_DEV0_EPF2_STRAP6__STRAP_APER0_AP_SIZE_DEV0_F2__SHIFT                                              0x4
#define RCC_DEV0_EPF2_STRAP6__STRAP_APER1_EN_DEV0_F2__SHIFT                                                   0x8
#define RCC_DEV0_EPF2_STRAP6__STRAP_APER1_PREFETCHABLE_EN_DEV0_F2__SHIFT                                      0x9
//RCC_DEV0_EPF2_STRAP13
#define RCC_DEV0_EPF2_STRAP13__STRAP_CLASS_CODE_PIF_DEV0_F2__SHIFT                                            0x0
#define RCC_DEV0_EPF2_STRAP13__STRAP_CLASS_CODE_SUB_DEV0_F2__SHIFT                                            0x8
#define RCC_DEV0_EPF2_STRAP13__STRAP_CLASS_CODE_BASE_DEV0_F2__SHIFT                                           0x10
//RCC_DEV0_EPF3_STRAP0
#define RCC_DEV0_EPF3_STRAP0__STRAP_DEVICE_ID_DEV0_F3__SHIFT                                                  0x0
#define RCC_DEV0_EPF3_STRAP0__STRAP_MAJOR_REV_ID_DEV0_F3__SHIFT                                               0x10
#define RCC_DEV0_EPF3_STRAP0__STRAP_MINOR_REV_ID_DEV0_F3__SHIFT                                               0x14
#define RCC_DEV0_EPF3_STRAP0__STRAP_FUNC_EN_DEV0_F3__SHIFT                                                    0x1c
#define RCC_DEV0_EPF3_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV0_F3__SHIFT                                      0x1d
#define RCC_DEV0_EPF3_STRAP0__STRAP_D1_SUPPORT_DEV0_F3__SHIFT                                                 0x1e
#define RCC_DEV0_EPF3_STRAP0__STRAP_D2_SUPPORT_DEV0_F3__SHIFT                                                 0x1f
//RCC_DEV0_EPF3_STRAP2
#define RCC_DEV0_EPF3_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F3__SHIFT                                              0x7
#define RCC_DEV0_EPF3_STRAP2__STRAP_RESIZE_BAR_EN_DEV0_F3__SHIFT                                              0x8
#define RCC_DEV0_EPF3_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV0_F3__SHIFT                                     0xe
#define RCC_DEV0_EPF3_STRAP2__STRAP_AER_EN_DEV0_F3__SHIFT                                                     0x10
#define RCC_DEV0_EPF3_STRAP2__STRAP_ACS_EN_DEV0_F3__SHIFT                                                     0x11
#define RCC_DEV0_EPF3_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV0_F3__SHIFT                                           0x14
#define RCC_DEV0_EPF3_STRAP2__STRAP_DPA_EN_DEV0_F3__SHIFT                                                     0x15
#define RCC_DEV0_EPF3_STRAP2__STRAP_VC_EN_DEV0_F3__SHIFT                                                      0x17
#define RCC_DEV0_EPF3_STRAP2__STRAP_MSI_MULTI_CAP_DEV0_F3__SHIFT                                              0x18
//RCC_DEV0_EPF3_STRAP3
#define RCC_DEV0_EPF3_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV0_F3__SHIFT                                 0x0
#define RCC_DEV0_EPF3_STRAP3__STRAP_PWR_EN_DEV0_F3__SHIFT                                                     0x1
#define RCC_DEV0_EPF3_STRAP3__STRAP_SUBSYS_ID_DEV0_F3__SHIFT                                                  0x2
#define RCC_DEV0_EPF3_STRAP3__STRAP_MSI_EN_DEV0_F3__SHIFT                                                     0x12
#define RCC_DEV0_EPF3_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV0_F3__SHIFT                                         0x13
#define RCC_DEV0_EPF3_STRAP3__STRAP_MSIX_EN_DEV0_F3__SHIFT                                                    0x14
#define RCC_DEV0_EPF3_STRAP3__STRAP_PMC_DSI_DEV0_F3__SHIFT                                                    0x18
#define RCC_DEV0_EPF3_STRAP3__STRAP_VENDOR_ID_BIT_DEV0_F3__SHIFT                                              0x19
#define RCC_DEV0_EPF3_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV0_F3__SHIFT                                   0x1a
#define RCC_DEV0_EPF3_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV0_F3__SHIFT                                  0x1b
//RCC_DEV0_EPF3_STRAP4
#define RCC_DEV0_EPF3_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV0_F3__SHIFT                                            0x14
#define RCC_DEV0_EPF3_STRAP4__STRAP_ATOMIC_EN_DEV0_F3__SHIFT                                                  0x15
#define RCC_DEV0_EPF3_STRAP4__STRAP_FLR_EN_DEV0_F3__SHIFT                                                     0x16
#define RCC_DEV0_EPF3_STRAP4__STRAP_PME_SUPPORT_DEV0_F3__SHIFT                                                0x17
#define RCC_DEV0_EPF3_STRAP4__STRAP_INTERRUPT_PIN_DEV0_F3__SHIFT                                              0x1c
#define RCC_DEV0_EPF3_STRAP4__STRAP_AUXPWR_SUPPORT_DEV0_F3__SHIFT                                             0x1f
//RCC_DEV0_EPF3_STRAP5
#define RCC_DEV0_EPF3_STRAP5__STRAP_SUBSYS_VEN_ID_DEV0_F3__SHIFT                                              0x0
#define RCC_DEV0_EPF3_STRAP5__STRAP_USB_DBESEL_DEV0_F3__SHIFT                                                 0x10
#define RCC_DEV0_EPF3_STRAP5__STRAP_USB_DBESELD_DEV0_F3__SHIFT                                                0x14
//RCC_DEV0_EPF3_STRAP6
#define RCC_DEV0_EPF3_STRAP6__STRAP_APER0_EN_DEV0_F3__SHIFT                                                   0x0
#define RCC_DEV0_EPF3_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV0_F3__SHIFT                                      0x1
#define RCC_DEV0_EPF3_STRAP6__STRAP_APER0_AP_SIZE_DEV0_F3__SHIFT                                              0x4
//RCC_DEV0_EPF3_STRAP13
#define RCC_DEV0_EPF3_STRAP13__STRAP_CLASS_CODE_PIF_DEV0_F3__SHIFT                                            0x0
#define RCC_DEV0_EPF3_STRAP13__STRAP_CLASS_CODE_SUB_DEV0_F3__SHIFT                                            0x8
#define RCC_DEV0_EPF3_STRAP13__STRAP_CLASS_CODE_BASE_DEV0_F3__SHIFT                                           0x10
//RCC_DEV0_EPF4_STRAP0
#define RCC_DEV0_EPF4_STRAP0__STRAP_DEVICE_ID_DEV0_F4__SHIFT                                                  0x0
#define RCC_DEV0_EPF4_STRAP0__STRAP_MAJOR_REV_ID_DEV0_F4__SHIFT                                               0x10
#define RCC_DEV0_EPF4_STRAP0__STRAP_MINOR_REV_ID_DEV0_F4__SHIFT                                               0x14
#define RCC_DEV0_EPF4_STRAP0__STRAP_FUNC_EN_DEV0_F4__SHIFT                                                    0x1c
#define RCC_DEV0_EPF4_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV0_F4__SHIFT                                      0x1d
#define RCC_DEV0_EPF4_STRAP0__STRAP_D1_SUPPORT_DEV0_F4__SHIFT                                                 0x1e
#define RCC_DEV0_EPF4_STRAP0__STRAP_D2_SUPPORT_DEV0_F4__SHIFT                                                 0x1f
//RCC_DEV0_EPF4_STRAP2
#define RCC_DEV0_EPF4_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F4__SHIFT                                              0x7
#define RCC_DEV0_EPF4_STRAP2__STRAP_RESIZE_BAR_EN_DEV0_F4__SHIFT                                              0x8
#define RCC_DEV0_EPF4_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV0_F4__SHIFT                                     0xe
#define RCC_DEV0_EPF4_STRAP2__STRAP_AER_EN_DEV0_F4__SHIFT                                                     0x10
#define RCC_DEV0_EPF4_STRAP2__STRAP_ACS_EN_DEV0_F4__SHIFT                                                     0x11
#define RCC_DEV0_EPF4_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV0_F4__SHIFT                                           0x14
#define RCC_DEV0_EPF4_STRAP2__STRAP_DPA_EN_DEV0_F4__SHIFT                                                     0x15
#define RCC_DEV0_EPF4_STRAP2__STRAP_VC_EN_DEV0_F4__SHIFT                                                      0x17
#define RCC_DEV0_EPF4_STRAP2__STRAP_MSI_MULTI_CAP_DEV0_F4__SHIFT                                              0x18
//RCC_DEV0_EPF4_STRAP3
#define RCC_DEV0_EPF4_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV0_F4__SHIFT                                 0x0
#define RCC_DEV0_EPF4_STRAP3__STRAP_PWR_EN_DEV0_F4__SHIFT                                                     0x1
#define RCC_DEV0_EPF4_STRAP3__STRAP_SUBSYS_ID_DEV0_F4__SHIFT                                                  0x2
#define RCC_DEV0_EPF4_STRAP3__STRAP_MSI_EN_DEV0_F4__SHIFT                                                     0x12
#define RCC_DEV0_EPF4_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV0_F4__SHIFT                                         0x13
#define RCC_DEV0_EPF4_STRAP3__STRAP_MSIX_EN_DEV0_F4__SHIFT                                                    0x14
#define RCC_DEV0_EPF4_STRAP3__STRAP_PMC_DSI_DEV0_F4__SHIFT                                                    0x18
#define RCC_DEV0_EPF4_STRAP3__STRAP_VENDOR_ID_BIT_DEV0_F4__SHIFT                                              0x19
#define RCC_DEV0_EPF4_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV0_F4__SHIFT                                   0x1a
#define RCC_DEV0_EPF4_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV0_F4__SHIFT                                  0x1b
//RCC_DEV0_EPF4_STRAP4
#define RCC_DEV0_EPF4_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV0_F4__SHIFT                                            0x14
#define RCC_DEV0_EPF4_STRAP4__STRAP_ATOMIC_EN_DEV0_F4__SHIFT                                                  0x15
#define RCC_DEV0_EPF4_STRAP4__STRAP_FLR_EN_DEV0_F4__SHIFT                                                     0x16
#define RCC_DEV0_EPF4_STRAP4__STRAP_PME_SUPPORT_DEV0_F4__SHIFT                                                0x17
#define RCC_DEV0_EPF4_STRAP4__STRAP_INTERRUPT_PIN_DEV0_F4__SHIFT                                              0x1c
#define RCC_DEV0_EPF4_STRAP4__STRAP_AUXPWR_SUPPORT_DEV0_F4__SHIFT                                             0x1f
//RCC_DEV0_EPF4_STRAP5
#define RCC_DEV0_EPF4_STRAP5__STRAP_SUBSYS_VEN_ID_DEV0_F4__SHIFT                                              0x0
#define RCC_DEV0_EPF4_STRAP5__STRAP_USB_DBESEL_DEV0_F4__SHIFT                                                 0x10
#define RCC_DEV0_EPF4_STRAP5__STRAP_USB_DBESELD_DEV0_F4__SHIFT                                                0x14
//RCC_DEV0_EPF4_STRAP6
#define RCC_DEV0_EPF4_STRAP6__STRAP_APER0_EN_DEV0_F4__SHIFT                                                   0x0
#define RCC_DEV0_EPF4_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV0_F4__SHIFT                                      0x1
#define RCC_DEV0_EPF4_STRAP6__STRAP_APER0_AP_SIZE_DEV0_F4__SHIFT                                              0x4
#define RCC_DEV0_EPF4_STRAP6__STRAP_APER1_EN_DEV0_F4__SHIFT                                                   0x8
#define RCC_DEV0_EPF4_STRAP6__STRAP_APER1_PREFETCHABLE_EN_DEV0_F4__SHIFT                                      0x9
#define RCC_DEV0_EPF4_STRAP6__STRAP_APER2_EN_DEV0_F4__SHIFT                                                   0x10
#define RCC_DEV0_EPF4_STRAP6__STRAP_APER2_PREFETCHABLE_EN_DEV0_F4__SHIFT                                      0x11
//RCC_DEV0_EPF4_STRAP13
#define RCC_DEV0_EPF4_STRAP13__STRAP_CLASS_CODE_PIF_DEV0_F4__SHIFT                                            0x0
#define RCC_DEV0_EPF4_STRAP13__STRAP_CLASS_CODE_SUB_DEV0_F4__SHIFT                                            0x8
#define RCC_DEV0_EPF4_STRAP13__STRAP_CLASS_CODE_BASE_DEV0_F4__SHIFT                                           0x10
//RCC_DEV0_EPF5_STRAP0
#define RCC_DEV0_EPF5_STRAP0__STRAP_DEVICE_ID_DEV0_F5__SHIFT                                                  0x0
#define RCC_DEV0_EPF5_STRAP0__STRAP_MAJOR_REV_ID_DEV0_F5__SHIFT                                               0x10
#define RCC_DEV0_EPF5_STRAP0__STRAP_MINOR_REV_ID_DEV0_F5__SHIFT                                               0x14
#define RCC_DEV0_EPF5_STRAP0__STRAP_FUNC_EN_DEV0_F5__SHIFT                                                    0x1c
#define RCC_DEV0_EPF5_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV0_F5__SHIFT                                      0x1d
#define RCC_DEV0_EPF5_STRAP0__STRAP_D1_SUPPORT_DEV0_F5__SHIFT                                                 0x1e
#define RCC_DEV0_EPF5_STRAP0__STRAP_D2_SUPPORT_DEV0_F5__SHIFT                                                 0x1f
//RCC_DEV0_EPF5_STRAP2
#define RCC_DEV0_EPF5_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F5__SHIFT                                              0x7
#define RCC_DEV0_EPF5_STRAP2__STRAP_RESIZE_BAR_EN_DEV0_F5__SHIFT                                              0x8
#define RCC_DEV0_EPF5_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV0_F5__SHIFT                                     0xe
#define RCC_DEV0_EPF5_STRAP2__STRAP_AER_EN_DEV0_F5__SHIFT                                                     0x10
#define RCC_DEV0_EPF5_STRAP2__STRAP_ACS_EN_DEV0_F5__SHIFT                                                     0x11
#define RCC_DEV0_EPF5_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV0_F5__SHIFT                                           0x14
#define RCC_DEV0_EPF5_STRAP2__STRAP_DPA_EN_DEV0_F5__SHIFT                                                     0x15
#define RCC_DEV0_EPF5_STRAP2__STRAP_VC_EN_DEV0_F5__SHIFT                                                      0x17
#define RCC_DEV0_EPF5_STRAP2__STRAP_MSI_MULTI_CAP_DEV0_F5__SHIFT                                              0x18
//RCC_DEV0_EPF5_STRAP3
#define RCC_DEV0_EPF5_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV0_F5__SHIFT                                 0x0
#define RCC_DEV0_EPF5_STRAP3__STRAP_PWR_EN_DEV0_F5__SHIFT                                                     0x1
#define RCC_DEV0_EPF5_STRAP3__STRAP_SUBSYS_ID_DEV0_F5__SHIFT                                                  0x2
#define RCC_DEV0_EPF5_STRAP3__STRAP_MSI_EN_DEV0_F5__SHIFT                                                     0x12
#define RCC_DEV0_EPF5_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV0_F5__SHIFT                                         0x13
#define RCC_DEV0_EPF5_STRAP3__STRAP_MSIX_EN_DEV0_F5__SHIFT                                                    0x14
#define RCC_DEV0_EPF5_STRAP3__STRAP_PMC_DSI_DEV0_F5__SHIFT                                                    0x18
#define RCC_DEV0_EPF5_STRAP3__STRAP_VENDOR_ID_BIT_DEV0_F5__SHIFT                                              0x19
#define RCC_DEV0_EPF5_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV0_F5__SHIFT                                   0x1a
#define RCC_DEV0_EPF5_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV0_F5__SHIFT                                  0x1b
//RCC_DEV0_EPF5_STRAP4
#define RCC_DEV0_EPF5_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV0_F5__SHIFT                                            0x14
#define RCC_DEV0_EPF5_STRAP4__STRAP_ATOMIC_EN_DEV0_F5__SHIFT                                                  0x15
#define RCC_DEV0_EPF5_STRAP4__STRAP_FLR_EN_DEV0_F5__SHIFT                                                     0x16
#define RCC_DEV0_EPF5_STRAP4__STRAP_PME_SUPPORT_DEV0_F5__SHIFT                                                0x17
#define RCC_DEV0_EPF5_STRAP4__STRAP_INTERRUPT_PIN_DEV0_F5__SHIFT                                              0x1c
#define RCC_DEV0_EPF5_STRAP4__STRAP_AUXPWR_SUPPORT_DEV0_F5__SHIFT                                             0x1f
//RCC_DEV0_EPF5_STRAP5
#define RCC_DEV0_EPF5_STRAP5__STRAP_SUBSYS_VEN_ID_DEV0_F5__SHIFT                                              0x0
//RCC_DEV0_EPF5_STRAP6
#define RCC_DEV0_EPF5_STRAP6__STRAP_APER0_EN_DEV0_F5__SHIFT                                                   0x0
#define RCC_DEV0_EPF5_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV0_F5__SHIFT                                      0x1
#define RCC_DEV0_EPF5_STRAP6__STRAP_APER0_AP_SIZE_DEV0_F5__SHIFT                                              0x4
#define RCC_DEV0_EPF5_STRAP6__STRAP_APER1_EN_DEV0_F5__SHIFT                                                   0x8
#define RCC_DEV0_EPF5_STRAP6__STRAP_APER1_PREFETCHABLE_EN_DEV0_F5__SHIFT                                      0x9
#define RCC_DEV0_EPF5_STRAP6__STRAP_APER2_EN_DEV0_F5__SHIFT                                                   0x10
#define RCC_DEV0_EPF5_STRAP6__STRAP_APER2_PREFETCHABLE_EN_DEV0_F5__SHIFT                                      0x11
//RCC_DEV0_EPF5_STRAP13
#define RCC_DEV0_EPF5_STRAP13__STRAP_CLASS_CODE_PIF_DEV0_F5__SHIFT                                            0x0
#define RCC_DEV0_EPF5_STRAP13__STRAP_CLASS_CODE_SUB_DEV0_F5__SHIFT                                            0x8
#define RCC_DEV0_EPF5_STRAP13__STRAP_CLASS_CODE_BASE_DEV0_F5__SHIFT                                           0x10
//RCC_DEV0_EPF6_STRAP0
#define RCC_DEV0_EPF6_STRAP0__STRAP_DEVICE_ID_DEV0_F6__SHIFT                                                  0x0
#define RCC_DEV0_EPF6_STRAP0__STRAP_MAJOR_REV_ID_DEV0_F6__SHIFT                                               0x10
#define RCC_DEV0_EPF6_STRAP0__STRAP_MINOR_REV_ID_DEV0_F6__SHIFT                                               0x14
#define RCC_DEV0_EPF6_STRAP0__STRAP_FUNC_EN_DEV0_F6__SHIFT                                                    0x1c
#define RCC_DEV0_EPF6_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV0_F6__SHIFT                                      0x1d
#define RCC_DEV0_EPF6_STRAP0__STRAP_D1_SUPPORT_DEV0_F6__SHIFT                                                 0x1e
#define RCC_DEV0_EPF6_STRAP0__STRAP_D2_SUPPORT_DEV0_F6__SHIFT                                                 0x1f
//RCC_DEV0_EPF6_STRAP2
#define RCC_DEV0_EPF6_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F6__SHIFT                                              0x7
#define RCC_DEV0_EPF6_STRAP2__STRAP_RESIZE_BAR_EN_DEV0_F6__SHIFT                                              0x8
#define RCC_DEV0_EPF6_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV0_F6__SHIFT                                     0xe
#define RCC_DEV0_EPF6_STRAP2__STRAP_AER_EN_DEV0_F6__SHIFT                                                     0x10
#define RCC_DEV0_EPF6_STRAP2__STRAP_ACS_EN_DEV0_F6__SHIFT                                                     0x11
#define RCC_DEV0_EPF6_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV0_F6__SHIFT                                           0x14
#define RCC_DEV0_EPF6_STRAP2__STRAP_DPA_EN_DEV0_F6__SHIFT                                                     0x15
#define RCC_DEV0_EPF6_STRAP2__STRAP_VC_EN_DEV0_F6__SHIFT                                                      0x17
#define RCC_DEV0_EPF6_STRAP2__STRAP_MSI_MULTI_CAP_DEV0_F6__SHIFT                                              0x18
//RCC_DEV0_EPF6_STRAP3
#define RCC_DEV0_EPF6_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV0_F6__SHIFT                                 0x0
#define RCC_DEV0_EPF6_STRAP3__STRAP_PWR_EN_DEV0_F6__SHIFT                                                     0x1
#define RCC_DEV0_EPF6_STRAP3__STRAP_SUBSYS_ID_DEV0_F6__SHIFT                                                  0x2
#define RCC_DEV0_EPF6_STRAP3__STRAP_MSI_EN_DEV0_F6__SHIFT                                                     0x12
#define RCC_DEV0_EPF6_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV0_F6__SHIFT                                         0x13
#define RCC_DEV0_EPF6_STRAP3__STRAP_MSIX_EN_DEV0_F6__SHIFT                                                    0x14
#define RCC_DEV0_EPF6_STRAP3__STRAP_PMC_DSI_DEV0_F6__SHIFT                                                    0x18
#define RCC_DEV0_EPF6_STRAP3__STRAP_VENDOR_ID_BIT_DEV0_F6__SHIFT                                              0x19
#define RCC_DEV0_EPF6_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV0_F6__SHIFT                                   0x1a
#define RCC_DEV0_EPF6_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV0_F6__SHIFT                                  0x1b
//RCC_DEV0_EPF6_STRAP4
#define RCC_DEV0_EPF6_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV0_F6__SHIFT                                            0x14
#define RCC_DEV0_EPF6_STRAP4__STRAP_ATOMIC_EN_DEV0_F6__SHIFT                                                  0x15
#define RCC_DEV0_EPF6_STRAP4__STRAP_FLR_EN_DEV0_F6__SHIFT                                                     0x16
#define RCC_DEV0_EPF6_STRAP4__STRAP_PME_SUPPORT_DEV0_F6__SHIFT                                                0x17
#define RCC_DEV0_EPF6_STRAP4__STRAP_INTERRUPT_PIN_DEV0_F6__SHIFT                                              0x1c
#define RCC_DEV0_EPF6_STRAP4__STRAP_AUXPWR_SUPPORT_DEV0_F6__SHIFT                                             0x1f
//RCC_DEV0_EPF6_STRAP5
#define RCC_DEV0_EPF6_STRAP5__STRAP_SUBSYS_VEN_ID_DEV0_F6__SHIFT                                              0x0
//RCC_DEV0_EPF6_STRAP6
#define RCC_DEV0_EPF6_STRAP6__STRAP_APER0_EN_DEV0_F6__SHIFT                                                   0x0
#define RCC_DEV0_EPF6_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV0_F6__SHIFT                                      0x1
#define RCC_DEV0_EPF6_STRAP6__STRAP_APER0_AP_SIZE_DEV0_F6__SHIFT                                              0x4
#define RCC_DEV0_EPF6_STRAP6__STRAP_APER1_EN_DEV0_F6__SHIFT                                                   0x8
#define RCC_DEV0_EPF6_STRAP6__STRAP_APER1_PREFETCHABLE_EN_DEV0_F6__SHIFT                                      0x9
#define RCC_DEV0_EPF6_STRAP6__STRAP_APER2_EN_DEV0_F6__SHIFT                                                   0x10
#define RCC_DEV0_EPF6_STRAP6__STRAP_APER2_PREFETCHABLE_EN_DEV0_F6__SHIFT                                      0x11
//RCC_DEV0_EPF6_STRAP13
#define RCC_DEV0_EPF6_STRAP13__STRAP_CLASS_CODE_PIF_DEV0_F6__SHIFT                                            0x0
#define RCC_DEV0_EPF6_STRAP13__STRAP_CLASS_CODE_SUB_DEV0_F6__SHIFT                                            0x8
#define RCC_DEV0_EPF6_STRAP13__STRAP_CLASS_CODE_BASE_DEV0_F6__SHIFT                                           0x10
//RCC_DEV0_EPF7_STRAP0
#define RCC_DEV0_EPF7_STRAP0__STRAP_DEVICE_ID_DEV0_F7__SHIFT                                                  0x0
#define RCC_DEV0_EPF7_STRAP0__STRAP_MAJOR_REV_ID_DEV0_F7__SHIFT                                               0x10
#define RCC_DEV0_EPF7_STRAP0__STRAP_MINOR_REV_ID_DEV0_F7__SHIFT                                               0x14
#define RCC_DEV0_EPF7_STRAP0__STRAP_FUNC_EN_DEV0_F7__SHIFT                                                    0x1c
#define RCC_DEV0_EPF7_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV0_F7__SHIFT                                      0x1d
#define RCC_DEV0_EPF7_STRAP0__STRAP_D1_SUPPORT_DEV0_F7__SHIFT                                                 0x1e
#define RCC_DEV0_EPF7_STRAP0__STRAP_D2_SUPPORT_DEV0_F7__SHIFT                                                 0x1f
//RCC_DEV0_EPF7_STRAP2
#define RCC_DEV0_EPF7_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F7__SHIFT                                              0x7
#define RCC_DEV0_EPF7_STRAP2__STRAP_RESIZE_BAR_EN_DEV0_F7__SHIFT                                              0x8
#define RCC_DEV0_EPF7_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV0_F7__SHIFT                                     0xe
#define RCC_DEV0_EPF7_STRAP2__STRAP_AER_EN_DEV0_F7__SHIFT                                                     0x10
#define RCC_DEV0_EPF7_STRAP2__STRAP_ACS_EN_DEV0_F7__SHIFT                                                     0x11
#define RCC_DEV0_EPF7_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV0_F7__SHIFT                                           0x14
#define RCC_DEV0_EPF7_STRAP2__STRAP_DPA_EN_DEV0_F7__SHIFT                                                     0x15
#define RCC_DEV0_EPF7_STRAP2__STRAP_VC_EN_DEV0_F7__SHIFT                                                      0x17
#define RCC_DEV0_EPF7_STRAP2__STRAP_MSI_MULTI_CAP_DEV0_F7__SHIFT                                              0x18
//RCC_DEV0_EPF7_STRAP3
#define RCC_DEV0_EPF7_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV0_F7__SHIFT                                 0x0
#define RCC_DEV0_EPF7_STRAP3__STRAP_PWR_EN_DEV0_F7__SHIFT                                                     0x1
#define RCC_DEV0_EPF7_STRAP3__STRAP_SUBSYS_ID_DEV0_F7__SHIFT                                                  0x2
#define RCC_DEV0_EPF7_STRAP3__STRAP_MSI_EN_DEV0_F7__SHIFT                                                     0x12
#define RCC_DEV0_EPF7_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV0_F7__SHIFT                                         0x13
#define RCC_DEV0_EPF7_STRAP3__STRAP_MSIX_EN_DEV0_F7__SHIFT                                                    0x14
#define RCC_DEV0_EPF7_STRAP3__STRAP_PMC_DSI_DEV0_F7__SHIFT                                                    0x18
#define RCC_DEV0_EPF7_STRAP3__STRAP_VENDOR_ID_BIT_DEV0_F7__SHIFT                                              0x19
#define RCC_DEV0_EPF7_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV0_F7__SHIFT                                   0x1a
#define RCC_DEV0_EPF7_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV0_F7__SHIFT                                  0x1b
//RCC_DEV0_EPF7_STRAP4
#define RCC_DEV0_EPF7_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV0_F7__SHIFT                                            0x14
#define RCC_DEV0_EPF7_STRAP4__STRAP_ATOMIC_EN_DEV0_F7__SHIFT                                                  0x15
#define RCC_DEV0_EPF7_STRAP4__STRAP_FLR_EN_DEV0_F7__SHIFT                                                     0x16
#define RCC_DEV0_EPF7_STRAP4__STRAP_PME_SUPPORT_DEV0_F7__SHIFT                                                0x17
#define RCC_DEV0_EPF7_STRAP4__STRAP_INTERRUPT_PIN_DEV0_F7__SHIFT                                              0x1c
#define RCC_DEV0_EPF7_STRAP4__STRAP_AUXPWR_SUPPORT_DEV0_F7__SHIFT                                             0x1f
//RCC_DEV0_EPF7_STRAP5
#define RCC_DEV0_EPF7_STRAP5__STRAP_SUBSYS_VEN_ID_DEV0_F7__SHIFT                                              0x0
//RCC_DEV0_EPF7_STRAP6
#define RCC_DEV0_EPF7_STRAP6__STRAP_APER0_EN_DEV0_F7__SHIFT                                                   0x0
#define RCC_DEV0_EPF7_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV0_F7__SHIFT                                      0x1
#define RCC_DEV0_EPF7_STRAP6__STRAP_APER0_AP_SIZE_DEV0_F7__SHIFT                                              0x4
#define RCC_DEV0_EPF7_STRAP6__STRAP_APER1_EN_DEV0_F7__SHIFT                                                   0x8
#define RCC_DEV0_EPF7_STRAP6__STRAP_APER1_PREFETCHABLE_EN_DEV0_F7__SHIFT                                      0x9
#define RCC_DEV0_EPF7_STRAP6__STRAP_APER2_EN_DEV0_F7__SHIFT                                                   0x10
#define RCC_DEV0_EPF7_STRAP6__STRAP_APER2_PREFETCHABLE_EN_DEV0_F7__SHIFT                                      0x11
//RCC_DEV0_EPF7_STRAP13
#define RCC_DEV0_EPF7_STRAP13__STRAP_CLASS_CODE_PIF_DEV0_F7__SHIFT                                            0x0
#define RCC_DEV0_EPF7_STRAP13__STRAP_CLASS_CODE_SUB_DEV0_F7__SHIFT                                            0x8
#define RCC_DEV0_EPF7_STRAP13__STRAP_CLASS_CODE_BASE_DEV0_F7__SHIFT                                           0x10
//RCC_DEV1_EPF0_STRAP0
#define RCC_DEV1_EPF0_STRAP0__STRAP_DEVICE_ID_DEV1_F0__SHIFT                                                  0x0
#define RCC_DEV1_EPF0_STRAP0__STRAP_MAJOR_REV_ID_DEV1_F0__SHIFT                                               0x10
#define RCC_DEV1_EPF0_STRAP0__STRAP_MINOR_REV_ID_DEV1_F0__SHIFT                                               0x14
#define RCC_DEV1_EPF0_STRAP0__STRAP_FUNC_EN_DEV1_F0__SHIFT                                                    0x1c
#define RCC_DEV1_EPF0_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV1_F0__SHIFT                                      0x1d
#define RCC_DEV1_EPF0_STRAP0__STRAP_D1_SUPPORT_DEV1_F0__SHIFT                                                 0x1e
#define RCC_DEV1_EPF0_STRAP0__STRAP_D2_SUPPORT_DEV1_F0__SHIFT                                                 0x1f
//RCC_DEV1_EPF0_STRAP2
#define RCC_DEV1_EPF0_STRAP2__STRAP_NO_SOFT_RESET_DEV1_F0__SHIFT                                              0x7
#define RCC_DEV1_EPF0_STRAP2__STRAP_RESIZE_BAR_EN_DEV1_F0__SHIFT                                              0x8
#define RCC_DEV1_EPF0_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV1_F0__SHIFT                                     0xe
#define RCC_DEV1_EPF0_STRAP2__STRAP_ARI_EN_DEV1_F0__SHIFT                                                     0xf
#define RCC_DEV1_EPF0_STRAP2__STRAP_AER_EN_DEV1_F0__SHIFT                                                     0x10
#define RCC_DEV1_EPF0_STRAP2__STRAP_ACS_EN_DEV1_F0__SHIFT                                                     0x11
#define RCC_DEV1_EPF0_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV1_F0__SHIFT                                           0x14
#define RCC_DEV1_EPF0_STRAP2__STRAP_DPA_EN_DEV1_F0__SHIFT                                                     0x15
#define RCC_DEV1_EPF0_STRAP2__STRAP_VC_EN_DEV1_F0__SHIFT                                                      0x17
#define RCC_DEV1_EPF0_STRAP2__STRAP_MSI_MULTI_CAP_DEV1_F0__SHIFT                                              0x18
//RCC_DEV1_EPF0_STRAP3
#define RCC_DEV1_EPF0_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV1_F0__SHIFT                                 0x0
#define RCC_DEV1_EPF0_STRAP3__STRAP_PWR_EN_DEV1_F0__SHIFT                                                     0x1
#define RCC_DEV1_EPF0_STRAP3__STRAP_SUBSYS_ID_DEV1_F0__SHIFT                                                  0x2
#define RCC_DEV1_EPF0_STRAP3__STRAP_MSI_EN_DEV1_F0__SHIFT                                                     0x12
#define RCC_DEV1_EPF0_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV1_F0__SHIFT                                         0x13
#define RCC_DEV1_EPF0_STRAP3__STRAP_MSIX_EN_DEV1_F0__SHIFT                                                    0x14
#define RCC_DEV1_EPF0_STRAP3__STRAP_PMC_DSI_DEV1_F0__SHIFT                                                    0x18
#define RCC_DEV1_EPF0_STRAP3__STRAP_VENDOR_ID_BIT_DEV1_F0__SHIFT                                              0x19
#define RCC_DEV1_EPF0_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV1_F0__SHIFT                                   0x1a
#define RCC_DEV1_EPF0_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV1_F0__SHIFT                                  0x1b
//RCC_DEV1_EPF0_STRAP4
#define RCC_DEV1_EPF0_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV1_F0__SHIFT                                            0x14
#define RCC_DEV1_EPF0_STRAP4__STRAP_ATOMIC_EN_DEV1_F0__SHIFT                                                  0x15
#define RCC_DEV1_EPF0_STRAP4__STRAP_FLR_EN_DEV1_F0__SHIFT                                                     0x16
#define RCC_DEV1_EPF0_STRAP4__STRAP_PME_SUPPORT_DEV1_F0__SHIFT                                                0x17
#define RCC_DEV1_EPF0_STRAP4__STRAP_INTERRUPT_PIN_DEV1_F0__SHIFT                                              0x1c
#define RCC_DEV1_EPF0_STRAP4__STRAP_AUXPWR_SUPPORT_DEV1_F0__SHIFT                                             0x1f
//RCC_DEV1_EPF0_STRAP5
#define RCC_DEV1_EPF0_STRAP5__STRAP_SUBSYS_VEN_ID_DEV1_F0__SHIFT                                              0x0
#define RCC_DEV1_EPF0_STRAP5__STRAP_SATAIDP_EN_DEV1_F0__SHIFT                                                 0x18
//RCC_DEV1_EPF0_STRAP6
#define RCC_DEV1_EPF0_STRAP6__STRAP_APER0_EN_DEV1_F0__SHIFT                                                   0x0
#define RCC_DEV1_EPF0_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV1_F0__SHIFT                                      0x1
#define RCC_DEV1_EPF0_STRAP6__STRAP_APER0_AP_SIZE_DEV1_F0__SHIFT                                              0x4
//RCC_DEV1_EPF0_STRAP13
#define RCC_DEV1_EPF0_STRAP13__STRAP_CLASS_CODE_PIF_DEV1_F0__SHIFT                                            0x0
#define RCC_DEV1_EPF0_STRAP13__STRAP_CLASS_CODE_SUB_DEV1_F0__SHIFT                                            0x8
#define RCC_DEV1_EPF0_STRAP13__STRAP_CLASS_CODE_BASE_DEV1_F0__SHIFT                                           0x10
//RCC_DEV1_EPF1_STRAP0
#define RCC_DEV1_EPF1_STRAP0__STRAP_DEVICE_ID_DEV1_F1__SHIFT                                                  0x0
#define RCC_DEV1_EPF1_STRAP0__STRAP_MAJOR_REV_ID_DEV1_F1__SHIFT                                               0x10
#define RCC_DEV1_EPF1_STRAP0__STRAP_MINOR_REV_ID_DEV1_F1__SHIFT                                               0x14
#define RCC_DEV1_EPF1_STRAP0__STRAP_FUNC_EN_DEV1_F1__SHIFT                                                    0x1c
#define RCC_DEV1_EPF1_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV1_F1__SHIFT                                      0x1d
#define RCC_DEV1_EPF1_STRAP0__STRAP_D1_SUPPORT_DEV1_F1__SHIFT                                                 0x1e
#define RCC_DEV1_EPF1_STRAP0__STRAP_D2_SUPPORT_DEV1_F1__SHIFT                                                 0x1f
//RCC_DEV1_EPF1_STRAP2
#define RCC_DEV1_EPF1_STRAP2__STRAP_NO_SOFT_RESET_DEV1_F1__SHIFT                                              0x7
#define RCC_DEV1_EPF1_STRAP2__STRAP_RESIZE_BAR_EN_DEV1_F1__SHIFT                                              0x8
#define RCC_DEV1_EPF1_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV1_F1__SHIFT                                     0xe
#define RCC_DEV1_EPF1_STRAP2__STRAP_AER_EN_DEV1_F1__SHIFT                                                     0x10
#define RCC_DEV1_EPF1_STRAP2__STRAP_ACS_EN_DEV1_F1__SHIFT                                                     0x11
#define RCC_DEV1_EPF1_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV1_F1__SHIFT                                           0x14
#define RCC_DEV1_EPF1_STRAP2__STRAP_DPA_EN_DEV1_F1__SHIFT                                                     0x15
#define RCC_DEV1_EPF1_STRAP2__STRAP_VC_EN_DEV1_F1__SHIFT                                                      0x17
#define RCC_DEV1_EPF1_STRAP2__STRAP_MSI_MULTI_CAP_DEV1_F1__SHIFT                                              0x18
//RCC_DEV1_EPF1_STRAP3
#define RCC_DEV1_EPF1_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV1_F1__SHIFT                                 0x0
#define RCC_DEV1_EPF1_STRAP3__STRAP_PWR_EN_DEV1_F1__SHIFT                                                     0x1
#define RCC_DEV1_EPF1_STRAP3__STRAP_SUBSYS_ID_DEV1_F1__SHIFT                                                  0x2
#define RCC_DEV1_EPF1_STRAP3__STRAP_MSI_EN_DEV1_F1__SHIFT                                                     0x12
#define RCC_DEV1_EPF1_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV1_F1__SHIFT                                         0x13
#define RCC_DEV1_EPF1_STRAP3__STRAP_MSIX_EN_DEV1_F1__SHIFT                                                    0x14
#define RCC_DEV1_EPF1_STRAP3__STRAP_PMC_DSI_DEV1_F1__SHIFT                                                    0x18
#define RCC_DEV1_EPF1_STRAP3__STRAP_VENDOR_ID_BIT_DEV1_F1__SHIFT                                              0x19
#define RCC_DEV1_EPF1_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV1_F1__SHIFT                                   0x1a
#define RCC_DEV1_EPF1_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV1_F1__SHIFT                                  0x1b
//RCC_DEV1_EPF1_STRAP4
#define RCC_DEV1_EPF1_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV1_F1__SHIFT                                            0x14
#define RCC_DEV1_EPF1_STRAP4__STRAP_ATOMIC_EN_DEV1_F1__SHIFT                                                  0x15
#define RCC_DEV1_EPF1_STRAP4__STRAP_FLR_EN_DEV1_F1__SHIFT                                                     0x16
#define RCC_DEV1_EPF1_STRAP4__STRAP_PME_SUPPORT_DEV1_F1__SHIFT                                                0x17
#define RCC_DEV1_EPF1_STRAP4__STRAP_INTERRUPT_PIN_DEV1_F1__SHIFT                                              0x1c
#define RCC_DEV1_EPF1_STRAP4__STRAP_AUXPWR_SUPPORT_DEV1_F1__SHIFT                                             0x1f
//RCC_DEV1_EPF1_STRAP5
#define RCC_DEV1_EPF1_STRAP5__STRAP_SUBSYS_VEN_ID_DEV1_F1__SHIFT                                              0x0
//RCC_DEV1_EPF1_STRAP6
#define RCC_DEV1_EPF1_STRAP6__STRAP_APER0_EN_DEV1_F1__SHIFT                                                   0x0
#define RCC_DEV1_EPF1_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV1_F1__SHIFT                                      0x1
#define RCC_DEV1_EPF1_STRAP6__STRAP_APER0_AP_SIZE_DEV1_F1__SHIFT                                              0x4
#define RCC_DEV1_EPF1_STRAP6__STRAP_APER1_EN_DEV1_F1__SHIFT                                                   0x8
#define RCC_DEV1_EPF1_STRAP6__STRAP_APER1_PREFETCHABLE_EN_DEV1_F1__SHIFT                                      0x9
#define RCC_DEV1_EPF1_STRAP6__STRAP_APER2_EN_DEV1_F1__SHIFT                                                   0x10
#define RCC_DEV1_EPF1_STRAP6__STRAP_APER2_PREFETCHABLE_EN_DEV1_F1__SHIFT                                      0x11
#define RCC_DEV1_EPF1_STRAP6__STRAP_APER3_EN_DEV1_F1__SHIFT                                                   0x18
#define RCC_DEV1_EPF1_STRAP6__STRAP_APER3_PREFETCHABLE_EN_DEV1_F1__SHIFT                                      0x19
//RCC_DEV1_EPF1_STRAP13
#define RCC_DEV1_EPF1_STRAP13__STRAP_CLASS_CODE_PIF_DEV1_F1__SHIFT                                            0x0
#define RCC_DEV1_EPF1_STRAP13__STRAP_CLASS_CODE_SUB_DEV1_F1__SHIFT                                            0x8
#define RCC_DEV1_EPF1_STRAP13__STRAP_CLASS_CODE_BASE_DEV1_F1__SHIFT                                           0x10
//RCC_DEV1_EPF2_STRAP0
#define RCC_DEV1_EPF2_STRAP0__STRAP_DEVICE_ID_DEV1_F2__SHIFT                                                  0x0
#define RCC_DEV1_EPF2_STRAP0__STRAP_MAJOR_REV_ID_DEV1_F2__SHIFT                                               0x10
#define RCC_DEV1_EPF2_STRAP0__STRAP_MINOR_REV_ID_DEV1_F2__SHIFT                                               0x14
#define RCC_DEV1_EPF2_STRAP0__STRAP_FUNC_EN_DEV1_F2__SHIFT                                                    0x1c
#define RCC_DEV1_EPF2_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV1_F2__SHIFT                                      0x1d
#define RCC_DEV1_EPF2_STRAP0__STRAP_D1_SUPPORT_DEV1_F2__SHIFT                                                 0x1e
#define RCC_DEV1_EPF2_STRAP0__STRAP_D2_SUPPORT_DEV1_F2__SHIFT                                                 0x1f
//RCC_DEV1_EPF2_STRAP2
#define RCC_DEV1_EPF2_STRAP2__STRAP_NO_SOFT_RESET_DEV1_F2__SHIFT                                              0x7
#define RCC_DEV1_EPF2_STRAP2__STRAP_RESIZE_BAR_EN_DEV1_F2__SHIFT                                              0x8
#define RCC_DEV1_EPF2_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV1_F2__SHIFT                                     0xe
#define RCC_DEV1_EPF2_STRAP2__STRAP_AER_EN_DEV1_F2__SHIFT                                                     0x10
#define RCC_DEV1_EPF2_STRAP2__STRAP_ACS_EN_DEV1_F2__SHIFT                                                     0x11
#define RCC_DEV1_EPF2_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV1_F2__SHIFT                                           0x14
#define RCC_DEV1_EPF2_STRAP2__STRAP_DPA_EN_DEV1_F2__SHIFT                                                     0x15
#define RCC_DEV1_EPF2_STRAP2__STRAP_VC_EN_DEV1_F2__SHIFT                                                      0x17
#define RCC_DEV1_EPF2_STRAP2__STRAP_MSI_MULTI_CAP_DEV1_F2__SHIFT                                              0x18
//RCC_DEV1_EPF2_STRAP3
#define RCC_DEV1_EPF2_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV1_F2__SHIFT                                 0x0
#define RCC_DEV1_EPF2_STRAP3__STRAP_PWR_EN_DEV1_F2__SHIFT                                                     0x1
#define RCC_DEV1_EPF2_STRAP3__STRAP_SUBSYS_ID_DEV1_F2__SHIFT                                                  0x2
#define RCC_DEV1_EPF2_STRAP3__STRAP_MSI_EN_DEV1_F2__SHIFT                                                     0x12
#define RCC_DEV1_EPF2_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV1_F2__SHIFT                                         0x13
#define RCC_DEV1_EPF2_STRAP3__STRAP_MSIX_EN_DEV1_F2__SHIFT                                                    0x14
#define RCC_DEV1_EPF2_STRAP3__STRAP_PMC_DSI_DEV1_F2__SHIFT                                                    0x18
#define RCC_DEV1_EPF2_STRAP3__STRAP_VENDOR_ID_BIT_DEV1_F2__SHIFT                                              0x19
#define RCC_DEV1_EPF2_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV1_F2__SHIFT                                   0x1a
#define RCC_DEV1_EPF2_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV1_F2__SHIFT                                  0x1b
//RCC_DEV1_EPF2_STRAP4
#define RCC_DEV1_EPF2_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV1_F2__SHIFT                                            0x14
#define RCC_DEV1_EPF2_STRAP4__STRAP_ATOMIC_EN_DEV1_F2__SHIFT                                                  0x15
#define RCC_DEV1_EPF2_STRAP4__STRAP_FLR_EN_DEV1_F2__SHIFT                                                     0x16
#define RCC_DEV1_EPF2_STRAP4__STRAP_PME_SUPPORT_DEV1_F2__SHIFT                                                0x17
#define RCC_DEV1_EPF2_STRAP4__STRAP_INTERRUPT_PIN_DEV1_F2__SHIFT                                              0x1c
#define RCC_DEV1_EPF2_STRAP4__STRAP_AUXPWR_SUPPORT_DEV1_F2__SHIFT                                             0x1f
//RCC_DEV1_EPF2_STRAP5
#define RCC_DEV1_EPF2_STRAP5__STRAP_SUBSYS_VEN_ID_DEV1_F2__SHIFT                                              0x0
//RCC_DEV1_EPF2_STRAP6
#define RCC_DEV1_EPF2_STRAP6__STRAP_APER0_EN_DEV1_F2__SHIFT                                                   0x0
#define RCC_DEV1_EPF2_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV1_F2__SHIFT                                      0x1
#define RCC_DEV1_EPF2_STRAP6__STRAP_APER0_AP_SIZE_DEV1_F2__SHIFT                                              0x4
#define RCC_DEV1_EPF2_STRAP6__STRAP_APER1_EN_DEV1_F2__SHIFT                                                   0x8
#define RCC_DEV1_EPF2_STRAP6__STRAP_APER1_PREFETCHABLE_EN_DEV1_F2__SHIFT                                      0x9
#define RCC_DEV1_EPF2_STRAP6__STRAP_APER2_EN_DEV1_F2__SHIFT                                                   0x10
#define RCC_DEV1_EPF2_STRAP6__STRAP_APER2_PREFETCHABLE_EN_DEV1_F2__SHIFT                                      0x11
#define RCC_DEV1_EPF2_STRAP6__STRAP_APER3_EN_DEV1_F2__SHIFT                                                   0x18
#define RCC_DEV1_EPF2_STRAP6__STRAP_APER3_PREFETCHABLE_EN_DEV1_F2__SHIFT                                      0x19
//RCC_DEV1_EPF2_STRAP13
#define RCC_DEV1_EPF2_STRAP13__STRAP_CLASS_CODE_PIF_DEV1_F2__SHIFT                                            0x0
#define RCC_DEV1_EPF2_STRAP13__STRAP_CLASS_CODE_SUB_DEV1_F2__SHIFT                                            0x8
#define RCC_DEV1_EPF2_STRAP13__STRAP_CLASS_CODE_BASE_DEV1_F2__SHIFT                                           0x10


// addressBlock: bif_rst_bif_rst_regblk
//HARD_RST_CTRL
#define HARD_RST_CTRL__DSPT_CFG_RST_EN__SHIFT                                                                 0x0
#define HARD_RST_CTRL__DSPT_CFG_STICKY_RST_EN__SHIFT                                                          0x1
#define HARD_RST_CTRL__DSPT_PRV_RST_EN__SHIFT                                                                 0x2
#define HARD_RST_CTRL__DSPT_PRV_STICKY_RST_EN__SHIFT                                                          0x3
#define HARD_RST_CTRL__EP_CFG_RST_EN__SHIFT                                                                   0x4
#define HARD_RST_CTRL__EP_CFG_STICKY_RST_EN__SHIFT                                                            0x5
#define HARD_RST_CTRL__EP_PRV_RST_EN__SHIFT                                                                   0x6
#define HARD_RST_CTRL__EP_PRV_STICKY_RST_EN__SHIFT                                                            0x7
#define HARD_RST_CTRL__SWUS_SHADOW_RST_EN__SHIFT                                                              0x1c
#define HARD_RST_CTRL__CORE_STICKY_RST_EN__SHIFT                                                              0x1d
#define HARD_RST_CTRL__RELOAD_STRAP_EN__SHIFT                                                                 0x1e
#define HARD_RST_CTRL__CORE_RST_EN__SHIFT                                                                     0x1f
//RSMU_SOFT_RST_CTRL
#define RSMU_SOFT_RST_CTRL__DSPT_CFG_RST_EN__SHIFT                                                            0x0
#define RSMU_SOFT_RST_CTRL__DSPT_CFG_STICKY_RST_EN__SHIFT                                                     0x1
#define RSMU_SOFT_RST_CTRL__DSPT_PRV_RST_EN__SHIFT                                                            0x2
#define RSMU_SOFT_RST_CTRL__DSPT_PRV_STICKY_RST_EN__SHIFT                                                     0x3
#define RSMU_SOFT_RST_CTRL__EP_CFG_RST_EN__SHIFT                                                              0x4
#define RSMU_SOFT_RST_CTRL__EP_CFG_STICKY_RST_EN__SHIFT                                                       0x5
#define RSMU_SOFT_RST_CTRL__EP_PRV_RST_EN__SHIFT                                                              0x6
#define RSMU_SOFT_RST_CTRL__EP_PRV_STICKY_RST_EN__SHIFT                                                       0x7
#define RSMU_SOFT_RST_CTRL__SWUS_SHADOW_RST_EN__SHIFT                                                         0x1c
#define RSMU_SOFT_RST_CTRL__CORE_STICKY_RST_EN__SHIFT                                                         0x1d
#define RSMU_SOFT_RST_CTRL__RELOAD_STRAP_EN__SHIFT                                                            0x1e
#define RSMU_SOFT_RST_CTRL__CORE_RST_EN__SHIFT                                                                0x1f
//SELF_SOFT_RST
#define SELF_SOFT_RST__DSPT0_CFG_RST__SHIFT                                                                   0x0
#define SELF_SOFT_RST__DSPT0_CFG_STICKY_RST__SHIFT                                                            0x1
#define SELF_SOFT_RST__DSPT0_PRV_RST__SHIFT                                                                   0x2
#define SELF_SOFT_RST__DSPT0_PRV_STICKY_RST__SHIFT                                                            0x3
#define SELF_SOFT_RST__EP0_CFG_RST__SHIFT                                                                     0x4
#define SELF_SOFT_RST__EP0_CFG_STICKY_RST__SHIFT                                                              0x5
#define SELF_SOFT_RST__EP0_PRV_RST__SHIFT                                                                     0x6
#define SELF_SOFT_RST__EP0_PRV_STICKY_RST__SHIFT                                                              0x7
#define SELF_SOFT_RST__SDP_PORT_RST__SHIFT                                                                    0x1b
#define SELF_SOFT_RST__SWUS_SHADOW_RST__SHIFT                                                                 0x1c
#define SELF_SOFT_RST__CORE_STICKY_RST__SHIFT                                                                 0x1d
#define SELF_SOFT_RST__RELOAD_STRAP__SHIFT                                                                    0x1e
#define SELF_SOFT_RST__CORE_RST__SHIFT                                                                        0x1f
//GFX_DRV_MODE1_RST_CTRL
#define GFX_DRV_MODE1_RST_CTRL__DRV_MODE1_PF_CFG_RST__SHIFT                                                   0x0
#define GFX_DRV_MODE1_RST_CTRL__DRV_MODE1_PF_CFG_FLR_EXC_RST__SHIFT                                           0x1
#define GFX_DRV_MODE1_RST_CTRL__DRV_MODE1_PF_CFG_STICKY_RST__SHIFT                                            0x2
#define GFX_DRV_MODE1_RST_CTRL__DRV_MODE1_PF_PRV_RST__SHIFT                                                   0x3
#define GFX_DRV_MODE1_RST_CTRL__DRV_MODE1_PF_PRV_STICKY_RST__SHIFT                                            0x4
#define GFX_DRV_MODE1_RST_CTRL__DRV_MODE1_VF_CFG_RST__SHIFT                                                   0x5
#define GFX_DRV_MODE1_RST_CTRL__DRV_MODE1_VF_CFG_STICKY_RST__SHIFT                                            0x6
#define GFX_DRV_MODE1_RST_CTRL__DRV_MODE1_VF_PRV_RST__SHIFT                                                   0x7
//BIF_RST_MISC_CTRL
#define BIF_RST_MISC_CTRL__ERRSTATUS_KEPT_IN_PERSTB__SHIFT                                                    0x0
#define BIF_RST_MISC_CTRL__DRV_RST_MODE__SHIFT                                                                0x2
#define BIF_RST_MISC_CTRL__DRV_RST_CFG_MASK__SHIFT                                                            0x4
#define BIF_RST_MISC_CTRL__DRV_RST_BITS_AUTO_CLEAR__SHIFT                                                     0x5
#define BIF_RST_MISC_CTRL__FLR_RST_BIT_AUTO_CLEAR__SHIFT                                                      0x6
#define BIF_RST_MISC_CTRL__STRAP_EP_LNK_RST_IOV_EN__SHIFT                                                     0x8
#define BIF_RST_MISC_CTRL__LNK_RST_GRACE_MODE__SHIFT                                                          0x9
#define BIF_RST_MISC_CTRL__LNK_RST_GRACE_TIMEOUT__SHIFT                                                       0xa
#define BIF_RST_MISC_CTRL__LNK_RST_TIMER_SEL__SHIFT                                                           0xd
#define BIF_RST_MISC_CTRL__LNK_RST_TIMER2_SEL__SHIFT                                                          0xf
#define BIF_RST_MISC_CTRL__SRIOV_SAVE_VFS_ON_VFENABLE_CLR__SHIFT                                              0x11
#define BIF_RST_MISC_CTRL__LNK_RST_DMA_DUMMY_DIS__SHIFT                                                       0x17
#define BIF_RST_MISC_CTRL__LNK_RST_DMA_DUMMY_RSPSTS__SHIFT                                                    0x18
//BIF_RST_MISC_CTRL2
#define BIF_RST_MISC_CTRL2__SWUS_LNK_RST_TRANS_IDLE__SHIFT                                                    0x10
#define BIF_RST_MISC_CTRL2__SWDS_LNK_RST_TRANS_IDLE__SHIFT                                                    0x11
#define BIF_RST_MISC_CTRL2__ENDP0_LNK_RST_TRANS_IDLE__SHIFT                                                   0x12
#define BIF_RST_MISC_CTRL2__ALL_RST_TRANS_IDLE__SHIFT                                                         0x1f
//BIF_RST_MISC_CTRL3
#define BIF_RST_MISC_CTRL3__TIMER_SCALE__SHIFT                                                                0x0
#define BIF_RST_MISC_CTRL3__PME_TURNOFF_TIMEOUT__SHIFT                                                        0x4
#define BIF_RST_MISC_CTRL3__PME_TURNOFF_MODE__SHIFT                                                           0x6
#define BIF_RST_MISC_CTRL3__RELOAD_STRAP_DELAY_HARD__SHIFT                                                    0x7
#define BIF_RST_MISC_CTRL3__RELOAD_STRAP_DELAY_SOFT__SHIFT                                                    0xa
#define BIF_RST_MISC_CTRL3__RELOAD_STRAP_DELAY_SELF__SHIFT                                                    0xd
//BIF_RST_GFXVF_FLR_IDLE
#define BIF_RST_GFXVF_FLR_IDLE__VF0_TRANS_IDLE__SHIFT                                                         0x0
#define BIF_RST_GFXVF_FLR_IDLE__VF1_TRANS_IDLE__SHIFT                                                         0x1
#define BIF_RST_GFXVF_FLR_IDLE__VF2_TRANS_IDLE__SHIFT                                                         0x2
#define BIF_RST_GFXVF_FLR_IDLE__VF3_TRANS_IDLE__SHIFT                                                         0x3
#define BIF_RST_GFXVF_FLR_IDLE__VF4_TRANS_IDLE__SHIFT                                                         0x4
#define BIF_RST_GFXVF_FLR_IDLE__VF5_TRANS_IDLE__SHIFT                                                         0x5
#define BIF_RST_GFXVF_FLR_IDLE__VF6_TRANS_IDLE__SHIFT                                                         0x6
#define BIF_RST_GFXVF_FLR_IDLE__VF7_TRANS_IDLE__SHIFT                                                         0x7
#define BIF_RST_GFXVF_FLR_IDLE__VF8_TRANS_IDLE__SHIFT                                                         0x8
#define BIF_RST_GFXVF_FLR_IDLE__VF9_TRANS_IDLE__SHIFT                                                         0x9
#define BIF_RST_GFXVF_FLR_IDLE__VF10_TRANS_IDLE__SHIFT                                                        0xa
#define BIF_RST_GFXVF_FLR_IDLE__VF11_TRANS_IDLE__SHIFT                                                        0xb
#define BIF_RST_GFXVF_FLR_IDLE__VF12_TRANS_IDLE__SHIFT                                                        0xc
#define BIF_RST_GFXVF_FLR_IDLE__VF13_TRANS_IDLE__SHIFT                                                        0xd
#define BIF_RST_GFXVF_FLR_IDLE__VF14_TRANS_IDLE__SHIFT                                                        0xe
#define BIF_RST_GFXVF_FLR_IDLE__VF15_TRANS_IDLE__SHIFT                                                        0xf
#define BIF_RST_GFXVF_FLR_IDLE__SOFTPF_TRANS_IDLE__SHIFT                                                      0x1f
//DEV0_PF0_FLR_RST_CTRL
#define DEV0_PF0_FLR_RST_CTRL__PF_CFG_EN__SHIFT                                                               0x0
#define DEV0_PF0_FLR_RST_CTRL__PF_CFG_FLR_EXC_EN__SHIFT                                                       0x1
#define DEV0_PF0_FLR_RST_CTRL__PF_CFG_STICKY_EN__SHIFT                                                        0x2
#define DEV0_PF0_FLR_RST_CTRL__PF_PRV_EN__SHIFT                                                               0x3
#define DEV0_PF0_FLR_RST_CTRL__PF_PRV_STICKY_EN__SHIFT                                                        0x4
#define DEV0_PF0_FLR_RST_CTRL__VF_CFG_EN__SHIFT                                                               0x5
#define DEV0_PF0_FLR_RST_CTRL__VF_CFG_STICKY_EN__SHIFT                                                        0x6
#define DEV0_PF0_FLR_RST_CTRL__VF_PRV_EN__SHIFT                                                               0x7
#define DEV0_PF0_FLR_RST_CTRL__SOFT_PF_CFG_EN__SHIFT                                                          0x8
#define DEV0_PF0_FLR_RST_CTRL__SOFT_PF_CFG_FLR_EXC_EN__SHIFT                                                  0x9
#define DEV0_PF0_FLR_RST_CTRL__SOFT_PF_CFG_STICKY_EN__SHIFT                                                   0xa
#define DEV0_PF0_FLR_RST_CTRL__SOFT_PF_PRV_EN__SHIFT                                                          0xb
#define DEV0_PF0_FLR_RST_CTRL__SOFT_PF_PRV_STICKY_EN__SHIFT                                                   0xc
#define DEV0_PF0_FLR_RST_CTRL__VF_VF_CFG_EN__SHIFT                                                            0xd
#define DEV0_PF0_FLR_RST_CTRL__VF_VF_CFG_STICKY_EN__SHIFT                                                     0xe
#define DEV0_PF0_FLR_RST_CTRL__VF_VF_PRV_EN__SHIFT                                                            0xf
#define DEV0_PF0_FLR_RST_CTRL__FLR_TWICE_EN__SHIFT                                                            0x10
#define DEV0_PF0_FLR_RST_CTRL__FLR_GRACE_MODE__SHIFT                                                          0x11
#define DEV0_PF0_FLR_RST_CTRL__FLR_GRACE_TIMEOUT__SHIFT                                                       0x12
#define DEV0_PF0_FLR_RST_CTRL__FLR_DMA_DUMMY_RSPSTS__SHIFT                                                    0x17
#define DEV0_PF0_FLR_RST_CTRL__FLR_HST_DUMMY_RSPSTS__SHIFT                                                    0x19
//DEV0_PF1_FLR_RST_CTRL
#define DEV0_PF1_FLR_RST_CTRL__PF_CFG_EN__SHIFT                                                               0x0
#define DEV0_PF1_FLR_RST_CTRL__PF_CFG_FLR_EXC_EN__SHIFT                                                       0x1
#define DEV0_PF1_FLR_RST_CTRL__PF_CFG_STICKY_EN__SHIFT                                                        0x2
#define DEV0_PF1_FLR_RST_CTRL__PF_PRV_EN__SHIFT                                                               0x3
#define DEV0_PF1_FLR_RST_CTRL__PF_PRV_STICKY_EN__SHIFT                                                        0x4
#define DEV0_PF1_FLR_RST_CTRL__FLR_GRACE_MODE__SHIFT                                                          0x11
#define DEV0_PF1_FLR_RST_CTRL__FLR_GRACE_TIMEOUT__SHIFT                                                       0x12
#define DEV0_PF1_FLR_RST_CTRL__FLR_DMA_DUMMY_RSPSTS__SHIFT                                                    0x17
#define DEV0_PF1_FLR_RST_CTRL__FLR_HST_DUMMY_RSPSTS__SHIFT                                                    0x19
//DEV0_PF2_FLR_RST_CTRL
#define DEV0_PF2_FLR_RST_CTRL__PF_CFG_EN__SHIFT                                                               0x0
#define DEV0_PF2_FLR_RST_CTRL__PF_CFG_FLR_EXC_EN__SHIFT                                                       0x1
#define DEV0_PF2_FLR_RST_CTRL__PF_CFG_STICKY_EN__SHIFT                                                        0x2
#define DEV0_PF2_FLR_RST_CTRL__PF_PRV_EN__SHIFT                                                               0x3
#define DEV0_PF2_FLR_RST_CTRL__PF_PRV_STICKY_EN__SHIFT                                                        0x4
#define DEV0_PF2_FLR_RST_CTRL__FLR_GRACE_MODE__SHIFT                                                          0x11
#define DEV0_PF2_FLR_RST_CTRL__FLR_GRACE_TIMEOUT__SHIFT                                                       0x12
#define DEV0_PF2_FLR_RST_CTRL__FLR_DMA_DUMMY_RSPSTS__SHIFT                                                    0x17
#define DEV0_PF2_FLR_RST_CTRL__FLR_HST_DUMMY_RSPSTS__SHIFT                                                    0x19
//DEV0_PF3_FLR_RST_CTRL
#define DEV0_PF3_FLR_RST_CTRL__PF_CFG_EN__SHIFT                                                               0x0
#define DEV0_PF3_FLR_RST_CTRL__PF_CFG_FLR_EXC_EN__SHIFT                                                       0x1
#define DEV0_PF3_FLR_RST_CTRL__PF_CFG_STICKY_EN__SHIFT                                                        0x2
#define DEV0_PF3_FLR_RST_CTRL__PF_PRV_EN__SHIFT                                                               0x3
#define DEV0_PF3_FLR_RST_CTRL__PF_PRV_STICKY_EN__SHIFT                                                        0x4
#define DEV0_PF3_FLR_RST_CTRL__FLR_GRACE_MODE__SHIFT                                                          0x11
#define DEV0_PF3_FLR_RST_CTRL__FLR_GRACE_TIMEOUT__SHIFT                                                       0x12
#define DEV0_PF3_FLR_RST_CTRL__FLR_DMA_DUMMY_RSPSTS__SHIFT                                                    0x17
#define DEV0_PF3_FLR_RST_CTRL__FLR_HST_DUMMY_RSPSTS__SHIFT                                                    0x19
//DEV0_PF4_FLR_RST_CTRL
#define DEV0_PF4_FLR_RST_CTRL__PF_CFG_EN__SHIFT                                                               0x0
#define DEV0_PF4_FLR_RST_CTRL__PF_CFG_FLR_EXC_EN__SHIFT                                                       0x1
#define DEV0_PF4_FLR_RST_CTRL__PF_CFG_STICKY_EN__SHIFT                                                        0x2
#define DEV0_PF4_FLR_RST_CTRL__PF_PRV_EN__SHIFT                                                               0x3
#define DEV0_PF4_FLR_RST_CTRL__PF_PRV_STICKY_EN__SHIFT                                                        0x4
#define DEV0_PF4_FLR_RST_CTRL__FLR_GRACE_MODE__SHIFT                                                          0x11
#define DEV0_PF4_FLR_RST_CTRL__FLR_GRACE_TIMEOUT__SHIFT                                                       0x12
#define DEV0_PF4_FLR_RST_CTRL__FLR_DMA_DUMMY_RSPSTS__SHIFT                                                    0x17
#define DEV0_PF4_FLR_RST_CTRL__FLR_HST_DUMMY_RSPSTS__SHIFT                                                    0x19
//DEV0_PF5_FLR_RST_CTRL
#define DEV0_PF5_FLR_RST_CTRL__PF_CFG_EN__SHIFT                                                               0x0
#define DEV0_PF5_FLR_RST_CTRL__PF_CFG_FLR_EXC_EN__SHIFT                                                       0x1
#define DEV0_PF5_FLR_RST_CTRL__PF_CFG_STICKY_EN__SHIFT                                                        0x2
#define DEV0_PF5_FLR_RST_CTRL__PF_PRV_EN__SHIFT                                                               0x3
#define DEV0_PF5_FLR_RST_CTRL__PF_PRV_STICKY_EN__SHIFT                                                        0x4
#define DEV0_PF5_FLR_RST_CTRL__FLR_GRACE_MODE__SHIFT                                                          0x11
#define DEV0_PF5_FLR_RST_CTRL__FLR_GRACE_TIMEOUT__SHIFT                                                       0x12
#define DEV0_PF5_FLR_RST_CTRL__FLR_DMA_DUMMY_RSPSTS__SHIFT                                                    0x17
#define DEV0_PF5_FLR_RST_CTRL__FLR_HST_DUMMY_RSPSTS__SHIFT                                                    0x19
//DEV0_PF6_FLR_RST_CTRL
#define DEV0_PF6_FLR_RST_CTRL__PF_CFG_EN__SHIFT                                                               0x0
#define DEV0_PF6_FLR_RST_CTRL__PF_CFG_FLR_EXC_EN__SHIFT                                                       0x1
#define DEV0_PF6_FLR_RST_CTRL__PF_CFG_STICKY_EN__SHIFT                                                        0x2
#define DEV0_PF6_FLR_RST_CTRL__PF_PRV_EN__SHIFT                                                               0x3
#define DEV0_PF6_FLR_RST_CTRL__PF_PRV_STICKY_EN__SHIFT                                                        0x4
#define DEV0_PF6_FLR_RST_CTRL__FLR_GRACE_MODE__SHIFT                                                          0x11
#define DEV0_PF6_FLR_RST_CTRL__FLR_GRACE_TIMEOUT__SHIFT                                                       0x12
#define DEV0_PF6_FLR_RST_CTRL__FLR_DMA_DUMMY_RSPSTS__SHIFT                                                    0x17
#define DEV0_PF6_FLR_RST_CTRL__FLR_HST_DUMMY_RSPSTS__SHIFT                                                    0x19
//DEV0_PF7_FLR_RST_CTRL
#define DEV0_PF7_FLR_RST_CTRL__PF_CFG_EN__SHIFT                                                               0x0
#define DEV0_PF7_FLR_RST_CTRL__PF_CFG_FLR_EXC_EN__SHIFT                                                       0x1
#define DEV0_PF7_FLR_RST_CTRL__PF_CFG_STICKY_EN__SHIFT                                                        0x2
#define DEV0_PF7_FLR_RST_CTRL__PF_PRV_EN__SHIFT                                                               0x3
#define DEV0_PF7_FLR_RST_CTRL__PF_PRV_STICKY_EN__SHIFT                                                        0x4
#define DEV0_PF7_FLR_RST_CTRL__FLR_GRACE_MODE__SHIFT                                                          0x11
#define DEV0_PF7_FLR_RST_CTRL__FLR_GRACE_TIMEOUT__SHIFT                                                       0x12
#define DEV0_PF7_FLR_RST_CTRL__FLR_DMA_DUMMY_RSPSTS__SHIFT                                                    0x17
#define DEV0_PF7_FLR_RST_CTRL__FLR_HST_DUMMY_RSPSTS__SHIFT                                                    0x19
//BIF_INST_RESET_INTR_STS
#define BIF_INST_RESET_INTR_STS__EP0_LINK_RESET_INTR_STS__SHIFT                                               0x0
#define BIF_INST_RESET_INTR_STS__EP0_LINK_RESET_CFG_ONLY_INTR_STS__SHIFT                                      0x1
#define BIF_INST_RESET_INTR_STS__DRV_RESET_M0_INTR_STS__SHIFT                                                 0x2
#define BIF_INST_RESET_INTR_STS__DRV_RESET_M1_INTR_STS__SHIFT                                                 0x3
#define BIF_INST_RESET_INTR_STS__DRV_RESET_M2_INTR_STS__SHIFT                                                 0x4
//BIF_PF_FLR_INTR_STS
#define BIF_PF_FLR_INTR_STS__DEV0_PF0_FLR_INTR_STS__SHIFT                                                     0x0
#define BIF_PF_FLR_INTR_STS__DEV0_PF1_FLR_INTR_STS__SHIFT                                                     0x1
#define BIF_PF_FLR_INTR_STS__DEV0_PF2_FLR_INTR_STS__SHIFT                                                     0x2
#define BIF_PF_FLR_INTR_STS__DEV0_PF3_FLR_INTR_STS__SHIFT                                                     0x3
#define BIF_PF_FLR_INTR_STS__DEV0_PF4_FLR_INTR_STS__SHIFT                                                     0x4
#define BIF_PF_FLR_INTR_STS__DEV0_PF5_FLR_INTR_STS__SHIFT                                                     0x5
#define BIF_PF_FLR_INTR_STS__DEV0_PF6_FLR_INTR_STS__SHIFT                                                     0x6
#define BIF_PF_FLR_INTR_STS__DEV0_PF7_FLR_INTR_STS__SHIFT                                                     0x7
//BIF_D3HOTD0_INTR_STS
#define BIF_D3HOTD0_INTR_STS__DEV0_PF0_D3HOTD0_INTR_STS__SHIFT                                                0x0
#define BIF_D3HOTD0_INTR_STS__DEV0_PF1_D3HOTD0_INTR_STS__SHIFT                                                0x1
#define BIF_D3HOTD0_INTR_STS__DEV0_PF2_D3HOTD0_INTR_STS__SHIFT                                                0x2
#define BIF_D3HOTD0_INTR_STS__DEV0_PF3_D3HOTD0_INTR_STS__SHIFT                                                0x3
#define BIF_D3HOTD0_INTR_STS__DEV0_PF4_D3HOTD0_INTR_STS__SHIFT                                                0x4
#define BIF_D3HOTD0_INTR_STS__DEV0_PF5_D3HOTD0_INTR_STS__SHIFT                                                0x5
#define BIF_D3HOTD0_INTR_STS__DEV0_PF6_D3HOTD0_INTR_STS__SHIFT                                                0x6
#define BIF_D3HOTD0_INTR_STS__DEV0_PF7_D3HOTD0_INTR_STS__SHIFT                                                0x7
//BIF_POWER_INTR_STS
#define BIF_POWER_INTR_STS__DEV0_PME_TURN_OFF_INTR_STS__SHIFT                                                 0x0
#define BIF_POWER_INTR_STS__PORT0_DSTATE_INTR_STS__SHIFT                                                      0x10
//BIF_PF_DSTATE_INTR_STS
#define BIF_PF_DSTATE_INTR_STS__DEV0_PF0_DSTATE_INTR_STS__SHIFT                                               0x0
#define BIF_PF_DSTATE_INTR_STS__DEV0_PF1_DSTATE_INTR_STS__SHIFT                                               0x1
#define BIF_PF_DSTATE_INTR_STS__DEV0_PF2_DSTATE_INTR_STS__SHIFT                                               0x2
#define BIF_PF_DSTATE_INTR_STS__DEV0_PF3_DSTATE_INTR_STS__SHIFT                                               0x3
#define BIF_PF_DSTATE_INTR_STS__DEV0_PF4_DSTATE_INTR_STS__SHIFT                                               0x4
#define BIF_PF_DSTATE_INTR_STS__DEV0_PF5_DSTATE_INTR_STS__SHIFT                                               0x5
#define BIF_PF_DSTATE_INTR_STS__DEV0_PF6_DSTATE_INTR_STS__SHIFT                                               0x6
#define BIF_PF_DSTATE_INTR_STS__DEV0_PF7_DSTATE_INTR_STS__SHIFT                                               0x7
//BIF_PF0_VF_FLR_INTR_STS
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF0_FLR_INTR_STS__SHIFT                                                  0x0
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF1_FLR_INTR_STS__SHIFT                                                  0x1
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF2_FLR_INTR_STS__SHIFT                                                  0x2
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF3_FLR_INTR_STS__SHIFT                                                  0x3
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF4_FLR_INTR_STS__SHIFT                                                  0x4
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF5_FLR_INTR_STS__SHIFT                                                  0x5
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF6_FLR_INTR_STS__SHIFT                                                  0x6
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF7_FLR_INTR_STS__SHIFT                                                  0x7
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF8_FLR_INTR_STS__SHIFT                                                  0x8
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF9_FLR_INTR_STS__SHIFT                                                  0x9
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF10_FLR_INTR_STS__SHIFT                                                 0xa
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF11_FLR_INTR_STS__SHIFT                                                 0xb
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF12_FLR_INTR_STS__SHIFT                                                 0xc
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF13_FLR_INTR_STS__SHIFT                                                 0xd
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF14_FLR_INTR_STS__SHIFT                                                 0xe
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF15_FLR_INTR_STS__SHIFT                                                 0xf
#define BIF_PF0_VF_FLR_INTR_STS__PF0_SOFTPF_FLR_INTR_STS__SHIFT                                               0x1f
//BIF_INST_RESET_INTR_MASK
#define BIF_INST_RESET_INTR_MASK__EP0_LINK_RESET_INTR_MASK__SHIFT                                             0x0
#define BIF_INST_RESET_INTR_MASK__EP0_LINK_RESET_CFG_ONLY_INTR_MASK__SHIFT                                    0x1
#define BIF_INST_RESET_INTR_MASK__DRV_RESET_M0_INTR_MASK__SHIFT                                               0x2
#define BIF_INST_RESET_INTR_MASK__DRV_RESET_M1_INTR_MASK__SHIFT                                               0x3
#define BIF_INST_RESET_INTR_MASK__DRV_RESET_M2_INTR_MASK__SHIFT                                               0x4
//BIF_PF_FLR_INTR_MASK
#define BIF_PF_FLR_INTR_MASK__DEV0_PF0_FLR_INTR_MASK__SHIFT                                                   0x0
#define BIF_PF_FLR_INTR_MASK__DEV0_PF1_FLR_INTR_MASK__SHIFT                                                   0x1
#define BIF_PF_FLR_INTR_MASK__DEV0_PF2_FLR_INTR_MASK__SHIFT                                                   0x2
#define BIF_PF_FLR_INTR_MASK__DEV0_PF3_FLR_INTR_MASK__SHIFT                                                   0x3
#define BIF_PF_FLR_INTR_MASK__DEV0_PF4_FLR_INTR_MASK__SHIFT                                                   0x4
#define BIF_PF_FLR_INTR_MASK__DEV0_PF5_FLR_INTR_MASK__SHIFT                                                   0x5
#define BIF_PF_FLR_INTR_MASK__DEV0_PF6_FLR_INTR_MASK__SHIFT                                                   0x6
#define BIF_PF_FLR_INTR_MASK__DEV0_PF7_FLR_INTR_MASK__SHIFT                                                   0x7
//BIF_D3HOTD0_INTR_MASK
#define BIF_D3HOTD0_INTR_MASK__DEV0_PF0_D3HOTD0_INTR_MASK__SHIFT                                              0x0
#define BIF_D3HOTD0_INTR_MASK__DEV0_PF1_D3HOTD0_INTR_MASK__SHIFT                                              0x1
#define BIF_D3HOTD0_INTR_MASK__DEV0_PF2_D3HOTD0_INTR_MASK__SHIFT                                              0x2
#define BIF_D3HOTD0_INTR_MASK__DEV0_PF3_D3HOTD0_INTR_MASK__SHIFT                                              0x3
#define BIF_D3HOTD0_INTR_MASK__DEV0_PF4_D3HOTD0_INTR_MASK__SHIFT                                              0x4
#define BIF_D3HOTD0_INTR_MASK__DEV0_PF5_D3HOTD0_INTR_MASK__SHIFT                                              0x5
#define BIF_D3HOTD0_INTR_MASK__DEV0_PF6_D3HOTD0_INTR_MASK__SHIFT                                              0x6
#define BIF_D3HOTD0_INTR_MASK__DEV0_PF7_D3HOTD0_INTR_MASK__SHIFT                                              0x7
//BIF_POWER_INTR_MASK
#define BIF_POWER_INTR_MASK__DEV0_PME_TURN_OFF_INTR_MASK__SHIFT                                               0x0
#define BIF_POWER_INTR_MASK__PORT0_DSTATE_INTR_MASK__SHIFT                                                    0x10
//BIF_PF_DSTATE_INTR_MASK
#define BIF_PF_DSTATE_INTR_MASK__DEV0_PF0_DSTATE_INTR_MASK__SHIFT                                             0x0
#define BIF_PF_DSTATE_INTR_MASK__DEV0_PF1_DSTATE_INTR_MASK__SHIFT                                             0x1
#define BIF_PF_DSTATE_INTR_MASK__DEV0_PF2_DSTATE_INTR_MASK__SHIFT                                             0x2
#define BIF_PF_DSTATE_INTR_MASK__DEV0_PF3_DSTATE_INTR_MASK__SHIFT                                             0x3
#define BIF_PF_DSTATE_INTR_MASK__DEV0_PF4_DSTATE_INTR_MASK__SHIFT                                             0x4
#define BIF_PF_DSTATE_INTR_MASK__DEV0_PF5_DSTATE_INTR_MASK__SHIFT                                             0x5
#define BIF_PF_DSTATE_INTR_MASK__DEV0_PF6_DSTATE_INTR_MASK__SHIFT                                             0x6
#define BIF_PF_DSTATE_INTR_MASK__DEV0_PF7_DSTATE_INTR_MASK__SHIFT                                             0x7
//BIF_PF0_VF_FLR_INTR_MASK
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF0_FLR_INTR_MASK__SHIFT                                                0x0
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF1_FLR_INTR_MASK__SHIFT                                                0x1
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF2_FLR_INTR_MASK__SHIFT                                                0x2
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF3_FLR_INTR_MASK__SHIFT                                                0x3
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF4_FLR_INTR_MASK__SHIFT                                                0x4
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF5_FLR_INTR_MASK__SHIFT                                                0x5
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF6_FLR_INTR_MASK__SHIFT                                                0x6
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF7_FLR_INTR_MASK__SHIFT                                                0x7
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF8_FLR_INTR_MASK__SHIFT                                                0x8
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF9_FLR_INTR_MASK__SHIFT                                                0x9
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF10_FLR_INTR_MASK__SHIFT                                               0xa
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF11_FLR_INTR_MASK__SHIFT                                               0xb
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF12_FLR_INTR_MASK__SHIFT                                               0xc
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF13_FLR_INTR_MASK__SHIFT                                               0xd
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF14_FLR_INTR_MASK__SHIFT                                               0xe
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF15_FLR_INTR_MASK__SHIFT                                               0xf
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_SOFTPF_FLR_INTR_MASK__SHIFT                                             0x1f
//BIF_PF_FLR_RST
#define BIF_PF_FLR_RST__DEV0_PF0_FLR_RST__SHIFT                                                               0x0
#define BIF_PF_FLR_RST__DEV0_PF1_FLR_RST__SHIFT                                                               0x1
#define BIF_PF_FLR_RST__DEV0_PF2_FLR_RST__SHIFT                                                               0x2
#define BIF_PF_FLR_RST__DEV0_PF3_FLR_RST__SHIFT                                                               0x3
#define BIF_PF_FLR_RST__DEV0_PF4_FLR_RST__SHIFT                                                               0x4
#define BIF_PF_FLR_RST__DEV0_PF5_FLR_RST__SHIFT                                                               0x5
#define BIF_PF_FLR_RST__DEV0_PF6_FLR_RST__SHIFT                                                               0x6
#define BIF_PF_FLR_RST__DEV0_PF7_FLR_RST__SHIFT                                                               0x7
//BIF_PF0_VF_FLR_RST
#define BIF_PF0_VF_FLR_RST__PF0_VF0_FLR_RST__SHIFT                                                            0x0
#define BIF_PF0_VF_FLR_RST__PF0_VF1_FLR_RST__SHIFT                                                            0x1
#define BIF_PF0_VF_FLR_RST__PF0_VF2_FLR_RST__SHIFT                                                            0x2
#define BIF_PF0_VF_FLR_RST__PF0_VF3_FLR_RST__SHIFT                                                            0x3
#define BIF_PF0_VF_FLR_RST__PF0_VF4_FLR_RST__SHIFT                                                            0x4
#define BIF_PF0_VF_FLR_RST__PF0_VF5_FLR_RST__SHIFT                                                            0x5
#define BIF_PF0_VF_FLR_RST__PF0_VF6_FLR_RST__SHIFT                                                            0x6
#define BIF_PF0_VF_FLR_RST__PF0_VF7_FLR_RST__SHIFT                                                            0x7
#define BIF_PF0_VF_FLR_RST__PF0_VF8_FLR_RST__SHIFT                                                            0x8
#define BIF_PF0_VF_FLR_RST__PF0_VF9_FLR_RST__SHIFT                                                            0x9
#define BIF_PF0_VF_FLR_RST__PF0_VF10_FLR_RST__SHIFT                                                           0xa
#define BIF_PF0_VF_FLR_RST__PF0_VF11_FLR_RST__SHIFT                                                           0xb
#define BIF_PF0_VF_FLR_RST__PF0_VF12_FLR_RST__SHIFT                                                           0xc
#define BIF_PF0_VF_FLR_RST__PF0_VF13_FLR_RST__SHIFT                                                           0xd
#define BIF_PF0_VF_FLR_RST__PF0_VF14_FLR_RST__SHIFT                                                           0xe
#define BIF_PF0_VF_FLR_RST__PF0_VF15_FLR_RST__SHIFT                                                           0xf
#define BIF_PF0_VF_FLR_RST__PF0_SOFTPF_FLR_RST__SHIFT                                                         0x1f
//BIF_DEV0_PF0_DSTATE_VALUE
#define BIF_DEV0_PF0_DSTATE_VALUE__DEV0_PF0_DSTATE_TGT_VALUE__SHIFT                                           0x0
#define BIF_DEV0_PF0_DSTATE_VALUE__DEV0_PF0_DSTATE_NEED_D3TOD0_RESET__SHIFT                                   0x2
#define BIF_DEV0_PF0_DSTATE_VALUE__DEV0_PF0_DSTATE_ACK_VALUE__SHIFT                                           0x10
//BIF_DEV0_PF1_DSTATE_VALUE
#define BIF_DEV0_PF1_DSTATE_VALUE__DEV0_PF1_DSTATE_TGT_VALUE__SHIFT                                           0x0
#define BIF_DEV0_PF1_DSTATE_VALUE__DEV0_PF1_DSTATE_NEED_D3TOD0_RESET__SHIFT                                   0x2
#define BIF_DEV0_PF1_DSTATE_VALUE__DEV0_PF1_DSTATE_ACK_VALUE__SHIFT                                           0x10
//BIF_DEV0_PF2_DSTATE_VALUE
#define BIF_DEV0_PF2_DSTATE_VALUE__DEV0_PF2_DSTATE_TGT_VALUE__SHIFT                                           0x0
#define BIF_DEV0_PF2_DSTATE_VALUE__DEV0_PF2_DSTATE_NEED_D3TOD0_RESET__SHIFT                                   0x2
#define BIF_DEV0_PF2_DSTATE_VALUE__DEV0_PF2_DSTATE_ACK_VALUE__SHIFT                                           0x10
//BIF_DEV0_PF3_DSTATE_VALUE
#define BIF_DEV0_PF3_DSTATE_VALUE__DEV0_PF3_DSTATE_TGT_VALUE__SHIFT                                           0x0
#define BIF_DEV0_PF3_DSTATE_VALUE__DEV0_PF3_DSTATE_NEED_D3TOD0_RESET__SHIFT                                   0x2
#define BIF_DEV0_PF3_DSTATE_VALUE__DEV0_PF3_DSTATE_ACK_VALUE__SHIFT                                           0x10
//BIF_DEV0_PF4_DSTATE_VALUE
#define BIF_DEV0_PF4_DSTATE_VALUE__DEV0_PF4_DSTATE_TGT_VALUE__SHIFT                                           0x0
#define BIF_DEV0_PF4_DSTATE_VALUE__DEV0_PF4_DSTATE_NEED_D3TOD0_RESET__SHIFT                                   0x2
#define BIF_DEV0_PF4_DSTATE_VALUE__DEV0_PF4_DSTATE_ACK_VALUE__SHIFT                                           0x10
//BIF_DEV0_PF5_DSTATE_VALUE
#define BIF_DEV0_PF5_DSTATE_VALUE__DEV0_PF5_DSTATE_TGT_VALUE__SHIFT                                           0x0
#define BIF_DEV0_PF5_DSTATE_VALUE__DEV0_PF5_DSTATE_NEED_D3TOD0_RESET__SHIFT                                   0x2
#define BIF_DEV0_PF5_DSTATE_VALUE__DEV0_PF5_DSTATE_ACK_VALUE__SHIFT                                           0x10
//BIF_DEV0_PF6_DSTATE_VALUE
#define BIF_DEV0_PF6_DSTATE_VALUE__DEV0_PF6_DSTATE_TGT_VALUE__SHIFT                                           0x0
#define BIF_DEV0_PF6_DSTATE_VALUE__DEV0_PF6_DSTATE_NEED_D3TOD0_RESET__SHIFT                                   0x2
#define BIF_DEV0_PF6_DSTATE_VALUE__DEV0_PF6_DSTATE_ACK_VALUE__SHIFT                                           0x10
//BIF_DEV0_PF7_DSTATE_VALUE
#define BIF_DEV0_PF7_DSTATE_VALUE__DEV0_PF7_DSTATE_TGT_VALUE__SHIFT                                           0x0
#define BIF_DEV0_PF7_DSTATE_VALUE__DEV0_PF7_DSTATE_NEED_D3TOD0_RESET__SHIFT                                   0x2
#define BIF_DEV0_PF7_DSTATE_VALUE__DEV0_PF7_DSTATE_ACK_VALUE__SHIFT                                           0x10
//DEV0_PF0_D3HOTD0_RST_CTRL
#define DEV0_PF0_D3HOTD0_RST_CTRL__PF_CFG_EN__SHIFT                                                           0x0
#define DEV0_PF0_D3HOTD0_RST_CTRL__PF_CFG_FLR_EXC_EN__SHIFT                                                   0x1
#define DEV0_PF0_D3HOTD0_RST_CTRL__PF_CFG_STICKY_EN__SHIFT                                                    0x2
#define DEV0_PF0_D3HOTD0_RST_CTRL__PF_PRV_EN__SHIFT                                                           0x3
#define DEV0_PF0_D3HOTD0_RST_CTRL__PF_PRV_STICKY_EN__SHIFT                                                    0x4
//DEV0_PF1_D3HOTD0_RST_CTRL
#define DEV0_PF1_D3HOTD0_RST_CTRL__PF_CFG_EN__SHIFT                                                           0x0
#define DEV0_PF1_D3HOTD0_RST_CTRL__PF_CFG_FLR_EXC_EN__SHIFT                                                   0x1
#define DEV0_PF1_D3HOTD0_RST_CTRL__PF_CFG_STICKY_EN__SHIFT                                                    0x2
#define DEV0_PF1_D3HOTD0_RST_CTRL__PF_PRV_EN__SHIFT                                                           0x3
#define DEV0_PF1_D3HOTD0_RST_CTRL__PF_PRV_STICKY_EN__SHIFT                                                    0x4
//DEV0_PF2_D3HOTD0_RST_CTRL
#define DEV0_PF2_D3HOTD0_RST_CTRL__PF_CFG_EN__SHIFT                                                           0x0
#define DEV0_PF2_D3HOTD0_RST_CTRL__PF_CFG_FLR_EXC_EN__SHIFT                                                   0x1
#define DEV0_PF2_D3HOTD0_RST_CTRL__PF_CFG_STICKY_EN__SHIFT                                                    0x2
#define DEV0_PF2_D3HOTD0_RST_CTRL__PF_PRV_EN__SHIFT                                                           0x3
#define DEV0_PF2_D3HOTD0_RST_CTRL__PF_PRV_STICKY_EN__SHIFT                                                    0x4
//DEV0_PF3_D3HOTD0_RST_CTRL
#define DEV0_PF3_D3HOTD0_RST_CTRL__PF_CFG_EN__SHIFT                                                           0x0
#define DEV0_PF3_D3HOTD0_RST_CTRL__PF_CFG_FLR_EXC_EN__SHIFT                                                   0x1
#define DEV0_PF3_D3HOTD0_RST_CTRL__PF_CFG_STICKY_EN__SHIFT                                                    0x2
#define DEV0_PF3_D3HOTD0_RST_CTRL__PF_PRV_EN__SHIFT                                                           0x3
#define DEV0_PF3_D3HOTD0_RST_CTRL__PF_PRV_STICKY_EN__SHIFT                                                    0x4
//DEV0_PF4_D3HOTD0_RST_CTRL
#define DEV0_PF4_D3HOTD0_RST_CTRL__PF_CFG_EN__SHIFT                                                           0x0
#define DEV0_PF4_D3HOTD0_RST_CTRL__PF_CFG_FLR_EXC_EN__SHIFT                                                   0x1
#define DEV0_PF4_D3HOTD0_RST_CTRL__PF_CFG_STICKY_EN__SHIFT                                                    0x2
#define DEV0_PF4_D3HOTD0_RST_CTRL__PF_PRV_EN__SHIFT                                                           0x3
#define DEV0_PF4_D3HOTD0_RST_CTRL__PF_PRV_STICKY_EN__SHIFT                                                    0x4
//DEV0_PF5_D3HOTD0_RST_CTRL
#define DEV0_PF5_D3HOTD0_RST_CTRL__PF_CFG_EN__SHIFT                                                           0x0
#define DEV0_PF5_D3HOTD0_RST_CTRL__PF_CFG_FLR_EXC_EN__SHIFT                                                   0x1
#define DEV0_PF5_D3HOTD0_RST_CTRL__PF_CFG_STICKY_EN__SHIFT                                                    0x2
#define DEV0_PF5_D3HOTD0_RST_CTRL__PF_PRV_EN__SHIFT                                                           0x3
#define DEV0_PF5_D3HOTD0_RST_CTRL__PF_PRV_STICKY_EN__SHIFT                                                    0x4
//DEV0_PF6_D3HOTD0_RST_CTRL
#define DEV0_PF6_D3HOTD0_RST_CTRL__PF_CFG_EN__SHIFT                                                           0x0
#define DEV0_PF6_D3HOTD0_RST_CTRL__PF_CFG_FLR_EXC_EN__SHIFT                                                   0x1
#define DEV0_PF6_D3HOTD0_RST_CTRL__PF_CFG_STICKY_EN__SHIFT                                                    0x2
#define DEV0_PF6_D3HOTD0_RST_CTRL__PF_PRV_EN__SHIFT                                                           0x3
#define DEV0_PF6_D3HOTD0_RST_CTRL__PF_PRV_STICKY_EN__SHIFT                                                    0x4
//DEV0_PF7_D3HOTD0_RST_CTRL
#define DEV0_PF7_D3HOTD0_RST_CTRL__PF_CFG_EN__SHIFT                                                           0x0
#define DEV0_PF7_D3HOTD0_RST_CTRL__PF_CFG_FLR_EXC_EN__SHIFT                                                   0x1
#define DEV0_PF7_D3HOTD0_RST_CTRL__PF_CFG_STICKY_EN__SHIFT                                                    0x2
#define DEV0_PF7_D3HOTD0_RST_CTRL__PF_PRV_EN__SHIFT                                                           0x3
#define DEV0_PF7_D3HOTD0_RST_CTRL__PF_PRV_STICKY_EN__SHIFT                                                    0x4
//BIF_PORT0_DSTATE_VALUE
#define BIF_PORT0_DSTATE_VALUE__PORT0_DSTATE_TGT_VALUE__SHIFT                                                 0x0
#define BIF_PORT0_DSTATE_VALUE__PORT0_DSTATE_ACK_VALUE__SHIFT                                                 0x10


// addressBlock: bif_misc_bif_misc_regblk
//MISC_SCRATCH
#define MISC_SCRATCH__MISC_SCRATCH0__SHIFT                                                                    0x0
//INTR_LINE_POLARITY
#define INTR_LINE_POLARITY__INTR_LINE_POLARITY_DEV0__SHIFT                                                    0x0
//INTR_LINE_ENABLE
#define INTR_LINE_ENABLE__INTR_LINE_ENABLE_DEV0__SHIFT                                                        0x0
//OUTSTANDING_VC_ALLOC
#define OUTSTANDING_VC_ALLOC__DMA_OUTSTANDING_VC0_ALLOC__SHIFT                                                0x0
#define OUTSTANDING_VC_ALLOC__DMA_OUTSTANDING_VC1_ALLOC__SHIFT                                                0x2
#define OUTSTANDING_VC_ALLOC__DMA_OUTSTANDING_VC2_ALLOC__SHIFT                                                0x4
#define OUTSTANDING_VC_ALLOC__DMA_OUTSTANDING_VC3_ALLOC__SHIFT                                                0x6
#define OUTSTANDING_VC_ALLOC__DMA_OUTSTANDING_VC4_ALLOC__SHIFT                                                0x8
#define OUTSTANDING_VC_ALLOC__DMA_OUTSTANDING_VC5_ALLOC__SHIFT                                                0xa
#define OUTSTANDING_VC_ALLOC__DMA_OUTSTANDING_VC6_ALLOC__SHIFT                                                0xc
#define OUTSTANDING_VC_ALLOC__DMA_OUTSTANDING_VC7_ALLOC__SHIFT                                                0xe
#define OUTSTANDING_VC_ALLOC__DMA_OUTSTANDING_THRD__SHIFT                                                     0x10
#define OUTSTANDING_VC_ALLOC__HST_OUTSTANDING_VC0_ALLOC__SHIFT                                                0x18
#define OUTSTANDING_VC_ALLOC__HST_OUTSTANDING_VC1_ALLOC__SHIFT                                                0x1a
#define OUTSTANDING_VC_ALLOC__HST_OUTSTANDING_THRD__SHIFT                                                     0x1c
//BIFC_MISC_CTRL0
#define BIFC_MISC_CTRL0__VWIRE_TARG_UNITID_CHECK_EN__SHIFT                                                    0x0
#define BIFC_MISC_CTRL0__VWIRE_SRC_UNITID_CHECK_EN__SHIFT                                                     0x1
#define BIFC_MISC_CTRL0__DMA_CHAIN_BREAK_IN_RCMODE__SHIFT                                                     0x8
#define BIFC_MISC_CTRL0__HST_ARB_CHAIN_LOCK__SHIFT                                                            0x9
#define BIFC_MISC_CTRL0__GSI_SST_ARB_CHAIN_LOCK__SHIFT                                                        0xa
#define BIFC_MISC_CTRL0__DMA_ATOMIC_LENGTH_CHK_DIS__SHIFT                                                     0x10
#define BIFC_MISC_CTRL0__DMA_ATOMIC_FAILED_STS_SEL__SHIFT                                                     0x11
#define BIFC_MISC_CTRL0__PCIE_CAPABILITY_PROT_DIS__SHIFT                                                      0x18
#define BIFC_MISC_CTRL0__VC7_DMA_IOCFG_DIS__SHIFT                                                             0x19
#define BIFC_MISC_CTRL0__DMA_2ND_REQ_DIS__SHIFT                                                               0x1a
#define BIFC_MISC_CTRL0__PORT_DSTATE_BYPASS_MODE__SHIFT                                                       0x1b
#define BIFC_MISC_CTRL0__PME_TURNOFF_MODE__SHIFT                                                              0x1c
#define BIFC_MISC_CTRL0__PCIESWUS_SELECTION__SHIFT                                                            0x1f
//BIFC_MISC_CTRL1
#define BIFC_MISC_CTRL1__THT_HST_CPLD_POISON_REPORT__SHIFT                                                    0x0
#define BIFC_MISC_CTRL1__DMA_REQ_POISON_REPORT__SHIFT                                                         0x1
#define BIFC_MISC_CTRL1__DMA_REQ_ACSVIO_REPORT__SHIFT                                                         0x2
#define BIFC_MISC_CTRL1__DMA_RSP_POISON_CPLD_REPORT__SHIFT                                                    0x3
#define BIFC_MISC_CTRL1__GSI_SMN_WORST_ERR_STSTUS__SHIFT                                                      0x4
#define BIFC_MISC_CTRL1__GSI_SDP_RDRSP_DATA_FORCE1_FOR_ERROR__SHIFT                                           0x5
#define BIFC_MISC_CTRL1__GSI_RDWR_BALANCE_DIS__SHIFT                                                          0x6
#define BIFC_MISC_CTRL1__GMI_MSG_BLOCKLVL_SEL__SHIFT                                                          0x7
#define BIFC_MISC_CTRL1__HST_UNSUPPORT_SDPCMD_STS__SHIFT                                                      0x8
#define BIFC_MISC_CTRL1__HST_UNSUPPORT_SDPCMD_DATASTS__SHIFT                                                  0xa
#define BIFC_MISC_CTRL1__DROP_OTHER_HT_ADDR_REQ__SHIFT                                                        0xc
#define BIFC_MISC_CTRL1__DMAWRREQ_HSTRDRSP_ORDER_FORCE__SHIFT                                                 0xd
#define BIFC_MISC_CTRL1__DMAWRREQ_HSTRDRSP_ORDER_FORCE_VALUE__SHIFT                                           0xe
#define BIFC_MISC_CTRL1__UPS_SDP_RDY_TIE1__SHIFT                                                              0xf
#define BIFC_MISC_CTRL1__GMI_RCC_DN_BME_DROP_DIS__SHIFT                                                       0x10
#define BIFC_MISC_CTRL1__GMI_RCC_EP_BME_DROP_DIS__SHIFT                                                       0x11
#define BIFC_MISC_CTRL1__GMI_BIH_DN_BME_DROP_DIS__SHIFT                                                       0x12
#define BIFC_MISC_CTRL1__GMI_BIH_EP_BME_DROP_DIS__SHIFT                                                       0x13
//BIFC_BME_ERR_LOG
#define BIFC_BME_ERR_LOG__DMA_ON_BME_LOW_DEV0_F0__SHIFT                                                       0x0
#define BIFC_BME_ERR_LOG__DMA_ON_BME_LOW_DEV0_F1__SHIFT                                                       0x1
#define BIFC_BME_ERR_LOG__DMA_ON_BME_LOW_DEV0_F2__SHIFT                                                       0x2
#define BIFC_BME_ERR_LOG__DMA_ON_BME_LOW_DEV0_F3__SHIFT                                                       0x3
#define BIFC_BME_ERR_LOG__DMA_ON_BME_LOW_DEV0_F4__SHIFT                                                       0x4
#define BIFC_BME_ERR_LOG__DMA_ON_BME_LOW_DEV0_F5__SHIFT                                                       0x5
#define BIFC_BME_ERR_LOG__DMA_ON_BME_LOW_DEV0_F6__SHIFT                                                       0x6
#define BIFC_BME_ERR_LOG__DMA_ON_BME_LOW_DEV0_F7__SHIFT                                                       0x7
#define BIFC_BME_ERR_LOG__CLEAR_DMA_ON_BME_LOW_DEV0_F0__SHIFT                                                 0x10
#define BIFC_BME_ERR_LOG__CLEAR_DMA_ON_BME_LOW_DEV0_F1__SHIFT                                                 0x11
#define BIFC_BME_ERR_LOG__CLEAR_DMA_ON_BME_LOW_DEV0_F2__SHIFT                                                 0x12
#define BIFC_BME_ERR_LOG__CLEAR_DMA_ON_BME_LOW_DEV0_F3__SHIFT                                                 0x13
#define BIFC_BME_ERR_LOG__CLEAR_DMA_ON_BME_LOW_DEV0_F4__SHIFT                                                 0x14
#define BIFC_BME_ERR_LOG__CLEAR_DMA_ON_BME_LOW_DEV0_F5__SHIFT                                                 0x15
#define BIFC_BME_ERR_LOG__CLEAR_DMA_ON_BME_LOW_DEV0_F6__SHIFT                                                 0x16
#define BIFC_BME_ERR_LOG__CLEAR_DMA_ON_BME_LOW_DEV0_F7__SHIFT                                                 0x17
//BIFC_RCCBIH_BME_ERR_LOG
#define BIFC_RCCBIH_BME_ERR_LOG__RCCBIH_ON_BME_LOW_DEV0_F0__SHIFT                                             0x0
#define BIFC_RCCBIH_BME_ERR_LOG__RCCBIH_ON_BME_LOW_DEV0_F1__SHIFT                                             0x1
#define BIFC_RCCBIH_BME_ERR_LOG__RCCBIH_ON_BME_LOW_DEV0_F2__SHIFT                                             0x2
#define BIFC_RCCBIH_BME_ERR_LOG__RCCBIH_ON_BME_LOW_DEV0_F3__SHIFT                                             0x3
#define BIFC_RCCBIH_BME_ERR_LOG__RCCBIH_ON_BME_LOW_DEV0_F4__SHIFT                                             0x4
#define BIFC_RCCBIH_BME_ERR_LOG__RCCBIH_ON_BME_LOW_DEV0_F5__SHIFT                                             0x5
#define BIFC_RCCBIH_BME_ERR_LOG__RCCBIH_ON_BME_LOW_DEV0_F6__SHIFT                                             0x6
#define BIFC_RCCBIH_BME_ERR_LOG__RCCBIH_ON_BME_LOW_DEV0_F7__SHIFT                                             0x7
#define BIFC_RCCBIH_BME_ERR_LOG__CLEAR_RCCBIH_ON_BME_LOW_DEV0_F0__SHIFT                                       0x10
#define BIFC_RCCBIH_BME_ERR_LOG__CLEAR_RCCBIH_ON_BME_LOW_DEV0_F1__SHIFT                                       0x11
#define BIFC_RCCBIH_BME_ERR_LOG__CLEAR_RCCBIH_ON_BME_LOW_DEV0_F2__SHIFT                                       0x12
#define BIFC_RCCBIH_BME_ERR_LOG__CLEAR_RCCBIH_ON_BME_LOW_DEV0_F3__SHIFT                                       0x13
#define BIFC_RCCBIH_BME_ERR_LOG__CLEAR_RCCBIH_ON_BME_LOW_DEV0_F4__SHIFT                                       0x14
#define BIFC_RCCBIH_BME_ERR_LOG__CLEAR_RCCBIH_ON_BME_LOW_DEV0_F5__SHIFT                                       0x15
#define BIFC_RCCBIH_BME_ERR_LOG__CLEAR_RCCBIH_ON_BME_LOW_DEV0_F6__SHIFT                                       0x16
#define BIFC_RCCBIH_BME_ERR_LOG__CLEAR_RCCBIH_ON_BME_LOW_DEV0_F7__SHIFT                                       0x17
//BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_IDO_OVERIDE_P_DEV0_F0__SHIFT                                    0x0
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_IDO_OVERIDE_NP_DEV0_F0__SHIFT                                   0x2
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_RO_OVERIDE_P_DEV0_F0__SHIFT                                     0x6
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_RO_OVERIDE_NP_DEV0_F0__SHIFT                                    0x8
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_SNR_OVERIDE_P_DEV0_F0__SHIFT                                    0xa
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_SNR_OVERIDE_NP_DEV0_F0__SHIFT                                   0xc
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_IDO_OVERIDE_P_DEV0_F1__SHIFT                                    0x10
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_IDO_OVERIDE_NP_DEV0_F1__SHIFT                                   0x12
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_RO_OVERIDE_P_DEV0_F1__SHIFT                                     0x16
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_RO_OVERIDE_NP_DEV0_F1__SHIFT                                    0x18
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_SNR_OVERIDE_P_DEV0_F1__SHIFT                                    0x1a
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_SNR_OVERIDE_NP_DEV0_F1__SHIFT                                   0x1c
//BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_IDO_OVERIDE_P_DEV0_F2__SHIFT                                    0x0
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_IDO_OVERIDE_NP_DEV0_F2__SHIFT                                   0x2
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_RO_OVERIDE_P_DEV0_F2__SHIFT                                     0x6
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_RO_OVERIDE_NP_DEV0_F2__SHIFT                                    0x8
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_SNR_OVERIDE_P_DEV0_F2__SHIFT                                    0xa
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_SNR_OVERIDE_NP_DEV0_F2__SHIFT                                   0xc
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_IDO_OVERIDE_P_DEV0_F3__SHIFT                                    0x10
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_IDO_OVERIDE_NP_DEV0_F3__SHIFT                                   0x12
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_RO_OVERIDE_P_DEV0_F3__SHIFT                                     0x16
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_RO_OVERIDE_NP_DEV0_F3__SHIFT                                    0x18
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_SNR_OVERIDE_P_DEV0_F3__SHIFT                                    0x1a
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_SNR_OVERIDE_NP_DEV0_F3__SHIFT                                   0x1c
//BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_IDO_OVERIDE_P_DEV0_F4__SHIFT                                    0x0
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_IDO_OVERIDE_NP_DEV0_F4__SHIFT                                   0x2
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_RO_OVERIDE_P_DEV0_F4__SHIFT                                     0x6
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_RO_OVERIDE_NP_DEV0_F4__SHIFT                                    0x8
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_SNR_OVERIDE_P_DEV0_F4__SHIFT                                    0xa
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_SNR_OVERIDE_NP_DEV0_F4__SHIFT                                   0xc
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_IDO_OVERIDE_P_DEV0_F5__SHIFT                                    0x10
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_IDO_OVERIDE_NP_DEV0_F5__SHIFT                                   0x12
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_RO_OVERIDE_P_DEV0_F5__SHIFT                                     0x16
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_RO_OVERIDE_NP_DEV0_F5__SHIFT                                    0x18
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_SNR_OVERIDE_P_DEV0_F5__SHIFT                                    0x1a
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_SNR_OVERIDE_NP_DEV0_F5__SHIFT                                   0x1c
//BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_IDO_OVERIDE_P_DEV0_F6__SHIFT                                    0x0
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_IDO_OVERIDE_NP_DEV0_F6__SHIFT                                   0x2
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_RO_OVERIDE_P_DEV0_F6__SHIFT                                     0x6
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_RO_OVERIDE_NP_DEV0_F6__SHIFT                                    0x8
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_SNR_OVERIDE_P_DEV0_F6__SHIFT                                    0xa
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_SNR_OVERIDE_NP_DEV0_F6__SHIFT                                   0xc
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_IDO_OVERIDE_P_DEV0_F7__SHIFT                                    0x10
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_IDO_OVERIDE_NP_DEV0_F7__SHIFT                                   0x12
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_RO_OVERIDE_P_DEV0_F7__SHIFT                                     0x16
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_RO_OVERIDE_NP_DEV0_F7__SHIFT                                    0x18
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_SNR_OVERIDE_P_DEV0_F7__SHIFT                                    0x1a
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_SNR_OVERIDE_NP_DEV0_F7__SHIFT                                   0x1c
//NBIF_VWIRE_CTRL
#define NBIF_VWIRE_CTRL__SMN_VWR_RESET_DELAY_CNT__SHIFT                                                       0x4
#define NBIF_VWIRE_CTRL__SMN_VWR_POSTED__SHIFT                                                                0x8
#define NBIF_VWIRE_CTRL__SDP_VWR_RESET_DELAY_CNT__SHIFT                                                       0x14
#define NBIF_VWIRE_CTRL__SDP_VWR_BLOCKLVL__SHIFT                                                              0x1a
//NBIF_SMN_VWR_VCHG_DIS_CTRL
#define NBIF_SMN_VWR_VCHG_DIS_CTRL__SMN_VWR_VCHG_SET0_DIS__SHIFT                                              0x0
#define NBIF_SMN_VWR_VCHG_DIS_CTRL__SMN_VWR_VCHG_SET1_DIS__SHIFT                                              0x1
#define NBIF_SMN_VWR_VCHG_DIS_CTRL__SMN_VWR_VCHG_SET2_DIS__SHIFT                                              0x2
#define NBIF_SMN_VWR_VCHG_DIS_CTRL__SMN_VWR_VCHG_SET3_DIS__SHIFT                                              0x3
#define NBIF_SMN_VWR_VCHG_DIS_CTRL__SMN_VWR_VCHG_SET4_DIS__SHIFT                                              0x4
#define NBIF_SMN_VWR_VCHG_DIS_CTRL__SMN_VWR_VCHG_SET5_DIS__SHIFT                                              0x5
#define NBIF_SMN_VWR_VCHG_DIS_CTRL__SMN_VWR_VCHG_SET6_DIS__SHIFT                                              0x6
//NBIF_SMN_VWR_VCHG_RST_CTRL0
#define NBIF_SMN_VWR_VCHG_RST_CTRL0__SMN_VWR_VCHG_SET0_RST_DEF_REV__SHIFT                                     0x0
#define NBIF_SMN_VWR_VCHG_RST_CTRL0__SMN_VWR_VCHG_SET1_RST_DEF_REV__SHIFT                                     0x1
#define NBIF_SMN_VWR_VCHG_RST_CTRL0__SMN_VWR_VCHG_SET2_RST_DEF_REV__SHIFT                                     0x2
#define NBIF_SMN_VWR_VCHG_RST_CTRL0__SMN_VWR_VCHG_SET3_RST_DEF_REV__SHIFT                                     0x3
#define NBIF_SMN_VWR_VCHG_RST_CTRL0__SMN_VWR_VCHG_SET4_RST_DEF_REV__SHIFT                                     0x4
#define NBIF_SMN_VWR_VCHG_RST_CTRL0__SMN_VWR_VCHG_SET5_RST_DEF_REV__SHIFT                                     0x5
#define NBIF_SMN_VWR_VCHG_RST_CTRL0__SMN_VWR_VCHG_SET6_RST_DEF_REV__SHIFT                                     0x6
//NBIF_SMN_VWR_VCHG_TRIG
#define NBIF_SMN_VWR_VCHG_TRIG__SMN_VWR_VCHG_SET0_TRIG__SHIFT                                                 0x0
#define NBIF_SMN_VWR_VCHG_TRIG__SMN_VWR_VCHG_SET1_TRIG__SHIFT                                                 0x1
#define NBIF_SMN_VWR_VCHG_TRIG__SMN_VWR_VCHG_SET2_TRIG__SHIFT                                                 0x2
#define NBIF_SMN_VWR_VCHG_TRIG__SMN_VWR_VCHG_SET3_TRIG__SHIFT                                                 0x3
#define NBIF_SMN_VWR_VCHG_TRIG__SMN_VWR_VCHG_SET4_TRIG__SHIFT                                                 0x4
#define NBIF_SMN_VWR_VCHG_TRIG__SMN_VWR_VCHG_SET5_TRIG__SHIFT                                                 0x5
#define NBIF_SMN_VWR_VCHG_TRIG__SMN_VWR_VCHG_SET6_TRIG__SHIFT                                                 0x6
//NBIF_SMN_VWR_WTRIG_CNTL
#define NBIF_SMN_VWR_WTRIG_CNTL__SMN_VWR_WTRIG_SET0_DIS__SHIFT                                                0x0
#define NBIF_SMN_VWR_WTRIG_CNTL__SMN_VWR_WTRIG_SET1_DIS__SHIFT                                                0x1
#define NBIF_SMN_VWR_WTRIG_CNTL__SMN_VWR_WTRIG_SET2_DIS__SHIFT                                                0x2
#define NBIF_SMN_VWR_WTRIG_CNTL__SMN_VWR_WTRIG_SET3_DIS__SHIFT                                                0x3
#define NBIF_SMN_VWR_WTRIG_CNTL__SMN_VWR_WTRIG_SET4_DIS__SHIFT                                                0x4
#define NBIF_SMN_VWR_WTRIG_CNTL__SMN_VWR_WTRIG_SET5_DIS__SHIFT                                                0x5
#define NBIF_SMN_VWR_WTRIG_CNTL__SMN_VWR_WTRIG_SET6_DIS__SHIFT                                                0x6
//NBIF_SMN_VWR_VCHG_DIS_CTRL_1
#define NBIF_SMN_VWR_VCHG_DIS_CTRL_1__SMN_VWR_VCHG_SET0_DIFFDET_DEF_REV__SHIFT                                0x0
#define NBIF_SMN_VWR_VCHG_DIS_CTRL_1__SMN_VWR_VCHG_SET1_DIFFDET_DEF_REV__SHIFT                                0x1
#define NBIF_SMN_VWR_VCHG_DIS_CTRL_1__SMN_VWR_VCHG_SET2_DIFFDET_DEF_REV__SHIFT                                0x2
#define NBIF_SMN_VWR_VCHG_DIS_CTRL_1__SMN_VWR_VCHG_SET3_DIFFDET_DEF_REV__SHIFT                                0x3
#define NBIF_SMN_VWR_VCHG_DIS_CTRL_1__SMN_VWR_VCHG_SET4_DIFFDET_DEF_REV__SHIFT                                0x4
#define NBIF_SMN_VWR_VCHG_DIS_CTRL_1__SMN_VWR_VCHG_SET5_DIFFDET_DEF_REV__SHIFT                                0x5
#define NBIF_SMN_VWR_VCHG_DIS_CTRL_1__SMN_VWR_VCHG_SET6_DIFFDET_DEF_REV__SHIFT                                0x6
//NBIF_MGCG_CTRL
#define NBIF_MGCG_CTRL__NBIF_MGCG_EN__SHIFT                                                                   0x0
#define NBIF_MGCG_CTRL__NBIF_MGCG_MODE__SHIFT                                                                 0x1
#define NBIF_MGCG_CTRL__NBIF_MGCG_HYSTERESIS__SHIFT                                                           0x2
//NBIF_DS_CTRL_LCLK
#define NBIF_DS_CTRL_LCLK__NBIF_LCLK_DS_EN__SHIFT                                                             0x0
#define NBIF_DS_CTRL_LCLK__NBIF_LCLK_DS_TIMER__SHIFT                                                          0x10
//SMN_MST_CNTL0
#define SMN_MST_CNTL0__SMN_ARB_MODE__SHIFT                                                                    0x0
#define SMN_MST_CNTL0__SMN_ZERO_BE_WR_EN_UPS__SHIFT                                                           0x8
#define SMN_MST_CNTL0__SMN_ZERO_BE_RD_EN_UPS__SHIFT                                                           0x9
#define SMN_MST_CNTL0__SMN_POST_MASK_EN_UPS__SHIFT                                                            0xa
#define SMN_MST_CNTL0__MULTI_SMN_TRANS_ID_DIS_UPS__SHIFT                                                      0xb
#define SMN_MST_CNTL0__SMN_ZERO_BE_WR_EN_DNS_DEV0__SHIFT                                                      0x10
#define SMN_MST_CNTL0__SMN_ZERO_BE_RD_EN_DNS_DEV0__SHIFT                                                      0x14
#define SMN_MST_CNTL0__SMN_POST_MASK_EN_DNS_DEV0__SHIFT                                                       0x18
#define SMN_MST_CNTL0__MULTI_SMN_TRANS_ID_DIS_DNS_DEV0__SHIFT                                                 0x1c
//SMN_MST_EP_CNTL1
#define SMN_MST_EP_CNTL1__SMN_POST_MASK_EN_EP_DEV0_PF0__SHIFT                                                 0x0
#define SMN_MST_EP_CNTL1__SMN_POST_MASK_EN_EP_DEV0_PF1__SHIFT                                                 0x1
#define SMN_MST_EP_CNTL1__SMN_POST_MASK_EN_EP_DEV0_PF2__SHIFT                                                 0x2
#define SMN_MST_EP_CNTL1__SMN_POST_MASK_EN_EP_DEV0_PF3__SHIFT                                                 0x3
#define SMN_MST_EP_CNTL1__SMN_POST_MASK_EN_EP_DEV0_PF4__SHIFT                                                 0x4
#define SMN_MST_EP_CNTL1__SMN_POST_MASK_EN_EP_DEV0_PF5__SHIFT                                                 0x5
#define SMN_MST_EP_CNTL1__SMN_POST_MASK_EN_EP_DEV0_PF6__SHIFT                                                 0x6
#define SMN_MST_EP_CNTL1__SMN_POST_MASK_EN_EP_DEV0_PF7__SHIFT                                                 0x7
//SMN_MST_EP_CNTL2
#define SMN_MST_EP_CNTL2__MULTI_SMN_TRANS_ID_DIS_EP_DEV0_PF0__SHIFT                                           0x0
#define SMN_MST_EP_CNTL2__MULTI_SMN_TRANS_ID_DIS_EP_DEV0_PF1__SHIFT                                           0x1
#define SMN_MST_EP_CNTL2__MULTI_SMN_TRANS_ID_DIS_EP_DEV0_PF2__SHIFT                                           0x2
#define SMN_MST_EP_CNTL2__MULTI_SMN_TRANS_ID_DIS_EP_DEV0_PF3__SHIFT                                           0x3
#define SMN_MST_EP_CNTL2__MULTI_SMN_TRANS_ID_DIS_EP_DEV0_PF4__SHIFT                                           0x4
#define SMN_MST_EP_CNTL2__MULTI_SMN_TRANS_ID_DIS_EP_DEV0_PF5__SHIFT                                           0x5
#define SMN_MST_EP_CNTL2__MULTI_SMN_TRANS_ID_DIS_EP_DEV0_PF6__SHIFT                                           0x6
#define SMN_MST_EP_CNTL2__MULTI_SMN_TRANS_ID_DIS_EP_DEV0_PF7__SHIFT                                           0x7
//NBIF_SDP_VWR_VCHG_DIS_CTRL
#define NBIF_SDP_VWR_VCHG_DIS_CTRL__SDP_VWR_VCHG_ENDP_F0_DIS__SHIFT                                           0x0
#define NBIF_SDP_VWR_VCHG_DIS_CTRL__SDP_VWR_VCHG_ENDP_F1_DIS__SHIFT                                           0x1
#define NBIF_SDP_VWR_VCHG_DIS_CTRL__SDP_VWR_VCHG_ENDP_F2_DIS__SHIFT                                           0x2
#define NBIF_SDP_VWR_VCHG_DIS_CTRL__SDP_VWR_VCHG_ENDP_F3_DIS__SHIFT                                           0x3
#define NBIF_SDP_VWR_VCHG_DIS_CTRL__SDP_VWR_VCHG_ENDP_F4_DIS__SHIFT                                           0x4
#define NBIF_SDP_VWR_VCHG_DIS_CTRL__SDP_VWR_VCHG_ENDP_F5_DIS__SHIFT                                           0x5
#define NBIF_SDP_VWR_VCHG_DIS_CTRL__SDP_VWR_VCHG_ENDP_F6_DIS__SHIFT                                           0x6
#define NBIF_SDP_VWR_VCHG_DIS_CTRL__SDP_VWR_VCHG_ENDP_F7_DIS__SHIFT                                           0x7
#define NBIF_SDP_VWR_VCHG_DIS_CTRL__SDP_VWR_VCHG_SWDS_P0_DIS__SHIFT                                           0x18
//NBIF_SDP_VWR_VCHG_RST_CTRL0
#define NBIF_SDP_VWR_VCHG_RST_CTRL0__SDP_VWR_VCHG_ENDP_F0_RST_OVRD_EN__SHIFT                                  0x0
#define NBIF_SDP_VWR_VCHG_RST_CTRL0__SDP_VWR_VCHG_ENDP_F1_RST_OVRD_EN__SHIFT                                  0x1
#define NBIF_SDP_VWR_VCHG_RST_CTRL0__SDP_VWR_VCHG_ENDP_F2_RST_OVRD_EN__SHIFT                                  0x2
#define NBIF_SDP_VWR_VCHG_RST_CTRL0__SDP_VWR_VCHG_ENDP_F3_RST_OVRD_EN__SHIFT                                  0x3
#define NBIF_SDP_VWR_VCHG_RST_CTRL0__SDP_VWR_VCHG_ENDP_F4_RST_OVRD_EN__SHIFT                                  0x4
#define NBIF_SDP_VWR_VCHG_RST_CTRL0__SDP_VWR_VCHG_ENDP_F5_RST_OVRD_EN__SHIFT                                  0x5
#define NBIF_SDP_VWR_VCHG_RST_CTRL0__SDP_VWR_VCHG_ENDP_F6_RST_OVRD_EN__SHIFT                                  0x6
#define NBIF_SDP_VWR_VCHG_RST_CTRL0__SDP_VWR_VCHG_ENDP_F7_RST_OVRD_EN__SHIFT                                  0x7
#define NBIF_SDP_VWR_VCHG_RST_CTRL0__SDP_VWR_VCHG_SWDS_P0_RST_OVRD_EN__SHIFT                                  0x18
//NBIF_SDP_VWR_VCHG_RST_CTRL1
#define NBIF_SDP_VWR_VCHG_RST_CTRL1__SDP_VWR_VCHG_ENDP_F0_RST_OVRD_VAL__SHIFT                                 0x0
#define NBIF_SDP_VWR_VCHG_RST_CTRL1__SDP_VWR_VCHG_ENDP_F1_RST_OVRD_VAL__SHIFT                                 0x1
#define NBIF_SDP_VWR_VCHG_RST_CTRL1__SDP_VWR_VCHG_ENDP_F2_RST_OVRD_VAL__SHIFT                                 0x2
#define NBIF_SDP_VWR_VCHG_RST_CTRL1__SDP_VWR_VCHG_ENDP_F3_RST_OVRD_VAL__SHIFT                                 0x3
#define NBIF_SDP_VWR_VCHG_RST_CTRL1__SDP_VWR_VCHG_ENDP_F4_RST_OVRD_VAL__SHIFT                                 0x4
#define NBIF_SDP_VWR_VCHG_RST_CTRL1__SDP_VWR_VCHG_ENDP_F5_RST_OVRD_VAL__SHIFT                                 0x5
#define NBIF_SDP_VWR_VCHG_RST_CTRL1__SDP_VWR_VCHG_ENDP_F6_RST_OVRD_VAL__SHIFT                                 0x6
#define NBIF_SDP_VWR_VCHG_RST_CTRL1__SDP_VWR_VCHG_ENDP_F7_RST_OVRD_VAL__SHIFT                                 0x7
#define NBIF_SDP_VWR_VCHG_RST_CTRL1__SDP_VWR_VCHG_SWDS_P0_RST_OVRD_VAL__SHIFT                                 0x18
//NBIF_SDP_VWR_VCHG_TRIG
#define NBIF_SDP_VWR_VCHG_TRIG__SDP_VWR_VCHG_ENDP_F0_TRIG__SHIFT                                              0x0
#define NBIF_SDP_VWR_VCHG_TRIG__SDP_VWR_VCHG_ENDP_F1_TRIG__SHIFT                                              0x1
#define NBIF_SDP_VWR_VCHG_TRIG__SDP_VWR_VCHG_ENDP_F2_TRIG__SHIFT                                              0x2
#define NBIF_SDP_VWR_VCHG_TRIG__SDP_VWR_VCHG_ENDP_F3_TRIG__SHIFT                                              0x3
#define NBIF_SDP_VWR_VCHG_TRIG__SDP_VWR_VCHG_ENDP_F4_TRIG__SHIFT                                              0x4
#define NBIF_SDP_VWR_VCHG_TRIG__SDP_VWR_VCHG_ENDP_F5_TRIG__SHIFT                                              0x5
#define NBIF_SDP_VWR_VCHG_TRIG__SDP_VWR_VCHG_ENDP_F6_TRIG__SHIFT                                              0x6
#define NBIF_SDP_VWR_VCHG_TRIG__SDP_VWR_VCHG_ENDP_F7_TRIG__SHIFT                                              0x7
#define NBIF_SDP_VWR_VCHG_TRIG__SDP_VWR_VCHG_SWDS_P0_TRIG__SHIFT                                              0x18
//BME_DUMMY_CNTL_0
#define BME_DUMMY_CNTL_0__BME_DUMMY_RSPSTS_DEV0_F0__SHIFT                                                     0x0
#define BME_DUMMY_CNTL_0__BME_DUMMY_RSPSTS_DEV0_F1__SHIFT                                                     0x2
#define BME_DUMMY_CNTL_0__BME_DUMMY_RSPSTS_DEV0_F2__SHIFT                                                     0x4
#define BME_DUMMY_CNTL_0__BME_DUMMY_RSPSTS_DEV0_F3__SHIFT                                                     0x6
#define BME_DUMMY_CNTL_0__BME_DUMMY_RSPSTS_DEV0_F4__SHIFT                                                     0x8
#define BME_DUMMY_CNTL_0__BME_DUMMY_RSPSTS_DEV0_F5__SHIFT                                                     0xa
#define BME_DUMMY_CNTL_0__BME_DUMMY_RSPSTS_DEV0_F6__SHIFT                                                     0xc
#define BME_DUMMY_CNTL_0__BME_DUMMY_RSPSTS_DEV0_F7__SHIFT                                                     0xe
//BIFC_THT_CNTL
#define BIFC_THT_CNTL__CREDIT_ALLOC_THT_RD_VC0__SHIFT                                                         0x0
#define BIFC_THT_CNTL__CREDIT_ALLOC_THT_WR_VC0__SHIFT                                                         0x4
#define BIFC_THT_CNTL__CREDIT_ALLOC_THT_WR_VC1__SHIFT                                                         0x8
//BIFC_HSTARB_CNTL
#define BIFC_HSTARB_CNTL__SLVARB_MODE__SHIFT                                                                  0x0
//BIFC_GSI_CNTL
#define BIFC_GSI_CNTL__GSI_SDP_RSP_ARB_MODE__SHIFT                                                            0x0
#define BIFC_GSI_CNTL__GSI_CPL_RSP_ARB_MODE__SHIFT                                                            0x2
#define BIFC_GSI_CNTL__GSI_CPL_INTERLEAVING_EN__SHIFT                                                         0x5
#define BIFC_GSI_CNTL__GSI_CPL_PCR_EP_CAUSE_UR_EN__SHIFT                                                      0x6
#define BIFC_GSI_CNTL__GSI_CPL_SMN_P_EP_CAUSE_UR_EN__SHIFT                                                    0x7
#define BIFC_GSI_CNTL__GSI_CPL_SMN_NP_EP_CAUSE_UR_EN__SHIFT                                                   0x8
#define BIFC_GSI_CNTL__GSI_CPL_SST_EP_CAUSE_UR_EN__SHIFT                                                      0x9
#define BIFC_GSI_CNTL__GSI_SDP_REQ_ARB_MODE__SHIFT                                                            0xa
#define BIFC_GSI_CNTL__GSI_SMN_REQ_ARB_MODE__SHIFT                                                            0xc
//BIFC_PCIEFUNC_CNTL
#define BIFC_PCIEFUNC_CNTL__DMA_NON_PCIEFUNC_BUSDEVFUNC__SHIFT                                                0x0
#define BIFC_PCIEFUNC_CNTL__MP1SYSHUBDATA_DRAM_IS_PCIEFUNC__SHIFT                                             0x10
//BIFC_SDP_CNTL_0
#define BIFC_SDP_CNTL_0__HRP_SDP_DISCON_HYSTERESIS__SHIFT                                                     0x0
#define BIFC_SDP_CNTL_0__GSI_SDP_DISCON_HYSTERESIS__SHIFT                                                     0x6
#define BIFC_SDP_CNTL_0__GMI_DNS_SDP_DISCON_HYSTERESIS__SHIFT                                                 0xc
#define BIFC_SDP_CNTL_0__GMI_UPS_SDP_DISCON_HYSTERESIS__SHIFT                                                 0x12
//BIFC_PERF_CNTL_0
#define BIFC_PERF_CNTL_0__PERF_CNT_MMIO_RD_EN__SHIFT                                                          0x0
#define BIFC_PERF_CNTL_0__PERF_CNT_MMIO_WR_EN__SHIFT                                                          0x1
#define BIFC_PERF_CNTL_0__PERF_CNT_MMIO_RD_RESET__SHIFT                                                       0x8
#define BIFC_PERF_CNTL_0__PERF_CNT_MMIO_WR_RESET__SHIFT                                                       0x9
#define BIFC_PERF_CNTL_0__PERF_CNT_MMIO_RD_SEL__SHIFT                                                         0x10
#define BIFC_PERF_CNTL_0__PERF_CNT_MMIO_WR_SEL__SHIFT                                                         0x18
//BIFC_PERF_CNTL_1
#define BIFC_PERF_CNTL_1__PERF_CNT_DMA_RD_EN__SHIFT                                                           0x0
#define BIFC_PERF_CNTL_1__PERF_CNT_DMA_WR_EN__SHIFT                                                           0x1
#define BIFC_PERF_CNTL_1__PERF_CNT_DMA_RD_RESET__SHIFT                                                        0x8
#define BIFC_PERF_CNTL_1__PERF_CNT_DMA_WR_RESET__SHIFT                                                        0x9
#define BIFC_PERF_CNTL_1__PERF_CNT_DMA_RD_SEL__SHIFT                                                          0x10
#define BIFC_PERF_CNTL_1__PERF_CNT_DMA_WR_SEL__SHIFT                                                          0x18
//BIFC_PERF_CNT_MMIO_RD
#define BIFC_PERF_CNT_MMIO_RD__PERF_CNT_MMIO_RD_VALUE__SHIFT                                                  0x0
//BIFC_PERF_CNT_MMIO_WR
#define BIFC_PERF_CNT_MMIO_WR__PERF_CNT_MMIO_WR_VALUE__SHIFT                                                  0x0
//BIFC_PERF_CNT_DMA_RD
#define BIFC_PERF_CNT_DMA_RD__PERF_CNT_DMA_RD_VALUE__SHIFT                                                    0x0
//BIFC_PERF_CNT_DMA_WR
#define BIFC_PERF_CNT_DMA_WR__PERF_CNT_DMA_WR_VALUE__SHIFT                                                    0x0
//NBIF_REGIF_ERRSET_CTRL
#define NBIF_REGIF_ERRSET_CTRL__DROP_NONPF_MMREGREQ_SETERR_DIS__SHIFT                                         0x0
//SMN_MST_EP_CNTL3
#define SMN_MST_EP_CNTL3__SMN_ZERO_BE_WR_EN_EP_DEV0_PF0__SHIFT                                                0x0
#define SMN_MST_EP_CNTL3__SMN_ZERO_BE_WR_EN_EP_DEV0_PF1__SHIFT                                                0x1
#define SMN_MST_EP_CNTL3__SMN_ZERO_BE_WR_EN_EP_DEV0_PF2__SHIFT                                                0x2
#define SMN_MST_EP_CNTL3__SMN_ZERO_BE_WR_EN_EP_DEV0_PF3__SHIFT                                                0x3
#define SMN_MST_EP_CNTL3__SMN_ZERO_BE_WR_EN_EP_DEV0_PF4__SHIFT                                                0x4
#define SMN_MST_EP_CNTL3__SMN_ZERO_BE_WR_EN_EP_DEV0_PF5__SHIFT                                                0x5
#define SMN_MST_EP_CNTL3__SMN_ZERO_BE_WR_EN_EP_DEV0_PF6__SHIFT                                                0x6
#define SMN_MST_EP_CNTL3__SMN_ZERO_BE_WR_EN_EP_DEV0_PF7__SHIFT                                                0x7
//SMN_MST_EP_CNTL4
#define SMN_MST_EP_CNTL4__SMN_ZERO_BE_RD_EN_EP_DEV0_PF0__SHIFT                                                0x0
#define SMN_MST_EP_CNTL4__SMN_ZERO_BE_RD_EN_EP_DEV0_PF1__SHIFT                                                0x1
#define SMN_MST_EP_CNTL4__SMN_ZERO_BE_RD_EN_EP_DEV0_PF2__SHIFT                                                0x2
#define SMN_MST_EP_CNTL4__SMN_ZERO_BE_RD_EN_EP_DEV0_PF3__SHIFT                                                0x3
#define SMN_MST_EP_CNTL4__SMN_ZERO_BE_RD_EN_EP_DEV0_PF4__SHIFT                                                0x4
#define SMN_MST_EP_CNTL4__SMN_ZERO_BE_RD_EN_EP_DEV0_PF5__SHIFT                                                0x5
#define SMN_MST_EP_CNTL4__SMN_ZERO_BE_RD_EN_EP_DEV0_PF6__SHIFT                                                0x6
#define SMN_MST_EP_CNTL4__SMN_ZERO_BE_RD_EN_EP_DEV0_PF7__SHIFT                                                0x7
//BIF_SELFRING_BUFFER_VID
#define BIF_SELFRING_BUFFER_VID__DOORBELL_MONITOR_CID__SHIFT                                                  0x0
#define BIF_SELFRING_BUFFER_VID__IOHUB_RAS_INTR_CID__SHIFT                                                    0x8
//BIF_SELFRING_VECTOR_CNTL
#define BIF_SELFRING_VECTOR_CNTL__MISC_DB_MNTR_INTR_DIS__SHIFT                                                0x0


// addressBlock: bif_ras_bif_ras_regblk
//BIF_RAS_LEAF0_CTRL
#define BIF_RAS_LEAF0_CTRL__POISON_DET_EN__SHIFT                                                              0x0
#define BIF_RAS_LEAF0_CTRL__POISON_ERREVENT_EN__SHIFT                                                         0x1
#define BIF_RAS_LEAF0_CTRL__POISON_STALL_EN__SHIFT                                                            0x2
#define BIF_RAS_LEAF0_CTRL__PARITY_DET_EN__SHIFT                                                              0x4
#define BIF_RAS_LEAF0_CTRL__PARITY_ERREVENT_EN__SHIFT                                                         0x5
#define BIF_RAS_LEAF0_CTRL__PARITY_STALL_EN__SHIFT                                                            0x6
#define BIF_RAS_LEAF0_CTRL__ERR_EVENT_RECV__SHIFT                                                             0x10
#define BIF_RAS_LEAF0_CTRL__LINK_DIS_RECV__SHIFT                                                              0x11
#define BIF_RAS_LEAF0_CTRL__POISON_ERR_DET__SHIFT                                                             0x12
#define BIF_RAS_LEAF0_CTRL__PARITY_ERR_DET__SHIFT                                                             0x13
#define BIF_RAS_LEAF0_CTRL__ERR_EVENT_SENT__SHIFT                                                             0x14
#define BIF_RAS_LEAF0_CTRL__EGRESS_STALLED__SHIFT                                                             0x15
//BIF_RAS_LEAF1_CTRL
#define BIF_RAS_LEAF1_CTRL__POISON_DET_EN__SHIFT                                                              0x0
#define BIF_RAS_LEAF1_CTRL__POISON_ERREVENT_EN__SHIFT                                                         0x1
#define BIF_RAS_LEAF1_CTRL__POISON_STALL_EN__SHIFT                                                            0x2
#define BIF_RAS_LEAF1_CTRL__PARITY_DET_EN__SHIFT                                                              0x4
#define BIF_RAS_LEAF1_CTRL__PARITY_ERREVENT_EN__SHIFT                                                         0x5
#define BIF_RAS_LEAF1_CTRL__PARITY_STALL_EN__SHIFT                                                            0x6
#define BIF_RAS_LEAF1_CTRL__ERR_EVENT_RECV__SHIFT                                                             0x10
#define BIF_RAS_LEAF1_CTRL__LINK_DIS_RECV__SHIFT                                                              0x11
#define BIF_RAS_LEAF1_CTRL__POISON_ERR_DET__SHIFT                                                             0x12
#define BIF_RAS_LEAF1_CTRL__PARITY_ERR_DET__SHIFT                                                             0x13
#define BIF_RAS_LEAF1_CTRL__ERR_EVENT_SENT__SHIFT                                                             0x14
#define BIF_RAS_LEAF1_CTRL__EGRESS_STALLED__SHIFT                                                             0x15
//BIF_RAS_LEAF2_CTRL
#define BIF_RAS_LEAF2_CTRL__POISON_DET_EN__SHIFT                                                              0x0
#define BIF_RAS_LEAF2_CTRL__POISON_ERREVENT_EN__SHIFT                                                         0x1
#define BIF_RAS_LEAF2_CTRL__POISON_STALL_EN__SHIFT                                                            0x2
#define BIF_RAS_LEAF2_CTRL__PARITY_DET_EN__SHIFT                                                              0x4
#define BIF_RAS_LEAF2_CTRL__PARITY_ERREVENT_EN__SHIFT                                                         0x5
#define BIF_RAS_LEAF2_CTRL__PARITY_STALL_EN__SHIFT                                                            0x6
#define BIF_RAS_LEAF2_CTRL__ERR_EVENT_RECV__SHIFT                                                             0x10
#define BIF_RAS_LEAF2_CTRL__LINK_DIS_RECV__SHIFT                                                              0x11
#define BIF_RAS_LEAF2_CTRL__POISON_ERR_DET__SHIFT                                                             0x12
#define BIF_RAS_LEAF2_CTRL__PARITY_ERR_DET__SHIFT                                                             0x13
#define BIF_RAS_LEAF2_CTRL__ERR_EVENT_SENT__SHIFT                                                             0x14
#define BIF_RAS_LEAF2_CTRL__EGRESS_STALLED__SHIFT                                                             0x15
//BIF_RAS_MISC_CTRL
#define BIF_RAS_MISC_CTRL__LINKDIS_TRIG_ERREVENT_EN__SHIFT                                                    0x0
//BIF_IOHUB_RAS_IH_CNTL
#define BIF_IOHUB_RAS_IH_CNTL__RAS_IH_INTR_EN__SHIFT                                                          0x0
//BIF_RAS_VWR_FROM_IOHUB
#define BIF_RAS_VWR_FROM_IOHUB__RAS_IH_INTR_TRIG__SHIFT                                                       0x0


// addressBlock: rcc_pfc_amdgfx_RCCPFCDEC
//RCC_PFC_LTR_CNTL
#define RCC_PFC_LTR_CNTL__SNOOP_LATENCY_VALUE__SHIFT                                                          0x0
#define RCC_PFC_LTR_CNTL__SNOOP_LATENCY_SCALE__SHIFT                                                          0xa
#define RCC_PFC_LTR_CNTL__SNOOP_REQUIREMENT__SHIFT                                                            0xf
#define RCC_PFC_LTR_CNTL__NONSNOOP_LATENCY_VALUE__SHIFT                                                       0x10
#define RCC_PFC_LTR_CNTL__NONSNOOP_LATENCY_SCALE__SHIFT                                                       0x1a
#define RCC_PFC_LTR_CNTL__NONSNOOP_REQUIREMENT__SHIFT                                                         0x1f
//RCC_PFC_PME_RESTORE
#define RCC_PFC_PME_RESTORE__PME_RESTORE_PME_EN__SHIFT                                                        0x0
#define RCC_PFC_PME_RESTORE__PME_RESTORE_PME_STATUS__SHIFT                                                    0x8
//RCC_PFC_STICKY_RESTORE_0
#define RCC_PFC_STICKY_RESTORE_0__RESTORE_PSN_ERR_STATUS__SHIFT                                               0x0
#define RCC_PFC_STICKY_RESTORE_0__RESTORE_CPL_TIMEOUT_STATUS__SHIFT                                           0x1
#define RCC_PFC_STICKY_RESTORE_0__RESTORE_CPL_ABORT_ERR_STATUS__SHIFT                                         0x2
#define RCC_PFC_STICKY_RESTORE_0__RESTORE_UNEXP_CPL_STATUS__SHIFT                                             0x3
#define RCC_PFC_STICKY_RESTORE_0__RESTORE_MAL_TLP_STATUS__SHIFT                                               0x4
#define RCC_PFC_STICKY_RESTORE_0__RESTORE_ECRC_ERR_STATUS__SHIFT                                              0x5
#define RCC_PFC_STICKY_RESTORE_0__RESTORE_UNSUPP_REQ_ERR_STATUS__SHIFT                                        0x6
#define RCC_PFC_STICKY_RESTORE_0__RESTORE_ADVISORY_NONFATAL_ERR_STATUS__SHIFT                                 0x7
//RCC_PFC_STICKY_RESTORE_1
#define RCC_PFC_STICKY_RESTORE_1__RESTORE_TLP_HDR_0__SHIFT                                                    0x0
//RCC_PFC_STICKY_RESTORE_2
#define RCC_PFC_STICKY_RESTORE_2__RESTORE_TLP_HDR_1__SHIFT                                                    0x0
//RCC_PFC_STICKY_RESTORE_3
#define RCC_PFC_STICKY_RESTORE_3__RESTORE_TLP_HDR_2__SHIFT                                                    0x0
//RCC_PFC_STICKY_RESTORE_4
#define RCC_PFC_STICKY_RESTORE_4__RESTORE_TLP_HDR_3__SHIFT                                                    0x0
//RCC_PFC_STICKY_RESTORE_5
#define RCC_PFC_STICKY_RESTORE_5__RESTORE_TLP_PREFIX__SHIFT                                                   0x0
//RCC_PFC_AUXPWR_CNTL
#define RCC_PFC_AUXPWR_CNTL__AUX_CURRENT_OVERRIDE__SHIFT                                                      0x0
#define RCC_PFC_AUXPWR_CNTL__AUX_POWER_DETECTED_OVERRIDE__SHIFT                                               0x3


// addressBlock: rcc_pfc_amdgfxaz_RCCPFCDEC
//RCCPFCAMDGFXAZ_RCC_PFC_LTR_CNTL
#define RCCPFCAMDGFXAZ_RCC_PFC_LTR_CNTL__SNOOP_LATENCY_VALUE__SHIFT                                           0x0
#define RCCPFCAMDGFXAZ_RCC_PFC_LTR_CNTL__SNOOP_LATENCY_SCALE__SHIFT                                           0xa
#define RCCPFCAMDGFXAZ_RCC_PFC_LTR_CNTL__SNOOP_REQUIREMENT__SHIFT                                             0xf
#define RCCPFCAMDGFXAZ_RCC_PFC_LTR_CNTL__NONSNOOP_LATENCY_VALUE__SHIFT                                        0x10
#define RCCPFCAMDGFXAZ_RCC_PFC_LTR_CNTL__NONSNOOP_LATENCY_SCALE__SHIFT                                        0x1a
#define RCCPFCAMDGFXAZ_RCC_PFC_LTR_CNTL__NONSNOOP_REQUIREMENT__SHIFT                                          0x1f
//RCCPFCAMDGFXAZ_RCC_PFC_PME_RESTORE
#define RCCPFCAMDGFXAZ_RCC_PFC_PME_RESTORE__PME_RESTORE_PME_EN__SHIFT                                         0x0
#define RCCPFCAMDGFXAZ_RCC_PFC_PME_RESTORE__PME_RESTORE_PME_STATUS__SHIFT                                     0x8
//RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_0
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_0__RESTORE_PSN_ERR_STATUS__SHIFT                                0x0
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_0__RESTORE_CPL_TIMEOUT_STATUS__SHIFT                            0x1
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_0__RESTORE_CPL_ABORT_ERR_STATUS__SHIFT                          0x2
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_0__RESTORE_UNEXP_CPL_STATUS__SHIFT                              0x3
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_0__RESTORE_MAL_TLP_STATUS__SHIFT                                0x4
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_0__RESTORE_ECRC_ERR_STATUS__SHIFT                               0x5
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_0__RESTORE_UNSUPP_REQ_ERR_STATUS__SHIFT                         0x6
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_0__RESTORE_ADVISORY_NONFATAL_ERR_STATUS__SHIFT                  0x7
//RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_1
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_1__RESTORE_TLP_HDR_0__SHIFT                                     0x0
//RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_2
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_2__RESTORE_TLP_HDR_1__SHIFT                                     0x0
//RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_3
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_3__RESTORE_TLP_HDR_2__SHIFT                                     0x0
//RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_4
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_4__RESTORE_TLP_HDR_3__SHIFT                                     0x0
//RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_5
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_5__RESTORE_TLP_PREFIX__SHIFT                                    0x0
//RCCPFCAMDGFXAZ_RCC_PFC_AUXPWR_CNTL
#define RCCPFCAMDGFXAZ_RCC_PFC_AUXPWR_CNTL__AUX_CURRENT_OVERRIDE__SHIFT                                       0x0
#define RCCPFCAMDGFXAZ_RCC_PFC_AUXPWR_CNTL__AUX_POWER_DETECTED_OVERRIDE__SHIFT                                0x3


// addressBlock: pciemsix_amdgfx_MSIXTDEC
//PCIEMSIX_VECT0_ADDR_LO
#define PCIEMSIX_VECT0_ADDR_LO__MSG_ADDR_LO__SHIFT                                                            0x2
//PCIEMSIX_VECT0_ADDR_HI
#define PCIEMSIX_VECT0_ADDR_HI__MSG_ADDR_HI__SHIFT                                                            0x0
//PCIEMSIX_VECT0_MSG_DATA
#define PCIEMSIX_VECT0_MSG_DATA__MSG_DATA__SHIFT                                                              0x0
//PCIEMSIX_VECT0_CONTROL
#define PCIEMSIX_VECT0_CONTROL__MASK_BIT__SHIFT                                                               0x0
//PCIEMSIX_VECT1_ADDR_LO
#define PCIEMSIX_VECT1_ADDR_LO__MSG_ADDR_LO__SHIFT                                                            0x2
//PCIEMSIX_VECT1_ADDR_HI
#define PCIEMSIX_VECT1_ADDR_HI__MSG_ADDR_HI__SHIFT                                                            0x0
//PCIEMSIX_VECT1_MSG_DATA
#define PCIEMSIX_VECT1_MSG_DATA__MSG_DATA__SHIFT                                                              0x0
//PCIEMSIX_VECT1_CONTROL
#define PCIEMSIX_VECT1_CONTROL__MASK_BIT__SHIFT                                                               0x0
//PCIEMSIX_VECT2_ADDR_LO
#define PCIEMSIX_VECT2_ADDR_LO__MSG_ADDR_LO__SHIFT                                                            0x2
//PCIEMSIX_VECT2_ADDR_HI
#define PCIEMSIX_VECT2_ADDR_HI__MSG_ADDR_HI__SHIFT                                                            0x0
//PCIEMSIX_VECT2_MSG_DATA
#define PCIEMSIX_VECT2_MSG_DATA__MSG_DATA__SHIFT                                                              0x0
//PCIEMSIX_VECT2_CONTROL
#define PCIEMSIX_VECT2_CONTROL__MASK_BIT__SHIFT                                                               0x0
//PCIEMSIX_VECT3_ADDR_LO
#define PCIEMSIX_VECT3_ADDR_LO__MSG_ADDR_LO__SHIFT                                                            0x2
//PCIEMSIX_VECT3_ADDR_HI
#define PCIEMSIX_VECT3_ADDR_HI__MSG_ADDR_HI__SHIFT                                                            0x0
//PCIEMSIX_VECT3_MSG_DATA
#define PCIEMSIX_VECT3_MSG_DATA__MSG_DATA__SHIFT                                                              0x0
//PCIEMSIX_VECT3_CONTROL
#define PCIEMSIX_VECT3_CONTROL__MASK_BIT__SHIFT                                                               0x0
//PCIEMSIX_VECT4_ADDR_LO
#define PCIEMSIX_VECT4_ADDR_LO__MSG_ADDR_LO__SHIFT                                                            0x2
//PCIEMSIX_VECT4_ADDR_HI
#define PCIEMSIX_VECT4_ADDR_HI__MSG_ADDR_HI__SHIFT                                                            0x0
//PCIEMSIX_VECT4_MSG_DATA
#define PCIEMSIX_VECT4_MSG_DATA__MSG_DATA__SHIFT                                                              0x0
//PCIEMSIX_VECT4_CONTROL
#define PCIEMSIX_VECT4_CONTROL__MASK_BIT__SHIFT                                                               0x0
//PCIEMSIX_VECT5_ADDR_LO
#define PCIEMSIX_VECT5_ADDR_LO__MSG_ADDR_LO__SHIFT                                                            0x2
//PCIEMSIX_VECT5_ADDR_HI
#define PCIEMSIX_VECT5_ADDR_HI__MSG_ADDR_HI__SHIFT                                                            0x0
//PCIEMSIX_VECT5_MSG_DATA
#define PCIEMSIX_VECT5_MSG_DATA__MSG_DATA__SHIFT                                                              0x0
//PCIEMSIX_VECT5_CONTROL
#define PCIEMSIX_VECT5_CONTROL__MASK_BIT__SHIFT                                                               0x0
//PCIEMSIX_VECT6_ADDR_LO
#define PCIEMSIX_VECT6_ADDR_LO__MSG_ADDR_LO__SHIFT                                                            0x2
//PCIEMSIX_VECT6_ADDR_HI
#define PCIEMSIX_VECT6_ADDR_HI__MSG_ADDR_HI__SHIFT                                                            0x0
//PCIEMSIX_VECT6_MSG_DATA
#define PCIEMSIX_VECT6_MSG_DATA__MSG_DATA__SHIFT                                                              0x0
//PCIEMSIX_VECT6_CONTROL
#define PCIEMSIX_VECT6_CONTROL__MASK_BIT__SHIFT                                                               0x0
//PCIEMSIX_VECT7_ADDR_LO
#define PCIEMSIX_VECT7_ADDR_LO__MSG_ADDR_LO__SHIFT                                                            0x2
//PCIEMSIX_VECT7_ADDR_HI
#define PCIEMSIX_VECT7_ADDR_HI__MSG_ADDR_HI__SHIFT                                                            0x0
//PCIEMSIX_VECT7_MSG_DATA
#define PCIEMSIX_VECT7_MSG_DATA__MSG_DATA__SHIFT                                                              0x0
//PCIEMSIX_VECT7_CONTROL
#define PCIEMSIX_VECT7_CONTROL__MASK_BIT__SHIFT                                                               0x0
//PCIEMSIX_VECT8_ADDR_LO
#define PCIEMSIX_VECT8_ADDR_LO__MSG_ADDR_LO__SHIFT                                                            0x2
//PCIEMSIX_VECT8_ADDR_HI
#define PCIEMSIX_VECT8_ADDR_HI__MSG_ADDR_HI__SHIFT                                                            0x0
//PCIEMSIX_VECT8_MSG_DATA
#define PCIEMSIX_VECT8_MSG_DATA__MSG_DATA__SHIFT                                                              0x0
//PCIEMSIX_VECT8_CONTROL
#define PCIEMSIX_VECT8_CONTROL__MASK_BIT__SHIFT                                                               0x0
//PCIEMSIX_VECT9_ADDR_LO
#define PCIEMSIX_VECT9_ADDR_LO__MSG_ADDR_LO__SHIFT                                                            0x2
//PCIEMSIX_VECT9_ADDR_HI
#define PCIEMSIX_VECT9_ADDR_HI__MSG_ADDR_HI__SHIFT                                                            0x0
//PCIEMSIX_VECT9_MSG_DATA
#define PCIEMSIX_VECT9_MSG_DATA__MSG_DATA__SHIFT                                                              0x0
//PCIEMSIX_VECT9_CONTROL
#define PCIEMSIX_VECT9_CONTROL__MASK_BIT__SHIFT                                                               0x0
//PCIEMSIX_VECT10_ADDR_LO
#define PCIEMSIX_VECT10_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT10_ADDR_HI
#define PCIEMSIX_VECT10_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT10_MSG_DATA
#define PCIEMSIX_VECT10_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT10_CONTROL
#define PCIEMSIX_VECT10_CONTROL__MASK_BIT__SHIFT                                                              0x0
//PCIEMSIX_VECT11_ADDR_LO
#define PCIEMSIX_VECT11_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT11_ADDR_HI
#define PCIEMSIX_VECT11_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT11_MSG_DATA
#define PCIEMSIX_VECT11_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT11_CONTROL
#define PCIEMSIX_VECT11_CONTROL__MASK_BIT__SHIFT                                                              0x0
//PCIEMSIX_VECT12_ADDR_LO
#define PCIEMSIX_VECT12_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT12_ADDR_HI
#define PCIEMSIX_VECT12_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT12_MSG_DATA
#define PCIEMSIX_VECT12_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT12_CONTROL
#define PCIEMSIX_VECT12_CONTROL__MASK_BIT__SHIFT                                                              0x0
//PCIEMSIX_VECT13_ADDR_LO
#define PCIEMSIX_VECT13_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT13_ADDR_HI
#define PCIEMSIX_VECT13_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT13_MSG_DATA
#define PCIEMSIX_VECT13_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT13_CONTROL
#define PCIEMSIX_VECT13_CONTROL__MASK_BIT__SHIFT                                                              0x0
//PCIEMSIX_VECT14_ADDR_LO
#define PCIEMSIX_VECT14_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT14_ADDR_HI
#define PCIEMSIX_VECT14_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT14_MSG_DATA
#define PCIEMSIX_VECT14_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT14_CONTROL
#define PCIEMSIX_VECT14_CONTROL__MASK_BIT__SHIFT                                                              0x0
//PCIEMSIX_VECT15_ADDR_LO
#define PCIEMSIX_VECT15_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT15_ADDR_HI
#define PCIEMSIX_VECT15_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT15_MSG_DATA
#define PCIEMSIX_VECT15_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT15_CONTROL
#define PCIEMSIX_VECT15_CONTROL__MASK_BIT__SHIFT                                                              0x0
//PCIEMSIX_VECT16_ADDR_LO
#define PCIEMSIX_VECT16_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT16_ADDR_HI
#define PCIEMSIX_VECT16_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT16_MSG_DATA
#define PCIEMSIX_VECT16_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT16_CONTROL
#define PCIEMSIX_VECT16_CONTROL__MASK_BIT__SHIFT                                                              0x0
//PCIEMSIX_VECT17_ADDR_LO
#define PCIEMSIX_VECT17_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT17_ADDR_HI
#define PCIEMSIX_VECT17_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT17_MSG_DATA
#define PCIEMSIX_VECT17_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT17_CONTROL
#define PCIEMSIX_VECT17_CONTROL__MASK_BIT__SHIFT                                                              0x0
//PCIEMSIX_VECT18_ADDR_LO
#define PCIEMSIX_VECT18_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT18_ADDR_HI
#define PCIEMSIX_VECT18_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT18_MSG_DATA
#define PCIEMSIX_VECT18_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT18_CONTROL
#define PCIEMSIX_VECT18_CONTROL__MASK_BIT__SHIFT                                                              0x0
//PCIEMSIX_VECT19_ADDR_LO
#define PCIEMSIX_VECT19_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT19_ADDR_HI
#define PCIEMSIX_VECT19_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT19_MSG_DATA
#define PCIEMSIX_VECT19_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT19_CONTROL
#define PCIEMSIX_VECT19_CONTROL__MASK_BIT__SHIFT                                                              0x0
//PCIEMSIX_VECT20_ADDR_LO
#define PCIEMSIX_VECT20_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT20_ADDR_HI
#define PCIEMSIX_VECT20_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT20_MSG_DATA
#define PCIEMSIX_VECT20_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT20_CONTROL
#define PCIEMSIX_VECT20_CONTROL__MASK_BIT__SHIFT                                                              0x0
//PCIEMSIX_VECT21_ADDR_LO
#define PCIEMSIX_VECT21_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT21_ADDR_HI
#define PCIEMSIX_VECT21_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT21_MSG_DATA
#define PCIEMSIX_VECT21_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT21_CONTROL
#define PCIEMSIX_VECT21_CONTROL__MASK_BIT__SHIFT                                                              0x0
//PCIEMSIX_VECT22_ADDR_LO
#define PCIEMSIX_VECT22_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT22_ADDR_HI
#define PCIEMSIX_VECT22_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT22_MSG_DATA
#define PCIEMSIX_VECT22_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT22_CONTROL
#define PCIEMSIX_VECT22_CONTROL__MASK_BIT__SHIFT                                                              0x0
//PCIEMSIX_VECT23_ADDR_LO
#define PCIEMSIX_VECT23_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT23_ADDR_HI
#define PCIEMSIX_VECT23_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT23_MSG_DATA
#define PCIEMSIX_VECT23_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT23_CONTROL
#define PCIEMSIX_VECT23_CONTROL__MASK_BIT__SHIFT                                                              0x0
//PCIEMSIX_VECT24_ADDR_LO
#define PCIEMSIX_VECT24_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT24_ADDR_HI
#define PCIEMSIX_VECT24_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT24_MSG_DATA
#define PCIEMSIX_VECT24_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT24_CONTROL
#define PCIEMSIX_VECT24_CONTROL__MASK_BIT__SHIFT                                                              0x0
//PCIEMSIX_VECT25_ADDR_LO
#define PCIEMSIX_VECT25_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT25_ADDR_HI
#define PCIEMSIX_VECT25_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT25_MSG_DATA
#define PCIEMSIX_VECT25_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT25_CONTROL
#define PCIEMSIX_VECT25_CONTROL__MASK_BIT__SHIFT                                                              0x0
//PCIEMSIX_VECT26_ADDR_LO
#define PCIEMSIX_VECT26_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT26_ADDR_HI
#define PCIEMSIX_VECT26_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT26_MSG_DATA
#define PCIEMSIX_VECT26_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT26_CONTROL
#define PCIEMSIX_VECT26_CONTROL__MASK_BIT__SHIFT                                                              0x0
//PCIEMSIX_VECT27_ADDR_LO
#define PCIEMSIX_VECT27_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT27_ADDR_HI
#define PCIEMSIX_VECT27_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT27_MSG_DATA
#define PCIEMSIX_VECT27_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT27_CONTROL
#define PCIEMSIX_VECT27_CONTROL__MASK_BIT__SHIFT                                                              0x0
//PCIEMSIX_VECT28_ADDR_LO
#define PCIEMSIX_VECT28_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT28_ADDR_HI
#define PCIEMSIX_VECT28_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT28_MSG_DATA
#define PCIEMSIX_VECT28_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT28_CONTROL
#define PCIEMSIX_VECT28_CONTROL__MASK_BIT__SHIFT                                                              0x0
//PCIEMSIX_VECT29_ADDR_LO
#define PCIEMSIX_VECT29_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT29_ADDR_HI
#define PCIEMSIX_VECT29_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT29_MSG_DATA
#define PCIEMSIX_VECT29_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT29_CONTROL
#define PCIEMSIX_VECT29_CONTROL__MASK_BIT__SHIFT                                                              0x0
//PCIEMSIX_VECT30_ADDR_LO
#define PCIEMSIX_VECT30_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT30_ADDR_HI
#define PCIEMSIX_VECT30_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT30_MSG_DATA
#define PCIEMSIX_VECT30_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT30_CONTROL
#define PCIEMSIX_VECT30_CONTROL__MASK_BIT__SHIFT                                                              0x0
//PCIEMSIX_VECT31_ADDR_LO
#define PCIEMSIX_VECT31_ADDR_LO__MSG_ADDR_LO__SHIFT                                                           0x2
//PCIEMSIX_VECT31_ADDR_HI
#define PCIEMSIX_VECT31_ADDR_HI__MSG_ADDR_HI__SHIFT                                                           0x0
//PCIEMSIX_VECT31_MSG_DATA
#define PCIEMSIX_VECT31_MSG_DATA__MSG_DATA__SHIFT                                                             0x0
//PCIEMSIX_VECT31_CONTROL
#define PCIEMSIX_VECT31_CONTROL__MASK_BIT__SHIFT                                                              0x0


// addressBlock: pciemsix_amdgfx_MSIXPDEC
//PCIEMSIX_PBA
#define PCIEMSIX_PBA__MSIX_PENDING_BITS__SHIFT                                                                0x0


// addressBlock: syshub_mmreg_ind_syshubind
//SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__HST_CL0_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                    0x0
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__HST_CL1_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                    0x1
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__HST_CL2_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                    0x2
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__HST_CL3_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                    0x3
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__HST_CL4_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                    0x4
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__HST_CL5_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                    0x5
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__HST_CL6_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                    0x6
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__HST_CL7_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                    0x7
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__DMA_CL0_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                    0x10
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__DMA_CL1_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                    0x11
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__DMA_CL2_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                    0x12
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__DMA_CL3_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                    0x13
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__DMA_CL4_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                    0x14
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__DMA_CL5_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                    0x15
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__DMA_CL6_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                    0x16
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__DMA_CL7_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                    0x17
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__SYSHUB_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                     0x1c
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__SYSHUB_SOCCLK_DS_EN__SHIFT                                      0x1f
//SYSHUBMMREGIND_SYSHUB_DS_CTRL2_SOCCLK
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL2_SOCCLK__SYSHUB_SOCCLK_DS_TIMER__SHIFT                                  0x0
//SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK__SYSHUB_bgen_socclk_HST_SW0_bypass_en__SHIFT  0x0
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK__SYSHUB_bgen_socclk_HST_SW1_bypass_en__SHIFT  0x1
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW0_bypass_en__SHIFT  0xf
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW1_bypass_en__SHIFT  0x10
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW2_bypass_en__SHIFT  0x11
//SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK__SYSHUB_bgen_socclk_HST_SW0_imm_en__SHIFT        0x0
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK__SYSHUB_bgen_socclk_HST_SW1_imm_en__SHIFT        0x1
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW0_imm_en__SHIFT        0xf
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW1_imm_en__SHIFT        0x10
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW2_imm_en__SHIFT        0x11
//SYSHUBMMREGIND_DMA_CLK0_SW0_SYSHUB_QOS_CNTL
#define SYSHUBMMREGIND_DMA_CLK0_SW0_SYSHUB_QOS_CNTL__QOS_CNTL_MODE__SHIFT                                     0x0
#define SYSHUBMMREGIND_DMA_CLK0_SW0_SYSHUB_QOS_CNTL__QOS_MAX_VALUE__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK0_SW0_SYSHUB_QOS_CNTL__QOS_MIN_VALUE__SHIFT                                     0x5
//SYSHUBMMREGIND_DMA_CLK0_SW1_SYSHUB_QOS_CNTL
#define SYSHUBMMREGIND_DMA_CLK0_SW1_SYSHUB_QOS_CNTL__QOS_CNTL_MODE__SHIFT                                     0x0
#define SYSHUBMMREGIND_DMA_CLK0_SW1_SYSHUB_QOS_CNTL__QOS_MAX_VALUE__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK0_SW1_SYSHUB_QOS_CNTL__QOS_MIN_VALUE__SHIFT                                     0x5
//SYSHUBMMREGIND_DMA_CLK0_SW0_CL0_CNTL
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL0_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                       0x0
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL0_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL0_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                   0x8
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL0_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                0x9
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL0_CNTL__READ_WRR_WEIGHT__SHIFT                                          0x10
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL0_CNTL__WRITE_WRR_WEIGHT__SHIFT                                         0x18
//SYSHUBMMREGIND_DMA_CLK0_SW0_CL1_CNTL
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL1_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                       0x0
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL1_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL1_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                   0x8
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL1_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                0x9
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL1_CNTL__READ_WRR_WEIGHT__SHIFT                                          0x10
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL1_CNTL__WRITE_WRR_WEIGHT__SHIFT                                         0x18
//SYSHUBMMREGIND_DMA_CLK0_SW0_CL2_CNTL
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL2_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                       0x0
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL2_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL2_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                   0x8
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL2_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                0x9
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL2_CNTL__READ_WRR_WEIGHT__SHIFT                                          0x10
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL2_CNTL__WRITE_WRR_WEIGHT__SHIFT                                         0x18
//SYSHUBMMREGIND_DMA_CLK0_SW0_CL3_CNTL
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL3_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                       0x0
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL3_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL3_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                   0x8
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL3_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                0x9
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL3_CNTL__READ_WRR_WEIGHT__SHIFT                                          0x10
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL3_CNTL__WRITE_WRR_WEIGHT__SHIFT                                         0x18
//SYSHUBMMREGIND_DMA_CLK0_SW0_CL4_CNTL
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL4_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                       0x0
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL4_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL4_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                   0x8
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL4_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                0x9
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL4_CNTL__READ_WRR_WEIGHT__SHIFT                                          0x10
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL4_CNTL__WRITE_WRR_WEIGHT__SHIFT                                         0x18
//SYSHUBMMREGIND_DMA_CLK0_SW0_CL5_CNTL
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL5_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                       0x0
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL5_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL5_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                   0x8
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL5_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                0x9
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL5_CNTL__READ_WRR_WEIGHT__SHIFT                                          0x10
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL5_CNTL__WRITE_WRR_WEIGHT__SHIFT                                         0x18
//SYSHUBMMREGIND_DMA_CLK0_SW1_CL0_CNTL
#define SYSHUBMMREGIND_DMA_CLK0_SW1_CL0_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                       0x0
#define SYSHUBMMREGIND_DMA_CLK0_SW1_CL0_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK0_SW1_CL0_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                   0x8
#define SYSHUBMMREGIND_DMA_CLK0_SW1_CL0_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                0x9
#define SYSHUBMMREGIND_DMA_CLK0_SW1_CL0_CNTL__READ_WRR_WEIGHT__SHIFT                                          0x10
#define SYSHUBMMREGIND_DMA_CLK0_SW1_CL0_CNTL__WRITE_WRR_WEIGHT__SHIFT                                         0x18
//SYSHUBMMREGIND_DMA_CLK0_SW2_CL0_CNTL
#define SYSHUBMMREGIND_DMA_CLK0_SW2_CL0_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                       0x0
#define SYSHUBMMREGIND_DMA_CLK0_SW2_CL0_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK0_SW2_CL0_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                   0x8
#define SYSHUBMMREGIND_DMA_CLK0_SW2_CL0_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                0x9
#define SYSHUBMMREGIND_DMA_CLK0_SW2_CL0_CNTL__READ_WRR_WEIGHT__SHIFT                                          0x10
#define SYSHUBMMREGIND_DMA_CLK0_SW2_CL0_CNTL__WRITE_WRR_WEIGHT__SHIFT                                         0x18
//SYSHUBMMREGIND_SYSHUB_CG_CNTL
#define SYSHUBMMREGIND_SYSHUB_CG_CNTL__SYSHUB_CG_EN__SHIFT                                                    0x0
#define SYSHUBMMREGIND_SYSHUB_CG_CNTL__SYSHUB_CG_IDLE_TIMER__SHIFT                                            0x8
#define SYSHUBMMREGIND_SYSHUB_CG_CNTL__SYSHUB_CG_WAKEUP_TIMER__SHIFT                                          0x10
//SYSHUBMMREGIND_SYSHUB_TRANS_IDLE
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF0__SHIFT                                        0x0
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF1__SHIFT                                        0x1
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF2__SHIFT                                        0x2
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF3__SHIFT                                        0x3
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF4__SHIFT                                        0x4
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF5__SHIFT                                        0x5
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF6__SHIFT                                        0x6
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF7__SHIFT                                        0x7
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF8__SHIFT                                        0x8
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF9__SHIFT                                        0x9
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF10__SHIFT                                       0xa
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF11__SHIFT                                       0xb
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF12__SHIFT                                       0xc
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF13__SHIFT                                       0xd
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF14__SHIFT                                       0xe
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF15__SHIFT                                       0xf
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_PF__SHIFT                                         0x10
//SYSHUBMMREGIND_SYSHUB_HP_TIMER
#define SYSHUBMMREGIND_SYSHUB_HP_TIMER__SYSHUB_HP_TIMER__SHIFT                                                0x0
//SYSHUBMMREGIND_SYSHUB_SCRATCH
#define SYSHUBMMREGIND_SYSHUB_SCRATCH__SCRATCH__SHIFT                                                         0x0
//SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__HST_CL0_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                  0x0
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__HST_CL1_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                  0x1
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__HST_CL2_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                  0x2
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__HST_CL3_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                  0x3
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__HST_CL4_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                  0x4
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__HST_CL5_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                  0x5
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__HST_CL6_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                  0x6
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__HST_CL7_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                  0x7
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__DMA_CL0_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                  0x10
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__DMA_CL1_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                  0x11
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__DMA_CL2_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                  0x12
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__DMA_CL3_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                  0x13
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__DMA_CL4_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                  0x14
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__DMA_CL5_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                  0x15
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__DMA_CL6_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                  0x16
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__DMA_CL7_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                  0x17
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__SYSHUB_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__SHIFT                   0x1c
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__SYSHUB_SHUBCLK_DS_EN__SHIFT                                    0x1f
//SYSHUBMMREGIND_SYSHUB_DS_CTRL2_SHUBCLK
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL2_SHUBCLK__SYSHUB_SHUBCLK_DS_TIMER__SHIFT                                0x0
//SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SHUBCLK
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SHUBCLK__SYSHUB_bgen_shubclk_DMA_SW0_bypass_en__SHIFT  0xf
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SHUBCLK__SYSHUB_bgen_shubclk_DMA_SW1_bypass_en__SHIFT  0x10
//SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SHUBCLK
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SHUBCLK__SYSHUB_bgen_shubclk_DMA_SW0_imm_en__SHIFT      0xf
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SHUBCLK__SYSHUB_bgen_shubclk_DMA_SW1_imm_en__SHIFT      0x10
//SYSHUBMMREGIND_DMA_CLK1_SW0_SYSHUB_QOS_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW0_SYSHUB_QOS_CNTL__QOS_CNTL_MODE__SHIFT                                     0x0
#define SYSHUBMMREGIND_DMA_CLK1_SW0_SYSHUB_QOS_CNTL__QOS_MAX_VALUE__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK1_SW0_SYSHUB_QOS_CNTL__QOS_MIN_VALUE__SHIFT                                     0x5
//SYSHUBMMREGIND_DMA_CLK1_SW1_SYSHUB_QOS_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW1_SYSHUB_QOS_CNTL__QOS_CNTL_MODE__SHIFT                                     0x0
#define SYSHUBMMREGIND_DMA_CLK1_SW1_SYSHUB_QOS_CNTL__QOS_MAX_VALUE__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK1_SW1_SYSHUB_QOS_CNTL__QOS_MIN_VALUE__SHIFT                                     0x5
//SYSHUBMMREGIND_DMA_CLK1_SW0_CL0_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL0_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                       0x0
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL0_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL0_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                   0x8
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL0_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                0x9
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL0_CNTL__READ_WRR_WEIGHT__SHIFT                                          0x10
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL0_CNTL__WRITE_WRR_WEIGHT__SHIFT                                         0x18
//SYSHUBMMREGIND_DMA_CLK1_SW0_CL1_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL1_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                       0x0
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL1_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL1_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                   0x8
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL1_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                0x9
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL1_CNTL__READ_WRR_WEIGHT__SHIFT                                          0x10
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL1_CNTL__WRITE_WRR_WEIGHT__SHIFT                                         0x18
//SYSHUBMMREGIND_DMA_CLK1_SW0_CL2_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL2_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                       0x0
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL2_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL2_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                   0x8
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL2_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                0x9
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL2_CNTL__READ_WRR_WEIGHT__SHIFT                                          0x10
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL2_CNTL__WRITE_WRR_WEIGHT__SHIFT                                         0x18
//SYSHUBMMREGIND_DMA_CLK1_SW0_CL3_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL3_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                       0x0
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL3_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL3_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                   0x8
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL3_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                0x9
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL3_CNTL__READ_WRR_WEIGHT__SHIFT                                          0x10
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL3_CNTL__WRITE_WRR_WEIGHT__SHIFT                                         0x18
//SYSHUBMMREGIND_DMA_CLK1_SW0_CL4_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL4_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                       0x0
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL4_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL4_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                   0x8
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL4_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                0x9
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL4_CNTL__READ_WRR_WEIGHT__SHIFT                                          0x10
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL4_CNTL__WRITE_WRR_WEIGHT__SHIFT                                         0x18
//SYSHUBMMREGIND_DMA_CLK1_SW1_CL0_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL0_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                       0x0
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL0_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL0_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                   0x8
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL0_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                0x9
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL0_CNTL__READ_WRR_WEIGHT__SHIFT                                          0x10
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL0_CNTL__WRITE_WRR_WEIGHT__SHIFT                                         0x18
//SYSHUBMMREGIND_DMA_CLK1_SW1_CL1_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL1_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                       0x0
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL1_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL1_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                   0x8
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL1_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                0x9
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL1_CNTL__READ_WRR_WEIGHT__SHIFT                                          0x10
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL1_CNTL__WRITE_WRR_WEIGHT__SHIFT                                         0x18
//SYSHUBMMREGIND_DMA_CLK1_SW1_CL2_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL2_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                       0x0
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL2_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL2_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                   0x8
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL2_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                0x9
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL2_CNTL__READ_WRR_WEIGHT__SHIFT                                          0x10
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL2_CNTL__WRITE_WRR_WEIGHT__SHIFT                                         0x18
//SYSHUBMMREGIND_DMA_CLK1_SW1_CL3_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL3_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                       0x0
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL3_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL3_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                   0x8
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL3_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                0x9
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL3_CNTL__READ_WRR_WEIGHT__SHIFT                                          0x10
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL3_CNTL__WRITE_WRR_WEIGHT__SHIFT                                         0x18
//SYSHUBMMREGIND_DMA_CLK1_SW1_CL4_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL4_CNTL__FLR_ON_RS_RESET_EN__SHIFT                                       0x0
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL4_CNTL__LKRST_ON_RS_RESET_EN__SHIFT                                     0x1
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL4_CNTL__QOS_STATIC_OVERRIDE_EN__SHIFT                                   0x8
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL4_CNTL__QOS_STATIC_OVERRIDE_VALUE__SHIFT                                0x9
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL4_CNTL__READ_WRR_WEIGHT__SHIFT                                          0x10
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL4_CNTL__WRITE_WRR_WEIGHT__SHIFT                                         0x18
//MASK


// addressBlock: bif_cfg_dev0_epf0_bifcfgdecp
//VENDOR_ID
#define VENDOR_ID__VENDOR_ID__MASK                                                                            0xFFFFL
//DEVICE_ID
#define DEVICE_ID__DEVICE_ID__MASK                                                                            0xFFFFL
//COMMAND
#define COMMAND__IO_ACCESS_EN__MASK                                                                           0x0001L
#define COMMAND__MEM_ACCESS_EN__MASK                                                                          0x0002L
#define COMMAND__BUS_MASTER_EN__MASK                                                                          0x0004L
#define COMMAND__SPECIAL_CYCLE_EN__MASK                                                                       0x0008L
#define COMMAND__MEM_WRITE_INVALIDATE_EN__MASK                                                                0x0010L
#define COMMAND__PAL_SNOOP_EN__MASK                                                                           0x0020L
#define COMMAND__PARITY_ERROR_RESPONSE__MASK                                                                  0x0040L
#define COMMAND__AD_STEPPING__MASK                                                                            0x0080L
#define COMMAND__SERR_EN__MASK                                                                                0x0100L
#define COMMAND__FAST_B2B_EN__MASK                                                                            0x0200L
#define COMMAND__INT_DIS__MASK                                                                                0x0400L
//STATUS
#define STATUS__INT_STATUS__MASK                                                                              0x0008L
#define STATUS__CAP_LIST__MASK                                                                                0x0010L
#define STATUS__PCI_66_EN__MASK                                                                               0x0020L
#define STATUS__FAST_BACK_CAPABLE__MASK                                                                       0x0080L
#define STATUS__MASTER_DATA_PARITY_ERROR__MASK                                                                0x0100L
#define STATUS__DEVSEL_TIMING__MASK                                                                           0x0600L
#define STATUS__SIGNAL_TARGET_ABORT__MASK                                                                     0x0800L
#define STATUS__RECEIVED_TARGET_ABORT__MASK                                                                   0x1000L
#define STATUS__RECEIVED_MASTER_ABORT__MASK                                                                   0x2000L
#define STATUS__SIGNALED_SYSTEM_ERROR__MASK                                                                   0x4000L
#define STATUS__PARITY_ERROR_DETECTED__MASK                                                                   0x8000L
//REVISION_ID
#define REVISION_ID__MINOR_REV_ID__MASK                                                                       0x0FL
#define REVISION_ID__MAJOR_REV_ID__MASK                                                                       0xF0L
//PROG_INTERFACE
#define PROG_INTERFACE__PROG_INTERFACE__MASK                                                                  0xFFL
//SUB_CLASS
#define SUB_CLASS__SUB_CLASS__MASK                                                                            0xFFL
//BASE_CLASS
#define BASE_CLASS__BASE_CLASS__MASK                                                                          0xFFL
//CACHE_LINE
#define CACHE_LINE__CACHE_LINE_SIZE__MASK                                                                     0xFFL
//LATENCY
#define LATENCY__LATENCY_TIMER__MASK                                                                          0xFFL
//HEADER
#define HEADER__HEADER_TYPE__MASK                                                                             0x7FL
#define HEADER__DEVICE_TYPE__MASK                                                                             0x80L
//BIST
#define BIST__BIST_COMP__MASK                                                                                 0x0FL
#define BIST__BIST_STRT__MASK                                                                                 0x40L
#define BIST__BIST_CAP__MASK                                                                                  0x80L
//BASE_ADDR_1
#define BASE_ADDR_1__BASE_ADDR__MASK                                                                          0xFFFFFFFFL
//BASE_ADDR_2
#define BASE_ADDR_2__BASE_ADDR__MASK                                                                          0xFFFFFFFFL
//BASE_ADDR_3
#define BASE_ADDR_3__BASE_ADDR__MASK                                                                          0xFFFFFFFFL
//BASE_ADDR_4
#define BASE_ADDR_4__BASE_ADDR__MASK                                                                          0xFFFFFFFFL
//BASE_ADDR_5
#define BASE_ADDR_5__BASE_ADDR__MASK                                                                          0xFFFFFFFFL
//BASE_ADDR_6
#define BASE_ADDR_6__BASE_ADDR__MASK                                                                          0xFFFFFFFFL
//ADAPTER_ID
#define ADAPTER_ID__SUBSYSTEM_VENDOR_ID__MASK                                                                 0x0000FFFFL
#define ADAPTER_ID__SUBSYSTEM_ID__MASK                                                                        0xFFFF0000L
//ROM_BASE_ADDR
#define ROM_BASE_ADDR__BASE_ADDR__MASK                                                                        0xFFFFFFFFL
//CAP_PTR
#define CAP_PTR__CAP_PTR__MASK                                                                                0x000000FFL
//INTERRUPT_LINE
#define INTERRUPT_LINE__INTERRUPT_LINE__MASK                                                                  0xFFL
//INTERRUPT_PIN
#define INTERRUPT_PIN__INTERRUPT_PIN__MASK                                                                    0xFFL
//MIN_GRANT
#define MIN_GRANT__MIN_GNT__MASK                                                                              0xFFL
//MAX_LATENCY
#define MAX_LATENCY__MAX_LAT__MASK                                                                            0xFFL
//VENDOR_CAP_LIST
#define VENDOR_CAP_LIST__CAP_ID__MASK                                                                         0x000000FFL
#define VENDOR_CAP_LIST__NEXT_PTR__MASK                                                                       0x0000FF00L
#define VENDOR_CAP_LIST__LENGTH__MASK                                                                         0x00FF0000L
//ADAPTER_ID_W
#define ADAPTER_ID_W__SUBSYSTEM_VENDOR_ID__MASK                                                               0x0000FFFFL
#define ADAPTER_ID_W__SUBSYSTEM_ID__MASK                                                                      0xFFFF0000L
//PMI_CAP_LIST
#define PMI_CAP_LIST__CAP_ID__MASK                                                                            0x00FFL
#define PMI_CAP_LIST__NEXT_PTR__MASK                                                                          0xFF00L
//PMI_CAP
#define PMI_CAP__VERSION__MASK                                                                                0x0007L
#define PMI_CAP__PME_CLOCK__MASK                                                                              0x0008L
#define PMI_CAP__DEV_SPECIFIC_INIT__MASK                                                                      0x0020L
#define PMI_CAP__AUX_CURRENT__MASK                                                                            0x01C0L
#define PMI_CAP__D1_SUPPORT__MASK                                                                             0x0200L
#define PMI_CAP__D2_SUPPORT__MASK                                                                             0x0400L
#define PMI_CAP__PME_SUPPORT__MASK                                                                            0xF800L
//PMI_STATUS_CNTL
#define PMI_STATUS_CNTL__POWER_STATE__MASK                                                                    0x00000003L
#define PMI_STATUS_CNTL__NO_SOFT_RESET__MASK                                                                  0x00000008L
#define PMI_STATUS_CNTL__PME_EN__MASK                                                                         0x00000100L
#define PMI_STATUS_CNTL__DATA_SELECT__MASK                                                                    0x00001E00L
#define PMI_STATUS_CNTL__DATA_SCALE__MASK                                                                     0x00006000L
#define PMI_STATUS_CNTL__PME_STATUS__MASK                                                                     0x00008000L
#define PMI_STATUS_CNTL__B2_B3_SUPPORT__MASK                                                                  0x00400000L
#define PMI_STATUS_CNTL__BUS_PWR_EN__MASK                                                                     0x00800000L
#define PMI_STATUS_CNTL__PMI_DATA__MASK                                                                       0xFF000000L
//PCIE_CAP_LIST
#define PCIE_CAP_LIST__CAP_ID__MASK                                                                           0x00FFL
#define PCIE_CAP_LIST__NEXT_PTR__MASK                                                                         0xFF00L
//PCIE_CAP
#define PCIE_CAP__VERSION__MASK                                                                               0x000FL
#define PCIE_CAP__DEVICE_TYPE__MASK                                                                           0x00F0L
#define PCIE_CAP__SLOT_IMPLEMENTED__MASK                                                                      0x0100L
#define PCIE_CAP__INT_MESSAGE_NUM__MASK                                                                       0x3E00L
//DEVICE_CAP
#define DEVICE_CAP__MAX_PAYLOAD_SUPPORT__MASK                                                                 0x00000007L
#define DEVICE_CAP__PHANTOM_FUNC__MASK                                                                        0x00000018L
#define DEVICE_CAP__EXTENDED_TAG__MASK                                                                        0x00000020L
#define DEVICE_CAP__L0S_ACCEPTABLE_LATENCY__MASK                                                              0x000001C0L
#define DEVICE_CAP__L1_ACCEPTABLE_LATENCY__MASK                                                               0x00000E00L
#define DEVICE_CAP__ROLE_BASED_ERR_REPORTING__MASK                                                            0x00008000L
#define DEVICE_CAP__CAPTURED_SLOT_POWER_LIMIT__MASK                                                           0x03FC0000L
#define DEVICE_CAP__CAPTURED_SLOT_POWER_SCALE__MASK                                                           0x0C000000L
#define DEVICE_CAP__FLR_CAPABLE__MASK                                                                         0x10000000L
//DEVICE_CNTL
#define DEVICE_CNTL__CORR_ERR_EN__MASK                                                                        0x0001L
#define DEVICE_CNTL__NON_FATAL_ERR_EN__MASK                                                                   0x0002L
#define DEVICE_CNTL__FATAL_ERR_EN__MASK                                                                       0x0004L
#define DEVICE_CNTL__USR_REPORT_EN__MASK                                                                      0x0008L
#define DEVICE_CNTL__RELAXED_ORD_EN__MASK                                                                     0x0010L
#define DEVICE_CNTL__MAX_PAYLOAD_SIZE__MASK                                                                   0x00E0L
#define DEVICE_CNTL__EXTENDED_TAG_EN__MASK                                                                    0x0100L
#define DEVICE_CNTL__PHANTOM_FUNC_EN__MASK                                                                    0x0200L
#define DEVICE_CNTL__AUX_POWER_PM_EN__MASK                                                                    0x0400L
#define DEVICE_CNTL__NO_SNOOP_EN__MASK                                                                        0x0800L
#define DEVICE_CNTL__MAX_READ_REQUEST_SIZE__MASK                                                              0x7000L
#define DEVICE_CNTL__INITIATE_FLR__MASK                                                                       0x8000L
//DEVICE_STATUS
#define DEVICE_STATUS__CORR_ERR__MASK                                                                         0x0001L
#define DEVICE_STATUS__NON_FATAL_ERR__MASK                                                                    0x0002L
#define DEVICE_STATUS__FATAL_ERR__MASK                                                                        0x0004L
#define DEVICE_STATUS__USR_DETECTED__MASK                                                                     0x0008L
#define DEVICE_STATUS__AUX_PWR__MASK                                                                          0x0010L
#define DEVICE_STATUS__TRANSACTIONS_PEND__MASK                                                                0x0020L
//LINK_CAP
#define LINK_CAP__LINK_SPEED__MASK                                                                            0x0000000FL
#define LINK_CAP__LINK_WIDTH__MASK                                                                            0x000003F0L
#define LINK_CAP__PM_SUPPORT__MASK                                                                            0x00000C00L
#define LINK_CAP__L0S_EXIT_LATENCY__MASK                                                                      0x00007000L
#define LINK_CAP__L1_EXIT_LATENCY__MASK                                                                       0x00038000L
#define LINK_CAP__CLOCK_POWER_MANAGEMENT__MASK                                                                0x00040000L
#define LINK_CAP__SURPRISE_DOWN_ERR_REPORTING__MASK                                                           0x00080000L
#define LINK_CAP__DL_ACTIVE_REPORTING_CAPABLE__MASK                                                           0x00100000L
#define LINK_CAP__LINK_BW_NOTIFICATION_CAP__MASK                                                              0x00200000L
#define LINK_CAP__ASPM_OPTIONALITY_COMPLIANCE__MASK                                                           0x00400000L
#define LINK_CAP__PORT_NUMBER__MASK                                                                           0xFF000000L
//LINK_CNTL
#define LINK_CNTL__PM_CONTROL__MASK                                                                           0x0003L
#define LINK_CNTL__READ_CPL_BOUNDARY__MASK                                                                    0x0008L
#define LINK_CNTL__LINK_DIS__MASK                                                                             0x0010L
#define LINK_CNTL__RETRAIN_LINK__MASK                                                                         0x0020L
#define LINK_CNTL__COMMON_CLOCK_CFG__MASK                                                                     0x0040L
#define LINK_CNTL__EXTENDED_SYNC__MASK                                                                        0x0080L
#define LINK_CNTL__CLOCK_POWER_MANAGEMENT_EN__MASK                                                            0x0100L
#define LINK_CNTL__HW_AUTONOMOUS_WIDTH_DISABLE__MASK                                                          0x0200L
#define LINK_CNTL__LINK_BW_MANAGEMENT_INT_EN__MASK                                                            0x0400L
#define LINK_CNTL__LINK_AUTONOMOUS_BW_INT_EN__MASK                                                            0x0800L
//LINK_STATUS
#define LINK_STATUS__CURRENT_LINK_SPEED__MASK                                                                 0x000FL
#define LINK_STATUS__NEGOTIATED_LINK_WIDTH__MASK                                                              0x03F0L
#define LINK_STATUS__LINK_TRAINING__MASK                                                                      0x0800L
#define LINK_STATUS__SLOT_CLOCK_CFG__MASK                                                                     0x1000L
#define LINK_STATUS__DL_ACTIVE__MASK                                                                          0x2000L
#define LINK_STATUS__LINK_BW_MANAGEMENT_STATUS__MASK                                                          0x4000L
#define LINK_STATUS__LINK_AUTONOMOUS_BW_STATUS__MASK                                                          0x8000L
//DEVICE_CAP2
#define DEVICE_CAP2__CPL_TIMEOUT_RANGE_SUPPORTED__MASK                                                        0x0000000FL
#define DEVICE_CAP2__CPL_TIMEOUT_DIS_SUPPORTED__MASK                                                          0x00000010L
#define DEVICE_CAP2__ARI_FORWARDING_SUPPORTED__MASK                                                           0x00000020L
#define DEVICE_CAP2__ATOMICOP_ROUTING_SUPPORTED__MASK                                                         0x00000040L
#define DEVICE_CAP2__ATOMICOP_32CMPLT_SUPPORTED__MASK                                                         0x00000080L
#define DEVICE_CAP2__ATOMICOP_64CMPLT_SUPPORTED__MASK                                                         0x00000100L
#define DEVICE_CAP2__CAS128_CMPLT_SUPPORTED__MASK                                                             0x00000200L
#define DEVICE_CAP2__NO_RO_ENABLED_P2P_PASSING__MASK                                                          0x00000400L
#define DEVICE_CAP2__LTR_SUPPORTED__MASK                                                                      0x00000800L
#define DEVICE_CAP2__TPH_CPLR_SUPPORTED__MASK                                                                 0x00003000L
#define DEVICE_CAP2__OBFF_SUPPORTED__MASK                                                                     0x000C0000L
#define DEVICE_CAP2__EXTENDED_FMT_FIELD_SUPPORTED__MASK                                                       0x00100000L
#define DEVICE_CAP2__END_END_TLP_PREFIX_SUPPORTED__MASK                                                       0x00200000L
#define DEVICE_CAP2__MAX_END_END_TLP_PREFIXES__MASK                                                           0x00C00000L
//DEVICE_CNTL2
#define DEVICE_CNTL2__CPL_TIMEOUT_VALUE__MASK                                                                 0x000FL
#define DEVICE_CNTL2__CPL_TIMEOUT_DIS__MASK                                                                   0x0010L
#define DEVICE_CNTL2__ARI_FORWARDING_EN__MASK                                                                 0x0020L
#define DEVICE_CNTL2__ATOMICOP_REQUEST_EN__MASK                                                               0x0040L
#define DEVICE_CNTL2__ATOMICOP_EGRESS_BLOCKING__MASK                                                          0x0080L
#define DEVICE_CNTL2__IDO_REQUEST_ENABLE__MASK                                                                0x0100L
#define DEVICE_CNTL2__IDO_COMPLETION_ENABLE__MASK                                                             0x0200L
#define DEVICE_CNTL2__LTR_EN__MASK                                                                            0x0400L
#define DEVICE_CNTL2__OBFF_EN__MASK                                                                           0x6000L
#define DEVICE_CNTL2__END_END_TLP_PREFIX_BLOCKING__MASK                                                       0x8000L
//DEVICE_STATUS2
#define DEVICE_STATUS2__RESERVED__MASK                                                                        0xFFFFL
//LINK_CAP2
#define LINK_CAP2__SUPPORTED_LINK_SPEED__MASK                                                                 0x000000FEL
#define LINK_CAP2__CROSSLINK_SUPPORTED__MASK                                                                  0x00000100L
#define LINK_CAP2__RESERVED__MASK                                                                             0xFFFFFE00L
//LINK_CNTL2
#define LINK_CNTL2__TARGET_LINK_SPEED__MASK                                                                   0x000FL
#define LINK_CNTL2__ENTER_COMPLIANCE__MASK                                                                    0x0010L
#define LINK_CNTL2__HW_AUTONOMOUS_SPEED_DISABLE__MASK                                                         0x0020L
#define LINK_CNTL2__SELECTABLE_DEEMPHASIS__MASK                                                               0x0040L
#define LINK_CNTL2__XMIT_MARGIN__MASK                                                                         0x0380L
#define LINK_CNTL2__ENTER_MOD_COMPLIANCE__MASK                                                                0x0400L
#define LINK_CNTL2__COMPLIANCE_SOS__MASK                                                                      0x0800L
#define LINK_CNTL2__COMPLIANCE_DEEMPHASIS__MASK                                                               0xF000L
//LINK_STATUS2
#define LINK_STATUS2__CUR_DEEMPHASIS_LEVEL__MASK                                                              0x0001L
#define LINK_STATUS2__EQUALIZATION_COMPLETE__MASK                                                             0x0002L
#define LINK_STATUS2__EQUALIZATION_PHASE1_SUCCESS__MASK                                                       0x0004L
#define LINK_STATUS2__EQUALIZATION_PHASE2_SUCCESS__MASK                                                       0x0008L
#define LINK_STATUS2__EQUALIZATION_PHASE3_SUCCESS__MASK                                                       0x0010L
#define LINK_STATUS2__LINK_EQUALIZATION_REQUEST__MASK                                                         0x0020L
//SLOT_CAP2
#define SLOT_CAP2__RESERVED__MASK                                                                             0xFFFFFFFFL
//SLOT_CNTL2
#define SLOT_CNTL2__RESERVED__MASK                                                                            0xFFFFL
//SLOT_STATUS2
#define SLOT_STATUS2__RESERVED__MASK                                                                          0xFFFFL
//MSI_CAP_LIST
#define MSI_CAP_LIST__CAP_ID__MASK                                                                            0x00FFL
#define MSI_CAP_LIST__NEXT_PTR__MASK                                                                          0xFF00L
//MSI_MSG_CNTL
#define MSI_MSG_CNTL__MSI_EN__MASK                                                                            0x0001L
#define MSI_MSG_CNTL__MSI_MULTI_CAP__MASK                                                                     0x000EL
#define MSI_MSG_CNTL__MSI_MULTI_EN__MASK                                                                      0x0070L
#define MSI_MSG_CNTL__MSI_64BIT__MASK                                                                         0x0080L
#define MSI_MSG_CNTL__MSI_PERVECTOR_MASKING_CAP__MASK                                                         0x0100L
//MSI_MSG_ADDR_LO
#define MSI_MSG_ADDR_LO__MSI_MSG_ADDR_LO__MASK                                                                0xFFFFFFFCL
//MSI_MSG_ADDR_HI
#define MSI_MSG_ADDR_HI__MSI_MSG_ADDR_HI__MASK                                                                0xFFFFFFFFL
//MSI_MSG_DATA
#define MSI_MSG_DATA__MSI_DATA__MASK                                                                          0x0000FFFFL
//MSI_MSG_DATA_64
#define MSI_MSG_DATA_64__MSI_DATA_64__MASK                                                                    0x0000FFFFL
//MSI_MASK
#define MSI_MASK__MSI_MASK__MASK                                                                              0xFFFFFFFFL
//MSI_PENDING
#define MSI_PENDING__MSI_PENDING__MASK                                                                        0xFFFFFFFFL
//MSI_MASK_64
#define MSI_MASK_64__MSI_MASK_64__MASK                                                                        0xFFFFFFFFL
//MSI_PENDING_64
#define MSI_PENDING_64__MSI_PENDING_64__MASK                                                                  0xFFFFFFFFL
//MSIX_CAP_LIST
#define MSIX_CAP_LIST__CAP_ID__MASK                                                                           0x00FFL
#define MSIX_CAP_LIST__NEXT_PTR__MASK                                                                         0xFF00L
//MSIX_MSG_CNTL
#define MSIX_MSG_CNTL__MSIX_TABLE_SIZE__MASK                                                                  0x07FFL
#define MSIX_MSG_CNTL__MSIX_FUNC_MASK__MASK                                                                   0x4000L
#define MSIX_MSG_CNTL__MSIX_EN__MASK                                                                          0x8000L
//MSIX_TABLE
#define MSIX_TABLE__MSIX_TABLE_BIR__MASK                                                                      0x00000007L
#define MSIX_TABLE__MSIX_TABLE_OFFSET__MASK                                                                   0xFFFFFFF8L
//MSIX_PBA
#define MSIX_PBA__MSIX_PBA_BIR__MASK                                                                          0x00000007L
#define MSIX_PBA__MSIX_PBA_OFFSET__MASK                                                                       0xFFFFFFF8L
//PCIE_VENDOR_SPECIFIC_ENH_CAP_LIST
#define PCIE_VENDOR_SPECIFIC_ENH_CAP_LIST__CAP_ID__MASK                                                       0x0000FFFFL
#define PCIE_VENDOR_SPECIFIC_ENH_CAP_LIST__CAP_VER__MASK                                                      0x000F0000L
#define PCIE_VENDOR_SPECIFIC_ENH_CAP_LIST__NEXT_PTR__MASK                                                     0xFFF00000L
//PCIE_VENDOR_SPECIFIC_HDR
#define PCIE_VENDOR_SPECIFIC_HDR__VSEC_ID__MASK                                                               0x0000FFFFL
#define PCIE_VENDOR_SPECIFIC_HDR__VSEC_REV__MASK                                                              0x000F0000L
#define PCIE_VENDOR_SPECIFIC_HDR__VSEC_LENGTH__MASK                                                           0xFFF00000L
//PCIE_VENDOR_SPECIFIC1
#define PCIE_VENDOR_SPECIFIC1__SCRATCH__MASK                                                                  0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC2
#define PCIE_VENDOR_SPECIFIC2__SCRATCH__MASK                                                                  0xFFFFFFFFL
//PCIE_VC_ENH_CAP_LIST
#define PCIE_VC_ENH_CAP_LIST__CAP_ID__MASK                                                                    0x0000FFFFL
#define PCIE_VC_ENH_CAP_LIST__CAP_VER__MASK                                                                   0x000F0000L
#define PCIE_VC_ENH_CAP_LIST__NEXT_PTR__MASK                                                                  0xFFF00000L
//PCIE_PORT_VC_CAP_REG1
#define PCIE_PORT_VC_CAP_REG1__EXT_VC_COUNT__MASK                                                             0x00000007L
#define PCIE_PORT_VC_CAP_REG1__LOW_PRIORITY_EXT_VC_COUNT__MASK                                                0x00000070L
#define PCIE_PORT_VC_CAP_REG1__REF_CLK__MASK                                                                  0x00000300L
#define PCIE_PORT_VC_CAP_REG1__PORT_ARB_TABLE_ENTRY_SIZE__MASK                                                0x00000C00L
//PCIE_PORT_VC_CAP_REG2
#define PCIE_PORT_VC_CAP_REG2__VC_ARB_CAP__MASK                                                               0x000000FFL
#define PCIE_PORT_VC_CAP_REG2__VC_ARB_TABLE_OFFSET__MASK                                                      0xFF000000L
//PCIE_PORT_VC_CNTL
#define PCIE_PORT_VC_CNTL__LOAD_VC_ARB_TABLE__MASK                                                            0x0001L
#define PCIE_PORT_VC_CNTL__VC_ARB_SELECT__MASK                                                                0x000EL
//PCIE_PORT_VC_STATUS
#define PCIE_PORT_VC_STATUS__VC_ARB_TABLE_STATUS__MASK                                                        0x0001L
//PCIE_VC0_RESOURCE_CAP
#define PCIE_VC0_RESOURCE_CAP__PORT_ARB_CAP__MASK                                                             0x000000FFL
#define PCIE_VC0_RESOURCE_CAP__REJECT_SNOOP_TRANS__MASK                                                       0x00008000L
#define PCIE_VC0_RESOURCE_CAP__MAX_TIME_SLOTS__MASK                                                           0x003F0000L
#define PCIE_VC0_RESOURCE_CAP__PORT_ARB_TABLE_OFFSET__MASK                                                    0xFF000000L
//PCIE_VC0_RESOURCE_CNTL
#define PCIE_VC0_RESOURCE_CNTL__TC_VC_MAP_TC0__MASK                                                           0x00000001L
#define PCIE_VC0_RESOURCE_CNTL__TC_VC_MAP_TC1_7__MASK                                                         0x000000FEL
#define PCIE_VC0_RESOURCE_CNTL__LOAD_PORT_ARB_TABLE__MASK                                                     0x00010000L
#define PCIE_VC0_RESOURCE_CNTL__PORT_ARB_SELECT__MASK                                                         0x000E0000L
#define PCIE_VC0_RESOURCE_CNTL__VC_ID__MASK                                                                   0x07000000L
#define PCIE_VC0_RESOURCE_CNTL__VC_ENABLE__MASK                                                               0x80000000L
//PCIE_VC0_RESOURCE_STATUS
#define PCIE_VC0_RESOURCE_STATUS__PORT_ARB_TABLE_STATUS__MASK                                                 0x0001L
#define PCIE_VC0_RESOURCE_STATUS__VC_NEGOTIATION_PENDING__MASK                                                0x0002L
//PCIE_VC1_RESOURCE_CAP
#define PCIE_VC1_RESOURCE_CAP__PORT_ARB_CAP__MASK                                                             0x000000FFL
#define PCIE_VC1_RESOURCE_CAP__REJECT_SNOOP_TRANS__MASK                                                       0x00008000L
#define PCIE_VC1_RESOURCE_CAP__MAX_TIME_SLOTS__MASK                                                           0x003F0000L
#define PCIE_VC1_RESOURCE_CAP__PORT_ARB_TABLE_OFFSET__MASK                                                    0xFF000000L
//PCIE_VC1_RESOURCE_CNTL
#define PCIE_VC1_RESOURCE_CNTL__TC_VC_MAP_TC0__MASK                                                           0x00000001L
#define PCIE_VC1_RESOURCE_CNTL__TC_VC_MAP_TC1_7__MASK                                                         0x000000FEL
#define PCIE_VC1_RESOURCE_CNTL__LOAD_PORT_ARB_TABLE__MASK                                                     0x00010000L
#define PCIE_VC1_RESOURCE_CNTL__PORT_ARB_SELECT__MASK                                                         0x000E0000L
#define PCIE_VC1_RESOURCE_CNTL__VC_ID__MASK                                                                   0x07000000L
#define PCIE_VC1_RESOURCE_CNTL__VC_ENABLE__MASK                                                               0x80000000L
//PCIE_VC1_RESOURCE_STATUS
#define PCIE_VC1_RESOURCE_STATUS__PORT_ARB_TABLE_STATUS__MASK                                                 0x0001L
#define PCIE_VC1_RESOURCE_STATUS__VC_NEGOTIATION_PENDING__MASK                                                0x0002L
//PCIE_DEV_SERIAL_NUM_ENH_CAP_LIST
#define PCIE_DEV_SERIAL_NUM_ENH_CAP_LIST__CAP_ID__MASK                                                        0x0000FFFFL
#define PCIE_DEV_SERIAL_NUM_ENH_CAP_LIST__CAP_VER__MASK                                                       0x000F0000L
#define PCIE_DEV_SERIAL_NUM_ENH_CAP_LIST__NEXT_PTR__MASK                                                      0xFFF00000L
//PCIE_DEV_SERIAL_NUM_DW1
#define PCIE_DEV_SERIAL_NUM_DW1__SERIAL_NUMBER_LO__MASK                                                       0xFFFFFFFFL
//PCIE_DEV_SERIAL_NUM_DW2
#define PCIE_DEV_SERIAL_NUM_DW2__SERIAL_NUMBER_HI__MASK                                                       0xFFFFFFFFL
//PCIE_ADV_ERR_RPT_ENH_CAP_LIST
#define PCIE_ADV_ERR_RPT_ENH_CAP_LIST__CAP_ID__MASK                                                           0x0000FFFFL
#define PCIE_ADV_ERR_RPT_ENH_CAP_LIST__CAP_VER__MASK                                                          0x000F0000L
#define PCIE_ADV_ERR_RPT_ENH_CAP_LIST__NEXT_PTR__MASK                                                         0xFFF00000L
//PCIE_UNCORR_ERR_STATUS
#define PCIE_UNCORR_ERR_STATUS__DLP_ERR_STATUS__MASK                                                          0x00000010L
#define PCIE_UNCORR_ERR_STATUS__SURPDN_ERR_STATUS__MASK                                                       0x00000020L
#define PCIE_UNCORR_ERR_STATUS__PSN_ERR_STATUS__MASK                                                          0x00001000L
#define PCIE_UNCORR_ERR_STATUS__FC_ERR_STATUS__MASK                                                           0x00002000L
#define PCIE_UNCORR_ERR_STATUS__CPL_TIMEOUT_STATUS__MASK                                                      0x00004000L
#define PCIE_UNCORR_ERR_STATUS__CPL_ABORT_ERR_STATUS__MASK                                                    0x00008000L
#define PCIE_UNCORR_ERR_STATUS__UNEXP_CPL_STATUS__MASK                                                        0x00010000L
#define PCIE_UNCORR_ERR_STATUS__RCV_OVFL_STATUS__MASK                                                         0x00020000L
#define PCIE_UNCORR_ERR_STATUS__MAL_TLP_STATUS__MASK                                                          0x00040000L
#define PCIE_UNCORR_ERR_STATUS__ECRC_ERR_STATUS__MASK                                                         0x00080000L
#define PCIE_UNCORR_ERR_STATUS__UNSUPP_REQ_ERR_STATUS__MASK                                                   0x00100000L
#define PCIE_UNCORR_ERR_STATUS__ACS_VIOLATION_STATUS__MASK                                                    0x00200000L
#define PCIE_UNCORR_ERR_STATUS__UNCORR_INT_ERR_STATUS__MASK                                                   0x00400000L
#define PCIE_UNCORR_ERR_STATUS__MC_BLOCKED_TLP_STATUS__MASK                                                   0x00800000L
#define PCIE_UNCORR_ERR_STATUS__ATOMICOP_EGRESS_BLOCKED_STATUS__MASK                                          0x01000000L
#define PCIE_UNCORR_ERR_STATUS__TLP_PREFIX_BLOCKED_ERR_STATUS__MASK                                           0x02000000L
//PCIE_UNCORR_ERR_MASK
#define PCIE_UNCORR_ERR_MASK__DLP_ERR_MASK__MASK                                                              0x00000010L
#define PCIE_UNCORR_ERR_MASK__SURPDN_ERR_MASK__MASK                                                           0x00000020L
#define PCIE_UNCORR_ERR_MASK__PSN_ERR_MASK__MASK                                                              0x00001000L
#define PCIE_UNCORR_ERR_MASK__FC_ERR_MASK__MASK                                                               0x00002000L
#define PCIE_UNCORR_ERR_MASK__CPL_TIMEOUT_MASK__MASK                                                          0x00004000L
#define PCIE_UNCORR_ERR_MASK__CPL_ABORT_ERR_MASK__MASK                                                        0x00008000L
#define PCIE_UNCORR_ERR_MASK__UNEXP_CPL_MASK__MASK                                                            0x00010000L
#define PCIE_UNCORR_ERR_MASK__RCV_OVFL_MASK__MASK                                                             0x00020000L
#define PCIE_UNCORR_ERR_MASK__MAL_TLP_MASK__MASK                                                              0x00040000L
#define PCIE_UNCORR_ERR_MASK__ECRC_ERR_MASK__MASK                                                             0x00080000L
#define PCIE_UNCORR_ERR_MASK__UNSUPP_REQ_ERR_MASK__MASK                                                       0x00100000L
#define PCIE_UNCORR_ERR_MASK__ACS_VIOLATION_MASK__MASK                                                        0x00200000L
#define PCIE_UNCORR_ERR_MASK__UNCORR_INT_ERR_MASK__MASK                                                       0x00400000L
#define PCIE_UNCORR_ERR_MASK__MC_BLOCKED_TLP_MASK__MASK                                                       0x00800000L
#define PCIE_UNCORR_ERR_MASK__ATOMICOP_EGRESS_BLOCKED_MASK__MASK                                              0x01000000L
#define PCIE_UNCORR_ERR_MASK__TLP_PREFIX_BLOCKED_ERR_MASK__MASK                                               0x02000000L
//PCIE_UNCORR_ERR_SEVERITY
#define PCIE_UNCORR_ERR_SEVERITY__DLP_ERR_SEVERITY__MASK                                                      0x00000010L
#define PCIE_UNCORR_ERR_SEVERITY__SURPDN_ERR_SEVERITY__MASK                                                   0x00000020L
#define PCIE_UNCORR_ERR_SEVERITY__PSN_ERR_SEVERITY__MASK                                                      0x00001000L
#define PCIE_UNCORR_ERR_SEVERITY__FC_ERR_SEVERITY__MASK                                                       0x00002000L
#define PCIE_UNCORR_ERR_SEVERITY__CPL_TIMEOUT_SEVERITY__MASK                                                  0x00004000L
#define PCIE_UNCORR_ERR_SEVERITY__CPL_ABORT_ERR_SEVERITY__MASK                                                0x00008000L
#define PCIE_UNCORR_ERR_SEVERITY__UNEXP_CPL_SEVERITY__MASK                                                    0x00010000L
#define PCIE_UNCORR_ERR_SEVERITY__RCV_OVFL_SEVERITY__MASK                                                     0x00020000L
#define PCIE_UNCORR_ERR_SEVERITY__MAL_TLP_SEVERITY__MASK                                                      0x00040000L
#define PCIE_UNCORR_ERR_SEVERITY__ECRC_ERR_SEVERITY__MASK                                                     0x00080000L
#define PCIE_UNCORR_ERR_SEVERITY__UNSUPP_REQ_ERR_SEVERITY__MASK                                               0x00100000L
#define PCIE_UNCORR_ERR_SEVERITY__ACS_VIOLATION_SEVERITY__MASK                                                0x00200000L
#define PCIE_UNCORR_ERR_SEVERITY__UNCORR_INT_ERR_SEVERITY__MASK                                               0x00400000L
#define PCIE_UNCORR_ERR_SEVERITY__MC_BLOCKED_TLP_SEVERITY__MASK                                               0x00800000L
#define PCIE_UNCORR_ERR_SEVERITY__ATOMICOP_EGRESS_BLOCKED_SEVERITY__MASK                                      0x01000000L
#define PCIE_UNCORR_ERR_SEVERITY__TLP_PREFIX_BLOCKED_ERR_SEVERITY__MASK                                       0x02000000L
//PCIE_CORR_ERR_STATUS
#define PCIE_CORR_ERR_STATUS__RCV_ERR_STATUS__MASK                                                            0x00000001L
#define PCIE_CORR_ERR_STATUS__BAD_TLP_STATUS__MASK                                                            0x00000040L
#define PCIE_CORR_ERR_STATUS__BAD_DLLP_STATUS__MASK                                                           0x00000080L
#define PCIE_CORR_ERR_STATUS__REPLAY_NUM_ROLLOVER_STATUS__MASK                                                0x00000100L
#define PCIE_CORR_ERR_STATUS__REPLAY_TIMER_TIMEOUT_STATUS__MASK                                               0x00001000L
#define PCIE_CORR_ERR_STATUS__ADVISORY_NONFATAL_ERR_STATUS__MASK                                              0x00002000L
#define PCIE_CORR_ERR_STATUS__CORR_INT_ERR_STATUS__MASK                                                       0x00004000L
#define PCIE_CORR_ERR_STATUS__HDR_LOG_OVFL_STATUS__MASK                                                       0x00008000L
//PCIE_CORR_ERR_MASK
#define PCIE_CORR_ERR_MASK__RCV_ERR_MASK__MASK                                                                0x00000001L
#define PCIE_CORR_ERR_MASK__BAD_TLP_MASK__MASK                                                                0x00000040L
#define PCIE_CORR_ERR_MASK__BAD_DLLP_MASK__MASK                                                               0x00000080L
#define PCIE_CORR_ERR_MASK__REPLAY_NUM_ROLLOVER_MASK__MASK                                                    0x00000100L
#define PCIE_CORR_ERR_MASK__REPLAY_TIMER_TIMEOUT_MASK__MASK                                                   0x00001000L
#define PCIE_CORR_ERR_MASK__ADVISORY_NONFATAL_ERR_MASK__MASK                                                  0x00002000L
#define PCIE_CORR_ERR_MASK__CORR_INT_ERR_MASK__MASK                                                           0x00004000L
#define PCIE_CORR_ERR_MASK__HDR_LOG_OVFL_MASK__MASK                                                           0x00008000L
//PCIE_ADV_ERR_CAP_CNTL
#define PCIE_ADV_ERR_CAP_CNTL__FIRST_ERR_PTR__MASK                                                            0x0000001FL
#define PCIE_ADV_ERR_CAP_CNTL__ECRC_GEN_CAP__MASK                                                             0x00000020L
#define PCIE_ADV_ERR_CAP_CNTL__ECRC_GEN_EN__MASK                                                              0x00000040L
#define PCIE_ADV_ERR_CAP_CNTL__ECRC_CHECK_CAP__MASK                                                           0x00000080L
#define PCIE_ADV_ERR_CAP_CNTL__ECRC_CHECK_EN__MASK                                                            0x00000100L
#define PCIE_ADV_ERR_CAP_CNTL__MULTI_HDR_RECD_CAP__MASK                                                       0x00000200L
#define PCIE_ADV_ERR_CAP_CNTL__MULTI_HDR_RECD_EN__MASK                                                        0x00000400L
#define PCIE_ADV_ERR_CAP_CNTL__TLP_PREFIX_LOG_PRESENT__MASK                                                   0x00000800L
//PCIE_HDR_LOG0
#define PCIE_HDR_LOG0__TLP_HDR__MASK                                                                          0xFFFFFFFFL
//PCIE_HDR_LOG1
#define PCIE_HDR_LOG1__TLP_HDR__MASK                                                                          0xFFFFFFFFL
//PCIE_HDR_LOG2
#define PCIE_HDR_LOG2__TLP_HDR__MASK                                                                          0xFFFFFFFFL
//PCIE_HDR_LOG3
#define PCIE_HDR_LOG3__TLP_HDR__MASK                                                                          0xFFFFFFFFL
//PCIE_ROOT_ERR_CMD
#define PCIE_ROOT_ERR_CMD__CORR_ERR_REP_EN__MASK                                                              0x00000001L
#define PCIE_ROOT_ERR_CMD__NONFATAL_ERR_REP_EN__MASK                                                          0x00000002L
#define PCIE_ROOT_ERR_CMD__FATAL_ERR_REP_EN__MASK                                                             0x00000004L
//PCIE_ROOT_ERR_STATUS
#define PCIE_ROOT_ERR_STATUS__ERR_CORR_RCVD__MASK                                                             0x00000001L
#define PCIE_ROOT_ERR_STATUS__MULT_ERR_CORR_RCVD__MASK                                                        0x00000002L
#define PCIE_ROOT_ERR_STATUS__ERR_FATAL_NONFATAL_RCVD__MASK                                                   0x00000004L
#define PCIE_ROOT_ERR_STATUS__MULT_ERR_FATAL_NONFATAL_RCVD__MASK                                              0x00000008L
#define PCIE_ROOT_ERR_STATUS__FIRST_UNCORRECTABLE_FATAL__MASK                                                 0x00000010L
#define PCIE_ROOT_ERR_STATUS__NONFATAL_ERROR_MSG_RCVD__MASK                                                   0x00000020L
#define PCIE_ROOT_ERR_STATUS__FATAL_ERROR_MSG_RCVD__MASK                                                      0x00000040L
#define PCIE_ROOT_ERR_STATUS__ADV_ERR_INT_MSG_NUM__MASK                                                       0xF8000000L
//PCIE_ERR_SRC_ID
#define PCIE_ERR_SRC_ID__ERR_CORR_SRC_ID__MASK                                                                0x0000FFFFL
#define PCIE_ERR_SRC_ID__ERR_FATAL_NONFATAL_SRC_ID__MASK                                                      0xFFFF0000L
//PCIE_TLP_PREFIX_LOG0
#define PCIE_TLP_PREFIX_LOG0__TLP_PREFIX__MASK                                                                0xFFFFFFFFL
//PCIE_TLP_PREFIX_LOG1
#define PCIE_TLP_PREFIX_LOG1__TLP_PREFIX__MASK                                                                0xFFFFFFFFL
//PCIE_TLP_PREFIX_LOG2
#define PCIE_TLP_PREFIX_LOG2__TLP_PREFIX__MASK                                                                0xFFFFFFFFL
//PCIE_TLP_PREFIX_LOG3
#define PCIE_TLP_PREFIX_LOG3__TLP_PREFIX__MASK                                                                0xFFFFFFFFL
//PCIE_BAR_ENH_CAP_LIST
#define PCIE_BAR_ENH_CAP_LIST__CAP_ID__MASK                                                                   0x0000FFFFL
#define PCIE_BAR_ENH_CAP_LIST__CAP_VER__MASK                                                                  0x000F0000L
#define PCIE_BAR_ENH_CAP_LIST__NEXT_PTR__MASK                                                                 0xFFF00000L
//PCIE_BAR1_CAP
#define PCIE_BAR1_CAP__BAR_SIZE_SUPPORTED__MASK                                                               0x00FFFFF0L
//PCIE_BAR1_CNTL
#define PCIE_BAR1_CNTL__BAR_INDEX__MASK                                                                       0x0007L
#define PCIE_BAR1_CNTL__BAR_TOTAL_NUM__MASK                                                                   0x00E0L
#define PCIE_BAR1_CNTL__BAR_SIZE__MASK                                                                        0x1F00L
//PCIE_BAR2_CAP
#define PCIE_BAR2_CAP__BAR_SIZE_SUPPORTED__MASK                                                               0x00FFFFF0L
//PCIE_BAR2_CNTL
#define PCIE_BAR2_CNTL__BAR_INDEX__MASK                                                                       0x0007L
#define PCIE_BAR2_CNTL__BAR_TOTAL_NUM__MASK                                                                   0x00E0L
#define PCIE_BAR2_CNTL__BAR_SIZE__MASK                                                                        0x1F00L
//PCIE_BAR3_CAP
#define PCIE_BAR3_CAP__BAR_SIZE_SUPPORTED__MASK                                                               0x00FFFFF0L
//PCIE_BAR3_CNTL
#define PCIE_BAR3_CNTL__BAR_INDEX__MASK                                                                       0x0007L
#define PCIE_BAR3_CNTL__BAR_TOTAL_NUM__MASK                                                                   0x00E0L
#define PCIE_BAR3_CNTL__BAR_SIZE__MASK                                                                        0x1F00L
//PCIE_BAR4_CAP
#define PCIE_BAR4_CAP__BAR_SIZE_SUPPORTED__MASK                                                               0x00FFFFF0L
//PCIE_BAR4_CNTL
#define PCIE_BAR4_CNTL__BAR_INDEX__MASK                                                                       0x0007L
#define PCIE_BAR4_CNTL__BAR_TOTAL_NUM__MASK                                                                   0x00E0L
#define PCIE_BAR4_CNTL__BAR_SIZE__MASK                                                                        0x1F00L
//PCIE_BAR5_CAP
#define PCIE_BAR5_CAP__BAR_SIZE_SUPPORTED__MASK                                                               0x00FFFFF0L
//PCIE_BAR5_CNTL
#define PCIE_BAR5_CNTL__BAR_INDEX__MASK                                                                       0x0007L
#define PCIE_BAR5_CNTL__BAR_TOTAL_NUM__MASK                                                                   0x00E0L
#define PCIE_BAR5_CNTL__BAR_SIZE__MASK                                                                        0x1F00L
//PCIE_BAR6_CAP
#define PCIE_BAR6_CAP__BAR_SIZE_SUPPORTED__MASK                                                               0x00FFFFF0L
//PCIE_BAR6_CNTL
#define PCIE_BAR6_CNTL__BAR_INDEX__MASK                                                                       0x0007L
#define PCIE_BAR6_CNTL__BAR_TOTAL_NUM__MASK                                                                   0x00E0L
#define PCIE_BAR6_CNTL__BAR_SIZE__MASK                                                                        0x1F00L
//PCIE_PWR_BUDGET_ENH_CAP_LIST
#define PCIE_PWR_BUDGET_ENH_CAP_LIST__CAP_ID__MASK                                                            0x0000FFFFL
#define PCIE_PWR_BUDGET_ENH_CAP_LIST__CAP_VER__MASK                                                           0x000F0000L
#define PCIE_PWR_BUDGET_ENH_CAP_LIST__NEXT_PTR__MASK                                                          0xFFF00000L
//PCIE_PWR_BUDGET_DATA_SELECT
#define PCIE_PWR_BUDGET_DATA_SELECT__DATA_SELECT__MASK                                                        0xFFL
//PCIE_PWR_BUDGET_DATA
#define PCIE_PWR_BUDGET_DATA__BASE_POWER__MASK                                                                0x000000FFL
#define PCIE_PWR_BUDGET_DATA__DATA_SCALE__MASK                                                                0x00000300L
#define PCIE_PWR_BUDGET_DATA__PM_SUB_STATE__MASK                                                              0x00001C00L
#define PCIE_PWR_BUDGET_DATA__PM_STATE__MASK                                                                  0x00006000L
#define PCIE_PWR_BUDGET_DATA__TYPE__MASK                                                                      0x00038000L
#define PCIE_PWR_BUDGET_DATA__POWER_RAIL__MASK                                                                0x001C0000L
//PCIE_PWR_BUDGET_CAP
#define PCIE_PWR_BUDGET_CAP__SYSTEM_ALLOCATED__MASK                                                           0x01L
//PCIE_DPA_ENH_CAP_LIST
#define PCIE_DPA_ENH_CAP_LIST__CAP_ID__MASK                                                                   0x0000FFFFL
#define PCIE_DPA_ENH_CAP_LIST__CAP_VER__MASK                                                                  0x000F0000L
#define PCIE_DPA_ENH_CAP_LIST__NEXT_PTR__MASK                                                                 0xFFF00000L
//PCIE_DPA_CAP
#define PCIE_DPA_CAP__SUBSTATE_MAX__MASK                                                                      0x0000001FL
#define PCIE_DPA_CAP__TRANS_LAT_UNIT__MASK                                                                    0x00000300L
#define PCIE_DPA_CAP__PWR_ALLOC_SCALE__MASK                                                                   0x00003000L
#define PCIE_DPA_CAP__TRANS_LAT_VAL_0__MASK                                                                   0x00FF0000L
#define PCIE_DPA_CAP__TRANS_LAT_VAL_1__MASK                                                                   0xFF000000L
//PCIE_DPA_LATENCY_INDICATOR
#define PCIE_DPA_LATENCY_INDICATOR__TRANS_LAT_INDICATOR_BITS__MASK                                            0xFFL
//PCIE_DPA_STATUS
#define PCIE_DPA_STATUS__SUBSTATE_STATUS__MASK                                                                0x001FL
#define PCIE_DPA_STATUS__SUBSTATE_CNTL_ENABLED__MASK                                                          0x0100L
//PCIE_DPA_CNTL
#define PCIE_DPA_CNTL__SUBSTATE_CNTL__MASK                                                                    0x1FL
//PCIE_DPA_SUBSTATE_PWR_ALLOC_0
#define PCIE_DPA_SUBSTATE_PWR_ALLOC_0__SUBSTATE_PWR_ALLOC__MASK                                               0xFFL
//PCIE_DPA_SUBSTATE_PWR_ALLOC_1
#define PCIE_DPA_SUBSTATE_PWR_ALLOC_1__SUBSTATE_PWR_ALLOC__MASK                                               0xFFL
//PCIE_DPA_SUBSTATE_PWR_ALLOC_2
#define PCIE_DPA_SUBSTATE_PWR_ALLOC_2__SUBSTATE_PWR_ALLOC__MASK                                               0xFFL
//PCIE_DPA_SUBSTATE_PWR_ALLOC_3
#define PCIE_DPA_SUBSTATE_PWR_ALLOC_3__SUBSTATE_PWR_ALLOC__MASK                                               0xFFL
//PCIE_DPA_SUBSTATE_PWR_ALLOC_4
#define PCIE_DPA_SUBSTATE_PWR_ALLOC_4__SUBSTATE_PWR_ALLOC__MASK                                               0xFFL
//PCIE_DPA_SUBSTATE_PWR_ALLOC_5
#define PCIE_DPA_SUBSTATE_PWR_ALLOC_5__SUBSTATE_PWR_ALLOC__MASK                                               0xFFL
//PCIE_DPA_SUBSTATE_PWR_ALLOC_6
#define PCIE_DPA_SUBSTATE_PWR_ALLOC_6__SUBSTATE_PWR_ALLOC__MASK                                               0xFFL
//PCIE_DPA_SUBSTATE_PWR_ALLOC_7
#define PCIE_DPA_SUBSTATE_PWR_ALLOC_7__SUBSTATE_PWR_ALLOC__MASK                                               0xFFL
//PCIE_SECONDARY_ENH_CAP_LIST
#define PCIE_SECONDARY_ENH_CAP_LIST__CAP_ID__MASK                                                             0x0000FFFFL
#define PCIE_SECONDARY_ENH_CAP_LIST__CAP_VER__MASK                                                            0x000F0000L
#define PCIE_SECONDARY_ENH_CAP_LIST__NEXT_PTR__MASK                                                           0xFFF00000L
//PCIE_LINK_CNTL3
#define PCIE_LINK_CNTL3__PERFORM_EQUALIZATION__MASK                                                           0x00000001L
#define PCIE_LINK_CNTL3__LINK_EQUALIZATION_REQ_INT_EN__MASK                                                   0x00000002L
#define PCIE_LINK_CNTL3__RESERVED__MASK                                                                       0xFFFFFFFCL
//PCIE_LANE_ERROR_STATUS
#define PCIE_LANE_ERROR_STATUS__LANE_ERROR_STATUS_BITS__MASK                                                  0x0000FFFFL
#define PCIE_LANE_ERROR_STATUS__RESERVED__MASK                                                                0xFFFF0000L
//PCIE_LANE_0_EQUALIZATION_CNTL
#define PCIE_LANE_0_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__MASK                                        0x000FL
#define PCIE_LANE_0_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__MASK                                   0x0070L
#define PCIE_LANE_0_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__MASK                                          0x0F00L
#define PCIE_LANE_0_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__MASK                                     0x7000L
#define PCIE_LANE_0_EQUALIZATION_CNTL__RESERVED__MASK                                                         0x8000L
//PCIE_LANE_1_EQUALIZATION_CNTL
#define PCIE_LANE_1_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__MASK                                        0x000FL
#define PCIE_LANE_1_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__MASK                                   0x0070L
#define PCIE_LANE_1_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__MASK                                          0x0F00L
#define PCIE_LANE_1_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__MASK                                     0x7000L
#define PCIE_LANE_1_EQUALIZATION_CNTL__RESERVED__MASK                                                         0x8000L
//PCIE_LANE_2_EQUALIZATION_CNTL
#define PCIE_LANE_2_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__MASK                                        0x000FL
#define PCIE_LANE_2_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__MASK                                   0x0070L
#define PCIE_LANE_2_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__MASK                                          0x0F00L
#define PCIE_LANE_2_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__MASK                                     0x7000L
#define PCIE_LANE_2_EQUALIZATION_CNTL__RESERVED__MASK                                                         0x8000L
//PCIE_LANE_3_EQUALIZATION_CNTL
#define PCIE_LANE_3_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__MASK                                        0x000FL
#define PCIE_LANE_3_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__MASK                                   0x0070L
#define PCIE_LANE_3_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__MASK                                          0x0F00L
#define PCIE_LANE_3_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__MASK                                     0x7000L
#define PCIE_LANE_3_EQUALIZATION_CNTL__RESERVED__MASK                                                         0x8000L
//PCIE_LANE_4_EQUALIZATION_CNTL
#define PCIE_LANE_4_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__MASK                                        0x000FL
#define PCIE_LANE_4_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__MASK                                   0x0070L
#define PCIE_LANE_4_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__MASK                                          0x0F00L
#define PCIE_LANE_4_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__MASK                                     0x7000L
#define PCIE_LANE_4_EQUALIZATION_CNTL__RESERVED__MASK                                                         0x8000L
//PCIE_LANE_5_EQUALIZATION_CNTL
#define PCIE_LANE_5_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__MASK                                        0x000FL
#define PCIE_LANE_5_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__MASK                                   0x0070L
#define PCIE_LANE_5_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__MASK                                          0x0F00L
#define PCIE_LANE_5_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__MASK                                     0x7000L
#define PCIE_LANE_5_EQUALIZATION_CNTL__RESERVED__MASK                                                         0x8000L
//PCIE_LANE_6_EQUALIZATION_CNTL
#define PCIE_LANE_6_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__MASK                                        0x000FL
#define PCIE_LANE_6_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__MASK                                   0x0070L
#define PCIE_LANE_6_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__MASK                                          0x0F00L
#define PCIE_LANE_6_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__MASK                                     0x7000L
#define PCIE_LANE_6_EQUALIZATION_CNTL__RESERVED__MASK                                                         0x8000L
//PCIE_LANE_7_EQUALIZATION_CNTL
#define PCIE_LANE_7_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__MASK                                        0x000FL
#define PCIE_LANE_7_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__MASK                                   0x0070L
#define PCIE_LANE_7_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__MASK                                          0x0F00L
#define PCIE_LANE_7_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__MASK                                     0x7000L
#define PCIE_LANE_7_EQUALIZATION_CNTL__RESERVED__MASK                                                         0x8000L
//PCIE_LANE_8_EQUALIZATION_CNTL
#define PCIE_LANE_8_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__MASK                                        0x000FL
#define PCIE_LANE_8_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__MASK                                   0x0070L
#define PCIE_LANE_8_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__MASK                                          0x0F00L
#define PCIE_LANE_8_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__MASK                                     0x7000L
#define PCIE_LANE_8_EQUALIZATION_CNTL__RESERVED__MASK                                                         0x8000L
//PCIE_LANE_9_EQUALIZATION_CNTL
#define PCIE_LANE_9_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__MASK                                        0x000FL
#define PCIE_LANE_9_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__MASK                                   0x0070L
#define PCIE_LANE_9_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__MASK                                          0x0F00L
#define PCIE_LANE_9_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__MASK                                     0x7000L
#define PCIE_LANE_9_EQUALIZATION_CNTL__RESERVED__MASK                                                         0x8000L
//PCIE_LANE_10_EQUALIZATION_CNTL
#define PCIE_LANE_10_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__MASK                                       0x000FL
#define PCIE_LANE_10_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__MASK                                  0x0070L
#define PCIE_LANE_10_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__MASK                                         0x0F00L
#define PCIE_LANE_10_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__MASK                                    0x7000L
#define PCIE_LANE_10_EQUALIZATION_CNTL__RESERVED__MASK                                                        0x8000L
//PCIE_LANE_11_EQUALIZATION_CNTL
#define PCIE_LANE_11_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__MASK                                       0x000FL
#define PCIE_LANE_11_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__MASK                                  0x0070L
#define PCIE_LANE_11_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__MASK                                         0x0F00L
#define PCIE_LANE_11_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__MASK                                    0x7000L
#define PCIE_LANE_11_EQUALIZATION_CNTL__RESERVED__MASK                                                        0x8000L
//PCIE_LANE_12_EQUALIZATION_CNTL
#define PCIE_LANE_12_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__MASK                                       0x000FL
#define PCIE_LANE_12_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__MASK                                  0x0070L
#define PCIE_LANE_12_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__MASK                                         0x0F00L
#define PCIE_LANE_12_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__MASK                                    0x7000L
#define PCIE_LANE_12_EQUALIZATION_CNTL__RESERVED__MASK                                                        0x8000L
//PCIE_LANE_13_EQUALIZATION_CNTL
#define PCIE_LANE_13_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__MASK                                       0x000FL
#define PCIE_LANE_13_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__MASK                                  0x0070L
#define PCIE_LANE_13_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__MASK                                         0x0F00L
#define PCIE_LANE_13_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__MASK                                    0x7000L
#define PCIE_LANE_13_EQUALIZATION_CNTL__RESERVED__MASK                                                        0x8000L
//PCIE_LANE_14_EQUALIZATION_CNTL
#define PCIE_LANE_14_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__MASK                                       0x000FL
#define PCIE_LANE_14_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__MASK                                  0x0070L
#define PCIE_LANE_14_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__MASK                                         0x0F00L
#define PCIE_LANE_14_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__MASK                                    0x7000L
#define PCIE_LANE_14_EQUALIZATION_CNTL__RESERVED__MASK                                                        0x8000L
//PCIE_LANE_15_EQUALIZATION_CNTL
#define PCIE_LANE_15_EQUALIZATION_CNTL__DOWNSTREAM_PORT_TX_PRESET__MASK                                       0x000FL
#define PCIE_LANE_15_EQUALIZATION_CNTL__DOWNSTREAM_PORT_RX_PRESET_HINT__MASK                                  0x0070L
#define PCIE_LANE_15_EQUALIZATION_CNTL__UPSTREAM_PORT_TX_PRESET__MASK                                         0x0F00L
#define PCIE_LANE_15_EQUALIZATION_CNTL__UPSTREAM_PORT_RX_PRESET_HINT__MASK                                    0x7000L
#define PCIE_LANE_15_EQUALIZATION_CNTL__RESERVED__MASK                                                        0x8000L
//PCIE_ACS_ENH_CAP_LIST
#define PCIE_ACS_ENH_CAP_LIST__CAP_ID__MASK                                                                   0x0000FFFFL
#define PCIE_ACS_ENH_CAP_LIST__CAP_VER__MASK                                                                  0x000F0000L
#define PCIE_ACS_ENH_CAP_LIST__NEXT_PTR__MASK                                                                 0xFFF00000L
//PCIE_ACS_CAP
#define PCIE_ACS_CAP__SOURCE_VALIDATION__MASK                                                                 0x0001L
#define PCIE_ACS_CAP__TRANSLATION_BLOCKING__MASK                                                              0x0002L
#define PCIE_ACS_CAP__P2P_REQUEST_REDIRECT__MASK                                                              0x0004L
#define PCIE_ACS_CAP__P2P_COMPLETION_REDIRECT__MASK                                                           0x0008L
#define PCIE_ACS_CAP__UPSTREAM_FORWARDING__MASK                                                               0x0010L
#define PCIE_ACS_CAP__P2P_EGRESS_CONTROL__MASK                                                                0x0020L
#define PCIE_ACS_CAP__DIRECT_TRANSLATED_P2P__MASK                                                             0x0040L
#define PCIE_ACS_CAP__EGRESS_CONTROL_VECTOR_SIZE__MASK                                                        0xFF00L
//PCIE_ACS_CNTL
#define PCIE_ACS_CNTL__SOURCE_VALIDATION_EN__MASK                                                             0x0001L
#define PCIE_ACS_CNTL__TRANSLATION_BLOCKING_EN__MASK                                                          0x0002L
#define PCIE_ACS_CNTL__P2P_REQUEST_REDIRECT_EN__MASK                                                          0x0004L
#define PCIE_ACS_CNTL__P2P_COMPLETION_REDIRECT_EN__MASK                                                       0x0008L
#define PCIE_ACS_CNTL__UPSTREAM_FORWARDING_EN__MASK                                                           0x0010L
#define PCIE_ACS_CNTL__P2P_EGRESS_CONTROL_EN__MASK                                                            0x0020L
#define PCIE_ACS_CNTL__DIRECT_TRANSLATED_P2P_EN__MASK                                                         0x0040L
//PCIE_ATS_ENH_CAP_LIST
#define PCIE_ATS_ENH_CAP_LIST__CAP_ID__MASK                                                                   0x0000FFFFL
#define PCIE_ATS_ENH_CAP_LIST__CAP_VER__MASK                                                                  0x000F0000L
#define PCIE_ATS_ENH_CAP_LIST__NEXT_PTR__MASK                                                                 0xFFF00000L
//PCIE_ATS_CAP
#define PCIE_ATS_CAP__INVALIDATE_Q_DEPTH__MASK                                                                0x001FL
#define PCIE_ATS_CAP__PAGE_ALIGNED_REQUEST__MASK                                                              0x0020L
#define PCIE_ATS_CAP__GLOBAL_INVALIDATE_SUPPORTED__MASK                                                       0x0040L
//PCIE_ATS_CNTL
#define PCIE_ATS_CNTL__STU__MASK                                                                              0x001FL
#define PCIE_ATS_CNTL__ATC_ENABLE__MASK                                                                       0x8000L
//PCIE_PAGE_REQ_ENH_CAP_LIST
#define PCIE_PAGE_REQ_ENH_CAP_LIST__CAP_ID__MASK                                                              0x0000FFFFL
#define PCIE_PAGE_REQ_ENH_CAP_LIST__CAP_VER__MASK                                                             0x000F0000L
#define PCIE_PAGE_REQ_ENH_CAP_LIST__NEXT_PTR__MASK                                                            0xFFF00000L
//PCIE_PAGE_REQ_CNTL
#define PCIE_PAGE_REQ_CNTL__PRI_ENABLE__MASK                                                                  0x0001L
#define PCIE_PAGE_REQ_CNTL__PRI_RESET__MASK                                                                   0x0002L
//PCIE_PAGE_REQ_STATUS
#define PCIE_PAGE_REQ_STATUS__RESPONSE_FAILURE__MASK                                                          0x0001L
#define PCIE_PAGE_REQ_STATUS__UNEXPECTED_PAGE_REQ_GRP_INDEX__MASK                                             0x0002L
#define PCIE_PAGE_REQ_STATUS__STOPPED__MASK                                                                   0x0100L
#define PCIE_PAGE_REQ_STATUS__PRG_RESPONSE_PASID_REQUIRED__MASK                                               0x8000L
//PCIE_OUTSTAND_PAGE_REQ_CAPACITY
#define PCIE_OUTSTAND_PAGE_REQ_CAPACITY__OUTSTAND_PAGE_REQ_CAPACITY__MASK                                     0xFFFFFFFFL
//PCIE_OUTSTAND_PAGE_REQ_ALLOC
#define PCIE_OUTSTAND_PAGE_REQ_ALLOC__OUTSTAND_PAGE_REQ_ALLOC__MASK                                           0xFFFFFFFFL
//PCIE_PASID_ENH_CAP_LIST
#define PCIE_PASID_ENH_CAP_LIST__CAP_ID__MASK                                                                 0x0000FFFFL
#define PCIE_PASID_ENH_CAP_LIST__CAP_VER__MASK                                                                0x000F0000L
#define PCIE_PASID_ENH_CAP_LIST__NEXT_PTR__MASK                                                               0xFFF00000L
//PCIE_PASID_CAP
#define PCIE_PASID_CAP__PASID_EXE_PERMISSION_SUPPORTED__MASK                                                  0x0002L
#define PCIE_PASID_CAP__PASID_PRIV_MODE_SUPPORTED__MASK                                                       0x0004L
#define PCIE_PASID_CAP__MAX_PASID_WIDTH__MASK                                                                 0x1F00L
//PCIE_PASID_CNTL
#define PCIE_PASID_CNTL__PASID_ENABLE__MASK                                                                   0x0001L
#define PCIE_PASID_CNTL__PASID_EXE_PERMISSION_ENABLE__MASK                                                    0x0002L
#define PCIE_PASID_CNTL__PASID_PRIV_MODE_SUPPORTED_ENABLE__MASK                                               0x0004L
//PCIE_TPH_REQR_ENH_CAP_LIST
#define PCIE_TPH_REQR_ENH_CAP_LIST__CAP_ID__MASK                                                              0x0000FFFFL
#define PCIE_TPH_REQR_ENH_CAP_LIST__CAP_VER__MASK                                                             0x000F0000L
#define PCIE_TPH_REQR_ENH_CAP_LIST__NEXT_PTR__MASK                                                            0xFFF00000L
//PCIE_TPH_REQR_CAP
#define PCIE_TPH_REQR_CAP__TPH_REQR_NO_ST_MODE_SUPPORTED__MASK                                                0x00000001L
#define PCIE_TPH_REQR_CAP__TPH_REQR_INT_VEC_MODE_SUPPORTED__MASK                                              0x00000002L
#define PCIE_TPH_REQR_CAP__TPH_REQR_DEV_SPC_MODE_SUPPORTED__MASK                                              0x00000004L
#define PCIE_TPH_REQR_CAP__TPH_REQR_EXTND_TPH_REQR_SUPPORED__MASK                                             0x00000100L
#define PCIE_TPH_REQR_CAP__TPH_REQR_ST_TABLE_LOCATION__MASK                                                   0x00000600L
#define PCIE_TPH_REQR_CAP__TPH_REQR_ST_TABLE_SIZE__MASK                                                       0x07FF0000L
//PCIE_TPH_REQR_CNTL
#define PCIE_TPH_REQR_CNTL__TPH_REQR_ST_MODE_SEL__MASK                                                        0x00000007L
#define PCIE_TPH_REQR_CNTL__TPH_REQR_EN__MASK                                                                 0x00000300L
//PCIE_MC_ENH_CAP_LIST
#define PCIE_MC_ENH_CAP_LIST__CAP_ID__MASK                                                                    0x0000FFFFL
#define PCIE_MC_ENH_CAP_LIST__CAP_VER__MASK                                                                   0x000F0000L
#define PCIE_MC_ENH_CAP_LIST__NEXT_PTR__MASK                                                                  0xFFF00000L
//PCIE_MC_CAP
#define PCIE_MC_CAP__MC_MAX_GROUP__MASK                                                                       0x003FL
#define PCIE_MC_CAP__MC_WIN_SIZE_REQ__MASK                                                                    0x3F00L
#define PCIE_MC_CAP__MC_ECRC_REGEN_SUPP__MASK                                                                 0x8000L
//PCIE_MC_CNTL
#define PCIE_MC_CNTL__MC_NUM_GROUP__MASK                                                                      0x003FL
#define PCIE_MC_CNTL__MC_ENABLE__MASK                                                                         0x8000L
//PCIE_MC_ADDR0
#define PCIE_MC_ADDR0__MC_INDEX_POS__MASK                                                                     0x0000003FL
#define PCIE_MC_ADDR0__MC_BASE_ADDR_0__MASK                                                                   0xFFFFF000L
//PCIE_MC_ADDR1
#define PCIE_MC_ADDR1__MC_BASE_ADDR_1__MASK                                                                   0xFFFFFFFFL
//PCIE_MC_RCV0
#define PCIE_MC_RCV0__MC_RECEIVE_0__MASK                                                                      0xFFFFFFFFL
//PCIE_MC_RCV1
#define PCIE_MC_RCV1__MC_RECEIVE_1__MASK                                                                      0xFFFFFFFFL
//PCIE_MC_BLOCK_ALL0
#define PCIE_MC_BLOCK_ALL0__MC_BLOCK_ALL_0__MASK                                                              0xFFFFFFFFL
//PCIE_MC_BLOCK_ALL1
#define PCIE_MC_BLOCK_ALL1__MC_BLOCK_ALL_1__MASK                                                              0xFFFFFFFFL
//PCIE_MC_BLOCK_UNTRANSLATED_0
#define PCIE_MC_BLOCK_UNTRANSLATED_0__MC_BLOCK_UNTRANSLATED_0__MASK                                           0xFFFFFFFFL
//PCIE_MC_BLOCK_UNTRANSLATED_1
#define PCIE_MC_BLOCK_UNTRANSLATED_1__MC_BLOCK_UNTRANSLATED_1__MASK                                           0xFFFFFFFFL
//PCIE_LTR_ENH_CAP_LIST
#define PCIE_LTR_ENH_CAP_LIST__CAP_ID__MASK                                                                   0x0000FFFFL
#define PCIE_LTR_ENH_CAP_LIST__CAP_VER__MASK                                                                  0x000F0000L
#define PCIE_LTR_ENH_CAP_LIST__NEXT_PTR__MASK                                                                 0xFFF00000L
//PCIE_LTR_CAP
#define PCIE_LTR_CAP__LTR_MAX_S_LATENCY_VALUE__MASK                                                           0x000003FFL
#define PCIE_LTR_CAP__LTR_MAX_S_LATENCY_SCALE__MASK                                                           0x00001C00L
#define PCIE_LTR_CAP__LTR_MAX_NS_LATENCY_VALUE__MASK                                                          0x03FF0000L
#define PCIE_LTR_CAP__LTR_MAX_NS_LATENCY_SCALE__MASK                                                          0x1C000000L
//PCIE_ARI_ENH_CAP_LIST
#define PCIE_ARI_ENH_CAP_LIST__CAP_ID__MASK                                                                   0x0000FFFFL
#define PCIE_ARI_ENH_CAP_LIST__CAP_VER__MASK                                                                  0x000F0000L
#define PCIE_ARI_ENH_CAP_LIST__NEXT_PTR__MASK                                                                 0xFFF00000L
//PCIE_ARI_CAP
#define PCIE_ARI_CAP__ARI_MFVC_FUNC_GROUPS_CAP__MASK                                                          0x0001L
#define PCIE_ARI_CAP__ARI_ACS_FUNC_GROUPS_CAP__MASK                                                           0x0002L
#define PCIE_ARI_CAP__ARI_NEXT_FUNC_NUM__MASK                                                                 0xFF00L
//PCIE_ARI_CNTL
#define PCIE_ARI_CNTL__ARI_MFVC_FUNC_GROUPS_EN__MASK                                                          0x0001L
#define PCIE_ARI_CNTL__ARI_ACS_FUNC_GROUPS_EN__MASK                                                           0x0002L
#define PCIE_ARI_CNTL__ARI_FUNCTION_GROUP__MASK                                                               0x0070L
//PCIE_SRIOV_ENH_CAP_LIST
#define PCIE_SRIOV_ENH_CAP_LIST__CAP_ID__MASK                                                                 0x0000FFFFL
#define PCIE_SRIOV_ENH_CAP_LIST__CAP_VER__MASK                                                                0x000F0000L
#define PCIE_SRIOV_ENH_CAP_LIST__NEXT_PTR__MASK                                                               0xFFF00000L
//PCIE_SRIOV_CAP
#define PCIE_SRIOV_CAP__SRIOV_VF_MIGRATION_CAP__MASK                                                          0x00000001L
#define PCIE_SRIOV_CAP__SRIOV_ARI_CAP_HIERARCHY_PRESERVED__MASK                                               0x00000002L
#define PCIE_SRIOV_CAP__SRIOV_VF_MIGRATION_INTR_MSG_NUM__MASK                                                 0xFFE00000L
//PCIE_SRIOV_CONTROL
#define PCIE_SRIOV_CONTROL__SRIOV_VF_ENABLE__MASK                                                             0x0001L
#define PCIE_SRIOV_CONTROL__SRIOV_VF_MIGRATION_ENABLE__MASK                                                   0x0002L
#define PCIE_SRIOV_CONTROL__SRIOV_VF_MIGRATION_INTR_ENABLE__MASK                                              0x0004L
#define PCIE_SRIOV_CONTROL__SRIOV_VF_MSE__MASK                                                                0x0008L
#define PCIE_SRIOV_CONTROL__SRIOV_ARI_CAP_HIERARCHY__MASK                                                     0x0010L
//PCIE_SRIOV_STATUS
#define PCIE_SRIOV_STATUS__SRIOV_VF_MIGRATION_STATUS__MASK                                                    0x0001L
//PCIE_SRIOV_INITIAL_VFS
#define PCIE_SRIOV_INITIAL_VFS__SRIOV_INITIAL_VFS__MASK                                                       0xFFFFL
//PCIE_SRIOV_TOTAL_VFS
#define PCIE_SRIOV_TOTAL_VFS__SRIOV_TOTAL_VFS__MASK                                                           0xFFFFL
//PCIE_SRIOV_NUM_VFS
#define PCIE_SRIOV_NUM_VFS__SRIOV_NUM_VFS__MASK                                                               0xFFFFL
//PCIE_SRIOV_FUNC_DEP_LINK
#define PCIE_SRIOV_FUNC_DEP_LINK__SRIOV_FUNC_DEP_LINK__MASK                                                   0x00FFL
//PCIE_SRIOV_FIRST_VF_OFFSET
#define PCIE_SRIOV_FIRST_VF_OFFSET__SRIOV_FIRST_VF_OFFSET__MASK                                               0xFFFFL
//PCIE_SRIOV_VF_STRIDE
#define PCIE_SRIOV_VF_STRIDE__SRIOV_VF_STRIDE__MASK                                                           0xFFFFL
//PCIE_SRIOV_VF_DEVICE_ID
#define PCIE_SRIOV_VF_DEVICE_ID__SRIOV_VF_DEVICE_ID__MASK                                                     0xFFFFL
//PCIE_SRIOV_SUPPORTED_PAGE_SIZE
#define PCIE_SRIOV_SUPPORTED_PAGE_SIZE__SRIOV_SUPPORTED_PAGE_SIZE__MASK                                       0xFFFFFFFFL
//PCIE_SRIOV_SYSTEM_PAGE_SIZE
#define PCIE_SRIOV_SYSTEM_PAGE_SIZE__SRIOV_SYSTEM_PAGE_SIZE__MASK                                             0xFFFFFFFFL
//PCIE_SRIOV_VF_BASE_ADDR_0
#define PCIE_SRIOV_VF_BASE_ADDR_0__VF_BASE_ADDR__MASK                                                         0xFFFFFFFFL
//PCIE_SRIOV_VF_BASE_ADDR_1
#define PCIE_SRIOV_VF_BASE_ADDR_1__VF_BASE_ADDR__MASK                                                         0xFFFFFFFFL
//PCIE_SRIOV_VF_BASE_ADDR_2
#define PCIE_SRIOV_VF_BASE_ADDR_2__VF_BASE_ADDR__MASK                                                         0xFFFFFFFFL
//PCIE_SRIOV_VF_BASE_ADDR_3
#define PCIE_SRIOV_VF_BASE_ADDR_3__VF_BASE_ADDR__MASK                                                         0xFFFFFFFFL
//PCIE_SRIOV_VF_BASE_ADDR_4
#define PCIE_SRIOV_VF_BASE_ADDR_4__VF_BASE_ADDR__MASK                                                         0xFFFFFFFFL
//PCIE_SRIOV_VF_BASE_ADDR_5
#define PCIE_SRIOV_VF_BASE_ADDR_5__VF_BASE_ADDR__MASK                                                         0xFFFFFFFFL
//PCIE_SRIOV_VF_MIGRATION_STATE_ARRAY_OFFSET
#define PCIE_SRIOV_VF_MIGRATION_STATE_ARRAY_OFFSET__SRIOV_VF_MIGRATION_STATE_BIF__MASK                        0x00000007L
#define PCIE_SRIOV_VF_MIGRATION_STATE_ARRAY_OFFSET__SRIOV_VF_MIGRATION_STATE_ARRAY_OFFSET__MASK               0xFFFFFFF8L
//PCIE_VENDOR_SPECIFIC_ENH_CAP_LIST_GPUIOV
#define PCIE_VENDOR_SPECIFIC_ENH_CAP_LIST_GPUIOV__CAP_ID__MASK                                                0x0000FFFFL
#define PCIE_VENDOR_SPECIFIC_ENH_CAP_LIST_GPUIOV__CAP_VER__MASK                                               0x000F0000L
#define PCIE_VENDOR_SPECIFIC_ENH_CAP_LIST_GPUIOV__NEXT_PTR__MASK                                              0xFFF00000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV__VSEC_ID__MASK                                                        0x0000FFFFL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV__VSEC_REV__MASK                                                       0x000F0000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV__VSEC_LENGTH__MASK                                                    0xFFF00000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_SRIOV_SHADOW
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_SRIOV_SHADOW__VF_EN__MASK                                             0x00000001L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_SRIOV_SHADOW__VF_NUM__MASK                                            0xFFFF0000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__GFX_CMD_COMPLETE_INTR_EN__MASK                           0x00000001L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__GFX_HANG_SELF_RECOVERED_INTR_EN__MASK                    0x00000002L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__GFX_HANG_NEED_FLR_INTR_EN__MASK                          0x00000004L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__GFX_VM_BUSY_TRANSITION_INTR_EN__MASK                     0x00000008L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__UVD_CMD_COMPLETE_INTR_EN__MASK                           0x00000100L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__UVD_HANG_SELF_RECOVERED_INTR_EN__MASK                    0x00000200L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__UVD_HANG_NEED_FLR_INTR_EN__MASK                          0x00000400L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__UVD_VM_BUSY_TRANSITION_INTR_EN__MASK                     0x00000800L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__VCE_CMD_COMPLETE_INTR_EN__MASK                           0x00010000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__VCE_HANG_SELF_RECOVERED_INTR_EN__MASK                    0x00020000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__VCE_HANG_NEED_FLR_INTR_EN__MASK                          0x00040000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__VCE_VM_BUSY_TRANSITION_INTR_EN__MASK                     0x00080000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__HVVM_MAILBOX_TRN_ACK_INTR_EN__MASK                       0x01000000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_ENABLE__HVVM_MAILBOX_RCV_VALID_INTR_EN__MASK                     0x02000000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__GFX_CMD_COMPLETE_INTR_STATUS__MASK                       0x00000001L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__GFX_HANG_SELF_RECOVERED_INTR_STATUS__MASK                0x00000002L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__GFX_HANG_NEED_FLR_INTR_STATUS__MASK                      0x00000004L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__GFX_VM_BUSY_TRANSITION_INTR_STATUS__MASK                 0x00000008L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__UVD_CMD_COMPLETE_INTR_STATUS__MASK                       0x00000100L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__UVD_HANG_SELF_RECOVERED_INTR_STATUS__MASK                0x00000200L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__UVD_HANG_NEED_FLR_INTR_STATUS__MASK                      0x00000400L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__UVD_VM_BUSY_TRANSITION_INTR_STATUS__MASK                 0x00000800L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__VCE_CMD_COMPLETE_INTR_STATUS__MASK                       0x00010000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__VCE_HANG_SELF_RECOVERED_INTR_STATUS__MASK                0x00020000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__VCE_HANG_NEED_FLR_INTR_STATUS__MASK                      0x00040000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__VCE_VM_BUSY_TRANSITION_INTR_STATUS__MASK                 0x00080000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__HVVM_MAILBOX_TRN_ACK_INTR_STATUS__MASK                   0x01000000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_INTR_STATUS__HVVM_MAILBOX_RCV_VALID_INTR_STATUS__MASK                 0x02000000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_RESET_CONTROL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_RESET_CONTROL__SOFT_PF_FLR__MASK                                      0x0001L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW0__VF_INDEX__MASK                                         0x000000FFL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW0__TRN_MSG_DATA__MASK                                     0x00000F00L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW0__TRN_MSG_VALID__MASK                                    0x00008000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW0__RCV_MSG_DATA__MASK                                     0x000F0000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW0__RCV_MSG_ACK__MASK                                      0x01000000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF0_TRN_ACK__MASK                                      0x00000001L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF0_RCV_VALID__MASK                                    0x00000002L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF1_TRN_ACK__MASK                                      0x00000004L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF1_RCV_VALID__MASK                                    0x00000008L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF2_TRN_ACK__MASK                                      0x00000010L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF2_RCV_VALID__MASK                                    0x00000020L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF3_TRN_ACK__MASK                                      0x00000040L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF3_RCV_VALID__MASK                                    0x00000080L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF4_TRN_ACK__MASK                                      0x00000100L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF4_RCV_VALID__MASK                                    0x00000200L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF5_TRN_ACK__MASK                                      0x00000400L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF5_RCV_VALID__MASK                                    0x00000800L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF6_TRN_ACK__MASK                                      0x00001000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF6_RCV_VALID__MASK                                    0x00002000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF7_TRN_ACK__MASK                                      0x00004000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF7_RCV_VALID__MASK                                    0x00008000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF8_TRN_ACK__MASK                                      0x00010000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF8_RCV_VALID__MASK                                    0x00020000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF9_TRN_ACK__MASK                                      0x00040000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF9_RCV_VALID__MASK                                    0x00080000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF10_TRN_ACK__MASK                                     0x00100000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF10_RCV_VALID__MASK                                   0x00200000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF11_TRN_ACK__MASK                                     0x00400000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF11_RCV_VALID__MASK                                   0x00800000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF12_TRN_ACK__MASK                                     0x01000000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF12_RCV_VALID__MASK                                   0x02000000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF13_TRN_ACK__MASK                                     0x04000000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF13_RCV_VALID__MASK                                   0x08000000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF14_TRN_ACK__MASK                                     0x10000000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF14_RCV_VALID__MASK                                   0x20000000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF15_TRN_ACK__MASK                                     0x40000000L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW1__VF15_RCV_VALID__MASK                                   0x80000000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW2
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW2__PF_TRN_ACK__MASK                                       0x00000001L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_HVVM_MBOX_DW2__PF_RCV_VALID__MASK                                     0x00000002L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_CONTEXT
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_CONTEXT__CONTEXT_SIZE__MASK                                           0x0000007FL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_CONTEXT__LOC__MASK                                                    0x00000080L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_CONTEXT__CONTEXT_OFFSET__MASK                                         0xFFFFFC00L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_TOTAL_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_TOTAL_FB__TOTAL_FB_AVAILABLE__MASK                                    0x0000FFFFL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_TOTAL_FB__TOTAL_FB_CONSUMED__MASK                                     0xFFFF0000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_OFFSETS
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_OFFSETS__UVDSCH_OFFSET__MASK                                          0x000000FFL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_OFFSETS__VCESCH_OFFSET__MASK                                          0x0000FF00L
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_OFFSETS__GFXSCH_OFFSET__MASK                                          0x00FF0000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF0_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF0_FB__VF0_FB_SIZE__MASK                                             0x0000FFFFL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF0_FB__VF0_FB_OFFSET__MASK                                           0xFFFF0000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF1_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF1_FB__VF1_FB_SIZE__MASK                                             0x0000FFFFL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF1_FB__VF1_FB_OFFSET__MASK                                           0xFFFF0000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF2_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF2_FB__VF2_FB_SIZE__MASK                                             0x0000FFFFL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF2_FB__VF2_FB_OFFSET__MASK                                           0xFFFF0000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF3_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF3_FB__VF3_FB_SIZE__MASK                                             0x0000FFFFL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF3_FB__VF3_FB_OFFSET__MASK                                           0xFFFF0000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF4_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF4_FB__VF4_FB_SIZE__MASK                                             0x0000FFFFL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF4_FB__VF4_FB_OFFSET__MASK                                           0xFFFF0000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF5_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF5_FB__VF5_FB_SIZE__MASK                                             0x0000FFFFL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF5_FB__VF5_FB_OFFSET__MASK                                           0xFFFF0000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF6_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF6_FB__VF6_FB_SIZE__MASK                                             0x0000FFFFL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF6_FB__VF6_FB_OFFSET__MASK                                           0xFFFF0000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF7_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF7_FB__VF7_FB_SIZE__MASK                                             0x0000FFFFL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF7_FB__VF7_FB_OFFSET__MASK                                           0xFFFF0000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF8_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF8_FB__VF8_FB_SIZE__MASK                                             0x0000FFFFL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF8_FB__VF8_FB_OFFSET__MASK                                           0xFFFF0000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF9_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF9_FB__VF9_FB_SIZE__MASK                                             0x0000FFFFL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF9_FB__VF9_FB_OFFSET__MASK                                           0xFFFF0000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF10_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF10_FB__VF10_FB_SIZE__MASK                                           0x0000FFFFL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF10_FB__VF10_FB_OFFSET__MASK                                         0xFFFF0000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF11_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF11_FB__VF11_FB_SIZE__MASK                                           0x0000FFFFL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF11_FB__VF11_FB_OFFSET__MASK                                         0xFFFF0000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF12_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF12_FB__VF12_FB_SIZE__MASK                                           0x0000FFFFL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF12_FB__VF12_FB_OFFSET__MASK                                         0xFFFF0000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF13_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF13_FB__VF13_FB_SIZE__MASK                                           0x0000FFFFL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF13_FB__VF13_FB_OFFSET__MASK                                         0xFFFF0000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF14_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF14_FB__VF14_FB_SIZE__MASK                                           0x0000FFFFL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF14_FB__VF14_FB_OFFSET__MASK                                         0xFFFF0000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF15_FB
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF15_FB__VF15_FB_SIZE__MASK                                           0x0000FFFFL
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VF15_FB__VF15_FB_OFFSET__MASK                                         0xFFFF0000L
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW0__DW0__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW1
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW1__DW1__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW2
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW2__DW2__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW3
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW3__DW3__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW4
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW4__DW4__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW5
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW5__DW5__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW6
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW6__DW6__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW7
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_UVDSCH_DW7__DW7__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW0__DW0__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW1
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW1__DW1__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW2
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW2__DW2__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW3
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW3__DW3__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW4
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW4__DW4__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW5
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW5__DW5__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW6
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW6__DW6__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW7
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_VCESCH_DW7__DW7__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW0
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW0__DW0__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW1
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW1__DW1__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW2
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW2__DW2__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW3
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW3__DW3__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW4
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW4__DW4__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW5
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW5__DW5__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW6
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW6__DW6__MASK                                                 0xFFFFFFFFL
//PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW7
#define PCIE_VENDOR_SPECIFIC_HDR_GPUIOV_GFXSCH_DW7__DW7__MASK                                                 0xFFFFFFFFL


// addressBlock: bif_cfg_dev0_swds_bifcfgdecp
//SUB_BUS_NUMBER_LATENCY
#define SUB_BUS_NUMBER_LATENCY__PRIMARY_BUS__MASK                                                             0x000000FFL
#define SUB_BUS_NUMBER_LATENCY__SECONDARY_BUS__MASK                                                           0x0000FF00L
#define SUB_BUS_NUMBER_LATENCY__SUB_BUS_NUM__MASK                                                             0x00FF0000L
#define SUB_BUS_NUMBER_LATENCY__SECONDARY_LATENCY_TIMER__MASK                                                 0xFF000000L
//IO_BASE_LIMIT
#define IO_BASE_LIMIT__IO_BASE_TYPE__MASK                                                                     0x000FL
#define IO_BASE_LIMIT__IO_BASE__MASK                                                                          0x00F0L
#define IO_BASE_LIMIT__IO_LIMIT_TYPE__MASK                                                                    0x0F00L
#define IO_BASE_LIMIT__IO_LIMIT__MASK                                                                         0xF000L
//SECONDARY_STATUS
#define SECONDARY_STATUS__CAP_LIST__MASK                                                                      0x0010L
#define SECONDARY_STATUS__PCI_66_EN__MASK                                                                     0x0020L
#define SECONDARY_STATUS__FAST_BACK_CAPABLE__MASK                                                             0x0080L
#define SECONDARY_STATUS__MASTER_DATA_PARITY_ERROR__MASK                                                      0x0100L
#define SECONDARY_STATUS__DEVSEL_TIMING__MASK                                                                 0x0600L
#define SECONDARY_STATUS__SIGNAL_TARGET_ABORT__MASK                                                           0x0800L
#define SECONDARY_STATUS__RECEIVED_TARGET_ABORT__MASK                                                         0x1000L
#define SECONDARY_STATUS__RECEIVED_MASTER_ABORT__MASK                                                         0x2000L
#define SECONDARY_STATUS__RECEIVED_SYSTEM_ERROR__MASK                                                         0x4000L
#define SECONDARY_STATUS__PARITY_ERROR_DETECTED__MASK                                                         0x8000L
//MEM_BASE_LIMIT
#define MEM_BASE_LIMIT__MEM_BASE_TYPE__MASK                                                                   0x0000000FL
#define MEM_BASE_LIMIT__MEM_BASE_31_20__MASK                                                                  0x0000FFF0L
#define MEM_BASE_LIMIT__MEM_LIMIT_TYPE__MASK                                                                  0x000F0000L
#define MEM_BASE_LIMIT__MEM_LIMIT_31_20__MASK                                                                 0xFFF00000L
//PREF_BASE_LIMIT
#define PREF_BASE_LIMIT__PREF_MEM_BASE_TYPE__MASK                                                             0x0000000FL
#define PREF_BASE_LIMIT__PREF_MEM_BASE_31_20__MASK                                                            0x0000FFF0L
#define PREF_BASE_LIMIT__PREF_MEM_LIMIT_TYPE__MASK                                                            0x000F0000L
#define PREF_BASE_LIMIT__PREF_MEM_LIMIT_31_20__MASK                                                           0xFFF00000L
//PREF_BASE_UPPER
#define PREF_BASE_UPPER__PREF_BASE_UPPER__MASK                                                                0xFFFFFFFFL
//PREF_LIMIT_UPPER
#define PREF_LIMIT_UPPER__PREF_LIMIT_UPPER__MASK                                                              0xFFFFFFFFL
//IO_BASE_LIMIT_HI
#define IO_BASE_LIMIT_HI__IO_BASE_31_16__MASK                                                                 0x0000FFFFL
#define IO_BASE_LIMIT_HI__IO_LIMIT_31_16__MASK                                                                0xFFFF0000L
//IRQ_BRIDGE_CNTL
#define IRQ_BRIDGE_CNTL__PARITY_RESPONSE_EN__MASK                                                             0x0001L
#define IRQ_BRIDGE_CNTL__SERR_EN__MASK                                                                        0x0002L
#define IRQ_BRIDGE_CNTL__ISA_EN__MASK                                                                         0x0004L
#define IRQ_BRIDGE_CNTL__VGA_EN__MASK                                                                         0x0008L
#define IRQ_BRIDGE_CNTL__VGA_DEC__MASK                                                                        0x0010L
#define IRQ_BRIDGE_CNTL__MASTER_ABORT_MODE__MASK                                                              0x0020L
#define IRQ_BRIDGE_CNTL__SECONDARY_BUS_RESET__MASK                                                            0x0040L
#define IRQ_BRIDGE_CNTL__FAST_B2B_EN__MASK                                                                    0x0080L
//SLOT_CAP
#define SLOT_CAP__ATTN_BUTTON_PRESENT__MASK                                                                   0x00000001L
#define SLOT_CAP__PWR_CONTROLLER_PRESENT__MASK                                                                0x00000002L
#define SLOT_CAP__MRL_SENSOR_PRESENT__MASK                                                                    0x00000004L
#define SLOT_CAP__ATTN_INDICATOR_PRESENT__MASK                                                                0x00000008L
#define SLOT_CAP__PWR_INDICATOR_PRESENT__MASK                                                                 0x00000010L
#define SLOT_CAP__HOTPLUG_SURPRISE__MASK                                                                      0x00000020L
#define SLOT_CAP__HOTPLUG_CAPABLE__MASK                                                                       0x00000040L
#define SLOT_CAP__SLOT_PWR_LIMIT_VALUE__MASK                                                                  0x00007F80L
#define SLOT_CAP__SLOT_PWR_LIMIT_SCALE__MASK                                                                  0x00018000L
#define SLOT_CAP__ELECTROMECH_INTERLOCK_PRESENT__MASK                                                         0x00020000L
#define SLOT_CAP__NO_COMMAND_COMPLETED_SUPPORTED__MASK                                                        0x00040000L
#define SLOT_CAP__PHYSICAL_SLOT_NUM__MASK                                                                     0xFFF80000L
//SLOT_CNTL
#define SLOT_CNTL__ATTN_BUTTON_PRESSED_EN__MASK                                                               0x0001L
#define SLOT_CNTL__PWR_FAULT_DETECTED_EN__MASK                                                                0x0002L
#define SLOT_CNTL__MRL_SENSOR_CHANGED_EN__MASK                                                                0x0004L
#define SLOT_CNTL__PRESENCE_DETECT_CHANGED_EN__MASK                                                           0x0008L
#define SLOT_CNTL__COMMAND_COMPLETED_INTR_EN__MASK                                                            0x0010L
#define SLOT_CNTL__HOTPLUG_INTR_EN__MASK                                                                      0x0020L
#define SLOT_CNTL__ATTN_INDICATOR_CNTL__MASK                                                                  0x00C0L
#define SLOT_CNTL__PWR_INDICATOR_CNTL__MASK                                                                   0x0300L
#define SLOT_CNTL__PWR_CONTROLLER_CNTL__MASK                                                                  0x0400L
#define SLOT_CNTL__ELECTROMECH_INTERLOCK_CNTL__MASK                                                           0x0800L
#define SLOT_CNTL__DL_STATE_CHANGED_EN__MASK                                                                  0x1000L
//SLOT_STATUS
#define SLOT_STATUS__ATTN_BUTTON_PRESSED__MASK                                                                0x0001L
#define SLOT_STATUS__PWR_FAULT_DETECTED__MASK                                                                 0x0002L
#define SLOT_STATUS__MRL_SENSOR_CHANGED__MASK                                                                 0x0004L
#define SLOT_STATUS__PRESENCE_DETECT_CHANGED__MASK                                                            0x0008L
#define SLOT_STATUS__COMMAND_COMPLETED__MASK                                                                  0x0010L
#define SLOT_STATUS__MRL_SENSOR_STATE__MASK                                                                   0x0020L
#define SLOT_STATUS__PRESENCE_DETECT_STATE__MASK                                                              0x0040L
#define SLOT_STATUS__ELECTROMECH_INTERLOCK_STATUS__MASK                                                       0x0080L
#define SLOT_STATUS__DL_STATE_CHANGED__MASK                                                                   0x0100L
//SSID_CAP_LIST
#define SSID_CAP_LIST__CAP_ID__MASK                                                                           0x00FFL
#define SSID_CAP_LIST__NEXT_PTR__MASK                                                                         0xFF00L
//SSID_CAP
#define SSID_CAP__SUBSYSTEM_VENDOR_ID__MASK                                                                   0x0000FFFFL
#define SSID_CAP__SUBSYSTEM_ID__MASK                                                                          0xFFFF0000L


// addressBlock: rcc_shadow_reg_shadowdec
//SHADOW_COMMAND
#define SHADOW_COMMAND__IOEN_UP__MASK                                                                         0x0001L
#define SHADOW_COMMAND__MEMEN_UP__MASK                                                                        0x0002L
//SHADOW_BASE_ADDR_1
#define SHADOW_BASE_ADDR_1__BAR1_UP__MASK                                                                     0xFFFFFFFFL
//SHADOW_BASE_ADDR_2
#define SHADOW_BASE_ADDR_2__BAR2_UP__MASK                                                                     0xFFFFFFFFL
//SHADOW_SUB_BUS_NUMBER_LATENCY
#define SHADOW_SUB_BUS_NUMBER_LATENCY__SECONDARY_BUS_UP__MASK                                                 0x0000FF00L
#define SHADOW_SUB_BUS_NUMBER_LATENCY__SUB_BUS_NUM_UP__MASK                                                   0x00FF0000L
//SHADOW_IO_BASE_LIMIT
#define SHADOW_IO_BASE_LIMIT__IO_BASE_UP__MASK                                                                0x00F0L
#define SHADOW_IO_BASE_LIMIT__IO_LIMIT_UP__MASK                                                               0xF000L
//SHADOW_MEM_BASE_LIMIT
#define SHADOW_MEM_BASE_LIMIT__MEM_BASE_TYPE__MASK                                                            0x0000000FL
#define SHADOW_MEM_BASE_LIMIT__MEM_BASE_31_20_UP__MASK                                                        0x0000FFF0L
#define SHADOW_MEM_BASE_LIMIT__MEM_LIMIT_TYPE__MASK                                                           0x000F0000L
#define SHADOW_MEM_BASE_LIMIT__MEM_LIMIT_31_20_UP__MASK                                                       0xFFF00000L
//SHADOW_PREF_BASE_LIMIT
#define SHADOW_PREF_BASE_LIMIT__PREF_MEM_BASE_TYPE__MASK                                                      0x0000000FL
#define SHADOW_PREF_BASE_LIMIT__PREF_MEM_BASE_31_20_UP__MASK                                                  0x0000FFF0L
#define SHADOW_PREF_BASE_LIMIT__PREF_MEM_LIMIT_TYPE__MASK                                                     0x000F0000L
#define SHADOW_PREF_BASE_LIMIT__PREF_MEM_LIMIT_31_20_UP__MASK                                                 0xFFF00000L
//SHADOW_PREF_BASE_UPPER
#define SHADOW_PREF_BASE_UPPER__PREF_BASE_UPPER_UP__MASK                                                      0xFFFFFFFFL
//SHADOW_PREF_LIMIT_UPPER
#define SHADOW_PREF_LIMIT_UPPER__PREF_LIMIT_UPPER_UP__MASK                                                    0xFFFFFFFFL
//SHADOW_IO_BASE_LIMIT_HI
#define SHADOW_IO_BASE_LIMIT_HI__IO_BASE_31_16_UP__MASK                                                       0x0000FFFFL
#define SHADOW_IO_BASE_LIMIT_HI__IO_LIMIT_31_16_UP__MASK                                                      0xFFFF0000L
//SHADOW_IRQ_BRIDGE_CNTL
#define SHADOW_IRQ_BRIDGE_CNTL__ISA_EN_UP__MASK                                                               0x0004L
#define SHADOW_IRQ_BRIDGE_CNTL__VGA_EN_UP__MASK                                                               0x0008L
#define SHADOW_IRQ_BRIDGE_CNTL__VGA_DEC_UP__MASK                                                              0x0010L
#define SHADOW_IRQ_BRIDGE_CNTL__SECONDARY_BUS_RESET_UP__MASK                                                  0x0040L
//SUC_INDEX
#define SUC_INDEX__SUC_INDEX__MASK                                                                            0xFFFFFFFFL
//SUC_DATA
#define SUC_DATA__SUC_DATA__MASK                                                                              0xFFFFFFFFL


// addressBlock: bif_bx_pf_SUMDEC
//SUM_INDEX
#define SUM_INDEX__SUM_INDEX__MASK                                                                            0xFFFFFFFFL
//SUM_DATA
#define SUM_DATA__SUM_DATA__MASK                                                                              0xFFFFFFFFL


// addressBlock: gdc_GDCDEC
//A2S_CNTL_CL0
#define A2S_CNTL_CL0__NSNOOP_MAP__MASK                                                                        0x00000003L
#define A2S_CNTL_CL0__REQPASSPW_VC0_MAP__MASK                                                                 0x0000000CL
#define A2S_CNTL_CL0__REQPASSPW_NVC0_MAP__MASK                                                                0x00000030L
#define A2S_CNTL_CL0__REQRSPPASSPW_VC0_MAP__MASK                                                              0x000000C0L
#define A2S_CNTL_CL0__REQRSPPASSPW_NVC0_MAP__MASK                                                             0x00000300L
#define A2S_CNTL_CL0__BLKLVL_MAP__MASK                                                                        0x00000C00L
#define A2S_CNTL_CL0__DATERR_MAP__MASK                                                                        0x00003000L
#define A2S_CNTL_CL0__EXOKAY_WR_MAP__MASK                                                                     0x0000C000L
#define A2S_CNTL_CL0__EXOKAY_RD_MAP__MASK                                                                     0x00030000L
#define A2S_CNTL_CL0__RESP_WR_MAP__MASK                                                                       0x000C0000L
#define A2S_CNTL_CL0__RESP_RD_MAP__MASK                                                                       0x00300000L
//A2S_CNTL_CL1
#define A2S_CNTL_CL1__NSNOOP_MAP__MASK                                                                        0x00000003L
#define A2S_CNTL_CL1__REQPASSPW_VC0_MAP__MASK                                                                 0x0000000CL
#define A2S_CNTL_CL1__REQPASSPW_NVC0_MAP__MASK                                                                0x00000030L
#define A2S_CNTL_CL1__REQRSPPASSPW_VC0_MAP__MASK                                                              0x000000C0L
#define A2S_CNTL_CL1__REQRSPPASSPW_NVC0_MAP__MASK                                                             0x00000300L
#define A2S_CNTL_CL1__BLKLVL_MAP__MASK                                                                        0x00000C00L
#define A2S_CNTL_CL1__DATERR_MAP__MASK                                                                        0x00003000L
#define A2S_CNTL_CL1__EXOKAY_WR_MAP__MASK                                                                     0x0000C000L
#define A2S_CNTL_CL1__EXOKAY_RD_MAP__MASK                                                                     0x00030000L
#define A2S_CNTL_CL1__RESP_WR_MAP__MASK                                                                       0x000C0000L
#define A2S_CNTL_CL1__RESP_RD_MAP__MASK                                                                       0x00300000L
//A2S_CNTL_CL2
#define A2S_CNTL_CL2__NSNOOP_MAP__MASK                                                                        0x00000003L
#define A2S_CNTL_CL2__REQPASSPW_VC0_MAP__MASK                                                                 0x0000000CL
#define A2S_CNTL_CL2__REQPASSPW_NVC0_MAP__MASK                                                                0x00000030L
#define A2S_CNTL_CL2__REQRSPPASSPW_VC0_MAP__MASK                                                              0x000000C0L
#define A2S_CNTL_CL2__REQRSPPASSPW_NVC0_MAP__MASK                                                             0x00000300L
#define A2S_CNTL_CL2__BLKLVL_MAP__MASK                                                                        0x00000C00L
#define A2S_CNTL_CL2__DATERR_MAP__MASK                                                                        0x00003000L
#define A2S_CNTL_CL2__EXOKAY_WR_MAP__MASK                                                                     0x0000C000L
#define A2S_CNTL_CL2__EXOKAY_RD_MAP__MASK                                                                     0x00030000L
#define A2S_CNTL_CL2__RESP_WR_MAP__MASK                                                                       0x000C0000L
#define A2S_CNTL_CL2__RESP_RD_MAP__MASK                                                                       0x00300000L
//A2S_CNTL_CL3
#define A2S_CNTL_CL3__NSNOOP_MAP__MASK                                                                        0x00000003L
#define A2S_CNTL_CL3__REQPASSPW_VC0_MAP__MASK                                                                 0x0000000CL
#define A2S_CNTL_CL3__REQPASSPW_NVC0_MAP__MASK                                                                0x00000030L
#define A2S_CNTL_CL3__REQRSPPASSPW_VC0_MAP__MASK                                                              0x000000C0L
#define A2S_CNTL_CL3__REQRSPPASSPW_NVC0_MAP__MASK                                                             0x00000300L
#define A2S_CNTL_CL3__BLKLVL_MAP__MASK                                                                        0x00000C00L
#define A2S_CNTL_CL3__DATERR_MAP__MASK                                                                        0x00003000L
#define A2S_CNTL_CL3__EXOKAY_WR_MAP__MASK                                                                     0x0000C000L
#define A2S_CNTL_CL3__EXOKAY_RD_MAP__MASK                                                                     0x00030000L
#define A2S_CNTL_CL3__RESP_WR_MAP__MASK                                                                       0x000C0000L
#define A2S_CNTL_CL3__RESP_RD_MAP__MASK                                                                       0x00300000L
//A2S_CNTL_CL4
#define A2S_CNTL_CL4__NSNOOP_MAP__MASK                                                                        0x00000003L
#define A2S_CNTL_CL4__REQPASSPW_VC0_MAP__MASK                                                                 0x0000000CL
#define A2S_CNTL_CL4__REQPASSPW_NVC0_MAP__MASK                                                                0x00000030L
#define A2S_CNTL_CL4__REQRSPPASSPW_VC0_MAP__MASK                                                              0x000000C0L
#define A2S_CNTL_CL4__REQRSPPASSPW_NVC0_MAP__MASK                                                             0x00000300L
#define A2S_CNTL_CL4__BLKLVL_MAP__MASK                                                                        0x00000C00L
#define A2S_CNTL_CL4__DATERR_MAP__MASK                                                                        0x00003000L
#define A2S_CNTL_CL4__EXOKAY_WR_MAP__MASK                                                                     0x0000C000L
#define A2S_CNTL_CL4__EXOKAY_RD_MAP__MASK                                                                     0x00030000L
#define A2S_CNTL_CL4__RESP_WR_MAP__MASK                                                                       0x000C0000L
#define A2S_CNTL_CL4__RESP_RD_MAP__MASK                                                                       0x00300000L
//A2S_CNTL_SW0
#define A2S_CNTL_SW0__WR_TAG_SET_MIN__MASK                                                                    0x00000007L
#define A2S_CNTL_SW0__RD_TAG_SET_MIN__MASK                                                                    0x00000038L
#define A2S_CNTL_SW0__FORCE_RSP_REORDER_EN__MASK                                                              0x00000040L
#define A2S_CNTL_SW0__RSP_REORDER_DIS__MASK                                                                   0x00000080L
#define A2S_CNTL_SW0__WRRSP_ACCUM_SEL__MASK                                                                   0x00000100L
#define A2S_CNTL_SW0__SDP_WR_CHAIN_DIS__MASK                                                                  0x00000200L
#define A2S_CNTL_SW0__WRRSP_TAGFIFO_CONT_RD_DIS__MASK                                                         0x00000400L
#define A2S_CNTL_SW0__RDRSP_TAGFIFO_CONT_RD_DIS__MASK                                                         0x00000800L
#define A2S_CNTL_SW0__RDRSP_STS_DATSTS_PRIORITY__MASK                                                         0x00001000L
#define A2S_CNTL_SW0__WRR_RD_WEIGHT__MASK                                                                     0x00FF0000L
#define A2S_CNTL_SW0__WRR_WR_WEIGHT__MASK                                                                     0xFF000000L
//A2S_CNTL_SW1
#define A2S_CNTL_SW1__WR_TAG_SET_MIN__MASK                                                                    0x00000007L
#define A2S_CNTL_SW1__RD_TAG_SET_MIN__MASK                                                                    0x00000038L
#define A2S_CNTL_SW1__FORCE_RSP_REORDER_EN__MASK                                                              0x00000040L
#define A2S_CNTL_SW1__RSP_REORDER_DIS__MASK                                                                   0x00000080L
#define A2S_CNTL_SW1__WRRSP_ACCUM_SEL__MASK                                                                   0x00000100L
#define A2S_CNTL_SW1__SDP_WR_CHAIN_DIS__MASK                                                                  0x00000200L
#define A2S_CNTL_SW1__WRRSP_TAGFIFO_CONT_RD_DIS__MASK                                                         0x00000400L
#define A2S_CNTL_SW1__RDRSP_TAGFIFO_CONT_RD_DIS__MASK                                                         0x00000800L
#define A2S_CNTL_SW1__RDRSP_STS_DATSTS_PRIORITY__MASK                                                         0x00001000L
#define A2S_CNTL_SW1__WRR_RD_WEIGHT__MASK                                                                     0x00FF0000L
#define A2S_CNTL_SW1__WRR_WR_WEIGHT__MASK                                                                     0xFF000000L
//A2S_CNTL_SW2
#define A2S_CNTL_SW2__WR_TAG_SET_MIN__MASK                                                                    0x00000007L
#define A2S_CNTL_SW2__RD_TAG_SET_MIN__MASK                                                                    0x00000038L
#define A2S_CNTL_SW2__FORCE_RSP_REORDER_EN__MASK                                                              0x00000040L
#define A2S_CNTL_SW2__RSP_REORDER_DIS__MASK                                                                   0x00000080L
#define A2S_CNTL_SW2__WRRSP_ACCUM_SEL__MASK                                                                   0x00000100L
#define A2S_CNTL_SW2__SDP_WR_CHAIN_DIS__MASK                                                                  0x00000200L
#define A2S_CNTL_SW2__WRRSP_TAGFIFO_CONT_RD_DIS__MASK                                                         0x00000400L
#define A2S_CNTL_SW2__RDRSP_TAGFIFO_CONT_RD_DIS__MASK                                                         0x00000800L
#define A2S_CNTL_SW2__RDRSP_STS_DATSTS_PRIORITY__MASK                                                         0x00001000L
#define A2S_CNTL_SW2__WRR_RD_WEIGHT__MASK                                                                     0x00FF0000L
#define A2S_CNTL_SW2__WRR_WR_WEIGHT__MASK                                                                     0xFF000000L
//NGDC_MGCG_CTRL
#define NGDC_MGCG_CTRL__NGDC_MGCG_EN__MASK                                                                    0x00000001L
#define NGDC_MGCG_CTRL__NGDC_MGCG_MODE__MASK                                                                  0x00000002L
#define NGDC_MGCG_CTRL__NGDC_MGCG_HYSTERESIS__MASK                                                            0x000003FCL
//A2S_MISC_CNTL
#define A2S_MISC_CNTL__BLKLVL_FOR_MSG__MASK                                                                   0x00000003L
#define A2S_MISC_CNTL__RESERVE_2_CRED_FOR_NPWR_REQ_DIS__MASK                                                  0x00000004L
//NGDC_SDP_PORT_CTRL
#define NGDC_SDP_PORT_CTRL__SDP_DISCON_HYSTERESIS__MASK                                                       0x0000003FL
//NGDC_RESERVED_0
#define NGDC_RESERVED_0__RESERVED__MASK                                                                       0xFFFFFFFFL
//NGDC_RESERVED_1
#define NGDC_RESERVED_1__RESERVED__MASK                                                                       0xFFFFFFFFL
//BIF_SDMA0_DOORBELL_RANGE
#define BIF_SDMA0_DOORBELL_RANGE__OFFSET__MASK                                                                0x00000FFCL
#define BIF_SDMA0_DOORBELL_RANGE__SIZE__MASK                                                                  0x001F0000L
//BIF_SDMA1_DOORBELL_RANGE
#define BIF_SDMA1_DOORBELL_RANGE__OFFSET__MASK                                                                0x00000FFCL
#define BIF_SDMA1_DOORBELL_RANGE__SIZE__MASK                                                                  0x001F0000L
//BIF_IH_DOORBELL_RANGE
#define BIF_IH_DOORBELL_RANGE__OFFSET__MASK                                                                   0x00000FFCL
#define BIF_IH_DOORBELL_RANGE__SIZE__MASK                                                                     0x001F0000L
//BIF_MMSCH0_DOORBELL_RANGE
#define BIF_MMSCH0_DOORBELL_RANGE__OFFSET__MASK                                                               0x00000FFCL
#define BIF_MMSCH0_DOORBELL_RANGE__SIZE__MASK                                                                 0x001F0000L
//BIF_DOORBELL_FENCE_CNTL
#define BIF_DOORBELL_FENCE_CNTL__DOORBELL_FENCE_ENABLE__MASK                                                  0x00000001L
//S2A_MISC_CNTL
#define S2A_MISC_CNTL__DOORBELL_64BIT_SUPPORT_SDMA0_DIS__MASK                                                 0x00000001L
#define S2A_MISC_CNTL__DOORBELL_64BIT_SUPPORT_SDMA1_DIS__MASK                                                 0x00000002L
#define S2A_MISC_CNTL__DOORBELL_64BIT_SUPPORT_CP_DIS__MASK                                                    0x00000004L
//A2S_CNTL2_SEC_CL0
#define A2S_CNTL2_SEC_CL0__SECLVL_MAP__MASK                                                                   0x00000007L
//A2S_CNTL2_SEC_CL1
#define A2S_CNTL2_SEC_CL1__SECLVL_MAP__MASK                                                                   0x00000007L
//A2S_CNTL2_SEC_CL2
#define A2S_CNTL2_SEC_CL2__SECLVL_MAP__MASK                                                                   0x00000007L
//A2S_CNTL2_SEC_CL3
#define A2S_CNTL2_SEC_CL3__SECLVL_MAP__MASK                                                                   0x00000007L
//A2S_CNTL2_SEC_CL4
#define A2S_CNTL2_SEC_CL4__SECLVL_MAP__MASK                                                                   0x00000007L


// addressBlock: nbif_sion_SIONDEC
//SION_CL0_RdRsp_BurstTarget_REG0
#define SION_CL0_RdRsp_BurstTarget_REG0__RdRsp_BurstTarget_31_0__MASK                                         0xFFFFFFFFL
//SION_CL0_RdRsp_BurstTarget_REG1
#define SION_CL0_RdRsp_BurstTarget_REG1__RdRsp_BurstTarget_63_32__MASK                                        0xFFFFFFFFL
//SION_CL0_RdRsp_TimeSlot_REG0
#define SION_CL0_RdRsp_TimeSlot_REG0__RdRsp_TimeSlot_31_0__MASK                                               0xFFFFFFFFL
//SION_CL0_RdRsp_TimeSlot_REG1
#define SION_CL0_RdRsp_TimeSlot_REG1__RdRsp_TimeSlot_63_32__MASK                                              0xFFFFFFFFL
//SION_CL0_WrRsp_BurstTarget_REG0
#define SION_CL0_WrRsp_BurstTarget_REG0__WrRsp_BurstTarget_31_0__MASK                                         0xFFFFFFFFL
//SION_CL0_WrRsp_BurstTarget_REG1
#define SION_CL0_WrRsp_BurstTarget_REG1__WrRsp_BurstTarget_63_32__MASK                                        0xFFFFFFFFL
//SION_CL0_WrRsp_TimeSlot_REG0
#define SION_CL0_WrRsp_TimeSlot_REG0__WrRsp_TimeSlot_31_0__MASK                                               0xFFFFFFFFL
//SION_CL0_WrRsp_TimeSlot_REG1
#define SION_CL0_WrRsp_TimeSlot_REG1__WrRsp_TimeSlot_63_32__MASK                                              0xFFFFFFFFL
//SION_CL0_Req_BurstTarget_REG0
#define SION_CL0_Req_BurstTarget_REG0__Req_BurstTarget_31_0__MASK                                             0xFFFFFFFFL
//SION_CL0_Req_BurstTarget_REG1
#define SION_CL0_Req_BurstTarget_REG1__Req_BurstTarget_63_32__MASK                                            0xFFFFFFFFL
//SION_CL0_Req_TimeSlot_REG0
#define SION_CL0_Req_TimeSlot_REG0__Req_TimeSlot_31_0__MASK                                                   0xFFFFFFFFL
//SION_CL0_Req_TimeSlot_REG1
#define SION_CL0_Req_TimeSlot_REG1__Req_TimeSlot_63_32__MASK                                                  0xFFFFFFFFL
//SION_CL0_ReqPoolCredit_Alloc_REG0
#define SION_CL0_ReqPoolCredit_Alloc_REG0__ReqPoolCredit_Alloc_31_0__MASK                                     0xFFFFFFFFL
//SION_CL0_ReqPoolCredit_Alloc_REG1
#define SION_CL0_ReqPoolCredit_Alloc_REG1__ReqPoolCredit_Alloc_63_32__MASK                                    0xFFFFFFFFL
//SION_CL0_DataPoolCredit_Alloc_REG0
#define SION_CL0_DataPoolCredit_Alloc_REG0__DataPoolCredit_Alloc_31_0__MASK                                   0xFFFFFFFFL
//SION_CL0_DataPoolCredit_Alloc_REG1
#define SION_CL0_DataPoolCredit_Alloc_REG1__DataPoolCredit_Alloc_63_32__MASK                                  0xFFFFFFFFL
//SION_CL0_RdRspPoolCredit_Alloc_REG0
#define SION_CL0_RdRspPoolCredit_Alloc_REG0__RdRspPoolCredit_Alloc_31_0__MASK                                 0xFFFFFFFFL
//SION_CL0_RdRspPoolCredit_Alloc_REG1
#define SION_CL0_RdRspPoolCredit_Alloc_REG1__RdRspPoolCredit_Alloc_63_32__MASK                                0xFFFFFFFFL
//SION_CL0_WrRspPoolCredit_Alloc_REG0
#define SION_CL0_WrRspPoolCredit_Alloc_REG0__WrRspPoolCredit_Alloc_31_0__MASK                                 0xFFFFFFFFL
//SION_CL0_WrRspPoolCredit_Alloc_REG1
#define SION_CL0_WrRspPoolCredit_Alloc_REG1__WrRspPoolCredit_Alloc_63_32__MASK                                0xFFFFFFFFL
//SION_CL1_RdRsp_BurstTarget_REG0
#define SION_CL1_RdRsp_BurstTarget_REG0__RdRsp_BurstTarget_31_0__MASK                                         0xFFFFFFFFL
//SION_CL1_RdRsp_BurstTarget_REG1
#define SION_CL1_RdRsp_BurstTarget_REG1__RdRsp_BurstTarget_63_32__MASK                                        0xFFFFFFFFL
//SION_CL1_RdRsp_TimeSlot_REG0
#define SION_CL1_RdRsp_TimeSlot_REG0__RdRsp_TimeSlot_31_0__MASK                                               0xFFFFFFFFL
//SION_CL1_RdRsp_TimeSlot_REG1
#define SION_CL1_RdRsp_TimeSlot_REG1__RdRsp_TimeSlot_63_32__MASK                                              0xFFFFFFFFL
//SION_CL1_WrRsp_BurstTarget_REG0
#define SION_CL1_WrRsp_BurstTarget_REG0__WrRsp_BurstTarget_31_0__MASK                                         0xFFFFFFFFL
//SION_CL1_WrRsp_BurstTarget_REG1
#define SION_CL1_WrRsp_BurstTarget_REG1__WrRsp_BurstTarget_63_32__MASK                                        0xFFFFFFFFL
//SION_CL1_WrRsp_TimeSlot_REG0
#define SION_CL1_WrRsp_TimeSlot_REG0__WrRsp_TimeSlot_31_0__MASK                                               0xFFFFFFFFL
//SION_CL1_WrRsp_TimeSlot_REG1
#define SION_CL1_WrRsp_TimeSlot_REG1__WrRsp_TimeSlot_63_32__MASK                                              0xFFFFFFFFL
//SION_CL1_Req_BurstTarget_REG0
#define SION_CL1_Req_BurstTarget_REG0__Req_BurstTarget_31_0__MASK                                             0xFFFFFFFFL
//SION_CL1_Req_BurstTarget_REG1
#define SION_CL1_Req_BurstTarget_REG1__Req_BurstTarget_63_32__MASK                                            0xFFFFFFFFL
//SION_CL1_Req_TimeSlot_REG0
#define SION_CL1_Req_TimeSlot_REG0__Req_TimeSlot_31_0__MASK                                                   0xFFFFFFFFL
//SION_CL1_Req_TimeSlot_REG1
#define SION_CL1_Req_TimeSlot_REG1__Req_TimeSlot_63_32__MASK                                                  0xFFFFFFFFL
//SION_CL1_ReqPoolCredit_Alloc_REG0
#define SION_CL1_ReqPoolCredit_Alloc_REG0__ReqPoolCredit_Alloc_31_0__MASK                                     0xFFFFFFFFL
//SION_CL1_ReqPoolCredit_Alloc_REG1
#define SION_CL1_ReqPoolCredit_Alloc_REG1__ReqPoolCredit_Alloc_63_32__MASK                                    0xFFFFFFFFL
//SION_CL1_DataPoolCredit_Alloc_REG0
#define SION_CL1_DataPoolCredit_Alloc_REG0__DataPoolCredit_Alloc_31_0__MASK                                   0xFFFFFFFFL
//SION_CL1_DataPoolCredit_Alloc_REG1
#define SION_CL1_DataPoolCredit_Alloc_REG1__DataPoolCredit_Alloc_63_32__MASK                                  0xFFFFFFFFL
//SION_CL1_RdRspPoolCredit_Alloc_REG0
#define SION_CL1_RdRspPoolCredit_Alloc_REG0__RdRspPoolCredit_Alloc_31_0__MASK                                 0xFFFFFFFFL
//SION_CL1_RdRspPoolCredit_Alloc_REG1
#define SION_CL1_RdRspPoolCredit_Alloc_REG1__RdRspPoolCredit_Alloc_63_32__MASK                                0xFFFFFFFFL
//SION_CL1_WrRspPoolCredit_Alloc_REG0
#define SION_CL1_WrRspPoolCredit_Alloc_REG0__WrRspPoolCredit_Alloc_31_0__MASK                                 0xFFFFFFFFL
//SION_CL1_WrRspPoolCredit_Alloc_REG1
#define SION_CL1_WrRspPoolCredit_Alloc_REG1__WrRspPoolCredit_Alloc_63_32__MASK                                0xFFFFFFFFL
//SION_CL2_RdRsp_BurstTarget_REG0
#define SION_CL2_RdRsp_BurstTarget_REG0__RdRsp_BurstTarget_31_0__MASK                                         0xFFFFFFFFL
//SION_CL2_RdRsp_BurstTarget_REG1
#define SION_CL2_RdRsp_BurstTarget_REG1__RdRsp_BurstTarget_63_32__MASK                                        0xFFFFFFFFL
//SION_CL2_RdRsp_TimeSlot_REG0
#define SION_CL2_RdRsp_TimeSlot_REG0__RdRsp_TimeSlot_31_0__MASK                                               0xFFFFFFFFL
//SION_CL2_RdRsp_TimeSlot_REG1
#define SION_CL2_RdRsp_TimeSlot_REG1__RdRsp_TimeSlot_63_32__MASK                                              0xFFFFFFFFL
//SION_CL2_WrRsp_BurstTarget_REG0
#define SION_CL2_WrRsp_BurstTarget_REG0__WrRsp_BurstTarget_31_0__MASK                                         0xFFFFFFFFL
//SION_CL2_WrRsp_BurstTarget_REG1
#define SION_CL2_WrRsp_BurstTarget_REG1__WrRsp_BurstTarget_63_32__MASK                                        0xFFFFFFFFL
//SION_CL2_WrRsp_TimeSlot_REG0
#define SION_CL2_WrRsp_TimeSlot_REG0__WrRsp_TimeSlot_31_0__MASK                                               0xFFFFFFFFL
//SION_CL2_WrRsp_TimeSlot_REG1
#define SION_CL2_WrRsp_TimeSlot_REG1__WrRsp_TimeSlot_63_32__MASK                                              0xFFFFFFFFL
//SION_CL2_Req_BurstTarget_REG0
#define SION_CL2_Req_BurstTarget_REG0__Req_BurstTarget_31_0__MASK                                             0xFFFFFFFFL
//SION_CL2_Req_BurstTarget_REG1
#define SION_CL2_Req_BurstTarget_REG1__Req_BurstTarget_63_32__MASK                                            0xFFFFFFFFL
//SION_CL2_Req_TimeSlot_REG0
#define SION_CL2_Req_TimeSlot_REG0__Req_TimeSlot_31_0__MASK                                                   0xFFFFFFFFL
//SION_CL2_Req_TimeSlot_REG1
#define SION_CL2_Req_TimeSlot_REG1__Req_TimeSlot_63_32__MASK                                                  0xFFFFFFFFL
//SION_CL2_ReqPoolCredit_Alloc_REG0
#define SION_CL2_ReqPoolCredit_Alloc_REG0__ReqPoolCredit_Alloc_31_0__MASK                                     0xFFFFFFFFL
//SION_CL2_ReqPoolCredit_Alloc_REG1
#define SION_CL2_ReqPoolCredit_Alloc_REG1__ReqPoolCredit_Alloc_63_32__MASK                                    0xFFFFFFFFL
//SION_CL2_DataPoolCredit_Alloc_REG0
#define SION_CL2_DataPoolCredit_Alloc_REG0__DataPoolCredit_Alloc_31_0__MASK                                   0xFFFFFFFFL
//SION_CL2_DataPoolCredit_Alloc_REG1
#define SION_CL2_DataPoolCredit_Alloc_REG1__DataPoolCredit_Alloc_63_32__MASK                                  0xFFFFFFFFL
//SION_CL2_RdRspPoolCredit_Alloc_REG0
#define SION_CL2_RdRspPoolCredit_Alloc_REG0__RdRspPoolCredit_Alloc_31_0__MASK                                 0xFFFFFFFFL
//SION_CL2_RdRspPoolCredit_Alloc_REG1
#define SION_CL2_RdRspPoolCredit_Alloc_REG1__RdRspPoolCredit_Alloc_63_32__MASK                                0xFFFFFFFFL
//SION_CL2_WrRspPoolCredit_Alloc_REG0
#define SION_CL2_WrRspPoolCredit_Alloc_REG0__WrRspPoolCredit_Alloc_31_0__MASK                                 0xFFFFFFFFL
//SION_CL2_WrRspPoolCredit_Alloc_REG1
#define SION_CL2_WrRspPoolCredit_Alloc_REG1__WrRspPoolCredit_Alloc_63_32__MASK                                0xFFFFFFFFL
//SION_CL3_RdRsp_BurstTarget_REG0
#define SION_CL3_RdRsp_BurstTarget_REG0__RdRsp_BurstTarget_31_0__MASK                                         0xFFFFFFFFL
//SION_CL3_RdRsp_BurstTarget_REG1
#define SION_CL3_RdRsp_BurstTarget_REG1__RdRsp_BurstTarget_63_32__MASK                                        0xFFFFFFFFL
//SION_CL3_RdRsp_TimeSlot_REG0
#define SION_CL3_RdRsp_TimeSlot_REG0__RdRsp_TimeSlot_31_0__MASK                                               0xFFFFFFFFL
//SION_CL3_RdRsp_TimeSlot_REG1
#define SION_CL3_RdRsp_TimeSlot_REG1__RdRsp_TimeSlot_63_32__MASK                                              0xFFFFFFFFL
//SION_CL3_WrRsp_BurstTarget_REG0
#define SION_CL3_WrRsp_BurstTarget_REG0__WrRsp_BurstTarget_31_0__MASK                                         0xFFFFFFFFL
//SION_CL3_WrRsp_BurstTarget_REG1
#define SION_CL3_WrRsp_BurstTarget_REG1__WrRsp_BurstTarget_63_32__MASK                                        0xFFFFFFFFL
//SION_CL3_WrRsp_TimeSlot_REG0
#define SION_CL3_WrRsp_TimeSlot_REG0__WrRsp_TimeSlot_31_0__MASK                                               0xFFFFFFFFL
//SION_CL3_WrRsp_TimeSlot_REG1
#define SION_CL3_WrRsp_TimeSlot_REG1__WrRsp_TimeSlot_63_32__MASK                                              0xFFFFFFFFL
//SION_CL3_Req_BurstTarget_REG0
#define SION_CL3_Req_BurstTarget_REG0__Req_BurstTarget_31_0__MASK                                             0xFFFFFFFFL
//SION_CL3_Req_BurstTarget_REG1
#define SION_CL3_Req_BurstTarget_REG1__Req_BurstTarget_63_32__MASK                                            0xFFFFFFFFL
//SION_CL3_Req_TimeSlot_REG0
#define SION_CL3_Req_TimeSlot_REG0__Req_TimeSlot_31_0__MASK                                                   0xFFFFFFFFL
//SION_CL3_Req_TimeSlot_REG1
#define SION_CL3_Req_TimeSlot_REG1__Req_TimeSlot_63_32__MASK                                                  0xFFFFFFFFL
//SION_CL3_ReqPoolCredit_Alloc_REG0
#define SION_CL3_ReqPoolCredit_Alloc_REG0__ReqPoolCredit_Alloc_31_0__MASK                                     0xFFFFFFFFL
//SION_CL3_ReqPoolCredit_Alloc_REG1
#define SION_CL3_ReqPoolCredit_Alloc_REG1__ReqPoolCredit_Alloc_63_32__MASK                                    0xFFFFFFFFL
//SION_CL3_DataPoolCredit_Alloc_REG0
#define SION_CL3_DataPoolCredit_Alloc_REG0__DataPoolCredit_Alloc_31_0__MASK                                   0xFFFFFFFFL
//SION_CL3_DataPoolCredit_Alloc_REG1
#define SION_CL3_DataPoolCredit_Alloc_REG1__DataPoolCredit_Alloc_63_32__MASK                                  0xFFFFFFFFL
//SION_CL3_RdRspPoolCredit_Alloc_REG0
#define SION_CL3_RdRspPoolCredit_Alloc_REG0__RdRspPoolCredit_Alloc_31_0__MASK                                 0xFFFFFFFFL
//SION_CL3_RdRspPoolCredit_Alloc_REG1
#define SION_CL3_RdRspPoolCredit_Alloc_REG1__RdRspPoolCredit_Alloc_63_32__MASK                                0xFFFFFFFFL
//SION_CL3_WrRspPoolCredit_Alloc_REG0
#define SION_CL3_WrRspPoolCredit_Alloc_REG0__WrRspPoolCredit_Alloc_31_0__MASK                                 0xFFFFFFFFL
//SION_CL3_WrRspPoolCredit_Alloc_REG1
#define SION_CL3_WrRspPoolCredit_Alloc_REG1__WrRspPoolCredit_Alloc_63_32__MASK                                0xFFFFFFFFL
//SION_CL4_RdRsp_BurstTarget_REG0
#define SION_CL4_RdRsp_BurstTarget_REG0__RdRsp_BurstTarget_31_0__MASK                                         0xFFFFFFFFL
//SION_CL4_RdRsp_BurstTarget_REG1
#define SION_CL4_RdRsp_BurstTarget_REG1__RdRsp_BurstTarget_63_32__MASK                                        0xFFFFFFFFL
//SION_CL4_RdRsp_TimeSlot_REG0
#define SION_CL4_RdRsp_TimeSlot_REG0__RdRsp_TimeSlot_31_0__MASK                                               0xFFFFFFFFL
//SION_CL4_RdRsp_TimeSlot_REG1
#define SION_CL4_RdRsp_TimeSlot_REG1__RdRsp_TimeSlot_63_32__MASK                                              0xFFFFFFFFL
//SION_CL4_WrRsp_BurstTarget_REG0
#define SION_CL4_WrRsp_BurstTarget_REG0__WrRsp_BurstTarget_31_0__MASK                                         0xFFFFFFFFL
//SION_CL4_WrRsp_BurstTarget_REG1
#define SION_CL4_WrRsp_BurstTarget_REG1__WrRsp_BurstTarget_63_32__MASK                                        0xFFFFFFFFL
//SION_CL4_WrRsp_TimeSlot_REG0
#define SION_CL4_WrRsp_TimeSlot_REG0__WrRsp_TimeSlot_31_0__MASK                                               0xFFFFFFFFL
//SION_CL4_WrRsp_TimeSlot_REG1
#define SION_CL4_WrRsp_TimeSlot_REG1__WrRsp_TimeSlot_63_32__MASK                                              0xFFFFFFFFL
//SION_CL4_Req_BurstTarget_REG0
#define SION_CL4_Req_BurstTarget_REG0__Req_BurstTarget_31_0__MASK                                             0xFFFFFFFFL
//SION_CL4_Req_BurstTarget_REG1
#define SION_CL4_Req_BurstTarget_REG1__Req_BurstTarget_63_32__MASK                                            0xFFFFFFFFL
//SION_CL4_Req_TimeSlot_REG0
#define SION_CL4_Req_TimeSlot_REG0__Req_TimeSlot_31_0__MASK                                                   0xFFFFFFFFL
//SION_CL4_Req_TimeSlot_REG1
#define SION_CL4_Req_TimeSlot_REG1__Req_TimeSlot_63_32__MASK                                                  0xFFFFFFFFL
//SION_CL4_ReqPoolCredit_Alloc_REG0
#define SION_CL4_ReqPoolCredit_Alloc_REG0__ReqPoolCredit_Alloc_31_0__MASK                                     0xFFFFFFFFL
//SION_CL4_ReqPoolCredit_Alloc_REG1
#define SION_CL4_ReqPoolCredit_Alloc_REG1__ReqPoolCredit_Alloc_63_32__MASK                                    0xFFFFFFFFL
//SION_CL4_DataPoolCredit_Alloc_REG0
#define SION_CL4_DataPoolCredit_Alloc_REG0__DataPoolCredit_Alloc_31_0__MASK                                   0xFFFFFFFFL
//SION_CL4_DataPoolCredit_Alloc_REG1
#define SION_CL4_DataPoolCredit_Alloc_REG1__DataPoolCredit_Alloc_63_32__MASK                                  0xFFFFFFFFL
//SION_CL4_RdRspPoolCredit_Alloc_REG0
#define SION_CL4_RdRspPoolCredit_Alloc_REG0__RdRspPoolCredit_Alloc_31_0__MASK                                 0xFFFFFFFFL
//SION_CL4_RdRspPoolCredit_Alloc_REG1
#define SION_CL4_RdRspPoolCredit_Alloc_REG1__RdRspPoolCredit_Alloc_63_32__MASK                                0xFFFFFFFFL
//SION_CL4_WrRspPoolCredit_Alloc_REG0
#define SION_CL4_WrRspPoolCredit_Alloc_REG0__WrRspPoolCredit_Alloc_31_0__MASK                                 0xFFFFFFFFL
//SION_CL4_WrRspPoolCredit_Alloc_REG1
#define SION_CL4_WrRspPoolCredit_Alloc_REG1__WrRspPoolCredit_Alloc_63_32__MASK                                0xFFFFFFFFL
//SION_CL5_RdRsp_BurstTarget_REG0
#define SION_CL5_RdRsp_BurstTarget_REG0__RdRsp_BurstTarget_31_0__MASK                                         0xFFFFFFFFL
//SION_CL5_RdRsp_BurstTarget_REG1
#define SION_CL5_RdRsp_BurstTarget_REG1__RdRsp_BurstTarget_63_32__MASK                                        0xFFFFFFFFL
//SION_CL5_RdRsp_TimeSlot_REG0
#define SION_CL5_RdRsp_TimeSlot_REG0__RdRsp_TimeSlot_31_0__MASK                                               0xFFFFFFFFL
//SION_CL5_RdRsp_TimeSlot_REG1
#define SION_CL5_RdRsp_TimeSlot_REG1__RdRsp_TimeSlot_63_32__MASK                                              0xFFFFFFFFL
//SION_CL5_WrRsp_BurstTarget_REG0
#define SION_CL5_WrRsp_BurstTarget_REG0__WrRsp_BurstTarget_31_0__MASK                                         0xFFFFFFFFL
//SION_CL5_WrRsp_BurstTarget_REG1
#define SION_CL5_WrRsp_BurstTarget_REG1__WrRsp_BurstTarget_63_32__MASK                                        0xFFFFFFFFL
//SION_CL5_WrRsp_TimeSlot_REG0
#define SION_CL5_WrRsp_TimeSlot_REG0__WrRsp_TimeSlot_31_0__MASK                                               0xFFFFFFFFL
//SION_CL5_WrRsp_TimeSlot_REG1
#define SION_CL5_WrRsp_TimeSlot_REG1__WrRsp_TimeSlot_63_32__MASK                                              0xFFFFFFFFL
//SION_CL5_Req_BurstTarget_REG0
#define SION_CL5_Req_BurstTarget_REG0__Req_BurstTarget_31_0__MASK                                             0xFFFFFFFFL
//SION_CL5_Req_BurstTarget_REG1
#define SION_CL5_Req_BurstTarget_REG1__Req_BurstTarget_63_32__MASK                                            0xFFFFFFFFL
//SION_CL5_Req_TimeSlot_REG0
#define SION_CL5_Req_TimeSlot_REG0__Req_TimeSlot_31_0__MASK                                                   0xFFFFFFFFL
//SION_CL5_Req_TimeSlot_REG1
#define SION_CL5_Req_TimeSlot_REG1__Req_TimeSlot_63_32__MASK                                                  0xFFFFFFFFL
//SION_CL5_ReqPoolCredit_Alloc_REG0
#define SION_CL5_ReqPoolCredit_Alloc_REG0__ReqPoolCredit_Alloc_31_0__MASK                                     0xFFFFFFFFL
//SION_CL5_ReqPoolCredit_Alloc_REG1
#define SION_CL5_ReqPoolCredit_Alloc_REG1__ReqPoolCredit_Alloc_63_32__MASK                                    0xFFFFFFFFL
//SION_CL5_DataPoolCredit_Alloc_REG0
#define SION_CL5_DataPoolCredit_Alloc_REG0__DataPoolCredit_Alloc_31_0__MASK                                   0xFFFFFFFFL
//SION_CL5_DataPoolCredit_Alloc_REG1
#define SION_CL5_DataPoolCredit_Alloc_REG1__DataPoolCredit_Alloc_63_32__MASK                                  0xFFFFFFFFL
//SION_CL5_RdRspPoolCredit_Alloc_REG0
#define SION_CL5_RdRspPoolCredit_Alloc_REG0__RdRspPoolCredit_Alloc_31_0__MASK                                 0xFFFFFFFFL
//SION_CL5_RdRspPoolCredit_Alloc_REG1
#define SION_CL5_RdRspPoolCredit_Alloc_REG1__RdRspPoolCredit_Alloc_63_32__MASK                                0xFFFFFFFFL
//SION_CL5_WrRspPoolCredit_Alloc_REG0
#define SION_CL5_WrRspPoolCredit_Alloc_REG0__WrRspPoolCredit_Alloc_31_0__MASK                                 0xFFFFFFFFL
//SION_CL5_WrRspPoolCredit_Alloc_REG1
#define SION_CL5_WrRspPoolCredit_Alloc_REG1__WrRspPoolCredit_Alloc_63_32__MASK                                0xFFFFFFFFL
//SION_CNTL_REG0
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_0_SOFT_OVERRIDE_CLK0__MASK                                 0x00000001L
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_0_SOFT_OVERRIDE_CLK1__MASK                                 0x00000002L
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_0_SOFT_OVERRIDE_CLK2__MASK                                 0x00000004L
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_0_SOFT_OVERRIDE_CLK3__MASK                                 0x00000008L
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_0_SOFT_OVERRIDE_CLK4__MASK                                 0x00000010L
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_0_SOFT_OVERRIDE_CLK5__MASK                                 0x00000020L
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_0_SOFT_OVERRIDE_CLK6__MASK                                 0x00000040L
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_0_SOFT_OVERRIDE_CLK7__MASK                                 0x00000080L
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_0_SOFT_OVERRIDE_CLK8__MASK                                 0x00000100L
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_0_SOFT_OVERRIDE_CLK9__MASK                                 0x00000200L
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_1_SOFT_OVERRIDE_CLK0__MASK                                 0x00000400L
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_1_SOFT_OVERRIDE_CLK1__MASK                                 0x00000800L
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_1_SOFT_OVERRIDE_CLK2__MASK                                 0x00001000L
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_1_SOFT_OVERRIDE_CLK3__MASK                                 0x00002000L
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_1_SOFT_OVERRIDE_CLK4__MASK                                 0x00004000L
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_1_SOFT_OVERRIDE_CLK5__MASK                                 0x00008000L
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_1_SOFT_OVERRIDE_CLK6__MASK                                 0x00010000L
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_1_SOFT_OVERRIDE_CLK7__MASK                                 0x00020000L
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_1_SOFT_OVERRIDE_CLK8__MASK                                 0x00040000L
#define SION_CNTL_REG0__NBIFSION_GLUE_CG_LCLK_CTRL_1_SOFT_OVERRIDE_CLK9__MASK                                 0x00080000L
//SION_CNTL_REG1
#define SION_CNTL_REG1__LIVELOCK_WATCHDOG_THRESHOLD__MASK                                                     0x000000FFL
#define SION_CNTL_REG1__CG_OFF_HYSTERESIS__MASK                                                               0x0000FF00L


// addressBlock: syshub_mmreg_direct_syshubdirect
//SYSHUB_DS_CTRL_SOCCLK
#define SYSHUB_DS_CTRL_SOCCLK__HST_CL0_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                    0x00000001L
#define SYSHUB_DS_CTRL_SOCCLK__HST_CL1_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                    0x00000002L
#define SYSHUB_DS_CTRL_SOCCLK__HST_CL2_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                    0x00000004L
#define SYSHUB_DS_CTRL_SOCCLK__HST_CL3_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                    0x00000008L
#define SYSHUB_DS_CTRL_SOCCLK__HST_CL4_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                    0x00000010L
#define SYSHUB_DS_CTRL_SOCCLK__HST_CL5_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                    0x00000020L
#define SYSHUB_DS_CTRL_SOCCLK__HST_CL6_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                    0x00000040L
#define SYSHUB_DS_CTRL_SOCCLK__HST_CL7_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                    0x00000080L
#define SYSHUB_DS_CTRL_SOCCLK__DMA_CL0_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                    0x00010000L
#define SYSHUB_DS_CTRL_SOCCLK__DMA_CL1_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                    0x00020000L
#define SYSHUB_DS_CTRL_SOCCLK__DMA_CL2_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                    0x00040000L
#define SYSHUB_DS_CTRL_SOCCLK__DMA_CL3_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                    0x00080000L
#define SYSHUB_DS_CTRL_SOCCLK__DMA_CL4_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                    0x00100000L
#define SYSHUB_DS_CTRL_SOCCLK__DMA_CL5_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                    0x00200000L
#define SYSHUB_DS_CTRL_SOCCLK__DMA_CL6_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                    0x00400000L
#define SYSHUB_DS_CTRL_SOCCLK__DMA_CL7_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                    0x00800000L
#define SYSHUB_DS_CTRL_SOCCLK__SYSHUB_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                     0x10000000L
#define SYSHUB_DS_CTRL_SOCCLK__SYSHUB_SOCCLK_DS_EN__MASK                                                      0x80000000L
//SYSHUB_DS_CTRL2_SOCCLK
#define SYSHUB_DS_CTRL2_SOCCLK__SYSHUB_SOCCLK_DS_TIMER__MASK                                                  0x0000FFFFL
//SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK
#define SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK__SYSHUB_bgen_socclk_HST_SW0_bypass_en__MASK                  0x00000001L
#define SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK__SYSHUB_bgen_socclk_HST_SW1_bypass_en__MASK                  0x00000002L
#define SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW0_bypass_en__MASK                  0x00008000L
#define SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW1_bypass_en__MASK                  0x00010000L
#define SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW2_bypass_en__MASK                  0x00020000L
//SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK
#define SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK__SYSHUB_bgen_socclk_HST_SW0_imm_en__MASK                        0x00000001L
#define SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK__SYSHUB_bgen_socclk_HST_SW1_imm_en__MASK                        0x00000002L
#define SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW0_imm_en__MASK                        0x00008000L
#define SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW1_imm_en__MASK                        0x00010000L
#define SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW2_imm_en__MASK                        0x00020000L
//DMA_CLK0_SW0_SYSHUB_QOS_CNTL
#define DMA_CLK0_SW0_SYSHUB_QOS_CNTL__QOS_CNTL_MODE__MASK                                                     0x00000001L
#define DMA_CLK0_SW0_SYSHUB_QOS_CNTL__QOS_MAX_VALUE__MASK                                                     0x0000001EL
#define DMA_CLK0_SW0_SYSHUB_QOS_CNTL__QOS_MIN_VALUE__MASK                                                     0x000001E0L
//DMA_CLK0_SW1_SYSHUB_QOS_CNTL
#define DMA_CLK0_SW1_SYSHUB_QOS_CNTL__QOS_CNTL_MODE__MASK                                                     0x00000001L
#define DMA_CLK0_SW1_SYSHUB_QOS_CNTL__QOS_MAX_VALUE__MASK                                                     0x0000001EL
#define DMA_CLK0_SW1_SYSHUB_QOS_CNTL__QOS_MIN_VALUE__MASK                                                     0x000001E0L
//DMA_CLK0_SW0_CL0_CNTL
#define DMA_CLK0_SW0_CL0_CNTL__FLR_ON_RS_RESET_EN__MASK                                                       0x00000001L
#define DMA_CLK0_SW0_CL0_CNTL__LKRST_ON_RS_RESET_EN__MASK                                                     0x00000002L
#define DMA_CLK0_SW0_CL0_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                                   0x00000100L
#define DMA_CLK0_SW0_CL0_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                                0x00001E00L
#define DMA_CLK0_SW0_CL0_CNTL__READ_WRR_WEIGHT__MASK                                                          0x00FF0000L
#define DMA_CLK0_SW0_CL0_CNTL__WRITE_WRR_WEIGHT__MASK                                                         0xFF000000L
//DMA_CLK0_SW0_CL1_CNTL
#define DMA_CLK0_SW0_CL1_CNTL__FLR_ON_RS_RESET_EN__MASK                                                       0x00000001L
#define DMA_CLK0_SW0_CL1_CNTL__LKRST_ON_RS_RESET_EN__MASK                                                     0x00000002L
#define DMA_CLK0_SW0_CL1_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                                   0x00000100L
#define DMA_CLK0_SW0_CL1_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                                0x00001E00L
#define DMA_CLK0_SW0_CL1_CNTL__READ_WRR_WEIGHT__MASK                                                          0x00FF0000L
#define DMA_CLK0_SW0_CL1_CNTL__WRITE_WRR_WEIGHT__MASK                                                         0xFF000000L
//DMA_CLK0_SW0_CL2_CNTL
#define DMA_CLK0_SW0_CL2_CNTL__FLR_ON_RS_RESET_EN__MASK                                                       0x00000001L
#define DMA_CLK0_SW0_CL2_CNTL__LKRST_ON_RS_RESET_EN__MASK                                                     0x00000002L
#define DMA_CLK0_SW0_CL2_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                                   0x00000100L
#define DMA_CLK0_SW0_CL2_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                                0x00001E00L
#define DMA_CLK0_SW0_CL2_CNTL__READ_WRR_WEIGHT__MASK                                                          0x00FF0000L
#define DMA_CLK0_SW0_CL2_CNTL__WRITE_WRR_WEIGHT__MASK                                                         0xFF000000L
//DMA_CLK0_SW0_CL3_CNTL
#define DMA_CLK0_SW0_CL3_CNTL__FLR_ON_RS_RESET_EN__MASK                                                       0x00000001L
#define DMA_CLK0_SW0_CL3_CNTL__LKRST_ON_RS_RESET_EN__MASK                                                     0x00000002L
#define DMA_CLK0_SW0_CL3_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                                   0x00000100L
#define DMA_CLK0_SW0_CL3_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                                0x00001E00L
#define DMA_CLK0_SW0_CL3_CNTL__READ_WRR_WEIGHT__MASK                                                          0x00FF0000L
#define DMA_CLK0_SW0_CL3_CNTL__WRITE_WRR_WEIGHT__MASK                                                         0xFF000000L
//DMA_CLK0_SW0_CL4_CNTL
#define DMA_CLK0_SW0_CL4_CNTL__FLR_ON_RS_RESET_EN__MASK                                                       0x00000001L
#define DMA_CLK0_SW0_CL4_CNTL__LKRST_ON_RS_RESET_EN__MASK                                                     0x00000002L
#define DMA_CLK0_SW0_CL4_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                                   0x00000100L
#define DMA_CLK0_SW0_CL4_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                                0x00001E00L
#define DMA_CLK0_SW0_CL4_CNTL__READ_WRR_WEIGHT__MASK                                                          0x00FF0000L
#define DMA_CLK0_SW0_CL4_CNTL__WRITE_WRR_WEIGHT__MASK                                                         0xFF000000L
//DMA_CLK0_SW0_CL5_CNTL
#define DMA_CLK0_SW0_CL5_CNTL__FLR_ON_RS_RESET_EN__MASK                                                       0x00000001L
#define DMA_CLK0_SW0_CL5_CNTL__LKRST_ON_RS_RESET_EN__MASK                                                     0x00000002L
#define DMA_CLK0_SW0_CL5_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                                   0x00000100L
#define DMA_CLK0_SW0_CL5_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                                0x00001E00L
#define DMA_CLK0_SW0_CL5_CNTL__READ_WRR_WEIGHT__MASK                                                          0x00FF0000L
#define DMA_CLK0_SW0_CL5_CNTL__WRITE_WRR_WEIGHT__MASK                                                         0xFF000000L
//DMA_CLK0_SW1_CL0_CNTL
#define DMA_CLK0_SW1_CL0_CNTL__FLR_ON_RS_RESET_EN__MASK                                                       0x00000001L
#define DMA_CLK0_SW1_CL0_CNTL__LKRST_ON_RS_RESET_EN__MASK                                                     0x00000002L
#define DMA_CLK0_SW1_CL0_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                                   0x00000100L
#define DMA_CLK0_SW1_CL0_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                                0x00001E00L
#define DMA_CLK0_SW1_CL0_CNTL__READ_WRR_WEIGHT__MASK                                                          0x00FF0000L
#define DMA_CLK0_SW1_CL0_CNTL__WRITE_WRR_WEIGHT__MASK                                                         0xFF000000L
//DMA_CLK0_SW2_CL0_CNTL
#define DMA_CLK0_SW2_CL0_CNTL__FLR_ON_RS_RESET_EN__MASK                                                       0x00000001L
#define DMA_CLK0_SW2_CL0_CNTL__LKRST_ON_RS_RESET_EN__MASK                                                     0x00000002L
#define DMA_CLK0_SW2_CL0_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                                   0x00000100L
#define DMA_CLK0_SW2_CL0_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                                0x00001E00L
#define DMA_CLK0_SW2_CL0_CNTL__READ_WRR_WEIGHT__MASK                                                          0x00FF0000L
#define DMA_CLK0_SW2_CL0_CNTL__WRITE_WRR_WEIGHT__MASK                                                         0xFF000000L
//SYSHUB_CG_CNTL
#define SYSHUB_CG_CNTL__SYSHUB_CG_EN__MASK                                                                    0x00000001L
#define SYSHUB_CG_CNTL__SYSHUB_CG_IDLE_TIMER__MASK                                                            0x0000FF00L
#define SYSHUB_CG_CNTL__SYSHUB_CG_WAKEUP_TIMER__MASK                                                          0x00FF0000L
//SYSHUB_TRANS_IDLE
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF0__MASK                                                        0x00000001L
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF1__MASK                                                        0x00000002L
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF2__MASK                                                        0x00000004L
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF3__MASK                                                        0x00000008L
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF4__MASK                                                        0x00000010L
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF5__MASK                                                        0x00000020L
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF6__MASK                                                        0x00000040L
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF7__MASK                                                        0x00000080L
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF8__MASK                                                        0x00000100L
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF9__MASK                                                        0x00000200L
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF10__MASK                                                       0x00000400L
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF11__MASK                                                       0x00000800L
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF12__MASK                                                       0x00001000L
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF13__MASK                                                       0x00002000L
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF14__MASK                                                       0x00004000L
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF15__MASK                                                       0x00008000L
#define SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_PF__MASK                                                         0x00010000L
//SYSHUB_HP_TIMER
#define SYSHUB_HP_TIMER__SYSHUB_HP_TIMER__MASK                                                                0xFFFFFFFFL
//SYSHUB_SCRATCH
#define SYSHUB_SCRATCH__SCRATCH__MASK                                                                         0xFFFFFFFFL
//SYSHUB_DS_CTRL_SHUBCLK
#define SYSHUB_DS_CTRL_SHUBCLK__HST_CL0_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                  0x00000001L
#define SYSHUB_DS_CTRL_SHUBCLK__HST_CL1_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                  0x00000002L
#define SYSHUB_DS_CTRL_SHUBCLK__HST_CL2_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                  0x00000004L
#define SYSHUB_DS_CTRL_SHUBCLK__HST_CL3_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                  0x00000008L
#define SYSHUB_DS_CTRL_SHUBCLK__HST_CL4_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                  0x00000010L
#define SYSHUB_DS_CTRL_SHUBCLK__HST_CL5_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                  0x00000020L
#define SYSHUB_DS_CTRL_SHUBCLK__HST_CL6_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                  0x00000040L
#define SYSHUB_DS_CTRL_SHUBCLK__HST_CL7_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                  0x00000080L
#define SYSHUB_DS_CTRL_SHUBCLK__DMA_CL0_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                  0x00010000L
#define SYSHUB_DS_CTRL_SHUBCLK__DMA_CL1_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                  0x00020000L
#define SYSHUB_DS_CTRL_SHUBCLK__DMA_CL2_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                  0x00040000L
#define SYSHUB_DS_CTRL_SHUBCLK__DMA_CL3_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                  0x00080000L
#define SYSHUB_DS_CTRL_SHUBCLK__DMA_CL4_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                  0x00100000L
#define SYSHUB_DS_CTRL_SHUBCLK__DMA_CL5_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                  0x00200000L
#define SYSHUB_DS_CTRL_SHUBCLK__DMA_CL6_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                  0x00400000L
#define SYSHUB_DS_CTRL_SHUBCLK__DMA_CL7_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                  0x00800000L
#define SYSHUB_DS_CTRL_SHUBCLK__SYSHUB_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                                   0x10000000L
#define SYSHUB_DS_CTRL_SHUBCLK__SYSHUB_SHUBCLK_DS_EN__MASK                                                    0x80000000L
//SYSHUB_DS_CTRL2_SHUBCLK
#define SYSHUB_DS_CTRL2_SHUBCLK__SYSHUB_SHUBCLK_DS_TIMER__MASK                                                0x0000FFFFL
//SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SHUBCLK
#define SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SHUBCLK__SYSHUB_bgen_shubclk_DMA_SW0_bypass_en__MASK                0x00008000L
#define SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SHUBCLK__SYSHUB_bgen_shubclk_DMA_SW1_bypass_en__MASK                0x00010000L
//SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SHUBCLK
#define SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SHUBCLK__SYSHUB_bgen_shubclk_DMA_SW0_imm_en__MASK                      0x00008000L
#define SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SHUBCLK__SYSHUB_bgen_shubclk_DMA_SW1_imm_en__MASK                      0x00010000L
//DMA_CLK1_SW0_SYSHUB_QOS_CNTL
#define DMA_CLK1_SW0_SYSHUB_QOS_CNTL__QOS_CNTL_MODE__MASK                                                     0x00000001L
#define DMA_CLK1_SW0_SYSHUB_QOS_CNTL__QOS_MAX_VALUE__MASK                                                     0x0000001EL
#define DMA_CLK1_SW0_SYSHUB_QOS_CNTL__QOS_MIN_VALUE__MASK                                                     0x000001E0L
//DMA_CLK1_SW1_SYSHUB_QOS_CNTL
#define DMA_CLK1_SW1_SYSHUB_QOS_CNTL__QOS_CNTL_MODE__MASK                                                     0x00000001L
#define DMA_CLK1_SW1_SYSHUB_QOS_CNTL__QOS_MAX_VALUE__MASK                                                     0x0000001EL
#define DMA_CLK1_SW1_SYSHUB_QOS_CNTL__QOS_MIN_VALUE__MASK                                                     0x000001E0L
//DMA_CLK1_SW0_CL0_CNTL
#define DMA_CLK1_SW0_CL0_CNTL__FLR_ON_RS_RESET_EN__MASK                                                       0x00000001L
#define DMA_CLK1_SW0_CL0_CNTL__LKRST_ON_RS_RESET_EN__MASK                                                     0x00000002L
#define DMA_CLK1_SW0_CL0_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                                   0x00000100L
#define DMA_CLK1_SW0_CL0_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                                0x00001E00L
#define DMA_CLK1_SW0_CL0_CNTL__READ_WRR_WEIGHT__MASK                                                          0x00FF0000L
#define DMA_CLK1_SW0_CL0_CNTL__WRITE_WRR_WEIGHT__MASK                                                         0xFF000000L
//DMA_CLK1_SW0_CL1_CNTL
#define DMA_CLK1_SW0_CL1_CNTL__FLR_ON_RS_RESET_EN__MASK                                                       0x00000001L
#define DMA_CLK1_SW0_CL1_CNTL__LKRST_ON_RS_RESET_EN__MASK                                                     0x00000002L
#define DMA_CLK1_SW0_CL1_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                                   0x00000100L
#define DMA_CLK1_SW0_CL1_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                                0x00001E00L
#define DMA_CLK1_SW0_CL1_CNTL__READ_WRR_WEIGHT__MASK                                                          0x00FF0000L
#define DMA_CLK1_SW0_CL1_CNTL__WRITE_WRR_WEIGHT__MASK                                                         0xFF000000L
//DMA_CLK1_SW0_CL2_CNTL
#define DMA_CLK1_SW0_CL2_CNTL__FLR_ON_RS_RESET_EN__MASK                                                       0x00000001L
#define DMA_CLK1_SW0_CL2_CNTL__LKRST_ON_RS_RESET_EN__MASK                                                     0x00000002L
#define DMA_CLK1_SW0_CL2_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                                   0x00000100L
#define DMA_CLK1_SW0_CL2_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                                0x00001E00L
#define DMA_CLK1_SW0_CL2_CNTL__READ_WRR_WEIGHT__MASK                                                          0x00FF0000L
#define DMA_CLK1_SW0_CL2_CNTL__WRITE_WRR_WEIGHT__MASK                                                         0xFF000000L
//DMA_CLK1_SW0_CL3_CNTL
#define DMA_CLK1_SW0_CL3_CNTL__FLR_ON_RS_RESET_EN__MASK                                                       0x00000001L
#define DMA_CLK1_SW0_CL3_CNTL__LKRST_ON_RS_RESET_EN__MASK                                                     0x00000002L
#define DMA_CLK1_SW0_CL3_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                                   0x00000100L
#define DMA_CLK1_SW0_CL3_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                                0x00001E00L
#define DMA_CLK1_SW0_CL3_CNTL__READ_WRR_WEIGHT__MASK                                                          0x00FF0000L
#define DMA_CLK1_SW0_CL3_CNTL__WRITE_WRR_WEIGHT__MASK                                                         0xFF000000L
//DMA_CLK1_SW0_CL4_CNTL
#define DMA_CLK1_SW0_CL4_CNTL__FLR_ON_RS_RESET_EN__MASK                                                       0x00000001L
#define DMA_CLK1_SW0_CL4_CNTL__LKRST_ON_RS_RESET_EN__MASK                                                     0x00000002L
#define DMA_CLK1_SW0_CL4_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                                   0x00000100L
#define DMA_CLK1_SW0_CL4_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                                0x00001E00L
#define DMA_CLK1_SW0_CL4_CNTL__READ_WRR_WEIGHT__MASK                                                          0x00FF0000L
#define DMA_CLK1_SW0_CL4_CNTL__WRITE_WRR_WEIGHT__MASK                                                         0xFF000000L
//DMA_CLK1_SW1_CL0_CNTL
#define DMA_CLK1_SW1_CL0_CNTL__FLR_ON_RS_RESET_EN__MASK                                                       0x00000001L
#define DMA_CLK1_SW1_CL0_CNTL__LKRST_ON_RS_RESET_EN__MASK                                                     0x00000002L
#define DMA_CLK1_SW1_CL0_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                                   0x00000100L
#define DMA_CLK1_SW1_CL0_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                                0x00001E00L
#define DMA_CLK1_SW1_CL0_CNTL__READ_WRR_WEIGHT__MASK                                                          0x00FF0000L
#define DMA_CLK1_SW1_CL0_CNTL__WRITE_WRR_WEIGHT__MASK                                                         0xFF000000L
//DMA_CLK1_SW1_CL1_CNTL
#define DMA_CLK1_SW1_CL1_CNTL__FLR_ON_RS_RESET_EN__MASK                                                       0x00000001L
#define DMA_CLK1_SW1_CL1_CNTL__LKRST_ON_RS_RESET_EN__MASK                                                     0x00000002L
#define DMA_CLK1_SW1_CL1_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                                   0x00000100L
#define DMA_CLK1_SW1_CL1_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                                0x00001E00L
#define DMA_CLK1_SW1_CL1_CNTL__READ_WRR_WEIGHT__MASK                                                          0x00FF0000L
#define DMA_CLK1_SW1_CL1_CNTL__WRITE_WRR_WEIGHT__MASK                                                         0xFF000000L
//DMA_CLK1_SW1_CL2_CNTL
#define DMA_CLK1_SW1_CL2_CNTL__FLR_ON_RS_RESET_EN__MASK                                                       0x00000001L
#define DMA_CLK1_SW1_CL2_CNTL__LKRST_ON_RS_RESET_EN__MASK                                                     0x00000002L
#define DMA_CLK1_SW1_CL2_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                                   0x00000100L
#define DMA_CLK1_SW1_CL2_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                                0x00001E00L
#define DMA_CLK1_SW1_CL2_CNTL__READ_WRR_WEIGHT__MASK                                                          0x00FF0000L
#define DMA_CLK1_SW1_CL2_CNTL__WRITE_WRR_WEIGHT__MASK                                                         0xFF000000L
//DMA_CLK1_SW1_CL3_CNTL
#define DMA_CLK1_SW1_CL3_CNTL__FLR_ON_RS_RESET_EN__MASK                                                       0x00000001L
#define DMA_CLK1_SW1_CL3_CNTL__LKRST_ON_RS_RESET_EN__MASK                                                     0x00000002L
#define DMA_CLK1_SW1_CL3_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                                   0x00000100L
#define DMA_CLK1_SW1_CL3_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                                0x00001E00L
#define DMA_CLK1_SW1_CL3_CNTL__READ_WRR_WEIGHT__MASK                                                          0x00FF0000L
#define DMA_CLK1_SW1_CL3_CNTL__WRITE_WRR_WEIGHT__MASK                                                         0xFF000000L
//DMA_CLK1_SW1_CL4_CNTL
#define DMA_CLK1_SW1_CL4_CNTL__FLR_ON_RS_RESET_EN__MASK                                                       0x00000001L
#define DMA_CLK1_SW1_CL4_CNTL__LKRST_ON_RS_RESET_EN__MASK                                                     0x00000002L
#define DMA_CLK1_SW1_CL4_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                                   0x00000100L
#define DMA_CLK1_SW1_CL4_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                                0x00001E00L
#define DMA_CLK1_SW1_CL4_CNTL__READ_WRR_WEIGHT__MASK                                                          0x00FF0000L
#define DMA_CLK1_SW1_CL4_CNTL__WRITE_WRR_WEIGHT__MASK                                                         0xFF000000L


// addressBlock: gdc_ras_gdc_ras_regblk
//GDC_RAS_LEAF0_CTRL
#define GDC_RAS_LEAF0_CTRL__POISON_DET_EN__MASK                                                               0x00000001L
#define GDC_RAS_LEAF0_CTRL__POISON_ERREVENT_EN__MASK                                                          0x00000002L
#define GDC_RAS_LEAF0_CTRL__POISON_STALL_EN__MASK                                                             0x00000004L
#define GDC_RAS_LEAF0_CTRL__PARITY_DET_EN__MASK                                                               0x00000010L
#define GDC_RAS_LEAF0_CTRL__PARITY_ERREVENT_EN__MASK                                                          0x00000020L
#define GDC_RAS_LEAF0_CTRL__PARITY_STALL_EN__MASK                                                             0x00000040L
#define GDC_RAS_LEAF0_CTRL__ERR_EVENT_RECV__MASK                                                              0x00010000L
#define GDC_RAS_LEAF0_CTRL__LINK_DIS_RECV__MASK                                                               0x00020000L
#define GDC_RAS_LEAF0_CTRL__POISON_ERR_DET__MASK                                                              0x00040000L
#define GDC_RAS_LEAF0_CTRL__PARITY_ERR_DET__MASK                                                              0x00080000L
#define GDC_RAS_LEAF0_CTRL__ERR_EVENT_SENT__MASK                                                              0x00100000L
#define GDC_RAS_LEAF0_CTRL__EGRESS_STALLED__MASK                                                              0x00200000L
//GDC_RAS_LEAF1_CTRL
#define GDC_RAS_LEAF1_CTRL__POISON_DET_EN__MASK                                                               0x00000001L
#define GDC_RAS_LEAF1_CTRL__POISON_ERREVENT_EN__MASK                                                          0x00000002L
#define GDC_RAS_LEAF1_CTRL__POISON_STALL_EN__MASK                                                             0x00000004L
#define GDC_RAS_LEAF1_CTRL__PARITY_DET_EN__MASK                                                               0x00000010L
#define GDC_RAS_LEAF1_CTRL__PARITY_ERREVENT_EN__MASK                                                          0x00000020L
#define GDC_RAS_LEAF1_CTRL__PARITY_STALL_EN__MASK                                                             0x00000040L
#define GDC_RAS_LEAF1_CTRL__ERR_EVENT_RECV__MASK                                                              0x00010000L
#define GDC_RAS_LEAF1_CTRL__LINK_DIS_RECV__MASK                                                               0x00020000L
#define GDC_RAS_LEAF1_CTRL__POISON_ERR_DET__MASK                                                              0x00040000L
#define GDC_RAS_LEAF1_CTRL__PARITY_ERR_DET__MASK                                                              0x00080000L
#define GDC_RAS_LEAF1_CTRL__ERR_EVENT_SENT__MASK                                                              0x00100000L
#define GDC_RAS_LEAF1_CTRL__EGRESS_STALLED__MASK                                                              0x00200000L
//GDC_RAS_LEAF2_CTRL
#define GDC_RAS_LEAF2_CTRL__POISON_DET_EN__MASK                                                               0x00000001L
#define GDC_RAS_LEAF2_CTRL__POISON_ERREVENT_EN__MASK                                                          0x00000002L
#define GDC_RAS_LEAF2_CTRL__POISON_STALL_EN__MASK                                                             0x00000004L
#define GDC_RAS_LEAF2_CTRL__PARITY_DET_EN__MASK                                                               0x00000010L
#define GDC_RAS_LEAF2_CTRL__PARITY_ERREVENT_EN__MASK                                                          0x00000020L
#define GDC_RAS_LEAF2_CTRL__PARITY_STALL_EN__MASK                                                             0x00000040L
#define GDC_RAS_LEAF2_CTRL__ERR_EVENT_RECV__MASK                                                              0x00010000L
#define GDC_RAS_LEAF2_CTRL__LINK_DIS_RECV__MASK                                                               0x00020000L
#define GDC_RAS_LEAF2_CTRL__POISON_ERR_DET__MASK                                                              0x00040000L
#define GDC_RAS_LEAF2_CTRL__PARITY_ERR_DET__MASK                                                              0x00080000L
#define GDC_RAS_LEAF2_CTRL__ERR_EVENT_SENT__MASK                                                              0x00100000L
#define GDC_RAS_LEAF2_CTRL__EGRESS_STALLED__MASK                                                              0x00200000L
//GDC_RAS_LEAF3_CTRL
#define GDC_RAS_LEAF3_CTRL__POISON_DET_EN__MASK                                                               0x00000001L
#define GDC_RAS_LEAF3_CTRL__POISON_ERREVENT_EN__MASK                                                          0x00000002L
#define GDC_RAS_LEAF3_CTRL__POISON_STALL_EN__MASK                                                             0x00000004L
#define GDC_RAS_LEAF3_CTRL__PARITY_DET_EN__MASK                                                               0x00000010L
#define GDC_RAS_LEAF3_CTRL__PARITY_ERREVENT_EN__MASK                                                          0x00000020L
#define GDC_RAS_LEAF3_CTRL__PARITY_STALL_EN__MASK                                                             0x00000040L
#define GDC_RAS_LEAF3_CTRL__ERR_EVENT_RECV__MASK                                                              0x00010000L
#define GDC_RAS_LEAF3_CTRL__LINK_DIS_RECV__MASK                                                               0x00020000L
#define GDC_RAS_LEAF3_CTRL__POISON_ERR_DET__MASK                                                              0x00040000L
#define GDC_RAS_LEAF3_CTRL__PARITY_ERR_DET__MASK                                                              0x00080000L
#define GDC_RAS_LEAF3_CTRL__ERR_EVENT_SENT__MASK                                                              0x00100000L
#define GDC_RAS_LEAF3_CTRL__EGRESS_STALLED__MASK                                                              0x00200000L
//GDC_RAS_LEAF4_CTRL
#define GDC_RAS_LEAF4_CTRL__POISON_DET_EN__MASK                                                               0x00000001L
#define GDC_RAS_LEAF4_CTRL__POISON_ERREVENT_EN__MASK                                                          0x00000002L
#define GDC_RAS_LEAF4_CTRL__POISON_STALL_EN__MASK                                                             0x00000004L
#define GDC_RAS_LEAF4_CTRL__PARITY_DET_EN__MASK                                                               0x00000010L
#define GDC_RAS_LEAF4_CTRL__PARITY_ERREVENT_EN__MASK                                                          0x00000020L
#define GDC_RAS_LEAF4_CTRL__PARITY_STALL_EN__MASK                                                             0x00000040L
#define GDC_RAS_LEAF4_CTRL__ERR_EVENT_RECV__MASK                                                              0x00010000L
#define GDC_RAS_LEAF4_CTRL__LINK_DIS_RECV__MASK                                                               0x00020000L
#define GDC_RAS_LEAF4_CTRL__POISON_ERR_DET__MASK                                                              0x00040000L
#define GDC_RAS_LEAF4_CTRL__PARITY_ERR_DET__MASK                                                              0x00080000L
#define GDC_RAS_LEAF4_CTRL__ERR_EVENT_SENT__MASK                                                              0x00100000L
#define GDC_RAS_LEAF4_CTRL__EGRESS_STALLED__MASK                                                              0x00200000L
//GDC_RAS_LEAF5_CTRL
#define GDC_RAS_LEAF5_CTRL__POISON_DET_EN__MASK                                                               0x00000001L
#define GDC_RAS_LEAF5_CTRL__POISON_ERREVENT_EN__MASK                                                          0x00000002L
#define GDC_RAS_LEAF5_CTRL__POISON_STALL_EN__MASK                                                             0x00000004L
#define GDC_RAS_LEAF5_CTRL__PARITY_DET_EN__MASK                                                               0x00000010L
#define GDC_RAS_LEAF5_CTRL__PARITY_ERREVENT_EN__MASK                                                          0x00000020L
#define GDC_RAS_LEAF5_CTRL__PARITY_STALL_EN__MASK                                                             0x00000040L
#define GDC_RAS_LEAF5_CTRL__ERR_EVENT_RECV__MASK                                                              0x00010000L
#define GDC_RAS_LEAF5_CTRL__LINK_DIS_RECV__MASK                                                               0x00020000L
#define GDC_RAS_LEAF5_CTRL__POISON_ERR_DET__MASK                                                              0x00040000L
#define GDC_RAS_LEAF5_CTRL__PARITY_ERR_DET__MASK                                                              0x00080000L
#define GDC_RAS_LEAF5_CTRL__ERR_EVENT_SENT__MASK                                                              0x00100000L
#define GDC_RAS_LEAF5_CTRL__EGRESS_STALLED__MASK                                                              0x00200000L


// addressBlock: gdc_rst_GDCRST_DEC
//SHUB_PF_FLR_RST
#define SHUB_PF_FLR_RST__PF0_FLR_RST__MASK                                                                    0x00000001L
#define SHUB_PF_FLR_RST__PF1_FLR_RST__MASK                                                                    0x00000002L
#define SHUB_PF_FLR_RST__PF2_FLR_RST__MASK                                                                    0x00000004L
#define SHUB_PF_FLR_RST__PF3_FLR_RST__MASK                                                                    0x00000008L
#define SHUB_PF_FLR_RST__PF4_FLR_RST__MASK                                                                    0x00000010L
#define SHUB_PF_FLR_RST__PF5_FLR_RST__MASK                                                                    0x00000020L
#define SHUB_PF_FLR_RST__PF6_FLR_RST__MASK                                                                    0x00000040L
#define SHUB_PF_FLR_RST__PF7_FLR_RST__MASK                                                                    0x00000080L
//SHUB_GFX_DRV_MODE1_RST
#define SHUB_GFX_DRV_MODE1_RST__GFX_DRV_MODE1_RST__MASK                                                       0x00000001L
//SHUB_LINK_RESET
#define SHUB_LINK_RESET__LINK_RESET__MASK                                                                     0x00000001L
//SHUB_PF0_VF_FLR_RST
#define SHUB_PF0_VF_FLR_RST__PF0_VF0_FLR_RST__MASK                                                            0x00000001L
#define SHUB_PF0_VF_FLR_RST__PF0_VF1_FLR_RST__MASK                                                            0x00000002L
#define SHUB_PF0_VF_FLR_RST__PF0_VF2_FLR_RST__MASK                                                            0x00000004L
#define SHUB_PF0_VF_FLR_RST__PF0_VF3_FLR_RST__MASK                                                            0x00000008L
#define SHUB_PF0_VF_FLR_RST__PF0_VF4_FLR_RST__MASK                                                            0x00000010L
#define SHUB_PF0_VF_FLR_RST__PF0_VF5_FLR_RST__MASK                                                            0x00000020L
#define SHUB_PF0_VF_FLR_RST__PF0_VF6_FLR_RST__MASK                                                            0x00000040L
#define SHUB_PF0_VF_FLR_RST__PF0_VF7_FLR_RST__MASK                                                            0x00000080L
#define SHUB_PF0_VF_FLR_RST__PF0_VF8_FLR_RST__MASK                                                            0x00000100L
#define SHUB_PF0_VF_FLR_RST__PF0_VF9_FLR_RST__MASK                                                            0x00000200L
#define SHUB_PF0_VF_FLR_RST__PF0_VF10_FLR_RST__MASK                                                           0x00000400L
#define SHUB_PF0_VF_FLR_RST__PF0_VF11_FLR_RST__MASK                                                           0x00000800L
#define SHUB_PF0_VF_FLR_RST__PF0_VF12_FLR_RST__MASK                                                           0x00001000L
#define SHUB_PF0_VF_FLR_RST__PF0_VF13_FLR_RST__MASK                                                           0x00002000L
#define SHUB_PF0_VF_FLR_RST__PF0_VF14_FLR_RST__MASK                                                           0x00004000L
#define SHUB_PF0_VF_FLR_RST__PF0_VF15_FLR_RST__MASK                                                           0x00008000L
#define SHUB_PF0_VF_FLR_RST__PF0_SOFTPF_FLR_RST__MASK                                                         0x80000000L
//SHUB_HARD_RST_CTRL
#define SHUB_HARD_RST_CTRL__COR_RESET_EN__MASK                                                                0x00000001L
#define SHUB_HARD_RST_CTRL__REG_RESET_EN__MASK                                                                0x00000002L
#define SHUB_HARD_RST_CTRL__STY_RESET_EN__MASK                                                                0x00000004L
#define SHUB_HARD_RST_CTRL__NIC400_RESET_EN__MASK                                                             0x00000008L
#define SHUB_HARD_RST_CTRL__SDP_PORT_RESET_EN__MASK                                                           0x00000010L
//SHUB_SOFT_RST_CTRL
#define SHUB_SOFT_RST_CTRL__COR_RESET_EN__MASK                                                                0x00000001L
#define SHUB_SOFT_RST_CTRL__REG_RESET_EN__MASK                                                                0x00000002L
#define SHUB_SOFT_RST_CTRL__STY_RESET_EN__MASK                                                                0x00000004L
#define SHUB_SOFT_RST_CTRL__NIC400_RESET_EN__MASK                                                             0x00000008L
#define SHUB_SOFT_RST_CTRL__SDP_PORT_RESET_EN__MASK                                                           0x00000010L
//SHUB_SDP_PORT_RST
#define SHUB_SDP_PORT_RST__SDP_PORT_RST__MASK                                                                 0x00000001L


// addressBlock: bif_bx_pf_SYSDEC
//SBIOS_SCRATCH_0
#define SBIOS_SCRATCH_0__SBIOS_SCRATCH_DW__MASK                                                               0xFFFFFFFFL
//SBIOS_SCRATCH_1
#define SBIOS_SCRATCH_1__SBIOS_SCRATCH_DW__MASK                                                               0xFFFFFFFFL
//SBIOS_SCRATCH_2
#define SBIOS_SCRATCH_2__SBIOS_SCRATCH_DW__MASK                                                               0xFFFFFFFFL
//SBIOS_SCRATCH_3
#define SBIOS_SCRATCH_3__SBIOS_SCRATCH_DW__MASK                                                               0xFFFFFFFFL
//BIOS_SCRATCH_0
#define BIOS_SCRATCH_0__BIOS_SCRATCH_0__MASK                                                                  0xFFFFFFFFL
//BIOS_SCRATCH_1
#define BIOS_SCRATCH_1__BIOS_SCRATCH_1__MASK                                                                  0xFFFFFFFFL
//BIOS_SCRATCH_2
#define BIOS_SCRATCH_2__BIOS_SCRATCH_2__MASK                                                                  0xFFFFFFFFL
//BIOS_SCRATCH_3
#define BIOS_SCRATCH_3__BIOS_SCRATCH_3__MASK                                                                  0xFFFFFFFFL
//BIOS_SCRATCH_4
#define BIOS_SCRATCH_4__BIOS_SCRATCH_4__MASK                                                                  0xFFFFFFFFL
//BIOS_SCRATCH_5
#define BIOS_SCRATCH_5__BIOS_SCRATCH_5__MASK                                                                  0xFFFFFFFFL
//BIOS_SCRATCH_6
#define BIOS_SCRATCH_6__BIOS_SCRATCH_6__MASK                                                                  0xFFFFFFFFL
//BIOS_SCRATCH_7
#define BIOS_SCRATCH_7__BIOS_SCRATCH_7__MASK                                                                  0xFFFFFFFFL
//BIOS_SCRATCH_8
#define BIOS_SCRATCH_8__BIOS_SCRATCH_8__MASK                                                                  0xFFFFFFFFL
//BIOS_SCRATCH_9
#define BIOS_SCRATCH_9__BIOS_SCRATCH_9__MASK                                                                  0xFFFFFFFFL
//BIOS_SCRATCH_10
#define BIOS_SCRATCH_10__BIOS_SCRATCH_10__MASK                                                                0xFFFFFFFFL
//BIOS_SCRATCH_11
#define BIOS_SCRATCH_11__BIOS_SCRATCH_11__MASK                                                                0xFFFFFFFFL
//BIOS_SCRATCH_12
#define BIOS_SCRATCH_12__BIOS_SCRATCH_12__MASK                                                                0xFFFFFFFFL
//BIOS_SCRATCH_13
#define BIOS_SCRATCH_13__BIOS_SCRATCH_13__MASK                                                                0xFFFFFFFFL
//BIOS_SCRATCH_14
#define BIOS_SCRATCH_14__BIOS_SCRATCH_14__MASK                                                                0xFFFFFFFFL
//BIOS_SCRATCH_15
#define BIOS_SCRATCH_15__BIOS_SCRATCH_15__MASK                                                                0xFFFFFFFFL
//BIF_RLC_INTR_CNTL
#define BIF_RLC_INTR_CNTL__RLC_CMD_COMPLETE__MASK                                                             0x00000001L
#define BIF_RLC_INTR_CNTL__RLC_HANG_SELF_RECOVERED__MASK                                                      0x00000002L
#define BIF_RLC_INTR_CNTL__RLC_HANG_NEED_FLR__MASK                                                            0x00000004L
#define BIF_RLC_INTR_CNTL__RLC_VM_BUSY_TRANSITION__MASK                                                       0x00000008L
//BIF_VCE_INTR_CNTL
#define BIF_VCE_INTR_CNTL__VCE_CMD_COMPLETE__MASK                                                             0x00000001L
#define BIF_VCE_INTR_CNTL__VCE_HANG_SELF_RECOVERED__MASK                                                      0x00000002L
#define BIF_VCE_INTR_CNTL__VCE_HANG_NEED_FLR__MASK                                                            0x00000004L
#define BIF_VCE_INTR_CNTL__VCE_VM_BUSY_TRANSITION__MASK                                                       0x00000008L
//BIF_UVD_INTR_CNTL
#define BIF_UVD_INTR_CNTL__UVD_CMD_COMPLETE__MASK                                                             0x00000001L
#define BIF_UVD_INTR_CNTL__UVD_HANG_SELF_RECOVERED__MASK                                                      0x00000002L
#define BIF_UVD_INTR_CNTL__UVD_HANG_NEED_FLR__MASK                                                            0x00000004L
#define BIF_UVD_INTR_CNTL__UVD_VM_BUSY_TRANSITION__MASK                                                       0x00000008L
//GFX_MMIOREG_CAM_ADDR0
#define GFX_MMIOREG_CAM_ADDR0__CAM_ADDR0__MASK                                                                0x000FFFFFL
//GFX_MMIOREG_CAM_REMAP_ADDR0
#define GFX_MMIOREG_CAM_REMAP_ADDR0__CAM_REMAP_ADDR0__MASK                                                    0x000FFFFFL
//GFX_MMIOREG_CAM_ADDR1
#define GFX_MMIOREG_CAM_ADDR1__CAM_ADDR1__MASK                                                                0x000FFFFFL
//GFX_MMIOREG_CAM_REMAP_ADDR1
#define GFX_MMIOREG_CAM_REMAP_ADDR1__CAM_REMAP_ADDR1__MASK                                                    0x000FFFFFL
//GFX_MMIOREG_CAM_ADDR2
#define GFX_MMIOREG_CAM_ADDR2__CAM_ADDR2__MASK                                                                0x000FFFFFL
//GFX_MMIOREG_CAM_REMAP_ADDR2
#define GFX_MMIOREG_CAM_REMAP_ADDR2__CAM_REMAP_ADDR2__MASK                                                    0x000FFFFFL
//GFX_MMIOREG_CAM_ADDR3
#define GFX_MMIOREG_CAM_ADDR3__CAM_ADDR3__MASK                                                                0x000FFFFFL
//GFX_MMIOREG_CAM_REMAP_ADDR3
#define GFX_MMIOREG_CAM_REMAP_ADDR3__CAM_REMAP_ADDR3__MASK                                                    0x000FFFFFL
//GFX_MMIOREG_CAM_ADDR4
#define GFX_MMIOREG_CAM_ADDR4__CAM_ADDR4__MASK                                                                0x000FFFFFL
//GFX_MMIOREG_CAM_REMAP_ADDR4
#define GFX_MMIOREG_CAM_REMAP_ADDR4__CAM_REMAP_ADDR4__MASK                                                    0x000FFFFFL
//GFX_MMIOREG_CAM_ADDR5
#define GFX_MMIOREG_CAM_ADDR5__CAM_ADDR5__MASK                                                                0x000FFFFFL
//GFX_MMIOREG_CAM_REMAP_ADDR5
#define GFX_MMIOREG_CAM_REMAP_ADDR5__CAM_REMAP_ADDR5__MASK                                                    0x000FFFFFL
//GFX_MMIOREG_CAM_ADDR6
#define GFX_MMIOREG_CAM_ADDR6__CAM_ADDR6__MASK                                                                0x000FFFFFL
//GFX_MMIOREG_CAM_REMAP_ADDR6
#define GFX_MMIOREG_CAM_REMAP_ADDR6__CAM_REMAP_ADDR6__MASK                                                    0x000FFFFFL
//GFX_MMIOREG_CAM_ADDR7
#define GFX_MMIOREG_CAM_ADDR7__CAM_ADDR7__MASK                                                                0x000FFFFFL
//GFX_MMIOREG_CAM_REMAP_ADDR7
#define GFX_MMIOREG_CAM_REMAP_ADDR7__CAM_REMAP_ADDR7__MASK                                                    0x000FFFFFL
//GFX_MMIOREG_CAM_CNTL
#define GFX_MMIOREG_CAM_CNTL__CAM_ENABLE__MASK                                                                0x000000FFL
//GFX_MMIOREG_CAM_ZERO_CPL
#define GFX_MMIOREG_CAM_ZERO_CPL__CAM_ZERO_CPL__MASK                                                          0xFFFFFFFFL
//GFX_MMIOREG_CAM_ONE_CPL
#define GFX_MMIOREG_CAM_ONE_CPL__CAM_ONE_CPL__MASK                                                            0xFFFFFFFFL
//GFX_MMIOREG_CAM_PROGRAMMABLE_CPL
#define GFX_MMIOREG_CAM_PROGRAMMABLE_CPL__CAM_PROGRAMMABLE_CPL__MASK                                          0xFFFFFFFFL


// addressBlock: bif_bx_pf_SYSPFVFDEC
//MM_INDEX
#define MM_INDEX__MM_OFFSET__MASK                                                                             0x7FFFFFFFL
#define MM_INDEX__MM_APER__MASK                                                                               0x80000000L
//MM_DATA
#define MM_DATA__MM_DATA__MASK                                                                                0xFFFFFFFFL
//MM_INDEX_HI
#define MM_INDEX_HI__MM_OFFSET_HI__MASK                                                                       0xFFFFFFFFL
//SYSHUB_INDEX_OVLP
#define SYSHUB_INDEX_OVLP__SYSHUB_OFFSET__MASK                                                                0x003FFFFFL
//SYSHUB_DATA_OVLP
#define SYSHUB_DATA_OVLP__SYSHUB_DATA__MASK                                                                   0xFFFFFFFFL
//PCIE_INDEX
#define PCIE_INDEX__PCIE_INDEX__MASK                                                                          0xFFFFFFFFL
//PCIE_DATA
#define PCIE_DATA__PCIE_DATA__MASK                                                                            0xFFFFFFFFL
//PCIE_INDEX2
#define PCIE_INDEX2__PCIE_INDEX2__MASK                                                                        0xFFFFFFFFL
//PCIE_DATA2
#define PCIE_DATA2__PCIE_DATA2__MASK                                                                          0xFFFFFFFFL


// addressBlock: rcc_dwn_BIFDEC1
//DN_PCIE_RESERVED
#define DN_PCIE_RESERVED__PCIE_RESERVED__MASK                                                                 0xFFFFFFFFL
//DN_PCIE_SCRATCH
#define DN_PCIE_SCRATCH__PCIE_SCRATCH__MASK                                                                   0xFFFFFFFFL
//DN_PCIE_CNTL
#define DN_PCIE_CNTL__HWINIT_WR_LOCK__MASK                                                                    0x00000001L
#define DN_PCIE_CNTL__UR_ERR_REPORT_DIS_DN__MASK                                                              0x00000080L
#define DN_PCIE_CNTL__RX_IGNORE_LTR_MSG_UR__MASK                                                              0x40000000L
//DN_PCIE_CONFIG_CNTL
#define DN_PCIE_CONFIG_CNTL__CI_EXTENDED_TAG_EN_OVERRIDE__MASK                                                0x06000000L
//DN_PCIE_RX_CNTL2
#define DN_PCIE_RX_CNTL2__FLR_EXTEND_MODE__MASK                                                               0x70000000L
//DN_PCIE_BUS_CNTL
#define DN_PCIE_BUS_CNTL__IMMEDIATE_PMI_DIS__MASK                                                             0x00000080L
#define DN_PCIE_BUS_CNTL__AER_CPL_TIMEOUT_RO_DIS_SWDN__MASK                                                   0x00000100L
//DN_PCIE_CFG_CNTL
#define DN_PCIE_CFG_CNTL__CFG_EN_DEC_TO_HIDDEN_REG__MASK                                                      0x00000001L
#define DN_PCIE_CFG_CNTL__CFG_EN_DEC_TO_GEN2_HIDDEN_REG__MASK                                                 0x00000002L
#define DN_PCIE_CFG_CNTL__CFG_EN_DEC_TO_GEN3_HIDDEN_REG__MASK                                                 0x00000004L
//DN_PCIE_STRAP_F0
#define DN_PCIE_STRAP_F0__STRAP_F0_EN__MASK                                                                   0x00000001L
#define DN_PCIE_STRAP_F0__STRAP_F0_MC_EN__MASK                                                                0x00020000L
#define DN_PCIE_STRAP_F0__STRAP_F0_MSI_MULTI_CAP__MASK                                                        0x00E00000L
//DN_PCIE_STRAP_MISC
#define DN_PCIE_STRAP_MISC__STRAP_CLK_PM_EN__MASK                                                             0x01000000L
#define DN_PCIE_STRAP_MISC__STRAP_MST_ADR64_EN__MASK                                                          0x20000000L
//DN_PCIE_STRAP_MISC2
#define DN_PCIE_STRAP_MISC2__STRAP_MSTCPL_TIMEOUT_EN__MASK                                                    0x00000004L


// addressBlock: rcc_dwnp_BIFDEC1
//PCIEP_RESERVED
#define PCIEP_RESERVED__PCIEP_RESERVED__MASK                                                                  0xFFFFFFFFL
//PCIEP_SCRATCH
#define PCIEP_SCRATCH__PCIEP_SCRATCH__MASK                                                                    0xFFFFFFFFL
//PCIE_ERR_CNTL
#define PCIE_ERR_CNTL__ERR_REPORTING_DIS__MASK                                                                0x00000001L
#define PCIE_ERR_CNTL__AER_HDR_LOG_TIMEOUT__MASK                                                              0x00000700L
#define PCIE_ERR_CNTL__AER_HDR_LOG_F0_TIMER_EXPIRED__MASK                                                     0x00000800L
#define PCIE_ERR_CNTL__SEND_ERR_MSG_IMMEDIATELY__MASK                                                         0x00020000L
//PCIE_RX_CNTL
#define PCIE_RX_CNTL__RX_IGNORE_MAX_PAYLOAD_ERR__MASK                                                         0x00000100L
#define PCIE_RX_CNTL__RX_IGNORE_TC_ERR_DN__MASK                                                               0x00000200L
#define PCIE_RX_CNTL__RX_PCIE_CPL_TIMEOUT_DIS__MASK                                                           0x00100000L
#define PCIE_RX_CNTL__RX_IGNORE_SHORTPREFIX_ERR_DN__MASK                                                      0x00200000L
#define PCIE_RX_CNTL__RX_RCB_FLR_TIMEOUT_DIS__MASK                                                            0x08000000L
//PCIE_LC_SPEED_CNTL
#define PCIE_LC_SPEED_CNTL__LC_GEN2_EN_STRAP__MASK                                                            0x00000001L
#define PCIE_LC_SPEED_CNTL__LC_GEN3_EN_STRAP__MASK                                                            0x00000002L
//PCIE_LC_CNTL2
#define PCIE_LC_CNTL2__LC_LINK_BW_NOTIFICATION_DIS__MASK                                                      0x08000000L
//PCIEP_STRAP_MISC
#define PCIEP_STRAP_MISC__STRAP_MULTI_FUNC_EN__MASK                                                           0x00000400L
//LTR_MSG_INFO_FROM_EP
#define LTR_MSG_INFO_FROM_EP__LTR_MSG_INFO_FROM_EP__MASK                                                      0xFFFFFFFFL


// addressBlock: rcc_ep_BIFDEC1
//EP_PCIE_SCRATCH
#define EP_PCIE_SCRATCH__PCIE_SCRATCH__MASK                                                                   0xFFFFFFFFL
//EP_PCIE_CNTL
#define EP_PCIE_CNTL__UR_ERR_REPORT_DIS__MASK                                                                 0x00000080L
#define EP_PCIE_CNTL__PCIE_MALFORM_ATOMIC_OPS__MASK                                                           0x00000100L
#define EP_PCIE_CNTL__RX_IGNORE_LTR_MSG_UR__MASK                                                              0x40000000L
//EP_PCIE_INT_CNTL
#define EP_PCIE_INT_CNTL__CORR_ERR_INT_EN__MASK                                                               0x00000001L
#define EP_PCIE_INT_CNTL__NON_FATAL_ERR_INT_EN__MASK                                                          0x00000002L
#define EP_PCIE_INT_CNTL__FATAL_ERR_INT_EN__MASK                                                              0x00000004L
#define EP_PCIE_INT_CNTL__USR_DETECTED_INT_EN__MASK                                                           0x00000008L
#define EP_PCIE_INT_CNTL__MISC_ERR_INT_EN__MASK                                                               0x00000010L
#define EP_PCIE_INT_CNTL__POWER_STATE_CHG_INT_EN__MASK                                                        0x00000040L
//EP_PCIE_INT_STATUS
#define EP_PCIE_INT_STATUS__CORR_ERR_INT_STATUS__MASK                                                         0x00000001L
#define EP_PCIE_INT_STATUS__NON_FATAL_ERR_INT_STATUS__MASK                                                    0x00000002L
#define EP_PCIE_INT_STATUS__FATAL_ERR_INT_STATUS__MASK                                                        0x00000004L
#define EP_PCIE_INT_STATUS__USR_DETECTED_INT_STATUS__MASK                                                     0x00000008L
#define EP_PCIE_INT_STATUS__MISC_ERR_INT_STATUS__MASK                                                         0x00000010L
#define EP_PCIE_INT_STATUS__POWER_STATE_CHG_INT_STATUS__MASK                                                  0x00000040L
//EP_PCIE_RX_CNTL2
#define EP_PCIE_RX_CNTL2__RX_IGNORE_EP_INVALIDPASID_UR__MASK                                                  0x00000001L
//EP_PCIE_BUS_CNTL
#define EP_PCIE_BUS_CNTL__IMMEDIATE_PMI_DIS__MASK                                                             0x00000080L
//EP_PCIE_CFG_CNTL
#define EP_PCIE_CFG_CNTL__CFG_EN_DEC_TO_HIDDEN_REG__MASK                                                      0x00000001L
#define EP_PCIE_CFG_CNTL__CFG_EN_DEC_TO_GEN2_HIDDEN_REG__MASK                                                 0x00000002L
#define EP_PCIE_CFG_CNTL__CFG_EN_DEC_TO_GEN3_HIDDEN_REG__MASK                                                 0x00000004L
//EP_PCIE_OBFF_CNTL
#define EP_PCIE_OBFF_CNTL__TX_OBFF_PRIV_DISABLE__MASK                                                         0x00000001L
#define EP_PCIE_OBFF_CNTL__TX_OBFF_WAKE_SIMPLE_MODE_EN__MASK                                                  0x00000002L
#define EP_PCIE_OBFF_CNTL__TX_OBFF_HOSTMEM_TO_ACTIVE__MASK                                                    0x00000004L
#define EP_PCIE_OBFF_CNTL__TX_OBFF_SLVCPL_TO_ACTIVE__MASK                                                     0x00000008L
#define EP_PCIE_OBFF_CNTL__TX_OBFF_WAKE_MAX_PULSE_WIDTH__MASK                                                 0x000000F0L
#define EP_PCIE_OBFF_CNTL__TX_OBFF_WAKE_MAX_TWO_FALLING_WIDTH__MASK                                           0x00000F00L
#define EP_PCIE_OBFF_CNTL__TX_OBFF_WAKE_SAMPLING_PERIOD__MASK                                                 0x0000F000L
#define EP_PCIE_OBFF_CNTL__TX_OBFF_INTR_TO_ACTIVE__MASK                                                       0x00010000L
#define EP_PCIE_OBFF_CNTL__TX_OBFF_ERR_TO_ACTIVE__MASK                                                        0x00020000L
#define EP_PCIE_OBFF_CNTL__TX_OBFF_ANY_MSG_TO_ACTIVE__MASK                                                    0x00040000L
#define EP_PCIE_OBFF_CNTL__TX_OBFF_ACCEPT_IN_NOND0__MASK                                                      0x00080000L
#define EP_PCIE_OBFF_CNTL__TX_OBFF_PENDING_REQ_TO_ACTIVE__MASK                                                0x00F00000L
//EP_PCIE_TX_LTR_CNTL
#define EP_PCIE_TX_LTR_CNTL__LTR_PRIV_S_SHORT_VALUE__MASK                                                     0x00000007L
#define EP_PCIE_TX_LTR_CNTL__LTR_PRIV_S_LONG_VALUE__MASK                                                      0x00000038L
#define EP_PCIE_TX_LTR_CNTL__LTR_PRIV_S_REQUIREMENT__MASK                                                     0x00000040L
#define EP_PCIE_TX_LTR_CNTL__LTR_PRIV_NS_SHORT_VALUE__MASK                                                    0x00000380L
#define EP_PCIE_TX_LTR_CNTL__LTR_PRIV_NS_LONG_VALUE__MASK                                                     0x00001C00L
#define EP_PCIE_TX_LTR_CNTL__LTR_PRIV_NS_REQUIREMENT__MASK                                                    0x00002000L
#define EP_PCIE_TX_LTR_CNTL__LTR_PRIV_MSG_DIS_IN_PM_NON_D0__MASK                                              0x00004000L
#define EP_PCIE_TX_LTR_CNTL__LTR_PRIV_RST_LTR_IN_DL_DOWN__MASK                                                0x00008000L
#define EP_PCIE_TX_LTR_CNTL__TX_CHK_FC_FOR_L1__MASK                                                           0x00010000L
//EP_PCIE_STRAP_MISC
#define EP_PCIE_STRAP_MISC__STRAP_MST_ADR64_EN__MASK                                                          0x20000000L
//EP_PCIE_STRAP_MISC2
#define EP_PCIE_STRAP_MISC2__STRAP_TPH_SUPPORTED__MASK                                                        0x00000010L
//EP_PCIE_STRAP_PI
//EP_PCIE_F0_DPA_CAP
#define EP_PCIE_F0_DPA_CAP__TRANS_LAT_UNIT__MASK                                                              0x00000300L
#define EP_PCIE_F0_DPA_CAP__PWR_ALLOC_SCALE__MASK                                                             0x00003000L
#define EP_PCIE_F0_DPA_CAP__TRANS_LAT_VAL_0__MASK                                                             0x00FF0000L
#define EP_PCIE_F0_DPA_CAP__TRANS_LAT_VAL_1__MASK                                                             0xFF000000L
//EP_PCIE_F0_DPA_LATENCY_INDICATOR
#define EP_PCIE_F0_DPA_LATENCY_INDICATOR__TRANS_LAT_INDICATOR_BITS__MASK                                      0xFFL
//EP_PCIE_F0_DPA_CNTL
#define EP_PCIE_F0_DPA_CNTL__SUBSTATE_STATUS__MASK                                                            0x001FL
#define EP_PCIE_F0_DPA_CNTL__DPA_COMPLIANCE_MODE__MASK                                                        0x0100L
//PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_0
#define PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_0__SUBSTATE_PWR_ALLOC__MASK                                            0xFFL
//PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_1
#define PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_1__SUBSTATE_PWR_ALLOC__MASK                                            0xFFL
//PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_2
#define PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_2__SUBSTATE_PWR_ALLOC__MASK                                            0xFFL
//PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_3
#define PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_3__SUBSTATE_PWR_ALLOC__MASK                                            0xFFL
//PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_4
#define PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_4__SUBSTATE_PWR_ALLOC__MASK                                            0xFFL
//PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_5
#define PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_5__SUBSTATE_PWR_ALLOC__MASK                                            0xFFL
//PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_6
#define PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_6__SUBSTATE_PWR_ALLOC__MASK                                            0xFFL
//PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_7
#define PCIE_F0_DPA_SUBSTATE_PWR_ALLOC_7__SUBSTATE_PWR_ALLOC__MASK                                            0xFFL
//EP_PCIE_PME_CONTROL
#define EP_PCIE_PME_CONTROL__PME_SERVICE_TIMER__MASK                                                          0x1FL
//EP_PCIEP_RESERVED
#define EP_PCIEP_RESERVED__PCIEP_RESERVED__MASK                                                               0xFFFFFFFFL
//EP_PCIE_TX_CNTL
#define EP_PCIE_TX_CNTL__TX_SNR_OVERRIDE__MASK                                                                0x00000C00L
#define EP_PCIE_TX_CNTL__TX_RO_OVERRIDE__MASK                                                                 0x00003000L
#define EP_PCIE_TX_CNTL__TX_F0_TPH_DIS__MASK                                                                  0x01000000L
#define EP_PCIE_TX_CNTL__TX_F1_TPH_DIS__MASK                                                                  0x02000000L
#define EP_PCIE_TX_CNTL__TX_F2_TPH_DIS__MASK                                                                  0x04000000L
//EP_PCIE_TX_REQUESTER_ID
#define EP_PCIE_TX_REQUESTER_ID__TX_REQUESTER_ID_FUNCTION__MASK                                               0x00000007L
#define EP_PCIE_TX_REQUESTER_ID__TX_REQUESTER_ID_DEVICE__MASK                                                 0x000000F8L
#define EP_PCIE_TX_REQUESTER_ID__TX_REQUESTER_ID_BUS__MASK                                                    0x0000FF00L
//EP_PCIE_ERR_CNTL
#define EP_PCIE_ERR_CNTL__ERR_REPORTING_DIS__MASK                                                             0x00000001L
#define EP_PCIE_ERR_CNTL__AER_HDR_LOG_TIMEOUT__MASK                                                           0x00000700L
#define EP_PCIE_ERR_CNTL__SEND_ERR_MSG_IMMEDIATELY__MASK                                                      0x00020000L
#define EP_PCIE_ERR_CNTL__STRAP_POISONED_ADVISORY_NONFATAL__MASK                                              0x00040000L
#define EP_PCIE_ERR_CNTL__AER_HDR_LOG_F0_TIMER_EXPIRED__MASK                                                  0x01000000L
#define EP_PCIE_ERR_CNTL__AER_HDR_LOG_F1_TIMER_EXPIRED__MASK                                                  0x02000000L
#define EP_PCIE_ERR_CNTL__AER_HDR_LOG_F2_TIMER_EXPIRED__MASK                                                  0x04000000L
#define EP_PCIE_ERR_CNTL__AER_HDR_LOG_F3_TIMER_EXPIRED__MASK                                                  0x08000000L
#define EP_PCIE_ERR_CNTL__AER_HDR_LOG_F4_TIMER_EXPIRED__MASK                                                  0x10000000L
#define EP_PCIE_ERR_CNTL__AER_HDR_LOG_F5_TIMER_EXPIRED__MASK                                                  0x20000000L
#define EP_PCIE_ERR_CNTL__AER_HDR_LOG_F6_TIMER_EXPIRED__MASK                                                  0x40000000L
#define EP_PCIE_ERR_CNTL__AER_HDR_LOG_F7_TIMER_EXPIRED__MASK                                                  0x80000000L
//EP_PCIE_RX_CNTL
#define EP_PCIE_RX_CNTL__RX_IGNORE_MAX_PAYLOAD_ERR__MASK                                                      0x00000100L
#define EP_PCIE_RX_CNTL__RX_IGNORE_TC_ERR__MASK                                                               0x00000200L
#define EP_PCIE_RX_CNTL__RX_PCIE_CPL_TIMEOUT_DIS__MASK                                                        0x00100000L
#define EP_PCIE_RX_CNTL__RX_IGNORE_SHORTPREFIX_ERR__MASK                                                      0x00200000L
#define EP_PCIE_RX_CNTL__RX_IGNORE_MAXPREFIX_ERR__MASK                                                        0x00400000L
#define EP_PCIE_RX_CNTL__RX_IGNORE_INVALIDPASID_ERR__MASK                                                     0x01000000L
#define EP_PCIE_RX_CNTL__RX_IGNORE_NOT_PASID_UR__MASK                                                         0x02000000L
#define EP_PCIE_RX_CNTL__RX_TPH_DIS__MASK                                                                     0x04000000L
//EP_PCIE_LC_SPEED_CNTL
#define EP_PCIE_LC_SPEED_CNTL__LC_GEN2_EN_STRAP__MASK                                                         0x00000001L
#define EP_PCIE_LC_SPEED_CNTL__LC_GEN3_EN_STRAP__MASK                                                         0x00000002L


// addressBlock: bif_bx_pf_BIFDEC1
//BIF_MM_INDACCESS_CNTL
#define BIF_MM_INDACCESS_CNTL__MM_INDACCESS_DIS__MASK                                                         0x00000002L
//BUS_CNTL
#define BUS_CNTL__PMI_INT_DIS_EP__MASK                                                                        0x00000008L
#define BUS_CNTL__PMI_INT_DIS_DN__MASK                                                                        0x00000010L
#define BUS_CNTL__PMI_INT_DIS_SWUS__MASK                                                                      0x00000020L
#define BUS_CNTL__VGA_REG_COHERENCY_DIS__MASK                                                                 0x00000040L
#define BUS_CNTL__VGA_MEM_COHERENCY_DIS__MASK                                                                 0x00000080L
#define BUS_CNTL__SET_AZ_TC__MASK                                                                             0x00001C00L
#define BUS_CNTL__SET_MC_TC__MASK                                                                             0x0000E000L
#define BUS_CNTL__ZERO_BE_WR_EN__MASK                                                                         0x00010000L
#define BUS_CNTL__ZERO_BE_RD_EN__MASK                                                                         0x00020000L
#define BUS_CNTL__RD_STALL_IO_WR__MASK                                                                        0x00040000L
#define BUS_CNTL__DEASRT_INTX_DSTATE_CHK_DIS_EP__MASK                                                         0x00080000L
#define BUS_CNTL__DEASRT_INTX_DSTATE_CHK_DIS_DN__MASK                                                         0x00100000L
#define BUS_CNTL__DEASRT_INTX_DSTATE_CHK_DIS_SWUS__MASK                                                       0x00200000L
#define BUS_CNTL__DEASRT_INTX_IN_NOND0_EN_EP__MASK                                                            0x00400000L
#define BUS_CNTL__DEASRT_INTX_IN_NOND0_EN_DN__MASK                                                            0x00800000L
#define BUS_CNTL__UR_OVRD_FOR_ECRC_EN__MASK                                                                   0x01000000L
//BIF_SCRATCH0
#define BIF_SCRATCH0__BIF_SCRATCH0__MASK                                                                      0xFFFFFFFFL
//BIF_SCRATCH1
#define BIF_SCRATCH1__BIF_SCRATCH1__MASK                                                                      0xFFFFFFFFL
//BX_RESET_EN
#define BX_RESET_EN__COR_RESET_EN__MASK                                                                       0x00000001L
#define BX_RESET_EN__REG_RESET_EN__MASK                                                                       0x00000002L
#define BX_RESET_EN__STY_RESET_EN__MASK                                                                       0x00000004L
#define BX_RESET_EN__FLR_TWICE_EN__MASK                                                                       0x00000100L
#define BX_RESET_EN__RESET_ON_VFENABLE_LOW_EN__MASK                                                           0x00010000L
//MM_CFGREGS_CNTL
#define MM_CFGREGS_CNTL__MM_CFG_FUNC_SEL__MASK                                                                0x00000007L
#define MM_CFGREGS_CNTL__MM_CFG_DEV_SEL__MASK                                                                 0x000000C0L
#define MM_CFGREGS_CNTL__MM_WR_TO_CFG_EN__MASK                                                                0x80000000L
//BX_RESET_CNTL
#define BX_RESET_CNTL__LINK_TRAIN_EN__MASK                                                                    0x00000001L
//INTERRUPT_CNTL
#define INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE__MASK                                                            0x00000001L
#define INTERRUPT_CNTL__IH_DUMMY_RD_EN__MASK                                                                  0x00000002L
#define INTERRUPT_CNTL__IH_REQ_NONSNOOP_EN__MASK                                                              0x00000008L
#define INTERRUPT_CNTL__IH_INTR_DLY_CNTR__MASK                                                                0x000000F0L
#define INTERRUPT_CNTL__GEN_IH_INT_EN__MASK                                                                   0x00000100L
#define INTERRUPT_CNTL__BIF_RB_REQ_NONSNOOP_EN__MASK                                                          0x00008000L
//INTERRUPT_CNTL2
#define INTERRUPT_CNTL2__IH_DUMMY_RD_ADDR__MASK                                                               0xFFFFFFFFL
//CLKREQB_PAD_CNTL
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_A__MASK                                                                 0x00000001L
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_SEL__MASK                                                               0x00000002L
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_MODE__MASK                                                              0x00000004L
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_SPARE__MASK                                                             0x00000018L
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_SN0__MASK                                                               0x00000020L
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_SN1__MASK                                                               0x00000040L
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_SN2__MASK                                                               0x00000080L
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_SN3__MASK                                                               0x00000100L
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_SLEWN__MASK                                                             0x00000200L
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_WAKE__MASK                                                              0x00000400L
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_SCHMEN__MASK                                                            0x00000800L
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_CNTL_EN__MASK                                                           0x00001000L
#define CLKREQB_PAD_CNTL__CLKREQB_PAD_Y__MASK                                                                 0x00002000L
#define CLKREQB_PAD_CNTL__CLKREQB_PERF_COUNTER_UPPER__MASK                                                    0xFF000000L
//CLKREQB_PERF_COUNTER
#define CLKREQB_PERF_COUNTER__CLKREQB_PERF_COUNTER_LOWER__MASK                                                0xFFFFFFFFL
//BIF_CLK_CTRL
#define BIF_CLK_CTRL__BIF_XSTCLK_READY__MASK                                                                  0x00000001L
#define BIF_CLK_CTRL__BACO_XSTCLK_SWITCH_BYPASS__MASK                                                         0x00000002L
//BIF_FEATURES_CONTROL_MISC
#define BIF_FEATURES_CONTROL_MISC__MST_BIF_REQ_EP_DIS__MASK                                                   0x00000001L
#define BIF_FEATURES_CONTROL_MISC__SLV_BIF_CPL_EP_DIS__MASK                                                   0x00000002L
#define BIF_FEATURES_CONTROL_MISC__BIF_SLV_REQ_EP_DIS__MASK                                                   0x00000004L
#define BIF_FEATURES_CONTROL_MISC__BIF_MST_CPL_EP_DIS__MASK                                                   0x00000008L
#define BIF_FEATURES_CONTROL_MISC__MC_BIF_REQ_ID_ROUTING_DIS__MASK                                            0x00000200L
#define BIF_FEATURES_CONTROL_MISC__AZ_BIF_REQ_ID_ROUTING_DIS__MASK                                            0x00000400L
#define BIF_FEATURES_CONTROL_MISC__ATC_PRG_RESP_PASID_UR_EN__MASK                                             0x00000800L
#define BIF_FEATURES_CONTROL_MISC__BIF_RB_SET_OVERFLOW_EN__MASK                                               0x00001000L
#define BIF_FEATURES_CONTROL_MISC__ATOMIC_ERR_INT_DIS__MASK                                                   0x00002000L
#define BIF_FEATURES_CONTROL_MISC__BME_HDL_NONVIR_EN__MASK                                                    0x00008000L
#define BIF_FEATURES_CONTROL_MISC__FLR_MST_PEND_CHK_DIS__MASK                                                 0x00020000L
#define BIF_FEATURES_CONTROL_MISC__FLR_SLV_PEND_CHK_DIS__MASK                                                 0x00040000L
#define BIF_FEATURES_CONTROL_MISC__DOORBELL_SELFRING_GPA_APER_CHK_48BIT_ADDR__MASK                            0x01000000L
//BIF_DOORBELL_CNTL
#define BIF_DOORBELL_CNTL__SELF_RING_DIS__MASK                                                                0x00000001L
#define BIF_DOORBELL_CNTL__TRANS_CHECK_DIS__MASK                                                              0x00000002L
#define BIF_DOORBELL_CNTL__UNTRANS_LBACK_EN__MASK                                                             0x00000004L
#define BIF_DOORBELL_CNTL__NON_CONSECUTIVE_BE_ZERO_DIS__MASK                                                  0x00000008L
#define BIF_DOORBELL_CNTL__DOORBELL_MONITOR_EN__MASK                                                          0x00000010L
#define BIF_DOORBELL_CNTL__DB_MNTR_INTGEN_DIS__MASK                                                           0x01000000L
#define BIF_DOORBELL_CNTL__DB_MNTR_INTGEN_MODE_0__MASK                                                        0x02000000L
#define BIF_DOORBELL_CNTL__DB_MNTR_INTGEN_MODE_1__MASK                                                        0x04000000L
#define BIF_DOORBELL_CNTL__DB_MNTR_INTGEN_MODE_2__MASK                                                        0x08000000L
//BIF_DOORBELL_INT_CNTL
#define BIF_DOORBELL_INT_CNTL__DOORBELL_INTERRUPT_STATUS__MASK                                                0x00000001L
#define BIF_DOORBELL_INT_CNTL__IOHC_RAS_INTERRUPT_STATUS__MASK                                                0x00000002L
#define BIF_DOORBELL_INT_CNTL__DOORBELL_INTERRUPT_CLEAR__MASK                                                 0x00010000L
#define BIF_DOORBELL_INT_CNTL__IOHC_RAS_INTERRUPT_CLEAR__MASK                                                 0x00020000L
//BIF_SLVARB_MODE
#define BIF_SLVARB_MODE__SLVARB_MODE__MASK                                                                    0x00000003L
//BIF_FB_EN
#define BIF_FB_EN__FB_READ_EN__MASK                                                                           0x00000001L
#define BIF_FB_EN__FB_WRITE_EN__MASK                                                                          0x00000002L
//BIF_BUSY_DELAY_CNTR
#define BIF_BUSY_DELAY_CNTR__DELAY_CNT__MASK                                                                  0x0000003FL
//BIF_PERFMON_CNTL
#define BIF_PERFMON_CNTL__PERFCOUNTER_EN__MASK                                                                0x00000001L
#define BIF_PERFMON_CNTL__PERFCOUNTER_RESET0__MASK                                                            0x00000002L
#define BIF_PERFMON_CNTL__PERFCOUNTER_RESET1__MASK                                                            0x00000004L
#define BIF_PERFMON_CNTL__PERF_SEL0__MASK                                                                     0x00001F00L
#define BIF_PERFMON_CNTL__PERF_SEL1__MASK                                                                     0x0003E000L
//BIF_PERFCOUNTER0_RESULT
#define BIF_PERFCOUNTER0_RESULT__PERFCOUNTER_RESULT__MASK                                                     0xFFFFFFFFL
//BIF_PERFCOUNTER1_RESULT
#define BIF_PERFCOUNTER1_RESULT__PERFCOUNTER_RESULT__MASK                                                     0xFFFFFFFFL
//BIF_MST_TRANS_PENDING_VF
#define BIF_MST_TRANS_PENDING_VF__BIF_MST_TRANS_PENDING__MASK                                                 0x0000FFFFL
//BIF_SLV_TRANS_PENDING_VF
#define BIF_SLV_TRANS_PENDING_VF__BIF_SLV_TRANS_PENDING__MASK                                                 0x0000FFFFL
//BACO_CNTL
#define BACO_CNTL__BACO_EN__MASK                                                                              0x00000001L
#define BACO_CNTL__BACO_BIF_LCLK_SWITCH__MASK                                                                 0x00000002L
#define BACO_CNTL__BACO_DUMMY_EN__MASK                                                                        0x00000004L
#define BACO_CNTL__BACO_POWER_OFF__MASK                                                                       0x00000008L
#define BACO_CNTL__BACO_DSTATE_BYPASS__MASK                                                                   0x00000020L
#define BACO_CNTL__BACO_RST_INTR_MASK__MASK                                                                   0x00000040L
#define BACO_CNTL__BACO_MODE__MASK                                                                            0x00000100L
#define BACO_CNTL__RCU_BIF_CONFIG_DONE__MASK                                                                  0x00000200L
#define BACO_CNTL__BACO_AUTO_EXIT__MASK                                                                       0x80000000L
//BIF_BACO_EXIT_TIME0
#define BIF_BACO_EXIT_TIME0__BACO_EXIT_PXEN_CLR_TIMER__MASK                                                   0x000FFFFFL
//BIF_BACO_EXIT_TIMER1
#define BIF_BACO_EXIT_TIMER1__BACO_EXIT_SIDEBAND_TIMER__MASK                                                  0x000FFFFFL
#define BIF_BACO_EXIT_TIMER1__BACO_HW_EXIT_DIS__MASK                                                          0x04000000L
#define BIF_BACO_EXIT_TIMER1__PX_EN_OE_IN_PX_EN_HIGH__MASK                                                    0x08000000L
#define BIF_BACO_EXIT_TIMER1__PX_EN_OE_IN_PX_EN_LOW__MASK                                                     0x10000000L
#define BIF_BACO_EXIT_TIMER1__BACO_MODE_SEL__MASK                                                             0x60000000L
#define BIF_BACO_EXIT_TIMER1__AUTO_BACO_EXIT_CLR_BY_HW_DIS__MASK                                              0x80000000L
//BIF_BACO_EXIT_TIMER2
#define BIF_BACO_EXIT_TIMER2__BACO_EXIT_LCLK_BAK_TIMER__MASK                                                  0x000FFFFFL
//BIF_BACO_EXIT_TIMER3
#define BIF_BACO_EXIT_TIMER3__BACO_EXIT_DUMMY_EN_CLR_TIMER__MASK                                              0x000FFFFFL
//BIF_BACO_EXIT_TIMER4
#define BIF_BACO_EXIT_TIMER4__BACO_EXIT_BACO_EN_CLR_TIMER__MASK                                               0x000FFFFFL
//MEM_TYPE_CNTL
#define MEM_TYPE_CNTL__BF_MEM_PHY_G5_G3__MASK                                                                 0x00000001L
//SMU_BIF_VDDGFX_PWR_STATUS
#define SMU_BIF_VDDGFX_PWR_STATUS__VDDGFX_GFX_PWR_OFF__MASK                                                   0x00000001L
//BIF_VDDGFX_GFX0_LOWER
#define BIF_VDDGFX_GFX0_LOWER__VDDGFX_GFX0_REG_LOWER__MASK                                                    0x0003FFFCL
#define BIF_VDDGFX_GFX0_LOWER__VDDGFX_GFX0_REG_CMP_EN__MASK                                                   0x40000000L
#define BIF_VDDGFX_GFX0_LOWER__VDDGFX_GFX0_REG_STALL_EN__MASK                                                 0x80000000L
//BIF_VDDGFX_GFX0_UPPER
#define BIF_VDDGFX_GFX0_UPPER__VDDGFX_GFX0_REG_UPPER__MASK                                                    0x0003FFFCL
//BIF_VDDGFX_GFX1_LOWER
#define BIF_VDDGFX_GFX1_LOWER__VDDGFX_GFX1_REG_LOWER__MASK                                                    0x0003FFFCL
#define BIF_VDDGFX_GFX1_LOWER__VDDGFX_GFX1_REG_CMP_EN__MASK                                                   0x40000000L
#define BIF_VDDGFX_GFX1_LOWER__VDDGFX_GFX1_REG_STALL_EN__MASK                                                 0x80000000L
//BIF_VDDGFX_GFX1_UPPER
#define BIF_VDDGFX_GFX1_UPPER__VDDGFX_GFX1_REG_UPPER__MASK                                                    0x0003FFFCL
//BIF_VDDGFX_GFX2_LOWER
#define BIF_VDDGFX_GFX2_LOWER__VDDGFX_GFX2_REG_LOWER__MASK                                                    0x0003FFFCL
#define BIF_VDDGFX_GFX2_LOWER__VDDGFX_GFX2_REG_CMP_EN__MASK                                                   0x40000000L
#define BIF_VDDGFX_GFX2_LOWER__VDDGFX_GFX2_REG_STALL_EN__MASK                                                 0x80000000L
//BIF_VDDGFX_GFX2_UPPER
#define BIF_VDDGFX_GFX2_UPPER__VDDGFX_GFX2_REG_UPPER__MASK                                                    0x0003FFFCL
//BIF_VDDGFX_GFX3_LOWER
#define BIF_VDDGFX_GFX3_LOWER__VDDGFX_GFX3_REG_LOWER__MASK                                                    0x0003FFFCL
#define BIF_VDDGFX_GFX3_LOWER__VDDGFX_GFX3_REG_CMP_EN__MASK                                                   0x40000000L
#define BIF_VDDGFX_GFX3_LOWER__VDDGFX_GFX3_REG_STALL_EN__MASK                                                 0x80000000L
//BIF_VDDGFX_GFX3_UPPER
#define BIF_VDDGFX_GFX3_UPPER__VDDGFX_GFX3_REG_UPPER__MASK                                                    0x0003FFFCL
//BIF_VDDGFX_GFX4_LOWER
#define BIF_VDDGFX_GFX4_LOWER__VDDGFX_GFX4_REG_LOWER__MASK                                                    0x0003FFFCL
#define BIF_VDDGFX_GFX4_LOWER__VDDGFX_GFX4_REG_CMP_EN__MASK                                                   0x40000000L
#define BIF_VDDGFX_GFX4_LOWER__VDDGFX_GFX4_REG_STALL_EN__MASK                                                 0x80000000L
//BIF_VDDGFX_GFX4_UPPER
#define BIF_VDDGFX_GFX4_UPPER__VDDGFX_GFX4_REG_UPPER__MASK                                                    0x0003FFFCL
//BIF_VDDGFX_GFX5_LOWER
#define BIF_VDDGFX_GFX5_LOWER__VDDGFX_GFX5_REG_LOWER__MASK                                                    0x0003FFFCL
#define BIF_VDDGFX_GFX5_LOWER__VDDGFX_GFX5_REG_CMP_EN__MASK                                                   0x40000000L
#define BIF_VDDGFX_GFX5_LOWER__VDDGFX_GFX5_REG_STALL_EN__MASK                                                 0x80000000L
//BIF_VDDGFX_GFX5_UPPER
#define BIF_VDDGFX_GFX5_UPPER__VDDGFX_GFX5_REG_UPPER__MASK                                                    0x0003FFFCL
//BIF_VDDGFX_RSV1_LOWER
#define BIF_VDDGFX_RSV1_LOWER__VDDGFX_RSV1_REG_LOWER__MASK                                                    0x0003FFFCL
#define BIF_VDDGFX_RSV1_LOWER__VDDGFX_RSV1_REG_CMP_EN__MASK                                                   0x40000000L
#define BIF_VDDGFX_RSV1_LOWER__VDDGFX_RSV1_REG_STALL_EN__MASK                                                 0x80000000L
//BIF_VDDGFX_RSV1_UPPER
#define BIF_VDDGFX_RSV1_UPPER__VDDGFX_RSV1_REG_UPPER__MASK                                                    0x0003FFFCL
//BIF_VDDGFX_RSV2_LOWER
#define BIF_VDDGFX_RSV2_LOWER__VDDGFX_RSV2_REG_LOWER__MASK                                                    0x0003FFFCL
#define BIF_VDDGFX_RSV2_LOWER__VDDGFX_RSV2_REG_CMP_EN__MASK                                                   0x40000000L
#define BIF_VDDGFX_RSV2_LOWER__VDDGFX_RSV2_REG_STALL_EN__MASK                                                 0x80000000L
//BIF_VDDGFX_RSV2_UPPER
#define BIF_VDDGFX_RSV2_UPPER__VDDGFX_RSV2_REG_UPPER__MASK                                                    0x0003FFFCL
//BIF_VDDGFX_RSV3_LOWER
#define BIF_VDDGFX_RSV3_LOWER__VDDGFX_RSV3_REG_LOWER__MASK                                                    0x0003FFFCL
#define BIF_VDDGFX_RSV3_LOWER__VDDGFX_RSV3_REG_CMP_EN__MASK                                                   0x40000000L
#define BIF_VDDGFX_RSV3_LOWER__VDDGFX_RSV3_REG_STALL_EN__MASK                                                 0x80000000L
//BIF_VDDGFX_RSV3_UPPER
#define BIF_VDDGFX_RSV3_UPPER__VDDGFX_RSV3_REG_UPPER__MASK                                                    0x0003FFFCL
//BIF_VDDGFX_RSV4_LOWER
#define BIF_VDDGFX_RSV4_LOWER__VDDGFX_RSV4_REG_LOWER__MASK                                                    0x0003FFFCL
#define BIF_VDDGFX_RSV4_LOWER__VDDGFX_RSV4_REG_CMP_EN__MASK                                                   0x40000000L
#define BIF_VDDGFX_RSV4_LOWER__VDDGFX_RSV4_REG_STALL_EN__MASK                                                 0x80000000L
//BIF_VDDGFX_RSV4_UPPER
#define BIF_VDDGFX_RSV4_UPPER__VDDGFX_RSV4_REG_UPPER__MASK                                                    0x0003FFFCL
//BIF_VDDGFX_FB_CMP
#define BIF_VDDGFX_FB_CMP__VDDGFX_FB_HDP_CMP_EN__MASK                                                         0x00000001L
#define BIF_VDDGFX_FB_CMP__VDDGFX_FB_HDP_STALL_EN__MASK                                                       0x00000002L
#define BIF_VDDGFX_FB_CMP__VDDGFX_FB_XDMA_CMP_EN__MASK                                                        0x00000004L
#define BIF_VDDGFX_FB_CMP__VDDGFX_FB_XDMA_STALL_EN__MASK                                                      0x00000008L
#define BIF_VDDGFX_FB_CMP__VDDGFX_FB_VGA_CMP_EN__MASK                                                         0x00000010L
#define BIF_VDDGFX_FB_CMP__VDDGFX_FB_VGA_STALL_EN__MASK                                                       0x00000020L
//BIF_DOORBELL_GBLAPER1_LOWER
#define BIF_DOORBELL_GBLAPER1_LOWER__DOORBELL_GBLAPER1_LOWER__MASK                                            0x00000FFCL
#define BIF_DOORBELL_GBLAPER1_LOWER__DOORBELL_GBLAPER1_EN__MASK                                               0x80000000L
//BIF_DOORBELL_GBLAPER1_UPPER
#define BIF_DOORBELL_GBLAPER1_UPPER__DOORBELL_GBLAPER1_UPPER__MASK                                            0x00000FFCL
//BIF_DOORBELL_GBLAPER2_LOWER
#define BIF_DOORBELL_GBLAPER2_LOWER__DOORBELL_GBLAPER2_LOWER__MASK                                            0x00000FFCL
#define BIF_DOORBELL_GBLAPER2_LOWER__DOORBELL_GBLAPER2_EN__MASK                                               0x80000000L
//BIF_DOORBELL_GBLAPER2_UPPER
#define BIF_DOORBELL_GBLAPER2_UPPER__DOORBELL_GBLAPER2_UPPER__MASK                                            0x00000FFCL
//REMAP_HDP_MEM_FLUSH_CNTL
#define REMAP_HDP_MEM_FLUSH_CNTL__ADDRESS__MASK                                                               0x0007FFFCL
//REMAP_HDP_REG_FLUSH_CNTL
#define REMAP_HDP_REG_FLUSH_CNTL__ADDRESS__MASK                                                               0x0007FFFCL
//BIF_RB_CNTL
#define BIF_RB_CNTL__RB_ENABLE__MASK                                                                          0x00000001L
#define BIF_RB_CNTL__RB_SIZE__MASK                                                                            0x0000003EL
#define BIF_RB_CNTL__WPTR_WRITEBACK_ENABLE__MASK                                                              0x00000100L
#define BIF_RB_CNTL__WPTR_WRITEBACK_TIMER__MASK                                                               0x00003E00L
#define BIF_RB_CNTL__BIF_RB_TRAN__MASK                                                                        0x00020000L
#define BIF_RB_CNTL__WPTR_OVERFLOW_CLEAR__MASK                                                                0x80000000L
//BIF_RB_BASE
#define BIF_RB_BASE__ADDR__MASK                                                                               0xFFFFFFFFL
//BIF_RB_RPTR
#define BIF_RB_RPTR__OFFSET__MASK                                                                             0x0003FFFCL
//BIF_RB_WPTR
#define BIF_RB_WPTR__BIF_RB_OVERFLOW__MASK                                                                    0x00000001L
#define BIF_RB_WPTR__OFFSET__MASK                                                                             0x0003FFFCL
//BIF_RB_WPTR_ADDR_HI
#define BIF_RB_WPTR_ADDR_HI__ADDR__MASK                                                                       0x000000FFL
//BIF_RB_WPTR_ADDR_LO
#define BIF_RB_WPTR_ADDR_LO__ADDR__MASK                                                                       0xFFFFFFFCL
//MAILBOX_INDEX
#define MAILBOX_INDEX__MAILBOX_INDEX__MASK                                                                    0x0000001FL
//BIF_GPUIOV_RESET_NOTIFICATION
#define BIF_GPUIOV_RESET_NOTIFICATION__RESET_NOTIFICATION__MASK                                               0xFFFFFFFFL
//BIF_UVD_GPUIOV_CFG_SIZE
#define BIF_UVD_GPUIOV_CFG_SIZE__UVD_GPUIOV_CFG_SIZE__MASK                                                    0x0000000FL
//BIF_VCE_GPUIOV_CFG_SIZE
#define BIF_VCE_GPUIOV_CFG_SIZE__VCE_GPUIOV_CFG_SIZE__MASK                                                    0x0000000FL
//BIF_GFX_SDMA_GPUIOV_CFG_SIZE
#define BIF_GFX_SDMA_GPUIOV_CFG_SIZE__GFX_SDMA_GPUIOV_CFG_SIZE__MASK                                          0x0000000FL
//BIF_GMI_WRR_WEIGHT
#define BIF_GMI_WRR_WEIGHT__GMI_REQ_REALTIME_WEIGHT__MASK                                                     0x000000FFL
#define BIF_GMI_WRR_WEIGHT__GMI_REQ_NORM_P_WEIGHT__MASK                                                       0x0000FF00L
#define BIF_GMI_WRR_WEIGHT__GMI_REQ_NORM_NP_WEIGHT__MASK                                                      0x00FF0000L
//NBIF_STRAP_WRITE_CTRL
#define NBIF_STRAP_WRITE_CTRL__NBIF_STRAP_WRITE_ONCE_ENABLE__MASK                                             0x00000001L
//BIF_PERSTB_PAD_CNTL
#define BIF_PERSTB_PAD_CNTL__PERSTB_PAD_CNTL__MASK                                                            0x0000FFFFL
//BIF_PX_EN_PAD_CNTL
#define BIF_PX_EN_PAD_CNTL__PX_EN_PAD_CNTL__MASK                                                              0x000000FFL
//BIF_REFPADKIN_PAD_CNTL
#define BIF_REFPADKIN_PAD_CNTL__REFPADKIN_PAD_CNTL__MASK                                                      0x000000FFL
//BIF_CLKREQB_PAD_CNTL
#define BIF_CLKREQB_PAD_CNTL__CLKREQB_PAD_CNTL__MASK                                                          0x00FFFFFFL


// addressBlock: rcc_pf_0_BIFDEC1
//RCC_BACO_CNTL_MISC
#define RCC_BACO_CNTL_MISC__BIF_ROM_REQ_DIS__MASK                                                             0x00000001L
#define RCC_BACO_CNTL_MISC__BIF_AZ_REQ_DIS__MASK                                                              0x00000002L
//RCC_RESET_EN
#define RCC_RESET_EN__DB_APER_RESET_EN__MASK                                                                  0x00008000L
//RCC_VDM_SUPPORT
#define RCC_VDM_SUPPORT__MCTP_SUPPORT__MASK                                                                   0x00000001L
#define RCC_VDM_SUPPORT__AMPTP_SUPPORT__MASK                                                                  0x00000002L
#define RCC_VDM_SUPPORT__OTHER_VDM_SUPPORT__MASK                                                              0x00000004L
#define RCC_VDM_SUPPORT__ROUTE_TO_RC_CHECK_IN_RCMODE__MASK                                                    0x00000008L
#define RCC_VDM_SUPPORT__ROUTE_BROADCAST_CHECK_IN_RCMODE__MASK                                                0x00000010L
//RCC_PEER_REG_RANGE0
#define RCC_PEER_REG_RANGE0__START_ADDR__MASK                                                                 0x0000FFFFL
#define RCC_PEER_REG_RANGE0__END_ADDR__MASK                                                                   0xFFFF0000L
//RCC_PEER_REG_RANGE1
#define RCC_PEER_REG_RANGE1__START_ADDR__MASK                                                                 0x0000FFFFL
#define RCC_PEER_REG_RANGE1__END_ADDR__MASK                                                                   0xFFFF0000L
//RCC_BUS_CNTL
#define RCC_BUS_CNTL__PMI_IO_DIS__MASK                                                                        0x00000004L
#define RCC_BUS_CNTL__PMI_MEM_DIS__MASK                                                                       0x00000008L
#define RCC_BUS_CNTL__PMI_BM_DIS__MASK                                                                        0x00000010L
#define RCC_BUS_CNTL__PMI_IO_DIS_DN__MASK                                                                     0x00000020L
#define RCC_BUS_CNTL__PMI_MEM_DIS_DN__MASK                                                                    0x00000040L
#define RCC_BUS_CNTL__PMI_IO_DIS_UP__MASK                                                                     0x00000080L
#define RCC_BUS_CNTL__PMI_MEM_DIS_UP__MASK                                                                    0x00000100L
#define RCC_BUS_CNTL__ROOT_ERR_LOG_ON_EVENT__MASK                                                             0x00001000L
#define RCC_BUS_CNTL__HOST_CPL_POISONED_LOG_IN_RC__MASK                                                       0x00002000L
#define RCC_BUS_CNTL__DN_SEC_SIG_CPLCA_WITH_EP_ERR__MASK                                                      0x00010000L
#define RCC_BUS_CNTL__DN_SEC_RCV_CPLCA_WITH_EP_ERR__MASK                                                      0x00020000L
#define RCC_BUS_CNTL__DN_SEC_RCV_CPLUR_WITH_EP_ERR__MASK                                                      0x00040000L
#define RCC_BUS_CNTL__DN_PRI_SIG_CPLCA_WITH_EP_ERR__MASK                                                      0x00080000L
#define RCC_BUS_CNTL__DN_PRI_RCV_CPLCA_WITH_EP_ERR__MASK                                                      0x00100000L
#define RCC_BUS_CNTL__DN_PRI_RCV_CPLUR_WITH_EP_ERR__MASK                                                      0x00200000L
#define RCC_BUS_CNTL__MAX_PAYLOAD_SIZE_MODE__MASK                                                             0x01000000L
#define RCC_BUS_CNTL__PRIV_MAX_PAYLOAD_SIZE__MASK                                                             0x0E000000L
#define RCC_BUS_CNTL__MAX_READ_REQUEST_SIZE_MODE__MASK                                                        0x10000000L
#define RCC_BUS_CNTL__PRIV_MAX_READ_REQUEST_SIZE__MASK                                                        0xE0000000L
//RCC_CONFIG_CNTL
#define RCC_CONFIG_CNTL__CFG_VGA_RAM_EN__MASK                                                                 0x00000001L
#define RCC_CONFIG_CNTL__GENMO_MONO_ADDRESS_B__MASK                                                           0x00000004L
#define RCC_CONFIG_CNTL__GRPH_ADRSEL__MASK                                                                    0x00000018L
//RCC_CONFIG_F0_BASE
#define RCC_CONFIG_F0_BASE__F0_BASE__MASK                                                                     0xFFFFFFFFL
//RCC_CONFIG_APER_SIZE
#define RCC_CONFIG_APER_SIZE__APER_SIZE__MASK                                                                 0xFFFFFFFFL
//RCC_CONFIG_REG_APER_SIZE
#define RCC_CONFIG_REG_APER_SIZE__REG_APER_SIZE__MASK                                                         0x000FFFFFL
//RCC_XDMA_LO
#define RCC_XDMA_LO__BIF_XDMA_LOWER_BOUND__MASK                                                               0x1FFFFFFFL
#define RCC_XDMA_LO__BIF_XDMA_APER_EN__MASK                                                                   0x80000000L
//RCC_XDMA_HI
#define RCC_XDMA_HI__BIF_XDMA_UPPER_BOUND__MASK                                                               0x1FFFFFFFL
//RCC_FEATURES_CONTROL_MISC
#define RCC_FEATURES_CONTROL_MISC__UR_PSN_PKT_REPORT_POISON_DIS__MASK                                         0x00000010L
#define RCC_FEATURES_CONTROL_MISC__POST_PSN_ONLY_PKT_REPORT_UR_ALL_DIS__MASK                                  0x00000020L
#define RCC_FEATURES_CONTROL_MISC__POST_PSN_ONLY_PKT_REPORT_UR_PART_DIS__MASK                                 0x00000040L
#define RCC_FEATURES_CONTROL_MISC__ATC_PRG_RESP_PASID_UR_EN__MASK                                             0x00000100L
#define RCC_FEATURES_CONTROL_MISC__RX_IGNORE_TRANSMRD_UR__MASK                                                0x00000200L
#define RCC_FEATURES_CONTROL_MISC__RX_IGNORE_TRANSMWR_UR__MASK                                                0x00000400L
#define RCC_FEATURES_CONTROL_MISC__RX_IGNORE_ATSTRANSREQ_UR__MASK                                             0x00000800L
#define RCC_FEATURES_CONTROL_MISC__RX_IGNORE_PAGEREQMSG_UR__MASK                                              0x00001000L
#define RCC_FEATURES_CONTROL_MISC__RX_IGNORE_INVCPL_UR__MASK                                                  0x00002000L
#define RCC_FEATURES_CONTROL_MISC__CLR_MSI_X_PENDING_WHEN_DISABLED_DIS__MASK                                  0x00004000L
#define RCC_FEATURES_CONTROL_MISC__CHECK_BME_ON_PENDING_PKT_GEN_DIS__MASK                                     0x00008000L
#define RCC_FEATURES_CONTROL_MISC__PSN_CHECK_ON_PAYLOAD_DIS__MASK                                             0x00010000L
#define RCC_FEATURES_CONTROL_MISC__CLR_MSI_PENDING_ON_MULTIEN_DIS__MASK                                       0x00020000L
#define RCC_FEATURES_CONTROL_MISC__SET_DEVICE_ERR_FOR_ECRC_EN__MASK                                           0x00040000L
//RCC_BUSNUM_CNTL1
#define RCC_BUSNUM_CNTL1__ID_MASK__MASK                                                                       0x000000FFL
//RCC_BUSNUM_LIST0
#define RCC_BUSNUM_LIST0__ID0__MASK                                                                           0x000000FFL
#define RCC_BUSNUM_LIST0__ID1__MASK                                                                           0x0000FF00L
#define RCC_BUSNUM_LIST0__ID2__MASK                                                                           0x00FF0000L
#define RCC_BUSNUM_LIST0__ID3__MASK                                                                           0xFF000000L
//RCC_BUSNUM_LIST1
#define RCC_BUSNUM_LIST1__ID4__MASK                                                                           0x000000FFL
#define RCC_BUSNUM_LIST1__ID5__MASK                                                                           0x0000FF00L
#define RCC_BUSNUM_LIST1__ID6__MASK                                                                           0x00FF0000L
#define RCC_BUSNUM_LIST1__ID7__MASK                                                                           0xFF000000L
//RCC_BUSNUM_CNTL2
#define RCC_BUSNUM_CNTL2__AUTOUPDATE_SEL__MASK                                                                0x000000FFL
#define RCC_BUSNUM_CNTL2__AUTOUPDATE_EN__MASK                                                                 0x00000100L
#define RCC_BUSNUM_CNTL2__HDPREG_CNTL__MASK                                                                   0x00010000L
#define RCC_BUSNUM_CNTL2__ERROR_MULTIPLE_ID_MATCH__MASK                                                       0x00020000L
//RCC_CAPTURE_HOST_BUSNUM
#define RCC_CAPTURE_HOST_BUSNUM__CHECK_EN__MASK                                                               0x00000001L
//RCC_HOST_BUSNUM
#define RCC_HOST_BUSNUM__HOST_ID__MASK                                                                        0x0000FFFFL
//RCC_PEER0_FB_OFFSET_HI
#define RCC_PEER0_FB_OFFSET_HI__PEER0_FB_OFFSET_HI__MASK                                                      0x000FFFFFL
//RCC_PEER0_FB_OFFSET_LO
#define RCC_PEER0_FB_OFFSET_LO__PEER0_FB_OFFSET_LO__MASK                                                      0x000FFFFFL
#define RCC_PEER0_FB_OFFSET_LO__PEER0_FB_EN__MASK                                                             0x80000000L
//RCC_PEER1_FB_OFFSET_HI
#define RCC_PEER1_FB_OFFSET_HI__PEER1_FB_OFFSET_HI__MASK                                                      0x000FFFFFL
//RCC_PEER1_FB_OFFSET_LO
#define RCC_PEER1_FB_OFFSET_LO__PEER1_FB_OFFSET_LO__MASK                                                      0x000FFFFFL
#define RCC_PEER1_FB_OFFSET_LO__PEER1_FB_EN__MASK                                                             0x80000000L
//RCC_PEER2_FB_OFFSET_HI
#define RCC_PEER2_FB_OFFSET_HI__PEER2_FB_OFFSET_HI__MASK                                                      0x000FFFFFL
//RCC_PEER2_FB_OFFSET_LO
#define RCC_PEER2_FB_OFFSET_LO__PEER2_FB_OFFSET_LO__MASK                                                      0x000FFFFFL
#define RCC_PEER2_FB_OFFSET_LO__PEER2_FB_EN__MASK                                                             0x80000000L
//RCC_PEER3_FB_OFFSET_HI
#define RCC_PEER3_FB_OFFSET_HI__PEER3_FB_OFFSET_HI__MASK                                                      0x000FFFFFL
//RCC_PEER3_FB_OFFSET_LO
#define RCC_PEER3_FB_OFFSET_LO__PEER3_FB_OFFSET_LO__MASK                                                      0x000FFFFFL
#define RCC_PEER3_FB_OFFSET_LO__PEER3_FB_EN__MASK                                                             0x80000000L
//RCC_DEVFUNCNUM_LIST0
#define RCC_DEVFUNCNUM_LIST0__DEVFUNC_ID0__MASK                                                               0x000000FFL
#define RCC_DEVFUNCNUM_LIST0__DEVFUNC_ID1__MASK                                                               0x0000FF00L
#define RCC_DEVFUNCNUM_LIST0__DEVFUNC_ID2__MASK                                                               0x00FF0000L
#define RCC_DEVFUNCNUM_LIST0__DEVFUNC_ID3__MASK                                                               0xFF000000L
//RCC_DEVFUNCNUM_LIST1
#define RCC_DEVFUNCNUM_LIST1__DEVFUNC_ID4__MASK                                                               0x000000FFL
#define RCC_DEVFUNCNUM_LIST1__DEVFUNC_ID5__MASK                                                               0x0000FF00L
#define RCC_DEVFUNCNUM_LIST1__DEVFUNC_ID6__MASK                                                               0x00FF0000L
#define RCC_DEVFUNCNUM_LIST1__DEVFUNC_ID7__MASK                                                               0xFF000000L
//RCC_DEV0_LINK_CNTL
#define RCC_DEV0_LINK_CNTL__LINK_DOWN_EXIT__MASK                                                              0x00000001L
#define RCC_DEV0_LINK_CNTL__LINK_DOWN_ENTRY__MASK                                                             0x00000100L
//RCC_CMN_LINK_CNTL
#define RCC_CMN_LINK_CNTL__BLOCK_PME_ON_L0S_DIS__MASK                                                         0x00000001L
#define RCC_CMN_LINK_CNTL__BLOCK_PME_ON_L1_DIS__MASK                                                          0x00000002L
#define RCC_CMN_LINK_CNTL__BLOCK_PME_ON_LDN_DIS__MASK                                                         0x00000004L
#define RCC_CMN_LINK_CNTL__PM_L1_IDLE_CHECK_DMA_EN__MASK                                                      0x00000008L
//RCC_EP_REQUESTERID_RESTORE
#define RCC_EP_REQUESTERID_RESTORE__EP_REQID_BUS__MASK                                                        0x000000FFL
#define RCC_EP_REQUESTERID_RESTORE__EP_REQID_DEV__MASK                                                        0x00001F00L
//RCC_LTR_LSWITCH_CNTL
#define RCC_LTR_LSWITCH_CNTL__LSWITCH_LATENCY_VALUE__MASK                                                     0x000003FFL
//RCC_MH_ARB_CNTL
#define RCC_MH_ARB_CNTL__MH_ARB_MODE__MASK                                                                    0x00000001L
#define RCC_MH_ARB_CNTL__MH_ARB_FIX_PRIORITY__MASK                                                            0x00007FFEL


// addressBlock: rcc_pf_0_BIFDEC2
//GFXMSIX_VECT0_ADDR_LO
#define GFXMSIX_VECT0_ADDR_LO__MSG_ADDR_LO__MASK                                                              0xFFFFFFFCL
//GFXMSIX_VECT0_ADDR_HI
#define GFXMSIX_VECT0_ADDR_HI__MSG_ADDR_HI__MASK                                                              0xFFFFFFFFL
//GFXMSIX_VECT0_MSG_DATA
#define GFXMSIX_VECT0_MSG_DATA__MSG_DATA__MASK                                                                0xFFFFFFFFL
//GFXMSIX_VECT0_CONTROL
#define GFXMSIX_VECT0_CONTROL__MASK_BIT__MASK                                                                 0x00000001L
//GFXMSIX_VECT1_ADDR_LO
#define GFXMSIX_VECT1_ADDR_LO__MSG_ADDR_LO__MASK                                                              0xFFFFFFFCL
//GFXMSIX_VECT1_ADDR_HI
#define GFXMSIX_VECT1_ADDR_HI__MSG_ADDR_HI__MASK                                                              0xFFFFFFFFL
//GFXMSIX_VECT1_MSG_DATA
#define GFXMSIX_VECT1_MSG_DATA__MSG_DATA__MASK                                                                0xFFFFFFFFL
//GFXMSIX_VECT1_CONTROL
#define GFXMSIX_VECT1_CONTROL__MASK_BIT__MASK                                                                 0x00000001L
//GFXMSIX_VECT2_ADDR_LO
#define GFXMSIX_VECT2_ADDR_LO__MSG_ADDR_LO__MASK                                                              0xFFFFFFFCL
//GFXMSIX_VECT2_ADDR_HI
#define GFXMSIX_VECT2_ADDR_HI__MSG_ADDR_HI__MASK                                                              0xFFFFFFFFL
//GFXMSIX_VECT2_MSG_DATA
#define GFXMSIX_VECT2_MSG_DATA__MSG_DATA__MASK                                                                0xFFFFFFFFL
//GFXMSIX_VECT2_CONTROL
#define GFXMSIX_VECT2_CONTROL__MASK_BIT__MASK                                                                 0x00000001L
//GFXMSIX_PBA
#define GFXMSIX_PBA__MSIX_PENDING_BITS_0__MASK                                                                0x00000001L
#define GFXMSIX_PBA__MSIX_PENDING_BITS_1__MASK                                                                0x00000002L
#define GFXMSIX_PBA__MSIX_PENDING_BITS_2__MASK                                                                0x00000004L


// addressBlock: rcc_strap_BIFDEC1
//RCC_DEV0_PORT_STRAP0
#define RCC_DEV0_PORT_STRAP0__STRAP_ARI_EN_DN_DEV0__MASK                                                      0x00000002L
#define RCC_DEV0_PORT_STRAP0__STRAP_ACS_EN_DN_DEV0__MASK                                                      0x00000004L
#define RCC_DEV0_PORT_STRAP0__STRAP_AER_EN_DN_DEV0__MASK                                                      0x00000008L
#define RCC_DEV0_PORT_STRAP0__STRAP_CPL_ABORT_ERR_EN_DN_DEV0__MASK                                            0x00000010L
#define RCC_DEV0_PORT_STRAP0__STRAP_DEVICE_ID_DN_DEV0__MASK                                                   0x001FFFE0L
#define RCC_DEV0_PORT_STRAP0__STRAP_INTERRUPT_PIN_DN_DEV0__MASK                                               0x00E00000L
#define RCC_DEV0_PORT_STRAP0__STRAP_IGNORE_E2E_PREFIX_UR_DN_DEV0__MASK                                        0x01000000L
#define RCC_DEV0_PORT_STRAP0__STRAP_MAX_PAYLOAD_SUPPORT_DN_DEV0__MASK                                         0x0E000000L
#define RCC_DEV0_PORT_STRAP0__STRAP_MAX_LINK_WIDTH_SUPPORT_DEV0__MASK                                         0x70000000L
#define RCC_DEV0_PORT_STRAP0__STRAP_EPF0_DUMMY_EN_DEV0__MASK                                                  0x80000000L
//RCC_DEV0_PORT_STRAP1
#define RCC_DEV0_PORT_STRAP1__STRAP_SUBSYS_ID_DN_DEV0__MASK                                                   0x0000FFFFL
#define RCC_DEV0_PORT_STRAP1__STRAP_SUBSYS_VEN_ID_DN_DEV0__MASK                                               0xFFFF0000L
//RCC_DEV0_PORT_STRAP2
#define RCC_DEV0_PORT_STRAP2__STRAP_DE_EMPHASIS_SEL_DN_DEV0__MASK                                             0x00000001L
#define RCC_DEV0_PORT_STRAP2__STRAP_DSN_EN_DN_DEV0__MASK                                                      0x00000002L
#define RCC_DEV0_PORT_STRAP2__STRAP_E2E_PREFIX_EN_DEV0__MASK                                                  0x00000004L
#define RCC_DEV0_PORT_STRAP2__STRAP_ECN1P1_EN_DEV0__MASK                                                      0x00000008L
#define RCC_DEV0_PORT_STRAP2__STRAP_ECRC_CHECK_EN_DEV0__MASK                                                  0x00000010L
#define RCC_DEV0_PORT_STRAP2__STRAP_ECRC_GEN_EN_DEV0__MASK                                                    0x00000020L
#define RCC_DEV0_PORT_STRAP2__STRAP_ERR_REPORTING_DIS_DEV0__MASK                                              0x00000040L
#define RCC_DEV0_PORT_STRAP2__STRAP_EXTENDED_FMT_SUPPORTED_DEV0__MASK                                         0x00000080L
#define RCC_DEV0_PORT_STRAP2__STRAP_EXTENDED_TAG_ECN_EN_DEV0__MASK                                            0x00000100L
#define RCC_DEV0_PORT_STRAP2__STRAP_EXT_VC_COUNT_DN_DEV0__MASK                                                0x00000E00L
#define RCC_DEV0_PORT_STRAP2__STRAP_FIRST_RCVD_ERR_LOG_DN_DEV0__MASK                                          0x00001000L
#define RCC_DEV0_PORT_STRAP2__STRAP_POISONED_ADVISORY_NONFATAL_DN_DEV0__MASK                                  0x00002000L
#define RCC_DEV0_PORT_STRAP2__STRAP_GEN2_COMPLIANCE_DEV0__MASK                                                0x00004000L
#define RCC_DEV0_PORT_STRAP2__STRAP_GEN2_EN_DEV0__MASK                                                        0x00008000L
#define RCC_DEV0_PORT_STRAP2__STRAP_GEN3_COMPLIANCE_DEV0__MASK                                                0x00010000L
#define RCC_DEV0_PORT_STRAP2__STRAP_TARGET_LINK_SPEED_DEV0__MASK                                              0x00060000L
#define RCC_DEV0_PORT_STRAP2__STRAP_INTERNAL_ERR_EN_DEV0__MASK                                                0x00080000L
#define RCC_DEV0_PORT_STRAP2__STRAP_L0S_ACCEPTABLE_LATENCY_DEV0__MASK                                         0x00700000L
#define RCC_DEV0_PORT_STRAP2__STRAP_L0S_EXIT_LATENCY_DEV0__MASK                                               0x03800000L
#define RCC_DEV0_PORT_STRAP2__STRAP_L1_ACCEPTABLE_LATENCY_DEV0__MASK                                          0x1C000000L
#define RCC_DEV0_PORT_STRAP2__STRAP_L1_EXIT_LATENCY_DEV0__MASK                                                0xE0000000L
//RCC_DEV0_PORT_STRAP3
#define RCC_DEV0_PORT_STRAP3__STRAP_LINK_BW_NOTIFICATION_CAP_DN_EN_DEV0__MASK                                 0x00000001L
#define RCC_DEV0_PORT_STRAP3__STRAP_LTR_EN_DEV0__MASK                                                         0x00000002L
#define RCC_DEV0_PORT_STRAP3__STRAP_LTR_EN_DN_DEV0__MASK                                                      0x00000004L
#define RCC_DEV0_PORT_STRAP3__STRAP_MAX_PAYLOAD_SUPPORT_DEV0__MASK                                            0x00000038L
#define RCC_DEV0_PORT_STRAP3__STRAP_MSI_EN_DN_DEV0__MASK                                                      0x00000040L
#define RCC_DEV0_PORT_STRAP3__STRAP_MSTCPL_TIMEOUT_EN_DEV0__MASK                                              0x00000080L
#define RCC_DEV0_PORT_STRAP3__STRAP_NO_SOFT_RESET_DN_DEV0__MASK                                               0x00000100L
#define RCC_DEV0_PORT_STRAP3__STRAP_OBFF_SUPPORTED_DEV0__MASK                                                 0x00000600L
#define RCC_DEV0_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_DOWNSTREAM_PORT_RX_PRESET_HINT_DEV0__MASK     0x00003800L
#define RCC_DEV0_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_DOWNSTREAM_PORT_TX_PRESET_DEV0__MASK          0x0003C000L
#define RCC_DEV0_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_UPSTREAM_PORT_RX_PRESET_HINT_DEV0__MASK       0x001C0000L
#define RCC_DEV0_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_UPSTREAM_PORT_TX_PRESET_DEV0__MASK            0x01E00000L
#define RCC_DEV0_PORT_STRAP3__STRAP_PM_SUPPORT_DEV0__MASK                                                     0x06000000L
#define RCC_DEV0_PORT_STRAP3__STRAP_PM_SUPPORT_DN_DEV0__MASK                                                  0x18000000L
#define RCC_DEV0_PORT_STRAP3__STRAP_ATOMIC_EN_DN_DEV0__MASK                                                   0x20000000L
#define RCC_DEV0_PORT_STRAP3__STRAP_VENDOR_ID_BIT_DN_DEV0__MASK                                               0x40000000L
#define RCC_DEV0_PORT_STRAP3__STRAP_PMC_DSI_DN_DEV0__MASK                                                     0x80000000L
//RCC_DEV0_PORT_STRAP4
#define RCC_DEV0_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_0_DEV0__MASK                                          0x000000FFL
#define RCC_DEV0_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_1_DEV0__MASK                                          0x0000FF00L
#define RCC_DEV0_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_2_DEV0__MASK                                          0x00FF0000L
#define RCC_DEV0_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_3_DEV0__MASK                                          0xFF000000L
//RCC_DEV0_PORT_STRAP5
#define RCC_DEV0_PORT_STRAP5__STRAP_PWR_BUDGET_DATA_8T0_4_DEV0__MASK                                          0x000000FFL
#define RCC_DEV0_PORT_STRAP5__STRAP_PWR_BUDGET_DATA_8T0_5_DEV0__MASK                                          0x0000FF00L
#define RCC_DEV0_PORT_STRAP5__STRAP_PWR_BUDGET_SYSTEM_ALLOCATED_DEV0__MASK                                    0x00010000L
#define RCC_DEV0_PORT_STRAP5__STRAP_ATOMIC_64BIT_EN_DN_DEV0__MASK                                             0x00020000L
#define RCC_DEV0_PORT_STRAP5__STRAP_ATOMIC_ROUTING_EN_DEV0__MASK                                              0x00040000L
#define RCC_DEV0_PORT_STRAP5__STRAP_VC_EN_DN_DEV0__MASK                                                       0x00080000L
#define RCC_DEV0_PORT_STRAP5__STRAP_TwoVC_EN_DEV0__MASK                                                       0x00100000L
#define RCC_DEV0_PORT_STRAP5__STRAP_TwoVC_EN_DN_DEV0__MASK                                                    0x00200000L
#define RCC_DEV0_PORT_STRAP5__STRAP_ACS_SOURCE_VALIDATION_DN_DEV0__MASK                                       0x00800000L
#define RCC_DEV0_PORT_STRAP5__STRAP_ACS_TRANSLATION_BLOCKING_DN_DEV0__MASK                                    0x01000000L
#define RCC_DEV0_PORT_STRAP5__STRAP_ACS_P2P_REQUEST_REDIRECT_DN_DEV0__MASK                                    0x02000000L
#define RCC_DEV0_PORT_STRAP5__STRAP_ACS_P2P_COMPLETION_REDIRECT_DN_DEV0__MASK                                 0x04000000L
#define RCC_DEV0_PORT_STRAP5__STRAP_ACS_UPSTREAM_FORWARDING_DN_DEV0__MASK                                     0x08000000L
#define RCC_DEV0_PORT_STRAP5__STRAP_ACS_P2P_EGRESS_CONTROL_DN_DEV0__MASK                                      0x10000000L
#define RCC_DEV0_PORT_STRAP5__STRAP_ACS_DIRECT_TRANSLATED_P2P_DN_DEV0__MASK                                   0x20000000L
#define RCC_DEV0_PORT_STRAP5__STRAP_MSI_MAP_EN_DEV0__MASK                                                     0x40000000L
#define RCC_DEV0_PORT_STRAP5__STRAP_SSID_EN_DEV0__MASK                                                        0x80000000L
//RCC_DEV0_PORT_STRAP6
#define RCC_DEV0_PORT_STRAP6__STRAP_CFG_CRS_EN_DEV0__MASK                                                     0x00000001L
#define RCC_DEV0_PORT_STRAP6__STRAP_SMN_ERR_STATUS_MASK_EN_DNS_DEV0__MASK                                     0x00000002L
//RCC_DEV0_PORT_STRAP7
#define RCC_DEV0_PORT_STRAP7__STRAP_PORT_NUMBER_DEV0__MASK                                                    0x000000FFL
#define RCC_DEV0_PORT_STRAP7__STRAP_MAJOR_REV_ID_DN_DEV0__MASK                                                0x00000F00L
#define RCC_DEV0_PORT_STRAP7__STRAP_MINOR_REV_ID_DN_DEV0__MASK                                                0x0000F000L
#define RCC_DEV0_PORT_STRAP7__STRAP_RP_BUSNUM_DEV0__MASK                                                      0x00FF0000L
#define RCC_DEV0_PORT_STRAP7__STRAP_DN_DEVNUM_DEV0__MASK                                                      0x1F000000L
#define RCC_DEV0_PORT_STRAP7__STRAP_DN_FUNCID_DEV0__MASK                                                      0xE0000000L
//RCC_DEV0_EPF0_STRAP0
#define RCC_DEV0_EPF0_STRAP0__STRAP_DEVICE_ID_DEV0_F0__MASK                                                   0x0000FFFFL
#define RCC_DEV0_EPF0_STRAP0__STRAP_MAJOR_REV_ID_DEV0_F0__MASK                                                0x000F0000L
#define RCC_DEV0_EPF0_STRAP0__STRAP_MINOR_REV_ID_DEV0_F0__MASK                                                0x00F00000L
#define RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0__MASK                                                  0x0F000000L
#define RCC_DEV0_EPF0_STRAP0__STRAP_FUNC_EN_DEV0_F0__MASK                                                     0x10000000L
#define RCC_DEV0_EPF0_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV0_F0__MASK                                       0x20000000L
#define RCC_DEV0_EPF0_STRAP0__STRAP_D1_SUPPORT_DEV0_F0__MASK                                                  0x40000000L
#define RCC_DEV0_EPF0_STRAP0__STRAP_D2_SUPPORT_DEV0_F0__MASK                                                  0x80000000L
//RCC_DEV0_EPF0_STRAP1
#define RCC_DEV0_EPF0_STRAP1__STRAP_SRIOV_VF_DEVICE_ID_DEV0_F0__MASK                                          0x0000FFFFL
#define RCC_DEV0_EPF0_STRAP1__STRAP_SRIOV_SUPPORTED_PAGE_SIZE_DEV0_F0__MASK                                   0xFFFF0000L
//RCC_DEV0_EPF0_STRAP13
#define RCC_DEV0_EPF0_STRAP13__STRAP_CLASS_CODE_PIF_DEV0_F0__MASK                                             0x000000FFL
#define RCC_DEV0_EPF0_STRAP13__STRAP_CLASS_CODE_SUB_DEV0_F0__MASK                                             0x0000FF00L
#define RCC_DEV0_EPF0_STRAP13__STRAP_CLASS_CODE_BASE_DEV0_F0__MASK                                            0x00FF0000L
//RCC_DEV0_EPF0_STRAP2
#define RCC_DEV0_EPF0_STRAP2__STRAP_SRIOV_EN_DEV0_F0__MASK                                                    0x00000001L
#define RCC_DEV0_EPF0_STRAP2__STRAP_SRIOV_TOTAL_VFS_DEV0_F0__MASK                                             0x0000003EL
#define RCC_DEV0_EPF0_STRAP2__STRAP_64BAR_DIS_DEV0_F0__MASK                                                   0x00000040L
#define RCC_DEV0_EPF0_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F0__MASK                                               0x00000080L
#define RCC_DEV0_EPF0_STRAP2__STRAP_RESIZE_BAR_EN_DEV0_F0__MASK                                               0x00000100L
#define RCC_DEV0_EPF0_STRAP2__STRAP_MAX_PASID_WIDTH_DEV0_F0__MASK                                             0x00003E00L
#define RCC_DEV0_EPF0_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV0_F0__MASK                                      0x00004000L
#define RCC_DEV0_EPF0_STRAP2__STRAP_ARI_EN_DEV0_F0__MASK                                                      0x00008000L
#define RCC_DEV0_EPF0_STRAP2__STRAP_AER_EN_DEV0_F0__MASK                                                      0x00010000L
#define RCC_DEV0_EPF0_STRAP2__STRAP_ACS_EN_DEV0_F0__MASK                                                      0x00020000L
#define RCC_DEV0_EPF0_STRAP2__STRAP_ATS_EN_DEV0_F0__MASK                                                      0x00040000L
#define RCC_DEV0_EPF0_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV0_F0__MASK                                            0x00100000L
#define RCC_DEV0_EPF0_STRAP2__STRAP_DPA_EN_DEV0_F0__MASK                                                      0x00200000L
#define RCC_DEV0_EPF0_STRAP2__STRAP_DSN_EN_DEV0_F0__MASK                                                      0x00400000L
#define RCC_DEV0_EPF0_STRAP2__STRAP_VC_EN_DEV0_F0__MASK                                                       0x00800000L
#define RCC_DEV0_EPF0_STRAP2__STRAP_MSI_MULTI_CAP_DEV0_F0__MASK                                               0x07000000L
#define RCC_DEV0_EPF0_STRAP2__STRAP_PAGE_REQ_EN_DEV0_F0__MASK                                                 0x08000000L
#define RCC_DEV0_EPF0_STRAP2__STRAP_PASID_EN_DEV0_F0__MASK                                                    0x10000000L
#define RCC_DEV0_EPF0_STRAP2__STRAP_PASID_EXE_PERMISSION_SUPPORTED_DEV0_F0__MASK                              0x20000000L
#define RCC_DEV0_EPF0_STRAP2__STRAP_PASID_GLOBAL_INVALIDATE_SUPPORTED_DEV0_F0__MASK                           0x40000000L
#define RCC_DEV0_EPF0_STRAP2__STRAP_PASID_PRIV_MODE_SUPPORTED_DEV0_F0__MASK                                   0x80000000L
//RCC_DEV0_EPF0_STRAP3
#define RCC_DEV0_EPF0_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV0_F0__MASK                                  0x00000001L
#define RCC_DEV0_EPF0_STRAP3__STRAP_PWR_EN_DEV0_F0__MASK                                                      0x00000002L
#define RCC_DEV0_EPF0_STRAP3__STRAP_SUBSYS_ID_DEV0_F0__MASK                                                   0x0003FFFCL
#define RCC_DEV0_EPF0_STRAP3__STRAP_MSI_EN_DEV0_F0__MASK                                                      0x00040000L
#define RCC_DEV0_EPF0_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV0_F0__MASK                                          0x00080000L
#define RCC_DEV0_EPF0_STRAP3__STRAP_MSIX_EN_DEV0_F0__MASK                                                     0x00100000L
#define RCC_DEV0_EPF0_STRAP3__STRAP_MSIX_TABLE_BIR_DEV0_F0__MASK                                              0x00E00000L
#define RCC_DEV0_EPF0_STRAP3__STRAP_PMC_DSI_DEV0_F0__MASK                                                     0x01000000L
#define RCC_DEV0_EPF0_STRAP3__STRAP_VENDOR_ID_BIT_DEV0_F0__MASK                                               0x02000000L
#define RCC_DEV0_EPF0_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV0_F0__MASK                                    0x04000000L
#define RCC_DEV0_EPF0_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV0_F0__MASK                                   0x08000000L
//RCC_DEV0_EPF0_STRAP4
#define RCC_DEV0_EPF0_STRAP4__STRAP_MSIX_TABLE_OFFSET_DEV0_F0__MASK                                           0x000FFFFFL
#define RCC_DEV0_EPF0_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV0_F0__MASK                                             0x00100000L
#define RCC_DEV0_EPF0_STRAP4__STRAP_ATOMIC_EN_DEV0_F0__MASK                                                   0x00200000L
#define RCC_DEV0_EPF0_STRAP4__STRAP_FLR_EN_DEV0_F0__MASK                                                      0x00400000L
#define RCC_DEV0_EPF0_STRAP4__STRAP_PME_SUPPORT_DEV0_F0__MASK                                                 0x0F800000L
#define RCC_DEV0_EPF0_STRAP4__STRAP_INTERRUPT_PIN_DEV0_F0__MASK                                               0x70000000L
#define RCC_DEV0_EPF0_STRAP4__STRAP_AUXPWR_SUPPORT_DEV0_F0__MASK                                              0x80000000L
//RCC_DEV0_EPF0_STRAP5
#define RCC_DEV0_EPF0_STRAP5__STRAP_SUBSYS_VEN_ID_DEV0_F0__MASK                                               0x0000FFFFL
//RCC_DEV0_EPF0_STRAP8
#define RCC_DEV0_EPF0_STRAP8__STRAP_BAR_COMPLIANCE_EN_DEV0_F0__MASK                                           0x00000001L
#define RCC_DEV0_EPF0_STRAP8__STRAP_DOORBELL_APER_SIZE_DEV0_F0__MASK                                          0x00000006L
#define RCC_DEV0_EPF0_STRAP8__STRAP_DOORBELL_BAR_DIS_DEV0_F0__MASK                                            0x00000008L
#define RCC_DEV0_EPF0_STRAP8__STRAP_FB_ALWAYS_ON_DEV0_F0__MASK                                                0x00000010L
#define RCC_DEV0_EPF0_STRAP8__STRAP_FB_CPL_TYPE_SEL_DEV0_F0__MASK                                             0x00000060L
#define RCC_DEV0_EPF0_STRAP8__STRAP_IO_BAR_DIS_DEV0_F0__MASK                                                  0x00000080L
#define RCC_DEV0_EPF0_STRAP8__STRAP_LFB_ERRMSG_EN_DEV0_F0__MASK                                               0x00000100L
#define RCC_DEV0_EPF0_STRAP8__STRAP_MEM_AP_SIZE_DEV0_F0__MASK                                                 0x00000E00L
#define RCC_DEV0_EPF0_STRAP8__STRAP_REG_AP_SIZE_DEV0_F0__MASK                                                 0x00003000L
#define RCC_DEV0_EPF0_STRAP8__STRAP_ROM_AP_SIZE_DEV0_F0__MASK                                                 0x0000C000L
#define RCC_DEV0_EPF0_STRAP8__STRAP_VF_DOORBELL_APER_SIZE_DEV0_F0__MASK                                       0x00070000L
#define RCC_DEV0_EPF0_STRAP8__STRAP_VF_MEM_AP_SIZE_DEV0_F0__MASK                                              0x00380000L
#define RCC_DEV0_EPF0_STRAP8__STRAP_VF_REG_AP_SIZE_DEV0_F0__MASK                                              0x00C00000L
#define RCC_DEV0_EPF0_STRAP8__STRAP_VGA_DIS_DEV0_F0__MASK                                                     0x01000000L
#define RCC_DEV0_EPF0_STRAP8__STRAP_NBIF_ROM_BAR_DIS_CHICKEN_DEV0_F0__MASK                                    0x02000000L
#define RCC_DEV0_EPF0_STRAP8__STRAP_VF_REG_PROT_DIS_DEV0_F0__MASK                                             0x04000000L
#define RCC_DEV0_EPF0_STRAP8__STRAP_VF_MSI_MULTI_CAP_DEV0_F0__MASK                                            0x38000000L
#define RCC_DEV0_EPF0_STRAP8__STRAP_SRIOV_VF_MAPPING_MODE_DEV0_F0__MASK                                       0xC0000000L
//RCC_DEV0_EPF0_STRAP9
//RCC_DEV0_EPF1_STRAP0
#define RCC_DEV0_EPF1_STRAP0__STRAP_DEVICE_ID_DEV0_F1__MASK                                                   0x0000FFFFL
#define RCC_DEV0_EPF1_STRAP0__STRAP_MAJOR_REV_ID_DEV0_F1__MASK                                                0x000F0000L
#define RCC_DEV0_EPF1_STRAP0__STRAP_MINOR_REV_ID_DEV0_F1__MASK                                                0x00F00000L
#define RCC_DEV0_EPF1_STRAP0__STRAP_FUNC_EN_DEV0_F1__MASK                                                     0x10000000L
#define RCC_DEV0_EPF1_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV0_F1__MASK                                       0x20000000L
#define RCC_DEV0_EPF1_STRAP0__STRAP_D1_SUPPORT_DEV0_F1__MASK                                                  0x40000000L
#define RCC_DEV0_EPF1_STRAP0__STRAP_D2_SUPPORT_DEV0_F1__MASK                                                  0x80000000L
//RCC_DEV0_EPF1_STRAP10
#define RCC_DEV0_EPF1_STRAP10__STRAP_APER1_RESIZE_EN_DEV0_F1__MASK                                            0x00000001L
#define RCC_DEV0_EPF1_STRAP10__STRAP_APER1_RESIZE_SUPPORT_DEV0_F1__MASK                                       0x001FFFFEL
//RCC_DEV0_EPF1_STRAP11
#define RCC_DEV0_EPF1_STRAP11__STRAP_APER2_RESIZE_EN_DEV0_F1__MASK                                            0x00000001L
#define RCC_DEV0_EPF1_STRAP11__STRAP_APER2_RESIZE_SUPPORT_DEV0_F1__MASK                                       0x001FFFFEL
//RCC_DEV0_EPF1_STRAP12
#define RCC_DEV0_EPF1_STRAP12__STRAP_APER3_RESIZE_EN_DEV0_F1__MASK                                            0x00000001L
#define RCC_DEV0_EPF1_STRAP12__STRAP_APER3_RESIZE_SUPPORT_DEV0_F1__MASK                                       0x001FFFFEL
//RCC_DEV0_EPF1_STRAP13
#define RCC_DEV0_EPF1_STRAP13__STRAP_CLASS_CODE_PIF_DEV0_F1__MASK                                             0x000000FFL
#define RCC_DEV0_EPF1_STRAP13__STRAP_CLASS_CODE_SUB_DEV0_F1__MASK                                             0x0000FF00L
#define RCC_DEV0_EPF1_STRAP13__STRAP_CLASS_CODE_BASE_DEV0_F1__MASK                                            0x00FF0000L
//RCC_DEV0_EPF1_STRAP2
#define RCC_DEV0_EPF1_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F1__MASK                                               0x00000080L
#define RCC_DEV0_EPF1_STRAP2__STRAP_RESIZE_BAR_EN_DEV0_F1__MASK                                               0x00000100L
#define RCC_DEV0_EPF1_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV0_F1__MASK                                      0x00004000L
#define RCC_DEV0_EPF1_STRAP2__STRAP_AER_EN_DEV0_F1__MASK                                                      0x00010000L
#define RCC_DEV0_EPF1_STRAP2__STRAP_ACS_EN_DEV0_F1__MASK                                                      0x00020000L
#define RCC_DEV0_EPF1_STRAP2__STRAP_ATS_EN_DEV0_F1__MASK                                                      0x00040000L
#define RCC_DEV0_EPF1_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV0_F1__MASK                                            0x00100000L
#define RCC_DEV0_EPF1_STRAP2__STRAP_DPA_EN_DEV0_F1__MASK                                                      0x00200000L
#define RCC_DEV0_EPF1_STRAP2__STRAP_DSN_EN_DEV0_F1__MASK                                                      0x00400000L
#define RCC_DEV0_EPF1_STRAP2__STRAP_VC_EN_DEV0_F1__MASK                                                       0x00800000L
#define RCC_DEV0_EPF1_STRAP2__STRAP_MSI_MULTI_CAP_DEV0_F1__MASK                                               0x07000000L
//RCC_DEV0_EPF1_STRAP3
#define RCC_DEV0_EPF1_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV0_F1__MASK                                  0x00000001L
#define RCC_DEV0_EPF1_STRAP3__STRAP_PWR_EN_DEV0_F1__MASK                                                      0x00000002L
#define RCC_DEV0_EPF1_STRAP3__STRAP_SUBSYS_ID_DEV0_F1__MASK                                                   0x0003FFFCL
#define RCC_DEV0_EPF1_STRAP3__STRAP_MSI_EN_DEV0_F1__MASK                                                      0x00040000L
#define RCC_DEV0_EPF1_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV0_F1__MASK                                          0x00080000L
#define RCC_DEV0_EPF1_STRAP3__STRAP_MSIX_EN_DEV0_F1__MASK                                                     0x00100000L
#define RCC_DEV0_EPF1_STRAP3__STRAP_PMC_DSI_DEV0_F1__MASK                                                     0x01000000L
#define RCC_DEV0_EPF1_STRAP3__STRAP_VENDOR_ID_BIT_DEV0_F1__MASK                                               0x02000000L
#define RCC_DEV0_EPF1_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV0_F1__MASK                                    0x04000000L
#define RCC_DEV0_EPF1_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV0_F1__MASK                                   0x08000000L
//RCC_DEV0_EPF1_STRAP4
#define RCC_DEV0_EPF1_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV0_F1__MASK                                             0x00100000L
#define RCC_DEV0_EPF1_STRAP4__STRAP_ATOMIC_EN_DEV0_F1__MASK                                                   0x00200000L
#define RCC_DEV0_EPF1_STRAP4__STRAP_FLR_EN_DEV0_F1__MASK                                                      0x00400000L
#define RCC_DEV0_EPF1_STRAP4__STRAP_PME_SUPPORT_DEV0_F1__MASK                                                 0x0F800000L
#define RCC_DEV0_EPF1_STRAP4__STRAP_INTERRUPT_PIN_DEV0_F1__MASK                                               0x70000000L
#define RCC_DEV0_EPF1_STRAP4__STRAP_AUXPWR_SUPPORT_DEV0_F1__MASK                                              0x80000000L
//RCC_DEV0_EPF1_STRAP5
#define RCC_DEV0_EPF1_STRAP5__STRAP_SUBSYS_VEN_ID_DEV0_F1__MASK                                               0x0000FFFFL
//RCC_DEV0_EPF1_STRAP6
#define RCC_DEV0_EPF1_STRAP6__STRAP_APER0_EN_DEV0_F1__MASK                                                    0x00000001L
#define RCC_DEV0_EPF1_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV0_F1__MASK                                       0x00000002L
#define RCC_DEV0_EPF1_STRAP6__STRAP_APER0_64BAR_EN_DEV0_F1__MASK                                              0x00000004L
#define RCC_DEV0_EPF1_STRAP6__STRAP_APER0_AP_SIZE_DEV0_F1__MASK                                               0x00000070L
#define RCC_DEV0_EPF1_STRAP6__STRAP_APER1_EN_DEV0_F1__MASK                                                    0x00000100L
#define RCC_DEV0_EPF1_STRAP6__STRAP_APER1_PREFETCHABLE_EN_DEV0_F1__MASK                                       0x00000200L
#define RCC_DEV0_EPF1_STRAP6__STRAP_APER2_EN_DEV0_F1__MASK                                                    0x00010000L
#define RCC_DEV0_EPF1_STRAP6__STRAP_APER2_PREFETCHABLE_EN_DEV0_F1__MASK                                       0x00020000L
#define RCC_DEV0_EPF1_STRAP6__STRAP_APER3_EN_DEV0_F1__MASK                                                    0x01000000L
#define RCC_DEV0_EPF1_STRAP6__STRAP_APER3_PREFETCHABLE_EN_DEV0_F1__MASK                                       0x02000000L
//RCC_DEV0_EPF1_STRAP7
#define RCC_DEV0_EPF1_STRAP7__STRAP_ROM_APER_EN_DEV0_F1__MASK                                                 0x00000001L
#define RCC_DEV0_EPF1_STRAP7__STRAP_ROM_APER_SIZE_DEV0_F1__MASK                                               0x0000001EL


// addressBlock: bif_bx_pf_BIFPFVFDEC1
//BIF_BME_STATUS
#define BIF_BME_STATUS__DMA_ON_BME_LOW__MASK                                                                  0x00000001L
#define BIF_BME_STATUS__CLEAR_DMA_ON_BME_LOW__MASK                                                            0x00010000L
//BIF_ATOMIC_ERR_LOG
#define BIF_ATOMIC_ERR_LOG__UR_ATOMIC_OPCODE__MASK                                                            0x00000001L
#define BIF_ATOMIC_ERR_LOG__UR_ATOMIC_REQEN_LOW__MASK                                                         0x00000002L
#define BIF_ATOMIC_ERR_LOG__CLEAR_UR_ATOMIC_OPCODE__MASK                                                      0x00010000L
#define BIF_ATOMIC_ERR_LOG__CLEAR_UR_ATOMIC_REQEN_LOW__MASK                                                   0x00020000L
//DOORBELL_SELFRING_GPA_APER_BASE_HIGH
#define DOORBELL_SELFRING_GPA_APER_BASE_HIGH__DOORBELL_SELFRING_GPA_APER_BASE_HIGH__MASK                      0xFFFFFFFFL
//DOORBELL_SELFRING_GPA_APER_BASE_LOW
#define DOORBELL_SELFRING_GPA_APER_BASE_LOW__DOORBELL_SELFRING_GPA_APER_BASE_LOW__MASK                        0xFFFFFFFFL
//DOORBELL_SELFRING_GPA_APER_CNTL
#define DOORBELL_SELFRING_GPA_APER_CNTL__DOORBELL_SELFRING_GPA_APER_EN__MASK                                  0x00000001L
#define DOORBELL_SELFRING_GPA_APER_CNTL__DOORBELL_SELFRING_GPA_APER_SIZE__MASK                                0x0000FF00L
//HDP_REG_COHERENCY_FLUSH_CNTL
#define HDP_REG_COHERENCY_FLUSH_CNTL__HDP_REG_FLUSH_ADDR__MASK                                                0x00000001L
//HDP_MEM_COHERENCY_FLUSH_CNTL
#define HDP_MEM_COHERENCY_FLUSH_CNTL__HDP_MEM_FLUSH_ADDR__MASK                                                0x00000001L
//GPU_HDP_FLUSH_REQ
#define GPU_HDP_FLUSH_REQ__CP0__MASK                                                                          0x00000001L
#define GPU_HDP_FLUSH_REQ__CP1__MASK                                                                          0x00000002L
#define GPU_HDP_FLUSH_REQ__CP2__MASK                                                                          0x00000004L
#define GPU_HDP_FLUSH_REQ__CP3__MASK                                                                          0x00000008L
#define GPU_HDP_FLUSH_REQ__CP4__MASK                                                                          0x00000010L
#define GPU_HDP_FLUSH_REQ__CP5__MASK                                                                          0x00000020L
#define GPU_HDP_FLUSH_REQ__CP6__MASK                                                                          0x00000040L
#define GPU_HDP_FLUSH_REQ__CP7__MASK                                                                          0x00000080L
#define GPU_HDP_FLUSH_REQ__CP8__MASK                                                                          0x00000100L
#define GPU_HDP_FLUSH_REQ__CP9__MASK                                                                          0x00000200L
#define GPU_HDP_FLUSH_REQ__SDMA0__MASK                                                                        0x00000400L
#define GPU_HDP_FLUSH_REQ__SDMA1__MASK                                                                        0x00000800L
//GPU_HDP_FLUSH_DONE
#define GPU_HDP_FLUSH_DONE__CP0__MASK                                                                         0x00000001L
#define GPU_HDP_FLUSH_DONE__CP1__MASK                                                                         0x00000002L
#define GPU_HDP_FLUSH_DONE__CP2__MASK                                                                         0x00000004L
#define GPU_HDP_FLUSH_DONE__CP3__MASK                                                                         0x00000008L
#define GPU_HDP_FLUSH_DONE__CP4__MASK                                                                         0x00000010L
#define GPU_HDP_FLUSH_DONE__CP5__MASK                                                                         0x00000020L
#define GPU_HDP_FLUSH_DONE__CP6__MASK                                                                         0x00000040L
#define GPU_HDP_FLUSH_DONE__CP7__MASK                                                                         0x00000080L
#define GPU_HDP_FLUSH_DONE__CP8__MASK                                                                         0x00000100L
#define GPU_HDP_FLUSH_DONE__CP9__MASK                                                                         0x00000200L
#define GPU_HDP_FLUSH_DONE__SDMA0__MASK                                                                       0x00000400L
#define GPU_HDP_FLUSH_DONE__SDMA1__MASK                                                                       0x00000800L
//BIF_TRANS_PENDING
#define BIF_TRANS_PENDING__BIF_MST_TRANS_PENDING__MASK                                                        0x00000001L
#define BIF_TRANS_PENDING__BIF_SLV_TRANS_PENDING__MASK                                                        0x00000002L
//MAILBOX_MSGBUF_TRN_DW0
#define MAILBOX_MSGBUF_TRN_DW0__MSGBUF_DATA__MASK                                                             0xFFFFFFFFL
//MAILBOX_MSGBUF_TRN_DW1
#define MAILBOX_MSGBUF_TRN_DW1__MSGBUF_DATA__MASK                                                             0xFFFFFFFFL
//MAILBOX_MSGBUF_TRN_DW2
#define MAILBOX_MSGBUF_TRN_DW2__MSGBUF_DATA__MASK                                                             0xFFFFFFFFL
//MAILBOX_MSGBUF_TRN_DW3
#define MAILBOX_MSGBUF_TRN_DW3__MSGBUF_DATA__MASK                                                             0xFFFFFFFFL
//MAILBOX_MSGBUF_RCV_DW0
#define MAILBOX_MSGBUF_RCV_DW0__MSGBUF_DATA__MASK                                                             0xFFFFFFFFL
//MAILBOX_MSGBUF_RCV_DW1
#define MAILBOX_MSGBUF_RCV_DW1__MSGBUF_DATA__MASK                                                             0xFFFFFFFFL
//MAILBOX_MSGBUF_RCV_DW2
#define MAILBOX_MSGBUF_RCV_DW2__MSGBUF_DATA__MASK                                                             0xFFFFFFFFL
//MAILBOX_MSGBUF_RCV_DW3
#define MAILBOX_MSGBUF_RCV_DW3__MSGBUF_DATA__MASK                                                             0xFFFFFFFFL
//MAILBOX_CONTROL
#define MAILBOX_CONTROL__TRN_MSG_VALID__MASK                                                                  0x00000001L
#define MAILBOX_CONTROL__TRN_MSG_ACK__MASK                                                                    0x00000002L
#define MAILBOX_CONTROL__RCV_MSG_VALID__MASK                                                                  0x00000100L
#define MAILBOX_CONTROL__RCV_MSG_ACK__MASK                                                                    0x00000200L
//MAILBOX_INT_CNTL
#define MAILBOX_INT_CNTL__VALID_INT_EN__MASK                                                                  0x00000001L
#define MAILBOX_INT_CNTL__ACK_INT_EN__MASK                                                                    0x00000002L
//BIF_VMHV_MAILBOX
#define BIF_VMHV_MAILBOX__VMHV_MAILBOX_TRN_ACK_INTR_EN__MASK                                                  0x00000001L
#define BIF_VMHV_MAILBOX__VMHV_MAILBOX_RCV_VALID_INTR_EN__MASK                                                0x00000002L
#define BIF_VMHV_MAILBOX__VMHV_MAILBOX_TRN_MSG_DATA__MASK                                                     0x00000F00L
#define BIF_VMHV_MAILBOX__VMHV_MAILBOX_TRN_MSG_VALID__MASK                                                    0x00008000L
#define BIF_VMHV_MAILBOX__VMHV_MAILBOX_RCV_MSG_DATA__MASK                                                     0x000F0000L
#define BIF_VMHV_MAILBOX__VMHV_MAILBOX_RCV_MSG_VALID__MASK                                                    0x00800000L
#define BIF_VMHV_MAILBOX__VMHV_MAILBOX_TRN_MSG_ACK__MASK                                                      0x01000000L
#define BIF_VMHV_MAILBOX__VMHV_MAILBOX_RCV_MSG_ACK__MASK                                                      0x02000000L


// addressBlock: rcc_pf_0_BIFPFVFDEC1
//RCC_DOORBELL_APER_EN
#define RCC_DOORBELL_APER_EN__BIF_DOORBELL_APER_EN__MASK                                                      0x00000001L
//RCC_CONFIG_MEMSIZE
#define RCC_CONFIG_MEMSIZE__CONFIG_MEMSIZE__MASK                                                              0xFFFFFFFFL
//RCC_CONFIG_RESERVED
#define RCC_CONFIG_RESERVED__CONFIG_RESERVED__MASK                                                            0xFFFFFFFFL
//RCC_IOV_FUNC_IDENTIFIER
#define RCC_IOV_FUNC_IDENTIFIER__FUNC_IDENTIFIER__MASK                                                        0x00000001L
#define RCC_IOV_FUNC_IDENTIFIER__IOV_ENABLE__MASK                                                             0x80000000L


// addressBlock: syshub_mmreg_ind_syshubdec
//SYSHUB_INDEX
#define SYSHUB_INDEX__INDEX__MASK                                                                             0xFFFFFFFFL
//SYSHUB_DATA
#define SYSHUB_DATA__DATA__MASK                                                                               0xFFFFFFFFL


// addressBlock: rcc_strap_rcc_strap_internal
//RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0__STRAP_ARI_EN_DN_DEV0__MASK                                     0x00000002L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0__STRAP_ACS_EN_DN_DEV0__MASK                                     0x00000004L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0__STRAP_AER_EN_DN_DEV0__MASK                                     0x00000008L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0__STRAP_CPL_ABORT_ERR_EN_DN_DEV0__MASK                           0x00000010L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0__STRAP_DEVICE_ID_DN_DEV0__MASK                                  0x001FFFE0L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0__STRAP_INTERRUPT_PIN_DN_DEV0__MASK                              0x00E00000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0__STRAP_IGNORE_E2E_PREFIX_UR_DN_DEV0__MASK                       0x01000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0__STRAP_MAX_PAYLOAD_SUPPORT_DN_DEV0__MASK                        0x0E000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0__STRAP_MAX_LINK_WIDTH_SUPPORT_DEV0__MASK                        0x70000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP0__STRAP_EPF0_DUMMY_EN_DEV0__MASK                                 0x80000000L
//RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP1
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP1__STRAP_SUBSYS_ID_DN_DEV0__MASK                                  0x0000FFFFL
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP1__STRAP_SUBSYS_VEN_ID_DN_DEV0__MASK                              0xFFFF0000L
//RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_DE_EMPHASIS_SEL_DN_DEV0__MASK                            0x00000001L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_DSN_EN_DN_DEV0__MASK                                     0x00000002L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_E2E_PREFIX_EN_DEV0__MASK                                 0x00000004L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_ECN1P1_EN_DEV0__MASK                                     0x00000008L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_ECRC_CHECK_EN_DEV0__MASK                                 0x00000010L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_ECRC_GEN_EN_DEV0__MASK                                   0x00000020L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_ERR_REPORTING_DIS_DEV0__MASK                             0x00000040L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_EXTENDED_FMT_SUPPORTED_DEV0__MASK                        0x00000080L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_EXTENDED_TAG_ECN_EN_DEV0__MASK                           0x00000100L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_EXT_VC_COUNT_DN_DEV0__MASK                               0x00000E00L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_FIRST_RCVD_ERR_LOG_DN_DEV0__MASK                         0x00001000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_POISONED_ADVISORY_NONFATAL_DN_DEV0__MASK                 0x00002000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_GEN2_COMPLIANCE_DEV0__MASK                               0x00004000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_GEN2_EN_DEV0__MASK                                       0x00008000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_GEN3_COMPLIANCE_DEV0__MASK                               0x00010000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_TARGET_LINK_SPEED_DEV0__MASK                             0x00060000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_INTERNAL_ERR_EN_DEV0__MASK                               0x00080000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_L0S_ACCEPTABLE_LATENCY_DEV0__MASK                        0x00700000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_L0S_EXIT_LATENCY_DEV0__MASK                              0x03800000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_L1_ACCEPTABLE_LATENCY_DEV0__MASK                         0x1C000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP2__STRAP_L1_EXIT_LATENCY_DEV0__MASK                               0xE0000000L
//RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_LINK_BW_NOTIFICATION_CAP_DN_EN_DEV0__MASK                0x00000001L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_LTR_EN_DEV0__MASK                                        0x00000002L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_LTR_EN_DN_DEV0__MASK                                     0x00000004L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_MAX_PAYLOAD_SUPPORT_DEV0__MASK                           0x00000038L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_MSI_EN_DN_DEV0__MASK                                     0x00000040L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_MSTCPL_TIMEOUT_EN_DEV0__MASK                             0x00000080L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_NO_SOFT_RESET_DN_DEV0__MASK                              0x00000100L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_OBFF_SUPPORTED_DEV0__MASK                                0x00000600L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_DOWNSTREAM_PORT_RX_PRESET_HINT_DEV0__MASK  0x00003800L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_DOWNSTREAM_PORT_TX_PRESET_DEV0__MASK  0x0003C000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_UPSTREAM_PORT_RX_PRESET_HINT_DEV0__MASK  0x001C0000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_UPSTREAM_PORT_TX_PRESET_DEV0__MASK  0x01E00000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_PM_SUPPORT_DEV0__MASK                                    0x06000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_PM_SUPPORT_DN_DEV0__MASK                                 0x18000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_ATOMIC_EN_DN_DEV0__MASK                                  0x20000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_VENDOR_ID_BIT_DN_DEV0__MASK                              0x40000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP3__STRAP_PMC_DSI_DN_DEV0__MASK                                    0x80000000L
//RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP4
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_0_DEV0__MASK                         0x000000FFL
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_1_DEV0__MASK                         0x0000FF00L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_2_DEV0__MASK                         0x00FF0000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_3_DEV0__MASK                         0xFF000000L
//RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_PWR_BUDGET_DATA_8T0_4_DEV0__MASK                         0x000000FFL
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_PWR_BUDGET_DATA_8T0_5_DEV0__MASK                         0x0000FF00L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_PWR_BUDGET_SYSTEM_ALLOCATED_DEV0__MASK                   0x00010000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_ATOMIC_64BIT_EN_DN_DEV0__MASK                            0x00020000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_ATOMIC_ROUTING_EN_DEV0__MASK                             0x00040000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_VC_EN_DN_DEV0__MASK                                      0x00080000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_TwoVC_EN_DEV0__MASK                                      0x00100000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_TwoVC_EN_DN_DEV0__MASK                                   0x00200000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_ACS_SOURCE_VALIDATION_DN_DEV0__MASK                      0x00800000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_ACS_TRANSLATION_BLOCKING_DN_DEV0__MASK                   0x01000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_ACS_P2P_REQUEST_REDIRECT_DN_DEV0__MASK                   0x02000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_ACS_P2P_COMPLETION_REDIRECT_DN_DEV0__MASK                0x04000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_ACS_UPSTREAM_FORWARDING_DN_DEV0__MASK                    0x08000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_ACS_P2P_EGRESS_CONTROL_DN_DEV0__MASK                     0x10000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_ACS_DIRECT_TRANSLATED_P2P_DN_DEV0__MASK                  0x20000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_MSI_MAP_EN_DEV0__MASK                                    0x40000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP5__STRAP_SSID_EN_DEV0__MASK                                       0x80000000L
//RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP6
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP6__STRAP_CFG_CRS_EN_DEV0__MASK                                    0x00000001L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP6__STRAP_SMN_ERR_STATUS_MASK_EN_DNS_DEV0__MASK                    0x00000002L
//RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP7
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP7__STRAP_PORT_NUMBER_DEV0__MASK                                   0x000000FFL
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP7__STRAP_MAJOR_REV_ID_DN_DEV0__MASK                               0x00000F00L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP7__STRAP_MINOR_REV_ID_DN_DEV0__MASK                               0x0000F000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP7__STRAP_RP_BUSNUM_DEV0__MASK                                     0x00FF0000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP7__STRAP_DN_DEVNUM_DEV0__MASK                                     0x1F000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_PORT_STRAP7__STRAP_DN_FUNCID_DEV0__MASK                                     0xE0000000L
//RCC_DEV1_PORT_STRAP0
#define RCC_DEV1_PORT_STRAP0__STRAP_ARI_EN_DN_DEV1__MASK                                                      0x00000002L
#define RCC_DEV1_PORT_STRAP0__STRAP_ACS_EN_DN_DEV1__MASK                                                      0x00000004L
#define RCC_DEV1_PORT_STRAP0__STRAP_AER_EN_DN_DEV1__MASK                                                      0x00000008L
#define RCC_DEV1_PORT_STRAP0__STRAP_CPL_ABORT_ERR_EN_DN_DEV1__MASK                                            0x00000010L
#define RCC_DEV1_PORT_STRAP0__STRAP_DEVICE_ID_DN_DEV1__MASK                                                   0x001FFFE0L
#define RCC_DEV1_PORT_STRAP0__STRAP_INTERRUPT_PIN_DN_DEV1__MASK                                               0x00E00000L
#define RCC_DEV1_PORT_STRAP0__STRAP_IGNORE_E2E_PREFIX_UR_DN_DEV1__MASK                                        0x01000000L
#define RCC_DEV1_PORT_STRAP0__STRAP_MAX_PAYLOAD_SUPPORT_DN_DEV1__MASK                                         0x0E000000L
#define RCC_DEV1_PORT_STRAP0__STRAP_MAX_LINK_WIDTH_SUPPORT_DEV1__MASK                                         0x70000000L
#define RCC_DEV1_PORT_STRAP0__STRAP_EPF0_DUMMY_EN_DEV1__MASK                                                  0x80000000L
//RCC_DEV1_PORT_STRAP1
#define RCC_DEV1_PORT_STRAP1__STRAP_SUBSYS_ID_DN_DEV1__MASK                                                   0x0000FFFFL
#define RCC_DEV1_PORT_STRAP1__STRAP_SUBSYS_VEN_ID_DN_DEV1__MASK                                               0xFFFF0000L
//RCC_DEV1_PORT_STRAP2
#define RCC_DEV1_PORT_STRAP2__STRAP_DE_EMPHASIS_SEL_DN_DEV1__MASK                                             0x00000001L
#define RCC_DEV1_PORT_STRAP2__STRAP_DSN_EN_DN_DEV1__MASK                                                      0x00000002L
#define RCC_DEV1_PORT_STRAP2__STRAP_E2E_PREFIX_EN_DEV1__MASK                                                  0x00000004L
#define RCC_DEV1_PORT_STRAP2__STRAP_ECN1P1_EN_DEV1__MASK                                                      0x00000008L
#define RCC_DEV1_PORT_STRAP2__STRAP_ECRC_CHECK_EN_DEV1__MASK                                                  0x00000010L
#define RCC_DEV1_PORT_STRAP2__STRAP_ECRC_GEN_EN_DEV1__MASK                                                    0x00000020L
#define RCC_DEV1_PORT_STRAP2__STRAP_ERR_REPORTING_DIS_DEV1__MASK                                              0x00000040L
#define RCC_DEV1_PORT_STRAP2__STRAP_EXTENDED_FMT_SUPPORTED_DEV1__MASK                                         0x00000080L
#define RCC_DEV1_PORT_STRAP2__STRAP_EXTENDED_TAG_ECN_EN_DEV1__MASK                                            0x00000100L
#define RCC_DEV1_PORT_STRAP2__STRAP_EXT_VC_COUNT_DN_DEV1__MASK                                                0x00000E00L
#define RCC_DEV1_PORT_STRAP2__STRAP_FIRST_RCVD_ERR_LOG_DN_DEV1__MASK                                          0x00001000L
#define RCC_DEV1_PORT_STRAP2__STRAP_POISONED_ADVISORY_NONFATAL_DN_DEV1__MASK                                  0x00002000L
#define RCC_DEV1_PORT_STRAP2__STRAP_GEN2_COMPLIANCE_DEV1__MASK                                                0x00004000L
#define RCC_DEV1_PORT_STRAP2__STRAP_GEN2_EN_DEV1__MASK                                                        0x00008000L
#define RCC_DEV1_PORT_STRAP2__STRAP_GEN3_COMPLIANCE_DEV1__MASK                                                0x00010000L
#define RCC_DEV1_PORT_STRAP2__STRAP_TARGET_LINK_SPEED_DEV1__MASK                                              0x00060000L
#define RCC_DEV1_PORT_STRAP2__STRAP_INTERNAL_ERR_EN_DEV1__MASK                                                0x00080000L
#define RCC_DEV1_PORT_STRAP2__STRAP_L0S_ACCEPTABLE_LATENCY_DEV1__MASK                                         0x00700000L
#define RCC_DEV1_PORT_STRAP2__STRAP_L0S_EXIT_LATENCY_DEV1__MASK                                               0x03800000L
#define RCC_DEV1_PORT_STRAP2__STRAP_L1_ACCEPTABLE_LATENCY_DEV1__MASK                                          0x1C000000L
#define RCC_DEV1_PORT_STRAP2__STRAP_L1_EXIT_LATENCY_DEV1__MASK                                                0xE0000000L
//RCC_DEV1_PORT_STRAP3
#define RCC_DEV1_PORT_STRAP3__STRAP_LINK_BW_NOTIFICATION_CAP_DN_EN_DEV1__MASK                                 0x00000001L
#define RCC_DEV1_PORT_STRAP3__STRAP_LTR_EN_DEV1__MASK                                                         0x00000002L
#define RCC_DEV1_PORT_STRAP3__STRAP_LTR_EN_DN_DEV1__MASK                                                      0x00000004L
#define RCC_DEV1_PORT_STRAP3__STRAP_MAX_PAYLOAD_SUPPORT_DEV1__MASK                                            0x00000038L
#define RCC_DEV1_PORT_STRAP3__STRAP_MSI_EN_DN_DEV1__MASK                                                      0x00000040L
#define RCC_DEV1_PORT_STRAP3__STRAP_MSTCPL_TIMEOUT_EN_DEV1__MASK                                              0x00000080L
#define RCC_DEV1_PORT_STRAP3__STRAP_NO_SOFT_RESET_DN_DEV1__MASK                                               0x00000100L
#define RCC_DEV1_PORT_STRAP3__STRAP_OBFF_SUPPORTED_DEV1__MASK                                                 0x00000600L
#define RCC_DEV1_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_DOWNSTREAM_PORT_RX_PRESET_HINT_DEV1__MASK     0x00003800L
#define RCC_DEV1_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_DOWNSTREAM_PORT_TX_PRESET_DEV1__MASK          0x0003C000L
#define RCC_DEV1_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_UPSTREAM_PORT_RX_PRESET_HINT_DEV1__MASK       0x001C0000L
#define RCC_DEV1_PORT_STRAP3__STRAP_PCIE_LANE_EQUALIZATION_CNTL_UPSTREAM_PORT_TX_PRESET_DEV1__MASK            0x01E00000L
#define RCC_DEV1_PORT_STRAP3__STRAP_PM_SUPPORT_DEV1__MASK                                                     0x06000000L
#define RCC_DEV1_PORT_STRAP3__STRAP_PM_SUPPORT_DN_DEV1__MASK                                                  0x18000000L
#define RCC_DEV1_PORT_STRAP3__STRAP_ATOMIC_EN_DN_DEV1__MASK                                                   0x20000000L
#define RCC_DEV1_PORT_STRAP3__STRAP_VENDOR_ID_BIT_DN_DEV1__MASK                                               0x40000000L
#define RCC_DEV1_PORT_STRAP3__STRAP_PMC_DSI_DN_DEV1__MASK                                                     0x80000000L
//RCC_DEV1_PORT_STRAP4
#define RCC_DEV1_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_0_DEV1__MASK                                          0x000000FFL
#define RCC_DEV1_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_1_DEV1__MASK                                          0x0000FF00L
#define RCC_DEV1_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_2_DEV1__MASK                                          0x00FF0000L
#define RCC_DEV1_PORT_STRAP4__STRAP_PWR_BUDGET_DATA_8T0_3_DEV1__MASK                                          0xFF000000L
//RCC_DEV1_PORT_STRAP5
#define RCC_DEV1_PORT_STRAP5__STRAP_PWR_BUDGET_DATA_8T0_4_DEV1__MASK                                          0x000000FFL
#define RCC_DEV1_PORT_STRAP5__STRAP_PWR_BUDGET_DATA_8T0_5_DEV1__MASK                                          0x0000FF00L
#define RCC_DEV1_PORT_STRAP5__STRAP_PWR_BUDGET_SYSTEM_ALLOCATED_DEV1__MASK                                    0x00010000L
#define RCC_DEV1_PORT_STRAP5__STRAP_ATOMIC_64BIT_EN_DN_DEV1__MASK                                             0x00020000L
#define RCC_DEV1_PORT_STRAP5__STRAP_ATOMIC_ROUTING_EN_DEV1__MASK                                              0x00040000L
#define RCC_DEV1_PORT_STRAP5__STRAP_VC_EN_DN_DEV1__MASK                                                       0x00080000L
#define RCC_DEV1_PORT_STRAP5__STRAP_TwoVC_EN_DEV1__MASK                                                       0x00100000L
#define RCC_DEV1_PORT_STRAP5__STRAP_TwoVC_EN_DN_DEV1__MASK                                                    0x00200000L
#define RCC_DEV1_PORT_STRAP5__STRAP_ACS_SOURCE_VALIDATION_DN_DEV1__MASK                                       0x00800000L
#define RCC_DEV1_PORT_STRAP5__STRAP_ACS_TRANSLATION_BLOCKING_DN_DEV1__MASK                                    0x01000000L
#define RCC_DEV1_PORT_STRAP5__STRAP_ACS_P2P_REQUEST_REDIRECT_DN_DEV1__MASK                                    0x02000000L
#define RCC_DEV1_PORT_STRAP5__STRAP_ACS_P2P_COMPLETION_REDIRECT_DN_DEV1__MASK                                 0x04000000L
#define RCC_DEV1_PORT_STRAP5__STRAP_ACS_UPSTREAM_FORWARDING_DN_DEV1__MASK                                     0x08000000L
#define RCC_DEV1_PORT_STRAP5__STRAP_ACS_P2P_EGRESS_CONTROL_DN_DEV1__MASK                                      0x10000000L
#define RCC_DEV1_PORT_STRAP5__STRAP_ACS_DIRECT_TRANSLATED_P2P_DN_DEV1__MASK                                   0x20000000L
#define RCC_DEV1_PORT_STRAP5__STRAP_MSI_MAP_EN_DEV1__MASK                                                     0x40000000L
#define RCC_DEV1_PORT_STRAP5__STRAP_SSID_EN_DEV1__MASK                                                        0x80000000L
//RCC_DEV1_PORT_STRAP6
#define RCC_DEV1_PORT_STRAP6__STRAP_CFG_CRS_EN_DEV1__MASK                                                     0x00000001L
#define RCC_DEV1_PORT_STRAP6__STRAP_SMN_ERR_STATUS_MASK_EN_DNS_DEV1__MASK                                     0x00000002L
//RCC_DEV1_PORT_STRAP7
#define RCC_DEV1_PORT_STRAP7__STRAP_PORT_NUMBER_DEV1__MASK                                                    0x000000FFL
#define RCC_DEV1_PORT_STRAP7__STRAP_MAJOR_REV_ID_DN_DEV1__MASK                                                0x00000F00L
#define RCC_DEV1_PORT_STRAP7__STRAP_MINOR_REV_ID_DN_DEV1__MASK                                                0x0000F000L
#define RCC_DEV1_PORT_STRAP7__STRAP_RP_BUSNUM_DEV1__MASK                                                      0x00FF0000L
#define RCC_DEV1_PORT_STRAP7__STRAP_DN_DEVNUM_DEV1__MASK                                                      0x1F000000L
#define RCC_DEV1_PORT_STRAP7__STRAP_DN_FUNCID_DEV1__MASK                                                      0xE0000000L
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP0__STRAP_DEVICE_ID_DEV0_F0__MASK                                  0x0000FFFFL
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP0__STRAP_MAJOR_REV_ID_DEV0_F0__MASK                               0x000F0000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP0__STRAP_MINOR_REV_ID_DEV0_F0__MASK                               0x00F00000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0__MASK                                 0x0F000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP0__STRAP_FUNC_EN_DEV0_F0__MASK                                    0x10000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV0_F0__MASK                      0x20000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP0__STRAP_D1_SUPPORT_DEV0_F0__MASK                                 0x40000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP0__STRAP_D2_SUPPORT_DEV0_F0__MASK                                 0x80000000L
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP1
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP1__STRAP_SRIOV_VF_DEVICE_ID_DEV0_F0__MASK                         0x0000FFFFL
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP1__STRAP_SRIOV_SUPPORTED_PAGE_SIZE_DEV0_F0__MASK                  0xFFFF0000L
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_SRIOV_EN_DEV0_F0__MASK                                   0x00000001L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_SRIOV_TOTAL_VFS_DEV0_F0__MASK                            0x0000003EL
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_64BAR_DIS_DEV0_F0__MASK                                  0x00000040L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F0__MASK                              0x00000080L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_RESIZE_BAR_EN_DEV0_F0__MASK                              0x00000100L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_MAX_PASID_WIDTH_DEV0_F0__MASK                            0x00003E00L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV0_F0__MASK                     0x00004000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_ARI_EN_DEV0_F0__MASK                                     0x00008000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_AER_EN_DEV0_F0__MASK                                     0x00010000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_ACS_EN_DEV0_F0__MASK                                     0x00020000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_ATS_EN_DEV0_F0__MASK                                     0x00040000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV0_F0__MASK                           0x00100000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_DPA_EN_DEV0_F0__MASK                                     0x00200000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_DSN_EN_DEV0_F0__MASK                                     0x00400000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_VC_EN_DEV0_F0__MASK                                      0x00800000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_MSI_MULTI_CAP_DEV0_F0__MASK                              0x07000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_PAGE_REQ_EN_DEV0_F0__MASK                                0x08000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_PASID_EN_DEV0_F0__MASK                                   0x10000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_PASID_EXE_PERMISSION_SUPPORTED_DEV0_F0__MASK             0x20000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_PASID_GLOBAL_INVALIDATE_SUPPORTED_DEV0_F0__MASK          0x40000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP2__STRAP_PASID_PRIV_MODE_SUPPORTED_DEV0_F0__MASK                  0x80000000L
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV0_F0__MASK                 0x00000001L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_PWR_EN_DEV0_F0__MASK                                     0x00000002L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_SUBSYS_ID_DEV0_F0__MASK                                  0x0003FFFCL
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_MSI_EN_DEV0_F0__MASK                                     0x00040000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV0_F0__MASK                         0x00080000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_MSIX_EN_DEV0_F0__MASK                                    0x00100000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_MSIX_TABLE_BIR_DEV0_F0__MASK                             0x00E00000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_PMC_DSI_DEV0_F0__MASK                                    0x01000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_VENDOR_ID_BIT_DEV0_F0__MASK                              0x02000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV0_F0__MASK                   0x04000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV0_F0__MASK                  0x08000000L
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP4
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP4__STRAP_MSIX_TABLE_OFFSET_DEV0_F0__MASK                          0x000FFFFFL
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV0_F0__MASK                            0x00100000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP4__STRAP_ATOMIC_EN_DEV0_F0__MASK                                  0x00200000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP4__STRAP_FLR_EN_DEV0_F0__MASK                                     0x00400000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP4__STRAP_PME_SUPPORT_DEV0_F0__MASK                                0x0F800000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP4__STRAP_INTERRUPT_PIN_DEV0_F0__MASK                              0x70000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP4__STRAP_AUXPWR_SUPPORT_DEV0_F0__MASK                             0x80000000L
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP5
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP5__STRAP_SUBSYS_VEN_ID_DEV0_F0__MASK                              0x0000FFFFL
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_BAR_COMPLIANCE_EN_DEV0_F0__MASK                          0x00000001L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_DOORBELL_APER_SIZE_DEV0_F0__MASK                         0x00000006L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_DOORBELL_BAR_DIS_DEV0_F0__MASK                           0x00000008L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_FB_ALWAYS_ON_DEV0_F0__MASK                               0x00000010L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_FB_CPL_TYPE_SEL_DEV0_F0__MASK                            0x00000060L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_IO_BAR_DIS_DEV0_F0__MASK                                 0x00000080L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_LFB_ERRMSG_EN_DEV0_F0__MASK                              0x00000100L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_MEM_AP_SIZE_DEV0_F0__MASK                                0x00000E00L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_REG_AP_SIZE_DEV0_F0__MASK                                0x00003000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_ROM_AP_SIZE_DEV0_F0__MASK                                0x0000C000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_VF_DOORBELL_APER_SIZE_DEV0_F0__MASK                      0x00070000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_VF_MEM_AP_SIZE_DEV0_F0__MASK                             0x00380000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_VF_REG_AP_SIZE_DEV0_F0__MASK                             0x00C00000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_VGA_DIS_DEV0_F0__MASK                                    0x01000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_NBIF_ROM_BAR_DIS_CHICKEN_DEV0_F0__MASK                   0x02000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_VF_REG_PROT_DIS_DEV0_F0__MASK                            0x04000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_VF_MSI_MULTI_CAP_DEV0_F0__MASK                           0x38000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP8__STRAP_SRIOV_VF_MAPPING_MODE_DEV0_F0__MASK                      0xC0000000L
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP9
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP13
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP13__STRAP_CLASS_CODE_PIF_DEV0_F0__MASK                            0x000000FFL
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP13__STRAP_CLASS_CODE_SUB_DEV0_F0__MASK                            0x0000FF00L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF0_STRAP13__STRAP_CLASS_CODE_BASE_DEV0_F0__MASK                           0x00FF0000L
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP0
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP0__STRAP_DEVICE_ID_DEV0_F1__MASK                                  0x0000FFFFL
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP0__STRAP_MAJOR_REV_ID_DEV0_F1__MASK                               0x000F0000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP0__STRAP_MINOR_REV_ID_DEV0_F1__MASK                               0x00F00000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP0__STRAP_FUNC_EN_DEV0_F1__MASK                                    0x10000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV0_F1__MASK                      0x20000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP0__STRAP_D1_SUPPORT_DEV0_F1__MASK                                 0x40000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP0__STRAP_D2_SUPPORT_DEV0_F1__MASK                                 0x80000000L
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F1__MASK                              0x00000080L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_RESIZE_BAR_EN_DEV0_F1__MASK                              0x00000100L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV0_F1__MASK                     0x00004000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_AER_EN_DEV0_F1__MASK                                     0x00010000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_ACS_EN_DEV0_F1__MASK                                     0x00020000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_ATS_EN_DEV0_F1__MASK                                     0x00040000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV0_F1__MASK                           0x00100000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_DPA_EN_DEV0_F1__MASK                                     0x00200000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_DSN_EN_DEV0_F1__MASK                                     0x00400000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_VC_EN_DEV0_F1__MASK                                      0x00800000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP2__STRAP_MSI_MULTI_CAP_DEV0_F1__MASK                              0x07000000L
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV0_F1__MASK                 0x00000001L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3__STRAP_PWR_EN_DEV0_F1__MASK                                     0x00000002L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3__STRAP_SUBSYS_ID_DEV0_F1__MASK                                  0x0003FFFCL
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3__STRAP_MSI_EN_DEV0_F1__MASK                                     0x00040000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV0_F1__MASK                         0x00080000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3__STRAP_MSIX_EN_DEV0_F1__MASK                                    0x00100000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3__STRAP_PMC_DSI_DEV0_F1__MASK                                    0x01000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3__STRAP_VENDOR_ID_BIT_DEV0_F1__MASK                              0x02000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV0_F1__MASK                   0x04000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV0_F1__MASK                  0x08000000L
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP4
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV0_F1__MASK                            0x00100000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP4__STRAP_ATOMIC_EN_DEV0_F1__MASK                                  0x00200000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP4__STRAP_FLR_EN_DEV0_F1__MASK                                     0x00400000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP4__STRAP_PME_SUPPORT_DEV0_F1__MASK                                0x0F800000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP4__STRAP_INTERRUPT_PIN_DEV0_F1__MASK                              0x70000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP4__STRAP_AUXPWR_SUPPORT_DEV0_F1__MASK                             0x80000000L
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP5
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP5__STRAP_SUBSYS_VEN_ID_DEV0_F1__MASK                              0x0000FFFFL
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6__STRAP_APER0_EN_DEV0_F1__MASK                                   0x00000001L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV0_F1__MASK                      0x00000002L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6__STRAP_APER0_64BAR_EN_DEV0_F1__MASK                             0x00000004L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6__STRAP_APER0_AP_SIZE_DEV0_F1__MASK                              0x00000070L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6__STRAP_APER1_EN_DEV0_F1__MASK                                   0x00000100L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6__STRAP_APER1_PREFETCHABLE_EN_DEV0_F1__MASK                      0x00000200L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6__STRAP_APER2_EN_DEV0_F1__MASK                                   0x00010000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6__STRAP_APER2_PREFETCHABLE_EN_DEV0_F1__MASK                      0x00020000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6__STRAP_APER3_EN_DEV0_F1__MASK                                   0x01000000L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP6__STRAP_APER3_PREFETCHABLE_EN_DEV0_F1__MASK                      0x02000000L
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP7
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP7__STRAP_ROM_APER_EN_DEV0_F1__MASK                                0x00000001L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP7__STRAP_ROM_APER_SIZE_DEV0_F1__MASK                              0x0000001EL
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP10
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP10__STRAP_APER1_RESIZE_EN_DEV0_F1__MASK                           0x00000001L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP10__STRAP_APER1_RESIZE_SUPPORT_DEV0_F1__MASK                      0x001FFFFEL
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP11
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP11__STRAP_APER2_RESIZE_EN_DEV0_F1__MASK                           0x00000001L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP11__STRAP_APER2_RESIZE_SUPPORT_DEV0_F1__MASK                      0x001FFFFEL
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP12
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP12__STRAP_APER3_RESIZE_EN_DEV0_F1__MASK                           0x00000001L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP12__STRAP_APER3_RESIZE_SUPPORT_DEV0_F1__MASK                      0x001FFFFEL
//RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP13
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP13__STRAP_CLASS_CODE_PIF_DEV0_F1__MASK                            0x000000FFL
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP13__STRAP_CLASS_CODE_SUB_DEV0_F1__MASK                            0x0000FF00L
#define RCCSTRAPRCCSTRAP_RCC_DEV0_EPF1_STRAP13__STRAP_CLASS_CODE_BASE_DEV0_F1__MASK                           0x00FF0000L
//RCC_DEV0_EPF2_STRAP0
#define RCC_DEV0_EPF2_STRAP0__STRAP_DEVICE_ID_DEV0_F2__MASK                                                   0x0000FFFFL
#define RCC_DEV0_EPF2_STRAP0__STRAP_MAJOR_REV_ID_DEV0_F2__MASK                                                0x000F0000L
#define RCC_DEV0_EPF2_STRAP0__STRAP_MINOR_REV_ID_DEV0_F2__MASK                                                0x00F00000L
#define RCC_DEV0_EPF2_STRAP0__STRAP_FUNC_EN_DEV0_F2__MASK                                                     0x10000000L
#define RCC_DEV0_EPF2_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV0_F2__MASK                                       0x20000000L
#define RCC_DEV0_EPF2_STRAP0__STRAP_D1_SUPPORT_DEV0_F2__MASK                                                  0x40000000L
#define RCC_DEV0_EPF2_STRAP0__STRAP_D2_SUPPORT_DEV0_F2__MASK                                                  0x80000000L
//RCC_DEV0_EPF2_STRAP2
#define RCC_DEV0_EPF2_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F2__MASK                                               0x00000080L
#define RCC_DEV0_EPF2_STRAP2__STRAP_RESIZE_BAR_EN_DEV0_F2__MASK                                               0x00000100L
#define RCC_DEV0_EPF2_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV0_F2__MASK                                      0x00004000L
#define RCC_DEV0_EPF2_STRAP2__STRAP_AER_EN_DEV0_F2__MASK                                                      0x00010000L
#define RCC_DEV0_EPF2_STRAP2__STRAP_ACS_EN_DEV0_F2__MASK                                                      0x00020000L
#define RCC_DEV0_EPF2_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV0_F2__MASK                                            0x00100000L
#define RCC_DEV0_EPF2_STRAP2__STRAP_DPA_EN_DEV0_F2__MASK                                                      0x00200000L
#define RCC_DEV0_EPF2_STRAP2__STRAP_VC_EN_DEV0_F2__MASK                                                       0x00800000L
#define RCC_DEV0_EPF2_STRAP2__STRAP_MSI_MULTI_CAP_DEV0_F2__MASK                                               0x07000000L
//RCC_DEV0_EPF2_STRAP3
#define RCC_DEV0_EPF2_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV0_F2__MASK                                  0x00000001L
#define RCC_DEV0_EPF2_STRAP3__STRAP_PWR_EN_DEV0_F2__MASK                                                      0x00000002L
#define RCC_DEV0_EPF2_STRAP3__STRAP_SUBSYS_ID_DEV0_F2__MASK                                                   0x0003FFFCL
#define RCC_DEV0_EPF2_STRAP3__STRAP_MSI_EN_DEV0_F2__MASK                                                      0x00040000L
#define RCC_DEV0_EPF2_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV0_F2__MASK                                          0x00080000L
#define RCC_DEV0_EPF2_STRAP3__STRAP_MSIX_EN_DEV0_F2__MASK                                                     0x00100000L
#define RCC_DEV0_EPF2_STRAP3__STRAP_PMC_DSI_DEV0_F2__MASK                                                     0x01000000L
#define RCC_DEV0_EPF2_STRAP3__STRAP_VENDOR_ID_BIT_DEV0_F2__MASK                                               0x02000000L
#define RCC_DEV0_EPF2_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV0_F2__MASK                                    0x04000000L
#define RCC_DEV0_EPF2_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV0_F2__MASK                                   0x08000000L
//RCC_DEV0_EPF2_STRAP4
#define RCC_DEV0_EPF2_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV0_F2__MASK                                             0x00100000L
#define RCC_DEV0_EPF2_STRAP4__STRAP_ATOMIC_EN_DEV0_F2__MASK                                                   0x00200000L
#define RCC_DEV0_EPF2_STRAP4__STRAP_FLR_EN_DEV0_F2__MASK                                                      0x00400000L
#define RCC_DEV0_EPF2_STRAP4__STRAP_PME_SUPPORT_DEV0_F2__MASK                                                 0x0F800000L
#define RCC_DEV0_EPF2_STRAP4__STRAP_INTERRUPT_PIN_DEV0_F2__MASK                                               0x70000000L
#define RCC_DEV0_EPF2_STRAP4__STRAP_AUXPWR_SUPPORT_DEV0_F2__MASK                                              0x80000000L
//RCC_DEV0_EPF2_STRAP5
#define RCC_DEV0_EPF2_STRAP5__STRAP_SUBSYS_VEN_ID_DEV0_F2__MASK                                               0x0000FFFFL
#define RCC_DEV0_EPF2_STRAP5__STRAP_SATAIDP_EN_DEV0_F2__MASK                                                  0x01000000L
//RCC_DEV0_EPF2_STRAP6
#define RCC_DEV0_EPF2_STRAP6__STRAP_APER0_EN_DEV0_F2__MASK                                                    0x00000001L
#define RCC_DEV0_EPF2_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV0_F2__MASK                                       0x00000002L
#define RCC_DEV0_EPF2_STRAP6__STRAP_APER0_AP_SIZE_DEV0_F2__MASK                                               0x00000070L
#define RCC_DEV0_EPF2_STRAP6__STRAP_APER1_EN_DEV0_F2__MASK                                                    0x00000100L
#define RCC_DEV0_EPF2_STRAP6__STRAP_APER1_PREFETCHABLE_EN_DEV0_F2__MASK                                       0x00000200L
//RCC_DEV0_EPF2_STRAP13
#define RCC_DEV0_EPF2_STRAP13__STRAP_CLASS_CODE_PIF_DEV0_F2__MASK                                             0x000000FFL
#define RCC_DEV0_EPF2_STRAP13__STRAP_CLASS_CODE_SUB_DEV0_F2__MASK                                             0x0000FF00L
#define RCC_DEV0_EPF2_STRAP13__STRAP_CLASS_CODE_BASE_DEV0_F2__MASK                                            0x00FF0000L
//RCC_DEV0_EPF3_STRAP0
#define RCC_DEV0_EPF3_STRAP0__STRAP_DEVICE_ID_DEV0_F3__MASK                                                   0x0000FFFFL
#define RCC_DEV0_EPF3_STRAP0__STRAP_MAJOR_REV_ID_DEV0_F3__MASK                                                0x000F0000L
#define RCC_DEV0_EPF3_STRAP0__STRAP_MINOR_REV_ID_DEV0_F3__MASK                                                0x00F00000L
#define RCC_DEV0_EPF3_STRAP0__STRAP_FUNC_EN_DEV0_F3__MASK                                                     0x10000000L
#define RCC_DEV0_EPF3_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV0_F3__MASK                                       0x20000000L
#define RCC_DEV0_EPF3_STRAP0__STRAP_D1_SUPPORT_DEV0_F3__MASK                                                  0x40000000L
#define RCC_DEV0_EPF3_STRAP0__STRAP_D2_SUPPORT_DEV0_F3__MASK                                                  0x80000000L
//RCC_DEV0_EPF3_STRAP2
#define RCC_DEV0_EPF3_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F3__MASK                                               0x00000080L
#define RCC_DEV0_EPF3_STRAP2__STRAP_RESIZE_BAR_EN_DEV0_F3__MASK                                               0x00000100L
#define RCC_DEV0_EPF3_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV0_F3__MASK                                      0x00004000L
#define RCC_DEV0_EPF3_STRAP2__STRAP_AER_EN_DEV0_F3__MASK                                                      0x00010000L
#define RCC_DEV0_EPF3_STRAP2__STRAP_ACS_EN_DEV0_F3__MASK                                                      0x00020000L
#define RCC_DEV0_EPF3_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV0_F3__MASK                                            0x00100000L
#define RCC_DEV0_EPF3_STRAP2__STRAP_DPA_EN_DEV0_F3__MASK                                                      0x00200000L
#define RCC_DEV0_EPF3_STRAP2__STRAP_VC_EN_DEV0_F3__MASK                                                       0x00800000L
#define RCC_DEV0_EPF3_STRAP2__STRAP_MSI_MULTI_CAP_DEV0_F3__MASK                                               0x07000000L
//RCC_DEV0_EPF3_STRAP3
#define RCC_DEV0_EPF3_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV0_F3__MASK                                  0x00000001L
#define RCC_DEV0_EPF3_STRAP3__STRAP_PWR_EN_DEV0_F3__MASK                                                      0x00000002L
#define RCC_DEV0_EPF3_STRAP3__STRAP_SUBSYS_ID_DEV0_F3__MASK                                                   0x0003FFFCL
#define RCC_DEV0_EPF3_STRAP3__STRAP_MSI_EN_DEV0_F3__MASK                                                      0x00040000L
#define RCC_DEV0_EPF3_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV0_F3__MASK                                          0x00080000L
#define RCC_DEV0_EPF3_STRAP3__STRAP_MSIX_EN_DEV0_F3__MASK                                                     0x00100000L
#define RCC_DEV0_EPF3_STRAP3__STRAP_PMC_DSI_DEV0_F3__MASK                                                     0x01000000L
#define RCC_DEV0_EPF3_STRAP3__STRAP_VENDOR_ID_BIT_DEV0_F3__MASK                                               0x02000000L
#define RCC_DEV0_EPF3_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV0_F3__MASK                                    0x04000000L
#define RCC_DEV0_EPF3_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV0_F3__MASK                                   0x08000000L
//RCC_DEV0_EPF3_STRAP4
#define RCC_DEV0_EPF3_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV0_F3__MASK                                             0x00100000L
#define RCC_DEV0_EPF3_STRAP4__STRAP_ATOMIC_EN_DEV0_F3__MASK                                                   0x00200000L
#define RCC_DEV0_EPF3_STRAP4__STRAP_FLR_EN_DEV0_F3__MASK                                                      0x00400000L
#define RCC_DEV0_EPF3_STRAP4__STRAP_PME_SUPPORT_DEV0_F3__MASK                                                 0x0F800000L
#define RCC_DEV0_EPF3_STRAP4__STRAP_INTERRUPT_PIN_DEV0_F3__MASK                                               0x70000000L
#define RCC_DEV0_EPF3_STRAP4__STRAP_AUXPWR_SUPPORT_DEV0_F3__MASK                                              0x80000000L
//RCC_DEV0_EPF3_STRAP5
#define RCC_DEV0_EPF3_STRAP5__STRAP_SUBSYS_VEN_ID_DEV0_F3__MASK                                               0x0000FFFFL
#define RCC_DEV0_EPF3_STRAP5__STRAP_USB_DBESEL_DEV0_F3__MASK                                                  0x000F0000L
#define RCC_DEV0_EPF3_STRAP5__STRAP_USB_DBESELD_DEV0_F3__MASK                                                 0x00F00000L
//RCC_DEV0_EPF3_STRAP6
#define RCC_DEV0_EPF3_STRAP6__STRAP_APER0_EN_DEV0_F3__MASK                                                    0x00000001L
#define RCC_DEV0_EPF3_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV0_F3__MASK                                       0x00000002L
#define RCC_DEV0_EPF3_STRAP6__STRAP_APER0_AP_SIZE_DEV0_F3__MASK                                               0x00000070L
//RCC_DEV0_EPF3_STRAP13
#define RCC_DEV0_EPF3_STRAP13__STRAP_CLASS_CODE_PIF_DEV0_F3__MASK                                             0x000000FFL
#define RCC_DEV0_EPF3_STRAP13__STRAP_CLASS_CODE_SUB_DEV0_F3__MASK                                             0x0000FF00L
#define RCC_DEV0_EPF3_STRAP13__STRAP_CLASS_CODE_BASE_DEV0_F3__MASK                                            0x00FF0000L
//RCC_DEV0_EPF4_STRAP0
#define RCC_DEV0_EPF4_STRAP0__STRAP_DEVICE_ID_DEV0_F4__MASK                                                   0x0000FFFFL
#define RCC_DEV0_EPF4_STRAP0__STRAP_MAJOR_REV_ID_DEV0_F4__MASK                                                0x000F0000L
#define RCC_DEV0_EPF4_STRAP0__STRAP_MINOR_REV_ID_DEV0_F4__MASK                                                0x00F00000L
#define RCC_DEV0_EPF4_STRAP0__STRAP_FUNC_EN_DEV0_F4__MASK                                                     0x10000000L
#define RCC_DEV0_EPF4_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV0_F4__MASK                                       0x20000000L
#define RCC_DEV0_EPF4_STRAP0__STRAP_D1_SUPPORT_DEV0_F4__MASK                                                  0x40000000L
#define RCC_DEV0_EPF4_STRAP0__STRAP_D2_SUPPORT_DEV0_F4__MASK                                                  0x80000000L
//RCC_DEV0_EPF4_STRAP2
#define RCC_DEV0_EPF4_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F4__MASK                                               0x00000080L
#define RCC_DEV0_EPF4_STRAP2__STRAP_RESIZE_BAR_EN_DEV0_F4__MASK                                               0x00000100L
#define RCC_DEV0_EPF4_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV0_F4__MASK                                      0x00004000L
#define RCC_DEV0_EPF4_STRAP2__STRAP_AER_EN_DEV0_F4__MASK                                                      0x00010000L
#define RCC_DEV0_EPF4_STRAP2__STRAP_ACS_EN_DEV0_F4__MASK                                                      0x00020000L
#define RCC_DEV0_EPF4_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV0_F4__MASK                                            0x00100000L
#define RCC_DEV0_EPF4_STRAP2__STRAP_DPA_EN_DEV0_F4__MASK                                                      0x00200000L
#define RCC_DEV0_EPF4_STRAP2__STRAP_VC_EN_DEV0_F4__MASK                                                       0x00800000L
#define RCC_DEV0_EPF4_STRAP2__STRAP_MSI_MULTI_CAP_DEV0_F4__MASK                                               0x07000000L
//RCC_DEV0_EPF4_STRAP3
#define RCC_DEV0_EPF4_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV0_F4__MASK                                  0x00000001L
#define RCC_DEV0_EPF4_STRAP3__STRAP_PWR_EN_DEV0_F4__MASK                                                      0x00000002L
#define RCC_DEV0_EPF4_STRAP3__STRAP_SUBSYS_ID_DEV0_F4__MASK                                                   0x0003FFFCL
#define RCC_DEV0_EPF4_STRAP3__STRAP_MSI_EN_DEV0_F4__MASK                                                      0x00040000L
#define RCC_DEV0_EPF4_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV0_F4__MASK                                          0x00080000L
#define RCC_DEV0_EPF4_STRAP3__STRAP_MSIX_EN_DEV0_F4__MASK                                                     0x00100000L
#define RCC_DEV0_EPF4_STRAP3__STRAP_PMC_DSI_DEV0_F4__MASK                                                     0x01000000L
#define RCC_DEV0_EPF4_STRAP3__STRAP_VENDOR_ID_BIT_DEV0_F4__MASK                                               0x02000000L
#define RCC_DEV0_EPF4_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV0_F4__MASK                                    0x04000000L
#define RCC_DEV0_EPF4_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV0_F4__MASK                                   0x08000000L
//RCC_DEV0_EPF4_STRAP4
#define RCC_DEV0_EPF4_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV0_F4__MASK                                             0x00100000L
#define RCC_DEV0_EPF4_STRAP4__STRAP_ATOMIC_EN_DEV0_F4__MASK                                                   0x00200000L
#define RCC_DEV0_EPF4_STRAP4__STRAP_FLR_EN_DEV0_F4__MASK                                                      0x00400000L
#define RCC_DEV0_EPF4_STRAP4__STRAP_PME_SUPPORT_DEV0_F4__MASK                                                 0x0F800000L
#define RCC_DEV0_EPF4_STRAP4__STRAP_INTERRUPT_PIN_DEV0_F4__MASK                                               0x70000000L
#define RCC_DEV0_EPF4_STRAP4__STRAP_AUXPWR_SUPPORT_DEV0_F4__MASK                                              0x80000000L
//RCC_DEV0_EPF4_STRAP5
#define RCC_DEV0_EPF4_STRAP5__STRAP_SUBSYS_VEN_ID_DEV0_F4__MASK                                               0x0000FFFFL
#define RCC_DEV0_EPF4_STRAP5__STRAP_USB_DBESEL_DEV0_F4__MASK                                                  0x000F0000L
#define RCC_DEV0_EPF4_STRAP5__STRAP_USB_DBESELD_DEV0_F4__MASK                                                 0x00F00000L
//RCC_DEV0_EPF4_STRAP6
#define RCC_DEV0_EPF4_STRAP6__STRAP_APER0_EN_DEV0_F4__MASK                                                    0x00000001L
#define RCC_DEV0_EPF4_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV0_F4__MASK                                       0x00000002L
#define RCC_DEV0_EPF4_STRAP6__STRAP_APER0_AP_SIZE_DEV0_F4__MASK                                               0x00000070L
#define RCC_DEV0_EPF4_STRAP6__STRAP_APER1_EN_DEV0_F4__MASK                                                    0x00000100L
#define RCC_DEV0_EPF4_STRAP6__STRAP_APER1_PREFETCHABLE_EN_DEV0_F4__MASK                                       0x00000200L
#define RCC_DEV0_EPF4_STRAP6__STRAP_APER2_EN_DEV0_F4__MASK                                                    0x00010000L
#define RCC_DEV0_EPF4_STRAP6__STRAP_APER2_PREFETCHABLE_EN_DEV0_F4__MASK                                       0x00020000L
//RCC_DEV0_EPF4_STRAP13
#define RCC_DEV0_EPF4_STRAP13__STRAP_CLASS_CODE_PIF_DEV0_F4__MASK                                             0x000000FFL
#define RCC_DEV0_EPF4_STRAP13__STRAP_CLASS_CODE_SUB_DEV0_F4__MASK                                             0x0000FF00L
#define RCC_DEV0_EPF4_STRAP13__STRAP_CLASS_CODE_BASE_DEV0_F4__MASK                                            0x00FF0000L
//RCC_DEV0_EPF5_STRAP0
#define RCC_DEV0_EPF5_STRAP0__STRAP_DEVICE_ID_DEV0_F5__MASK                                                   0x0000FFFFL
#define RCC_DEV0_EPF5_STRAP0__STRAP_MAJOR_REV_ID_DEV0_F5__MASK                                                0x000F0000L
#define RCC_DEV0_EPF5_STRAP0__STRAP_MINOR_REV_ID_DEV0_F5__MASK                                                0x00F00000L
#define RCC_DEV0_EPF5_STRAP0__STRAP_FUNC_EN_DEV0_F5__MASK                                                     0x10000000L
#define RCC_DEV0_EPF5_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV0_F5__MASK                                       0x20000000L
#define RCC_DEV0_EPF5_STRAP0__STRAP_D1_SUPPORT_DEV0_F5__MASK                                                  0x40000000L
#define RCC_DEV0_EPF5_STRAP0__STRAP_D2_SUPPORT_DEV0_F5__MASK                                                  0x80000000L
//RCC_DEV0_EPF5_STRAP2
#define RCC_DEV0_EPF5_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F5__MASK                                               0x00000080L
#define RCC_DEV0_EPF5_STRAP2__STRAP_RESIZE_BAR_EN_DEV0_F5__MASK                                               0x00000100L
#define RCC_DEV0_EPF5_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV0_F5__MASK                                      0x00004000L
#define RCC_DEV0_EPF5_STRAP2__STRAP_AER_EN_DEV0_F5__MASK                                                      0x00010000L
#define RCC_DEV0_EPF5_STRAP2__STRAP_ACS_EN_DEV0_F5__MASK                                                      0x00020000L
#define RCC_DEV0_EPF5_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV0_F5__MASK                                            0x00100000L
#define RCC_DEV0_EPF5_STRAP2__STRAP_DPA_EN_DEV0_F5__MASK                                                      0x00200000L
#define RCC_DEV0_EPF5_STRAP2__STRAP_VC_EN_DEV0_F5__MASK                                                       0x00800000L
#define RCC_DEV0_EPF5_STRAP2__STRAP_MSI_MULTI_CAP_DEV0_F5__MASK                                               0x07000000L
//RCC_DEV0_EPF5_STRAP3
#define RCC_DEV0_EPF5_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV0_F5__MASK                                  0x00000001L
#define RCC_DEV0_EPF5_STRAP3__STRAP_PWR_EN_DEV0_F5__MASK                                                      0x00000002L
#define RCC_DEV0_EPF5_STRAP3__STRAP_SUBSYS_ID_DEV0_F5__MASK                                                   0x0003FFFCL
#define RCC_DEV0_EPF5_STRAP3__STRAP_MSI_EN_DEV0_F5__MASK                                                      0x00040000L
#define RCC_DEV0_EPF5_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV0_F5__MASK                                          0x00080000L
#define RCC_DEV0_EPF5_STRAP3__STRAP_MSIX_EN_DEV0_F5__MASK                                                     0x00100000L
#define RCC_DEV0_EPF5_STRAP3__STRAP_PMC_DSI_DEV0_F5__MASK                                                     0x01000000L
#define RCC_DEV0_EPF5_STRAP3__STRAP_VENDOR_ID_BIT_DEV0_F5__MASK                                               0x02000000L
#define RCC_DEV0_EPF5_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV0_F5__MASK                                    0x04000000L
#define RCC_DEV0_EPF5_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV0_F5__MASK                                   0x08000000L
//RCC_DEV0_EPF5_STRAP4
#define RCC_DEV0_EPF5_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV0_F5__MASK                                             0x00100000L
#define RCC_DEV0_EPF5_STRAP4__STRAP_ATOMIC_EN_DEV0_F5__MASK                                                   0x00200000L
#define RCC_DEV0_EPF5_STRAP4__STRAP_FLR_EN_DEV0_F5__MASK                                                      0x00400000L
#define RCC_DEV0_EPF5_STRAP4__STRAP_PME_SUPPORT_DEV0_F5__MASK                                                 0x0F800000L
#define RCC_DEV0_EPF5_STRAP4__STRAP_INTERRUPT_PIN_DEV0_F5__MASK                                               0x70000000L
#define RCC_DEV0_EPF5_STRAP4__STRAP_AUXPWR_SUPPORT_DEV0_F5__MASK                                              0x80000000L
//RCC_DEV0_EPF5_STRAP5
#define RCC_DEV0_EPF5_STRAP5__STRAP_SUBSYS_VEN_ID_DEV0_F5__MASK                                               0x0000FFFFL
//RCC_DEV0_EPF5_STRAP6
#define RCC_DEV0_EPF5_STRAP6__STRAP_APER0_EN_DEV0_F5__MASK                                                    0x00000001L
#define RCC_DEV0_EPF5_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV0_F5__MASK                                       0x00000002L
#define RCC_DEV0_EPF5_STRAP6__STRAP_APER0_AP_SIZE_DEV0_F5__MASK                                               0x00000070L
#define RCC_DEV0_EPF5_STRAP6__STRAP_APER1_EN_DEV0_F5__MASK                                                    0x00000100L
#define RCC_DEV0_EPF5_STRAP6__STRAP_APER1_PREFETCHABLE_EN_DEV0_F5__MASK                                       0x00000200L
#define RCC_DEV0_EPF5_STRAP6__STRAP_APER2_EN_DEV0_F5__MASK                                                    0x00010000L
#define RCC_DEV0_EPF5_STRAP6__STRAP_APER2_PREFETCHABLE_EN_DEV0_F5__MASK                                       0x00020000L
//RCC_DEV0_EPF5_STRAP13
#define RCC_DEV0_EPF5_STRAP13__STRAP_CLASS_CODE_PIF_DEV0_F5__MASK                                             0x000000FFL
#define RCC_DEV0_EPF5_STRAP13__STRAP_CLASS_CODE_SUB_DEV0_F5__MASK                                             0x0000FF00L
#define RCC_DEV0_EPF5_STRAP13__STRAP_CLASS_CODE_BASE_DEV0_F5__MASK                                            0x00FF0000L
//RCC_DEV0_EPF6_STRAP0
#define RCC_DEV0_EPF6_STRAP0__STRAP_DEVICE_ID_DEV0_F6__MASK                                                   0x0000FFFFL
#define RCC_DEV0_EPF6_STRAP0__STRAP_MAJOR_REV_ID_DEV0_F6__MASK                                                0x000F0000L
#define RCC_DEV0_EPF6_STRAP0__STRAP_MINOR_REV_ID_DEV0_F6__MASK                                                0x00F00000L
#define RCC_DEV0_EPF6_STRAP0__STRAP_FUNC_EN_DEV0_F6__MASK                                                     0x10000000L
#define RCC_DEV0_EPF6_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV0_F6__MASK                                       0x20000000L
#define RCC_DEV0_EPF6_STRAP0__STRAP_D1_SUPPORT_DEV0_F6__MASK                                                  0x40000000L
#define RCC_DEV0_EPF6_STRAP0__STRAP_D2_SUPPORT_DEV0_F6__MASK                                                  0x80000000L
//RCC_DEV0_EPF6_STRAP2
#define RCC_DEV0_EPF6_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F6__MASK                                               0x00000080L
#define RCC_DEV0_EPF6_STRAP2__STRAP_RESIZE_BAR_EN_DEV0_F6__MASK                                               0x00000100L
#define RCC_DEV0_EPF6_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV0_F6__MASK                                      0x00004000L
#define RCC_DEV0_EPF6_STRAP2__STRAP_AER_EN_DEV0_F6__MASK                                                      0x00010000L
#define RCC_DEV0_EPF6_STRAP2__STRAP_ACS_EN_DEV0_F6__MASK                                                      0x00020000L
#define RCC_DEV0_EPF6_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV0_F6__MASK                                            0x00100000L
#define RCC_DEV0_EPF6_STRAP2__STRAP_DPA_EN_DEV0_F6__MASK                                                      0x00200000L
#define RCC_DEV0_EPF6_STRAP2__STRAP_VC_EN_DEV0_F6__MASK                                                       0x00800000L
#define RCC_DEV0_EPF6_STRAP2__STRAP_MSI_MULTI_CAP_DEV0_F6__MASK                                               0x07000000L
//RCC_DEV0_EPF6_STRAP3
#define RCC_DEV0_EPF6_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV0_F6__MASK                                  0x00000001L
#define RCC_DEV0_EPF6_STRAP3__STRAP_PWR_EN_DEV0_F6__MASK                                                      0x00000002L
#define RCC_DEV0_EPF6_STRAP3__STRAP_SUBSYS_ID_DEV0_F6__MASK                                                   0x0003FFFCL
#define RCC_DEV0_EPF6_STRAP3__STRAP_MSI_EN_DEV0_F6__MASK                                                      0x00040000L
#define RCC_DEV0_EPF6_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV0_F6__MASK                                          0x00080000L
#define RCC_DEV0_EPF6_STRAP3__STRAP_MSIX_EN_DEV0_F6__MASK                                                     0x00100000L
#define RCC_DEV0_EPF6_STRAP3__STRAP_PMC_DSI_DEV0_F6__MASK                                                     0x01000000L
#define RCC_DEV0_EPF6_STRAP3__STRAP_VENDOR_ID_BIT_DEV0_F6__MASK                                               0x02000000L
#define RCC_DEV0_EPF6_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV0_F6__MASK                                    0x04000000L
#define RCC_DEV0_EPF6_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV0_F6__MASK                                   0x08000000L
//RCC_DEV0_EPF6_STRAP4
#define RCC_DEV0_EPF6_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV0_F6__MASK                                             0x00100000L
#define RCC_DEV0_EPF6_STRAP4__STRAP_ATOMIC_EN_DEV0_F6__MASK                                                   0x00200000L
#define RCC_DEV0_EPF6_STRAP4__STRAP_FLR_EN_DEV0_F6__MASK                                                      0x00400000L
#define RCC_DEV0_EPF6_STRAP4__STRAP_PME_SUPPORT_DEV0_F6__MASK                                                 0x0F800000L
#define RCC_DEV0_EPF6_STRAP4__STRAP_INTERRUPT_PIN_DEV0_F6__MASK                                               0x70000000L
#define RCC_DEV0_EPF6_STRAP4__STRAP_AUXPWR_SUPPORT_DEV0_F6__MASK                                              0x80000000L
//RCC_DEV0_EPF6_STRAP5
#define RCC_DEV0_EPF6_STRAP5__STRAP_SUBSYS_VEN_ID_DEV0_F6__MASK                                               0x0000FFFFL
//RCC_DEV0_EPF6_STRAP6
#define RCC_DEV0_EPF6_STRAP6__STRAP_APER0_EN_DEV0_F6__MASK                                                    0x00000001L
#define RCC_DEV0_EPF6_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV0_F6__MASK                                       0x00000002L
#define RCC_DEV0_EPF6_STRAP6__STRAP_APER0_AP_SIZE_DEV0_F6__MASK                                               0x00000070L
#define RCC_DEV0_EPF6_STRAP6__STRAP_APER1_EN_DEV0_F6__MASK                                                    0x00000100L
#define RCC_DEV0_EPF6_STRAP6__STRAP_APER1_PREFETCHABLE_EN_DEV0_F6__MASK                                       0x00000200L
#define RCC_DEV0_EPF6_STRAP6__STRAP_APER2_EN_DEV0_F6__MASK                                                    0x00010000L
#define RCC_DEV0_EPF6_STRAP6__STRAP_APER2_PREFETCHABLE_EN_DEV0_F6__MASK                                       0x00020000L
//RCC_DEV0_EPF6_STRAP13
#define RCC_DEV0_EPF6_STRAP13__STRAP_CLASS_CODE_PIF_DEV0_F6__MASK                                             0x000000FFL
#define RCC_DEV0_EPF6_STRAP13__STRAP_CLASS_CODE_SUB_DEV0_F6__MASK                                             0x0000FF00L
#define RCC_DEV0_EPF6_STRAP13__STRAP_CLASS_CODE_BASE_DEV0_F6__MASK                                            0x00FF0000L
//RCC_DEV0_EPF7_STRAP0
#define RCC_DEV0_EPF7_STRAP0__STRAP_DEVICE_ID_DEV0_F7__MASK                                                   0x0000FFFFL
#define RCC_DEV0_EPF7_STRAP0__STRAP_MAJOR_REV_ID_DEV0_F7__MASK                                                0x000F0000L
#define RCC_DEV0_EPF7_STRAP0__STRAP_MINOR_REV_ID_DEV0_F7__MASK                                                0x00F00000L
#define RCC_DEV0_EPF7_STRAP0__STRAP_FUNC_EN_DEV0_F7__MASK                                                     0x10000000L
#define RCC_DEV0_EPF7_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV0_F7__MASK                                       0x20000000L
#define RCC_DEV0_EPF7_STRAP0__STRAP_D1_SUPPORT_DEV0_F7__MASK                                                  0x40000000L
#define RCC_DEV0_EPF7_STRAP0__STRAP_D2_SUPPORT_DEV0_F7__MASK                                                  0x80000000L
//RCC_DEV0_EPF7_STRAP2
#define RCC_DEV0_EPF7_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F7__MASK                                               0x00000080L
#define RCC_DEV0_EPF7_STRAP2__STRAP_RESIZE_BAR_EN_DEV0_F7__MASK                                               0x00000100L
#define RCC_DEV0_EPF7_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV0_F7__MASK                                      0x00004000L
#define RCC_DEV0_EPF7_STRAP2__STRAP_AER_EN_DEV0_F7__MASK                                                      0x00010000L
#define RCC_DEV0_EPF7_STRAP2__STRAP_ACS_EN_DEV0_F7__MASK                                                      0x00020000L
#define RCC_DEV0_EPF7_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV0_F7__MASK                                            0x00100000L
#define RCC_DEV0_EPF7_STRAP2__STRAP_DPA_EN_DEV0_F7__MASK                                                      0x00200000L
#define RCC_DEV0_EPF7_STRAP2__STRAP_VC_EN_DEV0_F7__MASK                                                       0x00800000L
#define RCC_DEV0_EPF7_STRAP2__STRAP_MSI_MULTI_CAP_DEV0_F7__MASK                                               0x07000000L
//RCC_DEV0_EPF7_STRAP3
#define RCC_DEV0_EPF7_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV0_F7__MASK                                  0x00000001L
#define RCC_DEV0_EPF7_STRAP3__STRAP_PWR_EN_DEV0_F7__MASK                                                      0x00000002L
#define RCC_DEV0_EPF7_STRAP3__STRAP_SUBSYS_ID_DEV0_F7__MASK                                                   0x0003FFFCL
#define RCC_DEV0_EPF7_STRAP3__STRAP_MSI_EN_DEV0_F7__MASK                                                      0x00040000L
#define RCC_DEV0_EPF7_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV0_F7__MASK                                          0x00080000L
#define RCC_DEV0_EPF7_STRAP3__STRAP_MSIX_EN_DEV0_F7__MASK                                                     0x00100000L
#define RCC_DEV0_EPF7_STRAP3__STRAP_PMC_DSI_DEV0_F7__MASK                                                     0x01000000L
#define RCC_DEV0_EPF7_STRAP3__STRAP_VENDOR_ID_BIT_DEV0_F7__MASK                                               0x02000000L
#define RCC_DEV0_EPF7_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV0_F7__MASK                                    0x04000000L
#define RCC_DEV0_EPF7_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV0_F7__MASK                                   0x08000000L
//RCC_DEV0_EPF7_STRAP4
#define RCC_DEV0_EPF7_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV0_F7__MASK                                             0x00100000L
#define RCC_DEV0_EPF7_STRAP4__STRAP_ATOMIC_EN_DEV0_F7__MASK                                                   0x00200000L
#define RCC_DEV0_EPF7_STRAP4__STRAP_FLR_EN_DEV0_F7__MASK                                                      0x00400000L
#define RCC_DEV0_EPF7_STRAP4__STRAP_PME_SUPPORT_DEV0_F7__MASK                                                 0x0F800000L
#define RCC_DEV0_EPF7_STRAP4__STRAP_INTERRUPT_PIN_DEV0_F7__MASK                                               0x70000000L
#define RCC_DEV0_EPF7_STRAP4__STRAP_AUXPWR_SUPPORT_DEV0_F7__MASK                                              0x80000000L
//RCC_DEV0_EPF7_STRAP5
#define RCC_DEV0_EPF7_STRAP5__STRAP_SUBSYS_VEN_ID_DEV0_F7__MASK                                               0x0000FFFFL
//RCC_DEV0_EPF7_STRAP6
#define RCC_DEV0_EPF7_STRAP6__STRAP_APER0_EN_DEV0_F7__MASK                                                    0x00000001L
#define RCC_DEV0_EPF7_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV0_F7__MASK                                       0x00000002L
#define RCC_DEV0_EPF7_STRAP6__STRAP_APER0_AP_SIZE_DEV0_F7__MASK                                               0x00000070L
#define RCC_DEV0_EPF7_STRAP6__STRAP_APER1_EN_DEV0_F7__MASK                                                    0x00000100L
#define RCC_DEV0_EPF7_STRAP6__STRAP_APER1_PREFETCHABLE_EN_DEV0_F7__MASK                                       0x00000200L
#define RCC_DEV0_EPF7_STRAP6__STRAP_APER2_EN_DEV0_F7__MASK                                                    0x00010000L
#define RCC_DEV0_EPF7_STRAP6__STRAP_APER2_PREFETCHABLE_EN_DEV0_F7__MASK                                       0x00020000L
//RCC_DEV0_EPF7_STRAP13
#define RCC_DEV0_EPF7_STRAP13__STRAP_CLASS_CODE_PIF_DEV0_F7__MASK                                             0x000000FFL
#define RCC_DEV0_EPF7_STRAP13__STRAP_CLASS_CODE_SUB_DEV0_F7__MASK                                             0x0000FF00L
#define RCC_DEV0_EPF7_STRAP13__STRAP_CLASS_CODE_BASE_DEV0_F7__MASK                                            0x00FF0000L
//RCC_DEV1_EPF0_STRAP0
#define RCC_DEV1_EPF0_STRAP0__STRAP_DEVICE_ID_DEV1_F0__MASK                                                   0x0000FFFFL
#define RCC_DEV1_EPF0_STRAP0__STRAP_MAJOR_REV_ID_DEV1_F0__MASK                                                0x000F0000L
#define RCC_DEV1_EPF0_STRAP0__STRAP_MINOR_REV_ID_DEV1_F0__MASK                                                0x00F00000L
#define RCC_DEV1_EPF0_STRAP0__STRAP_FUNC_EN_DEV1_F0__MASK                                                     0x10000000L
#define RCC_DEV1_EPF0_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV1_F0__MASK                                       0x20000000L
#define RCC_DEV1_EPF0_STRAP0__STRAP_D1_SUPPORT_DEV1_F0__MASK                                                  0x40000000L
#define RCC_DEV1_EPF0_STRAP0__STRAP_D2_SUPPORT_DEV1_F0__MASK                                                  0x80000000L
//RCC_DEV1_EPF0_STRAP2
#define RCC_DEV1_EPF0_STRAP2__STRAP_NO_SOFT_RESET_DEV1_F0__MASK                                               0x00000080L
#define RCC_DEV1_EPF0_STRAP2__STRAP_RESIZE_BAR_EN_DEV1_F0__MASK                                               0x00000100L
#define RCC_DEV1_EPF0_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV1_F0__MASK                                      0x00004000L
#define RCC_DEV1_EPF0_STRAP2__STRAP_ARI_EN_DEV1_F0__MASK                                                      0x00008000L
#define RCC_DEV1_EPF0_STRAP2__STRAP_AER_EN_DEV1_F0__MASK                                                      0x00010000L
#define RCC_DEV1_EPF0_STRAP2__STRAP_ACS_EN_DEV1_F0__MASK                                                      0x00020000L
#define RCC_DEV1_EPF0_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV1_F0__MASK                                            0x00100000L
#define RCC_DEV1_EPF0_STRAP2__STRAP_DPA_EN_DEV1_F0__MASK                                                      0x00200000L
#define RCC_DEV1_EPF0_STRAP2__STRAP_VC_EN_DEV1_F0__MASK                                                       0x00800000L
#define RCC_DEV1_EPF0_STRAP2__STRAP_MSI_MULTI_CAP_DEV1_F0__MASK                                               0x07000000L
//RCC_DEV1_EPF0_STRAP3
#define RCC_DEV1_EPF0_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV1_F0__MASK                                  0x00000001L
#define RCC_DEV1_EPF0_STRAP3__STRAP_PWR_EN_DEV1_F0__MASK                                                      0x00000002L
#define RCC_DEV1_EPF0_STRAP3__STRAP_SUBSYS_ID_DEV1_F0__MASK                                                   0x0003FFFCL
#define RCC_DEV1_EPF0_STRAP3__STRAP_MSI_EN_DEV1_F0__MASK                                                      0x00040000L
#define RCC_DEV1_EPF0_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV1_F0__MASK                                          0x00080000L
#define RCC_DEV1_EPF0_STRAP3__STRAP_MSIX_EN_DEV1_F0__MASK                                                     0x00100000L
#define RCC_DEV1_EPF0_STRAP3__STRAP_PMC_DSI_DEV1_F0__MASK                                                     0x01000000L
#define RCC_DEV1_EPF0_STRAP3__STRAP_VENDOR_ID_BIT_DEV1_F0__MASK                                               0x02000000L
#define RCC_DEV1_EPF0_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV1_F0__MASK                                    0x04000000L
#define RCC_DEV1_EPF0_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV1_F0__MASK                                   0x08000000L
//RCC_DEV1_EPF0_STRAP4
#define RCC_DEV1_EPF0_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV1_F0__MASK                                             0x00100000L
#define RCC_DEV1_EPF0_STRAP4__STRAP_ATOMIC_EN_DEV1_F0__MASK                                                   0x00200000L
#define RCC_DEV1_EPF0_STRAP4__STRAP_FLR_EN_DEV1_F0__MASK                                                      0x00400000L
#define RCC_DEV1_EPF0_STRAP4__STRAP_PME_SUPPORT_DEV1_F0__MASK                                                 0x0F800000L
#define RCC_DEV1_EPF0_STRAP4__STRAP_INTERRUPT_PIN_DEV1_F0__MASK                                               0x70000000L
#define RCC_DEV1_EPF0_STRAP4__STRAP_AUXPWR_SUPPORT_DEV1_F0__MASK                                              0x80000000L
//RCC_DEV1_EPF0_STRAP5
#define RCC_DEV1_EPF0_STRAP5__STRAP_SUBSYS_VEN_ID_DEV1_F0__MASK                                               0x0000FFFFL
#define RCC_DEV1_EPF0_STRAP5__STRAP_SATAIDP_EN_DEV1_F0__MASK                                                  0x01000000L
//RCC_DEV1_EPF0_STRAP6
#define RCC_DEV1_EPF0_STRAP6__STRAP_APER0_EN_DEV1_F0__MASK                                                    0x00000001L
#define RCC_DEV1_EPF0_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV1_F0__MASK                                       0x00000002L
#define RCC_DEV1_EPF0_STRAP6__STRAP_APER0_AP_SIZE_DEV1_F0__MASK                                               0x00000070L
//RCC_DEV1_EPF0_STRAP13
#define RCC_DEV1_EPF0_STRAP13__STRAP_CLASS_CODE_PIF_DEV1_F0__MASK                                             0x000000FFL
#define RCC_DEV1_EPF0_STRAP13__STRAP_CLASS_CODE_SUB_DEV1_F0__MASK                                             0x0000FF00L
#define RCC_DEV1_EPF0_STRAP13__STRAP_CLASS_CODE_BASE_DEV1_F0__MASK                                            0x00FF0000L
//RCC_DEV1_EPF1_STRAP0
#define RCC_DEV1_EPF1_STRAP0__STRAP_DEVICE_ID_DEV1_F1__MASK                                                   0x0000FFFFL
#define RCC_DEV1_EPF1_STRAP0__STRAP_MAJOR_REV_ID_DEV1_F1__MASK                                                0x000F0000L
#define RCC_DEV1_EPF1_STRAP0__STRAP_MINOR_REV_ID_DEV1_F1__MASK                                                0x00F00000L
#define RCC_DEV1_EPF1_STRAP0__STRAP_FUNC_EN_DEV1_F1__MASK                                                     0x10000000L
#define RCC_DEV1_EPF1_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV1_F1__MASK                                       0x20000000L
#define RCC_DEV1_EPF1_STRAP0__STRAP_D1_SUPPORT_DEV1_F1__MASK                                                  0x40000000L
#define RCC_DEV1_EPF1_STRAP0__STRAP_D2_SUPPORT_DEV1_F1__MASK                                                  0x80000000L
//RCC_DEV1_EPF1_STRAP2
#define RCC_DEV1_EPF1_STRAP2__STRAP_NO_SOFT_RESET_DEV1_F1__MASK                                               0x00000080L
#define RCC_DEV1_EPF1_STRAP2__STRAP_RESIZE_BAR_EN_DEV1_F1__MASK                                               0x00000100L
#define RCC_DEV1_EPF1_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV1_F1__MASK                                      0x00004000L
#define RCC_DEV1_EPF1_STRAP2__STRAP_AER_EN_DEV1_F1__MASK                                                      0x00010000L
#define RCC_DEV1_EPF1_STRAP2__STRAP_ACS_EN_DEV1_F1__MASK                                                      0x00020000L
#define RCC_DEV1_EPF1_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV1_F1__MASK                                            0x00100000L
#define RCC_DEV1_EPF1_STRAP2__STRAP_DPA_EN_DEV1_F1__MASK                                                      0x00200000L
#define RCC_DEV1_EPF1_STRAP2__STRAP_VC_EN_DEV1_F1__MASK                                                       0x00800000L
#define RCC_DEV1_EPF1_STRAP2__STRAP_MSI_MULTI_CAP_DEV1_F1__MASK                                               0x07000000L
//RCC_DEV1_EPF1_STRAP3
#define RCC_DEV1_EPF1_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV1_F1__MASK                                  0x00000001L
#define RCC_DEV1_EPF1_STRAP3__STRAP_PWR_EN_DEV1_F1__MASK                                                      0x00000002L
#define RCC_DEV1_EPF1_STRAP3__STRAP_SUBSYS_ID_DEV1_F1__MASK                                                   0x0003FFFCL
#define RCC_DEV1_EPF1_STRAP3__STRAP_MSI_EN_DEV1_F1__MASK                                                      0x00040000L
#define RCC_DEV1_EPF1_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV1_F1__MASK                                          0x00080000L
#define RCC_DEV1_EPF1_STRAP3__STRAP_MSIX_EN_DEV1_F1__MASK                                                     0x00100000L
#define RCC_DEV1_EPF1_STRAP3__STRAP_PMC_DSI_DEV1_F1__MASK                                                     0x01000000L
#define RCC_DEV1_EPF1_STRAP3__STRAP_VENDOR_ID_BIT_DEV1_F1__MASK                                               0x02000000L
#define RCC_DEV1_EPF1_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV1_F1__MASK                                    0x04000000L
#define RCC_DEV1_EPF1_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV1_F1__MASK                                   0x08000000L
//RCC_DEV1_EPF1_STRAP4
#define RCC_DEV1_EPF1_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV1_F1__MASK                                             0x00100000L
#define RCC_DEV1_EPF1_STRAP4__STRAP_ATOMIC_EN_DEV1_F1__MASK                                                   0x00200000L
#define RCC_DEV1_EPF1_STRAP4__STRAP_FLR_EN_DEV1_F1__MASK                                                      0x00400000L
#define RCC_DEV1_EPF1_STRAP4__STRAP_PME_SUPPORT_DEV1_F1__MASK                                                 0x0F800000L
#define RCC_DEV1_EPF1_STRAP4__STRAP_INTERRUPT_PIN_DEV1_F1__MASK                                               0x70000000L
#define RCC_DEV1_EPF1_STRAP4__STRAP_AUXPWR_SUPPORT_DEV1_F1__MASK                                              0x80000000L
//RCC_DEV1_EPF1_STRAP5
#define RCC_DEV1_EPF1_STRAP5__STRAP_SUBSYS_VEN_ID_DEV1_F1__MASK                                               0x0000FFFFL
//RCC_DEV1_EPF1_STRAP6
#define RCC_DEV1_EPF1_STRAP6__STRAP_APER0_EN_DEV1_F1__MASK                                                    0x00000001L
#define RCC_DEV1_EPF1_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV1_F1__MASK                                       0x00000002L
#define RCC_DEV1_EPF1_STRAP6__STRAP_APER0_AP_SIZE_DEV1_F1__MASK                                               0x00000070L
#define RCC_DEV1_EPF1_STRAP6__STRAP_APER1_EN_DEV1_F1__MASK                                                    0x00000100L
#define RCC_DEV1_EPF1_STRAP6__STRAP_APER1_PREFETCHABLE_EN_DEV1_F1__MASK                                       0x00000200L
#define RCC_DEV1_EPF1_STRAP6__STRAP_APER2_EN_DEV1_F1__MASK                                                    0x00010000L
#define RCC_DEV1_EPF1_STRAP6__STRAP_APER2_PREFETCHABLE_EN_DEV1_F1__MASK                                       0x00020000L
#define RCC_DEV1_EPF1_STRAP6__STRAP_APER3_EN_DEV1_F1__MASK                                                    0x01000000L
#define RCC_DEV1_EPF1_STRAP6__STRAP_APER3_PREFETCHABLE_EN_DEV1_F1__MASK                                       0x02000000L
//RCC_DEV1_EPF1_STRAP13
#define RCC_DEV1_EPF1_STRAP13__STRAP_CLASS_CODE_PIF_DEV1_F1__MASK                                             0x000000FFL
#define RCC_DEV1_EPF1_STRAP13__STRAP_CLASS_CODE_SUB_DEV1_F1__MASK                                             0x0000FF00L
#define RCC_DEV1_EPF1_STRAP13__STRAP_CLASS_CODE_BASE_DEV1_F1__MASK                                            0x00FF0000L
//RCC_DEV1_EPF2_STRAP0
#define RCC_DEV1_EPF2_STRAP0__STRAP_DEVICE_ID_DEV1_F2__MASK                                                   0x0000FFFFL
#define RCC_DEV1_EPF2_STRAP0__STRAP_MAJOR_REV_ID_DEV1_F2__MASK                                                0x000F0000L
#define RCC_DEV1_EPF2_STRAP0__STRAP_MINOR_REV_ID_DEV1_F2__MASK                                                0x00F00000L
#define RCC_DEV1_EPF2_STRAP0__STRAP_FUNC_EN_DEV1_F2__MASK                                                     0x10000000L
#define RCC_DEV1_EPF2_STRAP0__STRAP_LEGACY_DEVICE_TYPE_EN_DEV1_F2__MASK                                       0x20000000L
#define RCC_DEV1_EPF2_STRAP0__STRAP_D1_SUPPORT_DEV1_F2__MASK                                                  0x40000000L
#define RCC_DEV1_EPF2_STRAP0__STRAP_D2_SUPPORT_DEV1_F2__MASK                                                  0x80000000L
//RCC_DEV1_EPF2_STRAP2
#define RCC_DEV1_EPF2_STRAP2__STRAP_NO_SOFT_RESET_DEV1_F2__MASK                                               0x00000080L
#define RCC_DEV1_EPF2_STRAP2__STRAP_RESIZE_BAR_EN_DEV1_F2__MASK                                               0x00000100L
#define RCC_DEV1_EPF2_STRAP2__STRAP_MSI_PERVECTOR_MASK_CAP_DEV1_F2__MASK                                      0x00004000L
#define RCC_DEV1_EPF2_STRAP2__STRAP_AER_EN_DEV1_F2__MASK                                                      0x00010000L
#define RCC_DEV1_EPF2_STRAP2__STRAP_ACS_EN_DEV1_F2__MASK                                                      0x00020000L
#define RCC_DEV1_EPF2_STRAP2__STRAP_CPL_ABORT_ERR_EN_DEV1_F2__MASK                                            0x00100000L
#define RCC_DEV1_EPF2_STRAP2__STRAP_DPA_EN_DEV1_F2__MASK                                                      0x00200000L
#define RCC_DEV1_EPF2_STRAP2__STRAP_VC_EN_DEV1_F2__MASK                                                       0x00800000L
#define RCC_DEV1_EPF2_STRAP2__STRAP_MSI_MULTI_CAP_DEV1_F2__MASK                                               0x07000000L
//RCC_DEV1_EPF2_STRAP3
#define RCC_DEV1_EPF2_STRAP3__STRAP_POISONED_ADVISORY_NONFATAL_DEV1_F2__MASK                                  0x00000001L
#define RCC_DEV1_EPF2_STRAP3__STRAP_PWR_EN_DEV1_F2__MASK                                                      0x00000002L
#define RCC_DEV1_EPF2_STRAP3__STRAP_SUBSYS_ID_DEV1_F2__MASK                                                   0x0003FFFCL
#define RCC_DEV1_EPF2_STRAP3__STRAP_MSI_EN_DEV1_F2__MASK                                                      0x00040000L
#define RCC_DEV1_EPF2_STRAP3__STRAP_MSI_CLR_PENDING_EN_DEV1_F2__MASK                                          0x00080000L
#define RCC_DEV1_EPF2_STRAP3__STRAP_MSIX_EN_DEV1_F2__MASK                                                     0x00100000L
#define RCC_DEV1_EPF2_STRAP3__STRAP_PMC_DSI_DEV1_F2__MASK                                                     0x01000000L
#define RCC_DEV1_EPF2_STRAP3__STRAP_VENDOR_ID_BIT_DEV1_F2__MASK                                               0x02000000L
#define RCC_DEV1_EPF2_STRAP3__STRAP_ALL_MSI_EVENT_SUPPORT_EN_DEV1_F2__MASK                                    0x04000000L
#define RCC_DEV1_EPF2_STRAP3__STRAP_SMN_ERR_STATUS_MASK_EN_EP_DEV1_F2__MASK                                   0x08000000L
//RCC_DEV1_EPF2_STRAP4
#define RCC_DEV1_EPF2_STRAP4__STRAP_ATOMIC_64BIT_EN_DEV1_F2__MASK                                             0x00100000L
#define RCC_DEV1_EPF2_STRAP4__STRAP_ATOMIC_EN_DEV1_F2__MASK                                                   0x00200000L
#define RCC_DEV1_EPF2_STRAP4__STRAP_FLR_EN_DEV1_F2__MASK                                                      0x00400000L
#define RCC_DEV1_EPF2_STRAP4__STRAP_PME_SUPPORT_DEV1_F2__MASK                                                 0x0F800000L
#define RCC_DEV1_EPF2_STRAP4__STRAP_INTERRUPT_PIN_DEV1_F2__MASK                                               0x70000000L
#define RCC_DEV1_EPF2_STRAP4__STRAP_AUXPWR_SUPPORT_DEV1_F2__MASK                                              0x80000000L
//RCC_DEV1_EPF2_STRAP5
#define RCC_DEV1_EPF2_STRAP5__STRAP_SUBSYS_VEN_ID_DEV1_F2__MASK                                               0x0000FFFFL
//RCC_DEV1_EPF2_STRAP6
#define RCC_DEV1_EPF2_STRAP6__STRAP_APER0_EN_DEV1_F2__MASK                                                    0x00000001L
#define RCC_DEV1_EPF2_STRAP6__STRAP_APER0_PREFETCHABLE_EN_DEV1_F2__MASK                                       0x00000002L
#define RCC_DEV1_EPF2_STRAP6__STRAP_APER0_AP_SIZE_DEV1_F2__MASK                                               0x00000070L
#define RCC_DEV1_EPF2_STRAP6__STRAP_APER1_EN_DEV1_F2__MASK                                                    0x00000100L
#define RCC_DEV1_EPF2_STRAP6__STRAP_APER1_PREFETCHABLE_EN_DEV1_F2__MASK                                       0x00000200L
#define RCC_DEV1_EPF2_STRAP6__STRAP_APER2_EN_DEV1_F2__MASK                                                    0x00010000L
#define RCC_DEV1_EPF2_STRAP6__STRAP_APER2_PREFETCHABLE_EN_DEV1_F2__MASK                                       0x00020000L
#define RCC_DEV1_EPF2_STRAP6__STRAP_APER3_EN_DEV1_F2__MASK                                                    0x01000000L
#define RCC_DEV1_EPF2_STRAP6__STRAP_APER3_PREFETCHABLE_EN_DEV1_F2__MASK                                       0x02000000L
//RCC_DEV1_EPF2_STRAP13
#define RCC_DEV1_EPF2_STRAP13__STRAP_CLASS_CODE_PIF_DEV1_F2__MASK                                             0x000000FFL
#define RCC_DEV1_EPF2_STRAP13__STRAP_CLASS_CODE_SUB_DEV1_F2__MASK                                             0x0000FF00L
#define RCC_DEV1_EPF2_STRAP13__STRAP_CLASS_CODE_BASE_DEV1_F2__MASK                                            0x00FF0000L


// addressBlock: bif_rst_bif_rst_regblk
//HARD_RST_CTRL
#define HARD_RST_CTRL__DSPT_CFG_RST_EN__MASK                                                                  0x00000001L
#define HARD_RST_CTRL__DSPT_CFG_STICKY_RST_EN__MASK                                                           0x00000002L
#define HARD_RST_CTRL__DSPT_PRV_RST_EN__MASK                                                                  0x00000004L
#define HARD_RST_CTRL__DSPT_PRV_STICKY_RST_EN__MASK                                                           0x00000008L
#define HARD_RST_CTRL__EP_CFG_RST_EN__MASK                                                                    0x00000010L
#define HARD_RST_CTRL__EP_CFG_STICKY_RST_EN__MASK                                                             0x00000020L
#define HARD_RST_CTRL__EP_PRV_RST_EN__MASK                                                                    0x00000040L
#define HARD_RST_CTRL__EP_PRV_STICKY_RST_EN__MASK                                                             0x00000080L
#define HARD_RST_CTRL__SWUS_SHADOW_RST_EN__MASK                                                               0x10000000L
#define HARD_RST_CTRL__CORE_STICKY_RST_EN__MASK                                                               0x20000000L
#define HARD_RST_CTRL__RELOAD_STRAP_EN__MASK                                                                  0x40000000L
#define HARD_RST_CTRL__CORE_RST_EN__MASK                                                                      0x80000000L
//RSMU_SOFT_RST_CTRL
#define RSMU_SOFT_RST_CTRL__DSPT_CFG_RST_EN__MASK                                                             0x00000001L
#define RSMU_SOFT_RST_CTRL__DSPT_CFG_STICKY_RST_EN__MASK                                                      0x00000002L
#define RSMU_SOFT_RST_CTRL__DSPT_PRV_RST_EN__MASK                                                             0x00000004L
#define RSMU_SOFT_RST_CTRL__DSPT_PRV_STICKY_RST_EN__MASK                                                      0x00000008L
#define RSMU_SOFT_RST_CTRL__EP_CFG_RST_EN__MASK                                                               0x00000010L
#define RSMU_SOFT_RST_CTRL__EP_CFG_STICKY_RST_EN__MASK                                                        0x00000020L
#define RSMU_SOFT_RST_CTRL__EP_PRV_RST_EN__MASK                                                               0x00000040L
#define RSMU_SOFT_RST_CTRL__EP_PRV_STICKY_RST_EN__MASK                                                        0x00000080L
#define RSMU_SOFT_RST_CTRL__SWUS_SHADOW_RST_EN__MASK                                                          0x10000000L
#define RSMU_SOFT_RST_CTRL__CORE_STICKY_RST_EN__MASK                                                          0x20000000L
#define RSMU_SOFT_RST_CTRL__RELOAD_STRAP_EN__MASK                                                             0x40000000L
#define RSMU_SOFT_RST_CTRL__CORE_RST_EN__MASK                                                                 0x80000000L
//SELF_SOFT_RST
#define SELF_SOFT_RST__DSPT0_CFG_RST__MASK                                                                    0x00000001L
#define SELF_SOFT_RST__DSPT0_CFG_STICKY_RST__MASK                                                             0x00000002L
#define SELF_SOFT_RST__DSPT0_PRV_RST__MASK                                                                    0x00000004L
#define SELF_SOFT_RST__DSPT0_PRV_STICKY_RST__MASK                                                             0x00000008L
#define SELF_SOFT_RST__EP0_CFG_RST__MASK                                                                      0x00000010L
#define SELF_SOFT_RST__EP0_CFG_STICKY_RST__MASK                                                               0x00000020L
#define SELF_SOFT_RST__EP0_PRV_RST__MASK                                                                      0x00000040L
#define SELF_SOFT_RST__EP0_PRV_STICKY_RST__MASK                                                               0x00000080L
#define SELF_SOFT_RST__SDP_PORT_RST__MASK                                                                     0x08000000L
#define SELF_SOFT_RST__SWUS_SHADOW_RST__MASK                                                                  0x10000000L
#define SELF_SOFT_RST__CORE_STICKY_RST__MASK                                                                  0x20000000L
#define SELF_SOFT_RST__RELOAD_STRAP__MASK                                                                     0x40000000L
#define SELF_SOFT_RST__CORE_RST__MASK                                                                         0x80000000L
//GFX_DRV_MODE1_RST_CTRL
#define GFX_DRV_MODE1_RST_CTRL__DRV_MODE1_PF_CFG_RST__MASK                                                    0x00000001L
#define GFX_DRV_MODE1_RST_CTRL__DRV_MODE1_PF_CFG_FLR_EXC_RST__MASK                                            0x00000002L
#define GFX_DRV_MODE1_RST_CTRL__DRV_MODE1_PF_CFG_STICKY_RST__MASK                                             0x00000004L
#define GFX_DRV_MODE1_RST_CTRL__DRV_MODE1_PF_PRV_RST__MASK                                                    0x00000008L
#define GFX_DRV_MODE1_RST_CTRL__DRV_MODE1_PF_PRV_STICKY_RST__MASK                                             0x00000010L
#define GFX_DRV_MODE1_RST_CTRL__DRV_MODE1_VF_CFG_RST__MASK                                                    0x00000020L
#define GFX_DRV_MODE1_RST_CTRL__DRV_MODE1_VF_CFG_STICKY_RST__MASK                                             0x00000040L
#define GFX_DRV_MODE1_RST_CTRL__DRV_MODE1_VF_PRV_RST__MASK                                                    0x00000080L
//BIF_RST_MISC_CTRL
#define BIF_RST_MISC_CTRL__ERRSTATUS_KEPT_IN_PERSTB__MASK                                                     0x00000001L
#define BIF_RST_MISC_CTRL__DRV_RST_MODE__MASK                                                                 0x0000000CL
#define BIF_RST_MISC_CTRL__DRV_RST_CFG_MASK__MASK                                                             0x00000010L
#define BIF_RST_MISC_CTRL__DRV_RST_BITS_AUTO_CLEAR__MASK                                                      0x00000020L
#define BIF_RST_MISC_CTRL__FLR_RST_BIT_AUTO_CLEAR__MASK                                                       0x00000040L
#define BIF_RST_MISC_CTRL__STRAP_EP_LNK_RST_IOV_EN__MASK                                                      0x00000100L
#define BIF_RST_MISC_CTRL__LNK_RST_GRACE_MODE__MASK                                                           0x00000200L
#define BIF_RST_MISC_CTRL__LNK_RST_GRACE_TIMEOUT__MASK                                                        0x00001C00L
#define BIF_RST_MISC_CTRL__LNK_RST_TIMER_SEL__MASK                                                            0x00006000L
#define BIF_RST_MISC_CTRL__LNK_RST_TIMER2_SEL__MASK                                                           0x00018000L
#define BIF_RST_MISC_CTRL__SRIOV_SAVE_VFS_ON_VFENABLE_CLR__MASK                                               0x00060000L
#define BIF_RST_MISC_CTRL__LNK_RST_DMA_DUMMY_DIS__MASK                                                        0x00800000L
#define BIF_RST_MISC_CTRL__LNK_RST_DMA_DUMMY_RSPSTS__MASK                                                     0x03000000L
//BIF_RST_MISC_CTRL2
#define BIF_RST_MISC_CTRL2__SWUS_LNK_RST_TRANS_IDLE__MASK                                                     0x00010000L
#define BIF_RST_MISC_CTRL2__SWDS_LNK_RST_TRANS_IDLE__MASK                                                     0x00020000L
#define BIF_RST_MISC_CTRL2__ENDP0_LNK_RST_TRANS_IDLE__MASK                                                    0x00040000L
#define BIF_RST_MISC_CTRL2__ALL_RST_TRANS_IDLE__MASK                                                          0x80000000L
//BIF_RST_MISC_CTRL3
#define BIF_RST_MISC_CTRL3__TIMER_SCALE__MASK                                                                 0x0000000FL
#define BIF_RST_MISC_CTRL3__PME_TURNOFF_TIMEOUT__MASK                                                         0x00000030L
#define BIF_RST_MISC_CTRL3__PME_TURNOFF_MODE__MASK                                                            0x00000040L
#define BIF_RST_MISC_CTRL3__RELOAD_STRAP_DELAY_HARD__MASK                                                     0x00000380L
#define BIF_RST_MISC_CTRL3__RELOAD_STRAP_DELAY_SOFT__MASK                                                     0x00001C00L
#define BIF_RST_MISC_CTRL3__RELOAD_STRAP_DELAY_SELF__MASK                                                     0x0000E000L
//BIF_RST_GFXVF_FLR_IDLE
#define BIF_RST_GFXVF_FLR_IDLE__VF0_TRANS_IDLE__MASK                                                          0x00000001L
#define BIF_RST_GFXVF_FLR_IDLE__VF1_TRANS_IDLE__MASK                                                          0x00000002L
#define BIF_RST_GFXVF_FLR_IDLE__VF2_TRANS_IDLE__MASK                                                          0x00000004L
#define BIF_RST_GFXVF_FLR_IDLE__VF3_TRANS_IDLE__MASK                                                          0x00000008L
#define BIF_RST_GFXVF_FLR_IDLE__VF4_TRANS_IDLE__MASK                                                          0x00000010L
#define BIF_RST_GFXVF_FLR_IDLE__VF5_TRANS_IDLE__MASK                                                          0x00000020L
#define BIF_RST_GFXVF_FLR_IDLE__VF6_TRANS_IDLE__MASK                                                          0x00000040L
#define BIF_RST_GFXVF_FLR_IDLE__VF7_TRANS_IDLE__MASK                                                          0x00000080L
#define BIF_RST_GFXVF_FLR_IDLE__VF8_TRANS_IDLE__MASK                                                          0x00000100L
#define BIF_RST_GFXVF_FLR_IDLE__VF9_TRANS_IDLE__MASK                                                          0x00000200L
#define BIF_RST_GFXVF_FLR_IDLE__VF10_TRANS_IDLE__MASK                                                         0x00000400L
#define BIF_RST_GFXVF_FLR_IDLE__VF11_TRANS_IDLE__MASK                                                         0x00000800L
#define BIF_RST_GFXVF_FLR_IDLE__VF12_TRANS_IDLE__MASK                                                         0x00001000L
#define BIF_RST_GFXVF_FLR_IDLE__VF13_TRANS_IDLE__MASK                                                         0x00002000L
#define BIF_RST_GFXVF_FLR_IDLE__VF14_TRANS_IDLE__MASK                                                         0x00004000L
#define BIF_RST_GFXVF_FLR_IDLE__VF15_TRANS_IDLE__MASK                                                         0x00008000L
#define BIF_RST_GFXVF_FLR_IDLE__SOFTPF_TRANS_IDLE__MASK                                                       0x80000000L
//DEV0_PF0_FLR_RST_CTRL
#define DEV0_PF0_FLR_RST_CTRL__PF_CFG_EN__MASK                                                                0x00000001L
#define DEV0_PF0_FLR_RST_CTRL__PF_CFG_FLR_EXC_EN__MASK                                                        0x00000002L
#define DEV0_PF0_FLR_RST_CTRL__PF_CFG_STICKY_EN__MASK                                                         0x00000004L
#define DEV0_PF0_FLR_RST_CTRL__PF_PRV_EN__MASK                                                                0x00000008L
#define DEV0_PF0_FLR_RST_CTRL__PF_PRV_STICKY_EN__MASK                                                         0x00000010L
#define DEV0_PF0_FLR_RST_CTRL__VF_CFG_EN__MASK                                                                0x00000020L
#define DEV0_PF0_FLR_RST_CTRL__VF_CFG_STICKY_EN__MASK                                                         0x00000040L
#define DEV0_PF0_FLR_RST_CTRL__VF_PRV_EN__MASK                                                                0x00000080L
#define DEV0_PF0_FLR_RST_CTRL__SOFT_PF_CFG_EN__MASK                                                           0x00000100L
#define DEV0_PF0_FLR_RST_CTRL__SOFT_PF_CFG_FLR_EXC_EN__MASK                                                   0x00000200L
#define DEV0_PF0_FLR_RST_CTRL__SOFT_PF_CFG_STICKY_EN__MASK                                                    0x00000400L
#define DEV0_PF0_FLR_RST_CTRL__SOFT_PF_PRV_EN__MASK                                                           0x00000800L
#define DEV0_PF0_FLR_RST_CTRL__SOFT_PF_PRV_STICKY_EN__MASK                                                    0x00001000L
#define DEV0_PF0_FLR_RST_CTRL__VF_VF_CFG_EN__MASK                                                             0x00002000L
#define DEV0_PF0_FLR_RST_CTRL__VF_VF_CFG_STICKY_EN__MASK                                                      0x00004000L
#define DEV0_PF0_FLR_RST_CTRL__VF_VF_PRV_EN__MASK                                                             0x00008000L
#define DEV0_PF0_FLR_RST_CTRL__FLR_TWICE_EN__MASK                                                             0x00010000L
#define DEV0_PF0_FLR_RST_CTRL__FLR_GRACE_MODE__MASK                                                           0x00020000L
#define DEV0_PF0_FLR_RST_CTRL__FLR_GRACE_TIMEOUT__MASK                                                        0x001C0000L
#define DEV0_PF0_FLR_RST_CTRL__FLR_DMA_DUMMY_RSPSTS__MASK                                                     0x01800000L
#define DEV0_PF0_FLR_RST_CTRL__FLR_HST_DUMMY_RSPSTS__MASK                                                     0x06000000L
//DEV0_PF1_FLR_RST_CTRL
#define DEV0_PF1_FLR_RST_CTRL__PF_CFG_EN__MASK                                                                0x00000001L
#define DEV0_PF1_FLR_RST_CTRL__PF_CFG_FLR_EXC_EN__MASK                                                        0x00000002L
#define DEV0_PF1_FLR_RST_CTRL__PF_CFG_STICKY_EN__MASK                                                         0x00000004L
#define DEV0_PF1_FLR_RST_CTRL__PF_PRV_EN__MASK                                                                0x00000008L
#define DEV0_PF1_FLR_RST_CTRL__PF_PRV_STICKY_EN__MASK                                                         0x00000010L
#define DEV0_PF1_FLR_RST_CTRL__FLR_GRACE_MODE__MASK                                                           0x00020000L
#define DEV0_PF1_FLR_RST_CTRL__FLR_GRACE_TIMEOUT__MASK                                                        0x001C0000L
#define DEV0_PF1_FLR_RST_CTRL__FLR_DMA_DUMMY_RSPSTS__MASK                                                     0x01800000L
#define DEV0_PF1_FLR_RST_CTRL__FLR_HST_DUMMY_RSPSTS__MASK                                                     0x06000000L
//DEV0_PF2_FLR_RST_CTRL
#define DEV0_PF2_FLR_RST_CTRL__PF_CFG_EN__MASK                                                                0x00000001L
#define DEV0_PF2_FLR_RST_CTRL__PF_CFG_FLR_EXC_EN__MASK                                                        0x00000002L
#define DEV0_PF2_FLR_RST_CTRL__PF_CFG_STICKY_EN__MASK                                                         0x00000004L
#define DEV0_PF2_FLR_RST_CTRL__PF_PRV_EN__MASK                                                                0x00000008L
#define DEV0_PF2_FLR_RST_CTRL__PF_PRV_STICKY_EN__MASK                                                         0x00000010L
#define DEV0_PF2_FLR_RST_CTRL__FLR_GRACE_MODE__MASK                                                           0x00020000L
#define DEV0_PF2_FLR_RST_CTRL__FLR_GRACE_TIMEOUT__MASK                                                        0x001C0000L
#define DEV0_PF2_FLR_RST_CTRL__FLR_DMA_DUMMY_RSPSTS__MASK                                                     0x01800000L
#define DEV0_PF2_FLR_RST_CTRL__FLR_HST_DUMMY_RSPSTS__MASK                                                     0x06000000L
//DEV0_PF3_FLR_RST_CTRL
#define DEV0_PF3_FLR_RST_CTRL__PF_CFG_EN__MASK                                                                0x00000001L
#define DEV0_PF3_FLR_RST_CTRL__PF_CFG_FLR_EXC_EN__MASK                                                        0x00000002L
#define DEV0_PF3_FLR_RST_CTRL__PF_CFG_STICKY_EN__MASK                                                         0x00000004L
#define DEV0_PF3_FLR_RST_CTRL__PF_PRV_EN__MASK                                                                0x00000008L
#define DEV0_PF3_FLR_RST_CTRL__PF_PRV_STICKY_EN__MASK                                                         0x00000010L
#define DEV0_PF3_FLR_RST_CTRL__FLR_GRACE_MODE__MASK                                                           0x00020000L
#define DEV0_PF3_FLR_RST_CTRL__FLR_GRACE_TIMEOUT__MASK                                                        0x001C0000L
#define DEV0_PF3_FLR_RST_CTRL__FLR_DMA_DUMMY_RSPSTS__MASK                                                     0x01800000L
#define DEV0_PF3_FLR_RST_CTRL__FLR_HST_DUMMY_RSPSTS__MASK                                                     0x06000000L
//DEV0_PF4_FLR_RST_CTRL
#define DEV0_PF4_FLR_RST_CTRL__PF_CFG_EN__MASK                                                                0x00000001L
#define DEV0_PF4_FLR_RST_CTRL__PF_CFG_FLR_EXC_EN__MASK                                                        0x00000002L
#define DEV0_PF4_FLR_RST_CTRL__PF_CFG_STICKY_EN__MASK                                                         0x00000004L
#define DEV0_PF4_FLR_RST_CTRL__PF_PRV_EN__MASK                                                                0x00000008L
#define DEV0_PF4_FLR_RST_CTRL__PF_PRV_STICKY_EN__MASK                                                         0x00000010L
#define DEV0_PF4_FLR_RST_CTRL__FLR_GRACE_MODE__MASK                                                           0x00020000L
#define DEV0_PF4_FLR_RST_CTRL__FLR_GRACE_TIMEOUT__MASK                                                        0x001C0000L
#define DEV0_PF4_FLR_RST_CTRL__FLR_DMA_DUMMY_RSPSTS__MASK                                                     0x01800000L
#define DEV0_PF4_FLR_RST_CTRL__FLR_HST_DUMMY_RSPSTS__MASK                                                     0x06000000L
//DEV0_PF5_FLR_RST_CTRL
#define DEV0_PF5_FLR_RST_CTRL__PF_CFG_EN__MASK                                                                0x00000001L
#define DEV0_PF5_FLR_RST_CTRL__PF_CFG_FLR_EXC_EN__MASK                                                        0x00000002L
#define DEV0_PF5_FLR_RST_CTRL__PF_CFG_STICKY_EN__MASK                                                         0x00000004L
#define DEV0_PF5_FLR_RST_CTRL__PF_PRV_EN__MASK                                                                0x00000008L
#define DEV0_PF5_FLR_RST_CTRL__PF_PRV_STICKY_EN__MASK                                                         0x00000010L
#define DEV0_PF5_FLR_RST_CTRL__FLR_GRACE_MODE__MASK                                                           0x00020000L
#define DEV0_PF5_FLR_RST_CTRL__FLR_GRACE_TIMEOUT__MASK                                                        0x001C0000L
#define DEV0_PF5_FLR_RST_CTRL__FLR_DMA_DUMMY_RSPSTS__MASK                                                     0x01800000L
#define DEV0_PF5_FLR_RST_CTRL__FLR_HST_DUMMY_RSPSTS__MASK                                                     0x06000000L
//DEV0_PF6_FLR_RST_CTRL
#define DEV0_PF6_FLR_RST_CTRL__PF_CFG_EN__MASK                                                                0x00000001L
#define DEV0_PF6_FLR_RST_CTRL__PF_CFG_FLR_EXC_EN__MASK                                                        0x00000002L
#define DEV0_PF6_FLR_RST_CTRL__PF_CFG_STICKY_EN__MASK                                                         0x00000004L
#define DEV0_PF6_FLR_RST_CTRL__PF_PRV_EN__MASK                                                                0x00000008L
#define DEV0_PF6_FLR_RST_CTRL__PF_PRV_STICKY_EN__MASK                                                         0x00000010L
#define DEV0_PF6_FLR_RST_CTRL__FLR_GRACE_MODE__MASK                                                           0x00020000L
#define DEV0_PF6_FLR_RST_CTRL__FLR_GRACE_TIMEOUT__MASK                                                        0x001C0000L
#define DEV0_PF6_FLR_RST_CTRL__FLR_DMA_DUMMY_RSPSTS__MASK                                                     0x01800000L
#define DEV0_PF6_FLR_RST_CTRL__FLR_HST_DUMMY_RSPSTS__MASK                                                     0x06000000L
//DEV0_PF7_FLR_RST_CTRL
#define DEV0_PF7_FLR_RST_CTRL__PF_CFG_EN__MASK                                                                0x00000001L
#define DEV0_PF7_FLR_RST_CTRL__PF_CFG_FLR_EXC_EN__MASK                                                        0x00000002L
#define DEV0_PF7_FLR_RST_CTRL__PF_CFG_STICKY_EN__MASK                                                         0x00000004L
#define DEV0_PF7_FLR_RST_CTRL__PF_PRV_EN__MASK                                                                0x00000008L
#define DEV0_PF7_FLR_RST_CTRL__PF_PRV_STICKY_EN__MASK                                                         0x00000010L
#define DEV0_PF7_FLR_RST_CTRL__FLR_GRACE_MODE__MASK                                                           0x00020000L
#define DEV0_PF7_FLR_RST_CTRL__FLR_GRACE_TIMEOUT__MASK                                                        0x001C0000L
#define DEV0_PF7_FLR_RST_CTRL__FLR_DMA_DUMMY_RSPSTS__MASK                                                     0x01800000L
#define DEV0_PF7_FLR_RST_CTRL__FLR_HST_DUMMY_RSPSTS__MASK                                                     0x06000000L
//BIF_INST_RESET_INTR_STS
#define BIF_INST_RESET_INTR_STS__EP0_LINK_RESET_INTR_STS__MASK                                                0x00000001L
#define BIF_INST_RESET_INTR_STS__EP0_LINK_RESET_CFG_ONLY_INTR_STS__MASK                                       0x00000002L
#define BIF_INST_RESET_INTR_STS__DRV_RESET_M0_INTR_STS__MASK                                                  0x00000004L
#define BIF_INST_RESET_INTR_STS__DRV_RESET_M1_INTR_STS__MASK                                                  0x00000008L
#define BIF_INST_RESET_INTR_STS__DRV_RESET_M2_INTR_STS__MASK                                                  0x00000010L
//BIF_PF_FLR_INTR_STS
#define BIF_PF_FLR_INTR_STS__DEV0_PF0_FLR_INTR_STS__MASK                                                      0x00000001L
#define BIF_PF_FLR_INTR_STS__DEV0_PF1_FLR_INTR_STS__MASK                                                      0x00000002L
#define BIF_PF_FLR_INTR_STS__DEV0_PF2_FLR_INTR_STS__MASK                                                      0x00000004L
#define BIF_PF_FLR_INTR_STS__DEV0_PF3_FLR_INTR_STS__MASK                                                      0x00000008L
#define BIF_PF_FLR_INTR_STS__DEV0_PF4_FLR_INTR_STS__MASK                                                      0x00000010L
#define BIF_PF_FLR_INTR_STS__DEV0_PF5_FLR_INTR_STS__MASK                                                      0x00000020L
#define BIF_PF_FLR_INTR_STS__DEV0_PF6_FLR_INTR_STS__MASK                                                      0x00000040L
#define BIF_PF_FLR_INTR_STS__DEV0_PF7_FLR_INTR_STS__MASK                                                      0x00000080L
//BIF_D3HOTD0_INTR_STS
#define BIF_D3HOTD0_INTR_STS__DEV0_PF0_D3HOTD0_INTR_STS__MASK                                                 0x00000001L
#define BIF_D3HOTD0_INTR_STS__DEV0_PF1_D3HOTD0_INTR_STS__MASK                                                 0x00000002L
#define BIF_D3HOTD0_INTR_STS__DEV0_PF2_D3HOTD0_INTR_STS__MASK                                                 0x00000004L
#define BIF_D3HOTD0_INTR_STS__DEV0_PF3_D3HOTD0_INTR_STS__MASK                                                 0x00000008L
#define BIF_D3HOTD0_INTR_STS__DEV0_PF4_D3HOTD0_INTR_STS__MASK                                                 0x00000010L
#define BIF_D3HOTD0_INTR_STS__DEV0_PF5_D3HOTD0_INTR_STS__MASK                                                 0x00000020L
#define BIF_D3HOTD0_INTR_STS__DEV0_PF6_D3HOTD0_INTR_STS__MASK                                                 0x00000040L
#define BIF_D3HOTD0_INTR_STS__DEV0_PF7_D3HOTD0_INTR_STS__MASK                                                 0x00000080L
//BIF_POWER_INTR_STS
#define BIF_POWER_INTR_STS__DEV0_PME_TURN_OFF_INTR_STS__MASK                                                  0x00000001L
#define BIF_POWER_INTR_STS__PORT0_DSTATE_INTR_STS__MASK                                                       0x00010000L
//BIF_PF_DSTATE_INTR_STS
#define BIF_PF_DSTATE_INTR_STS__DEV0_PF0_DSTATE_INTR_STS__MASK                                                0x00000001L
#define BIF_PF_DSTATE_INTR_STS__DEV0_PF1_DSTATE_INTR_STS__MASK                                                0x00000002L
#define BIF_PF_DSTATE_INTR_STS__DEV0_PF2_DSTATE_INTR_STS__MASK                                                0x00000004L
#define BIF_PF_DSTATE_INTR_STS__DEV0_PF3_DSTATE_INTR_STS__MASK                                                0x00000008L
#define BIF_PF_DSTATE_INTR_STS__DEV0_PF4_DSTATE_INTR_STS__MASK                                                0x00000010L
#define BIF_PF_DSTATE_INTR_STS__DEV0_PF5_DSTATE_INTR_STS__MASK                                                0x00000020L
#define BIF_PF_DSTATE_INTR_STS__DEV0_PF6_DSTATE_INTR_STS__MASK                                                0x00000040L
#define BIF_PF_DSTATE_INTR_STS__DEV0_PF7_DSTATE_INTR_STS__MASK                                                0x00000080L
//BIF_PF0_VF_FLR_INTR_STS
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF0_FLR_INTR_STS__MASK                                                   0x00000001L
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF1_FLR_INTR_STS__MASK                                                   0x00000002L
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF2_FLR_INTR_STS__MASK                                                   0x00000004L
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF3_FLR_INTR_STS__MASK                                                   0x00000008L
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF4_FLR_INTR_STS__MASK                                                   0x00000010L
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF5_FLR_INTR_STS__MASK                                                   0x00000020L
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF6_FLR_INTR_STS__MASK                                                   0x00000040L
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF7_FLR_INTR_STS__MASK                                                   0x00000080L
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF8_FLR_INTR_STS__MASK                                                   0x00000100L
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF9_FLR_INTR_STS__MASK                                                   0x00000200L
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF10_FLR_INTR_STS__MASK                                                  0x00000400L
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF11_FLR_INTR_STS__MASK                                                  0x00000800L
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF12_FLR_INTR_STS__MASK                                                  0x00001000L
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF13_FLR_INTR_STS__MASK                                                  0x00002000L
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF14_FLR_INTR_STS__MASK                                                  0x00004000L
#define BIF_PF0_VF_FLR_INTR_STS__PF0_VF15_FLR_INTR_STS__MASK                                                  0x00008000L
#define BIF_PF0_VF_FLR_INTR_STS__PF0_SOFTPF_FLR_INTR_STS__MASK                                                0x80000000L
//BIF_INST_RESET_INTR_MASK
#define BIF_INST_RESET_INTR_MASK__EP0_LINK_RESET_INTR_MASK__MASK                                              0x00000001L
#define BIF_INST_RESET_INTR_MASK__EP0_LINK_RESET_CFG_ONLY_INTR_MASK__MASK                                     0x00000002L
#define BIF_INST_RESET_INTR_MASK__DRV_RESET_M0_INTR_MASK__MASK                                                0x00000004L
#define BIF_INST_RESET_INTR_MASK__DRV_RESET_M1_INTR_MASK__MASK                                                0x00000008L
#define BIF_INST_RESET_INTR_MASK__DRV_RESET_M2_INTR_MASK__MASK                                                0x00000010L
//BIF_PF_FLR_INTR_MASK
#define BIF_PF_FLR_INTR_MASK__DEV0_PF0_FLR_INTR_MASK__MASK                                                    0x00000001L
#define BIF_PF_FLR_INTR_MASK__DEV0_PF1_FLR_INTR_MASK__MASK                                                    0x00000002L
#define BIF_PF_FLR_INTR_MASK__DEV0_PF2_FLR_INTR_MASK__MASK                                                    0x00000004L
#define BIF_PF_FLR_INTR_MASK__DEV0_PF3_FLR_INTR_MASK__MASK                                                    0x00000008L
#define BIF_PF_FLR_INTR_MASK__DEV0_PF4_FLR_INTR_MASK__MASK                                                    0x00000010L
#define BIF_PF_FLR_INTR_MASK__DEV0_PF5_FLR_INTR_MASK__MASK                                                    0x00000020L
#define BIF_PF_FLR_INTR_MASK__DEV0_PF6_FLR_INTR_MASK__MASK                                                    0x00000040L
#define BIF_PF_FLR_INTR_MASK__DEV0_PF7_FLR_INTR_MASK__MASK                                                    0x00000080L
//BIF_D3HOTD0_INTR_MASK
#define BIF_D3HOTD0_INTR_MASK__DEV0_PF0_D3HOTD0_INTR_MASK__MASK                                               0x00000001L
#define BIF_D3HOTD0_INTR_MASK__DEV0_PF1_D3HOTD0_INTR_MASK__MASK                                               0x00000002L
#define BIF_D3HOTD0_INTR_MASK__DEV0_PF2_D3HOTD0_INTR_MASK__MASK                                               0x00000004L
#define BIF_D3HOTD0_INTR_MASK__DEV0_PF3_D3HOTD0_INTR_MASK__MASK                                               0x00000008L
#define BIF_D3HOTD0_INTR_MASK__DEV0_PF4_D3HOTD0_INTR_MASK__MASK                                               0x00000010L
#define BIF_D3HOTD0_INTR_MASK__DEV0_PF5_D3HOTD0_INTR_MASK__MASK                                               0x00000020L
#define BIF_D3HOTD0_INTR_MASK__DEV0_PF6_D3HOTD0_INTR_MASK__MASK                                               0x00000040L
#define BIF_D3HOTD0_INTR_MASK__DEV0_PF7_D3HOTD0_INTR_MASK__MASK                                               0x00000080L
//BIF_POWER_INTR_MASK
#define BIF_POWER_INTR_MASK__DEV0_PME_TURN_OFF_INTR_MASK__MASK                                                0x00000001L
#define BIF_POWER_INTR_MASK__PORT0_DSTATE_INTR_MASK__MASK                                                     0x00010000L
//BIF_PF_DSTATE_INTR_MASK
#define BIF_PF_DSTATE_INTR_MASK__DEV0_PF0_DSTATE_INTR_MASK__MASK                                              0x00000001L
#define BIF_PF_DSTATE_INTR_MASK__DEV0_PF1_DSTATE_INTR_MASK__MASK                                              0x00000002L
#define BIF_PF_DSTATE_INTR_MASK__DEV0_PF2_DSTATE_INTR_MASK__MASK                                              0x00000004L
#define BIF_PF_DSTATE_INTR_MASK__DEV0_PF3_DSTATE_INTR_MASK__MASK                                              0x00000008L
#define BIF_PF_DSTATE_INTR_MASK__DEV0_PF4_DSTATE_INTR_MASK__MASK                                              0x00000010L
#define BIF_PF_DSTATE_INTR_MASK__DEV0_PF5_DSTATE_INTR_MASK__MASK                                              0x00000020L
#define BIF_PF_DSTATE_INTR_MASK__DEV0_PF6_DSTATE_INTR_MASK__MASK                                              0x00000040L
#define BIF_PF_DSTATE_INTR_MASK__DEV0_PF7_DSTATE_INTR_MASK__MASK                                              0x00000080L
//BIF_PF0_VF_FLR_INTR_MASK
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF0_FLR_INTR_MASK__MASK                                                 0x00000001L
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF1_FLR_INTR_MASK__MASK                                                 0x00000002L
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF2_FLR_INTR_MASK__MASK                                                 0x00000004L
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF3_FLR_INTR_MASK__MASK                                                 0x00000008L
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF4_FLR_INTR_MASK__MASK                                                 0x00000010L
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF5_FLR_INTR_MASK__MASK                                                 0x00000020L
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF6_FLR_INTR_MASK__MASK                                                 0x00000040L
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF7_FLR_INTR_MASK__MASK                                                 0x00000080L
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF8_FLR_INTR_MASK__MASK                                                 0x00000100L
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF9_FLR_INTR_MASK__MASK                                                 0x00000200L
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF10_FLR_INTR_MASK__MASK                                                0x00000400L
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF11_FLR_INTR_MASK__MASK                                                0x00000800L
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF12_FLR_INTR_MASK__MASK                                                0x00001000L
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF13_FLR_INTR_MASK__MASK                                                0x00002000L
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF14_FLR_INTR_MASK__MASK                                                0x00004000L
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_VF15_FLR_INTR_MASK__MASK                                                0x00008000L
#define BIF_PF0_VF_FLR_INTR_MASK__PF0_SOFTPF_FLR_INTR_MASK__MASK                                              0x80000000L
//BIF_PF_FLR_RST
#define BIF_PF_FLR_RST__DEV0_PF0_FLR_RST__MASK                                                                0x00000001L
#define BIF_PF_FLR_RST__DEV0_PF1_FLR_RST__MASK                                                                0x00000002L
#define BIF_PF_FLR_RST__DEV0_PF2_FLR_RST__MASK                                                                0x00000004L
#define BIF_PF_FLR_RST__DEV0_PF3_FLR_RST__MASK                                                                0x00000008L
#define BIF_PF_FLR_RST__DEV0_PF4_FLR_RST__MASK                                                                0x00000010L
#define BIF_PF_FLR_RST__DEV0_PF5_FLR_RST__MASK                                                                0x00000020L
#define BIF_PF_FLR_RST__DEV0_PF6_FLR_RST__MASK                                                                0x00000040L
#define BIF_PF_FLR_RST__DEV0_PF7_FLR_RST__MASK                                                                0x00000080L
//BIF_PF0_VF_FLR_RST
#define BIF_PF0_VF_FLR_RST__PF0_VF0_FLR_RST__MASK                                                             0x00000001L
#define BIF_PF0_VF_FLR_RST__PF0_VF1_FLR_RST__MASK                                                             0x00000002L
#define BIF_PF0_VF_FLR_RST__PF0_VF2_FLR_RST__MASK                                                             0x00000004L
#define BIF_PF0_VF_FLR_RST__PF0_VF3_FLR_RST__MASK                                                             0x00000008L
#define BIF_PF0_VF_FLR_RST__PF0_VF4_FLR_RST__MASK                                                             0x00000010L
#define BIF_PF0_VF_FLR_RST__PF0_VF5_FLR_RST__MASK                                                             0x00000020L
#define BIF_PF0_VF_FLR_RST__PF0_VF6_FLR_RST__MASK                                                             0x00000040L
#define BIF_PF0_VF_FLR_RST__PF0_VF7_FLR_RST__MASK                                                             0x00000080L
#define BIF_PF0_VF_FLR_RST__PF0_VF8_FLR_RST__MASK                                                             0x00000100L
#define BIF_PF0_VF_FLR_RST__PF0_VF9_FLR_RST__MASK                                                             0x00000200L
#define BIF_PF0_VF_FLR_RST__PF0_VF10_FLR_RST__MASK                                                            0x00000400L
#define BIF_PF0_VF_FLR_RST__PF0_VF11_FLR_RST__MASK                                                            0x00000800L
#define BIF_PF0_VF_FLR_RST__PF0_VF12_FLR_RST__MASK                                                            0x00001000L
#define BIF_PF0_VF_FLR_RST__PF0_VF13_FLR_RST__MASK                                                            0x00002000L
#define BIF_PF0_VF_FLR_RST__PF0_VF14_FLR_RST__MASK                                                            0x00004000L
#define BIF_PF0_VF_FLR_RST__PF0_VF15_FLR_RST__MASK                                                            0x00008000L
#define BIF_PF0_VF_FLR_RST__PF0_SOFTPF_FLR_RST__MASK                                                          0x80000000L
//BIF_DEV0_PF0_DSTATE_VALUE
#define BIF_DEV0_PF0_DSTATE_VALUE__DEV0_PF0_DSTATE_TGT_VALUE__MASK                                            0x00000003L
#define BIF_DEV0_PF0_DSTATE_VALUE__DEV0_PF0_DSTATE_NEED_D3TOD0_RESET__MASK                                    0x00000004L
#define BIF_DEV0_PF0_DSTATE_VALUE__DEV0_PF0_DSTATE_ACK_VALUE__MASK                                            0x00030000L
//BIF_DEV0_PF1_DSTATE_VALUE
#define BIF_DEV0_PF1_DSTATE_VALUE__DEV0_PF1_DSTATE_TGT_VALUE__MASK                                            0x00000003L
#define BIF_DEV0_PF1_DSTATE_VALUE__DEV0_PF1_DSTATE_NEED_D3TOD0_RESET__MASK                                    0x00000004L
#define BIF_DEV0_PF1_DSTATE_VALUE__DEV0_PF1_DSTATE_ACK_VALUE__MASK                                            0x00030000L
//BIF_DEV0_PF2_DSTATE_VALUE
#define BIF_DEV0_PF2_DSTATE_VALUE__DEV0_PF2_DSTATE_TGT_VALUE__MASK                                            0x00000003L
#define BIF_DEV0_PF2_DSTATE_VALUE__DEV0_PF2_DSTATE_NEED_D3TOD0_RESET__MASK                                    0x00000004L
#define BIF_DEV0_PF2_DSTATE_VALUE__DEV0_PF2_DSTATE_ACK_VALUE__MASK                                            0x00030000L
//BIF_DEV0_PF3_DSTATE_VALUE
#define BIF_DEV0_PF3_DSTATE_VALUE__DEV0_PF3_DSTATE_TGT_VALUE__MASK                                            0x00000003L
#define BIF_DEV0_PF3_DSTATE_VALUE__DEV0_PF3_DSTATE_NEED_D3TOD0_RESET__MASK                                    0x00000004L
#define BIF_DEV0_PF3_DSTATE_VALUE__DEV0_PF3_DSTATE_ACK_VALUE__MASK                                            0x00030000L
//BIF_DEV0_PF4_DSTATE_VALUE
#define BIF_DEV0_PF4_DSTATE_VALUE__DEV0_PF4_DSTATE_TGT_VALUE__MASK                                            0x00000003L
#define BIF_DEV0_PF4_DSTATE_VALUE__DEV0_PF4_DSTATE_NEED_D3TOD0_RESET__MASK                                    0x00000004L
#define BIF_DEV0_PF4_DSTATE_VALUE__DEV0_PF4_DSTATE_ACK_VALUE__MASK                                            0x00030000L
//BIF_DEV0_PF5_DSTATE_VALUE
#define BIF_DEV0_PF5_DSTATE_VALUE__DEV0_PF5_DSTATE_TGT_VALUE__MASK                                            0x00000003L
#define BIF_DEV0_PF5_DSTATE_VALUE__DEV0_PF5_DSTATE_NEED_D3TOD0_RESET__MASK                                    0x00000004L
#define BIF_DEV0_PF5_DSTATE_VALUE__DEV0_PF5_DSTATE_ACK_VALUE__MASK                                            0x00030000L
//BIF_DEV0_PF6_DSTATE_VALUE
#define BIF_DEV0_PF6_DSTATE_VALUE__DEV0_PF6_DSTATE_TGT_VALUE__MASK                                            0x00000003L
#define BIF_DEV0_PF6_DSTATE_VALUE__DEV0_PF6_DSTATE_NEED_D3TOD0_RESET__MASK                                    0x00000004L
#define BIF_DEV0_PF6_DSTATE_VALUE__DEV0_PF6_DSTATE_ACK_VALUE__MASK                                            0x00030000L
//BIF_DEV0_PF7_DSTATE_VALUE
#define BIF_DEV0_PF7_DSTATE_VALUE__DEV0_PF7_DSTATE_TGT_VALUE__MASK                                            0x00000003L
#define BIF_DEV0_PF7_DSTATE_VALUE__DEV0_PF7_DSTATE_NEED_D3TOD0_RESET__MASK                                    0x00000004L
#define BIF_DEV0_PF7_DSTATE_VALUE__DEV0_PF7_DSTATE_ACK_VALUE__MASK                                            0x00030000L
//DEV0_PF0_D3HOTD0_RST_CTRL
#define DEV0_PF0_D3HOTD0_RST_CTRL__PF_CFG_EN__MASK                                                            0x00000001L
#define DEV0_PF0_D3HOTD0_RST_CTRL__PF_CFG_FLR_EXC_EN__MASK                                                    0x00000002L
#define DEV0_PF0_D3HOTD0_RST_CTRL__PF_CFG_STICKY_EN__MASK                                                     0x00000004L
#define DEV0_PF0_D3HOTD0_RST_CTRL__PF_PRV_EN__MASK                                                            0x00000008L
#define DEV0_PF0_D3HOTD0_RST_CTRL__PF_PRV_STICKY_EN__MASK                                                     0x00000010L
//DEV0_PF1_D3HOTD0_RST_CTRL
#define DEV0_PF1_D3HOTD0_RST_CTRL__PF_CFG_EN__MASK                                                            0x00000001L
#define DEV0_PF1_D3HOTD0_RST_CTRL__PF_CFG_FLR_EXC_EN__MASK                                                    0x00000002L
#define DEV0_PF1_D3HOTD0_RST_CTRL__PF_CFG_STICKY_EN__MASK                                                     0x00000004L
#define DEV0_PF1_D3HOTD0_RST_CTRL__PF_PRV_EN__MASK                                                            0x00000008L
#define DEV0_PF1_D3HOTD0_RST_CTRL__PF_PRV_STICKY_EN__MASK                                                     0x00000010L
//DEV0_PF2_D3HOTD0_RST_CTRL
#define DEV0_PF2_D3HOTD0_RST_CTRL__PF_CFG_EN__MASK                                                            0x00000001L
#define DEV0_PF2_D3HOTD0_RST_CTRL__PF_CFG_FLR_EXC_EN__MASK                                                    0x00000002L
#define DEV0_PF2_D3HOTD0_RST_CTRL__PF_CFG_STICKY_EN__MASK                                                     0x00000004L
#define DEV0_PF2_D3HOTD0_RST_CTRL__PF_PRV_EN__MASK                                                            0x00000008L
#define DEV0_PF2_D3HOTD0_RST_CTRL__PF_PRV_STICKY_EN__MASK                                                     0x00000010L
//DEV0_PF3_D3HOTD0_RST_CTRL
#define DEV0_PF3_D3HOTD0_RST_CTRL__PF_CFG_EN__MASK                                                            0x00000001L
#define DEV0_PF3_D3HOTD0_RST_CTRL__PF_CFG_FLR_EXC_EN__MASK                                                    0x00000002L
#define DEV0_PF3_D3HOTD0_RST_CTRL__PF_CFG_STICKY_EN__MASK                                                     0x00000004L
#define DEV0_PF3_D3HOTD0_RST_CTRL__PF_PRV_EN__MASK                                                            0x00000008L
#define DEV0_PF3_D3HOTD0_RST_CTRL__PF_PRV_STICKY_EN__MASK                                                     0x00000010L
//DEV0_PF4_D3HOTD0_RST_CTRL
#define DEV0_PF4_D3HOTD0_RST_CTRL__PF_CFG_EN__MASK                                                            0x00000001L
#define DEV0_PF4_D3HOTD0_RST_CTRL__PF_CFG_FLR_EXC_EN__MASK                                                    0x00000002L
#define DEV0_PF4_D3HOTD0_RST_CTRL__PF_CFG_STICKY_EN__MASK                                                     0x00000004L
#define DEV0_PF4_D3HOTD0_RST_CTRL__PF_PRV_EN__MASK                                                            0x00000008L
#define DEV0_PF4_D3HOTD0_RST_CTRL__PF_PRV_STICKY_EN__MASK                                                     0x00000010L
//DEV0_PF5_D3HOTD0_RST_CTRL
#define DEV0_PF5_D3HOTD0_RST_CTRL__PF_CFG_EN__MASK                                                            0x00000001L
#define DEV0_PF5_D3HOTD0_RST_CTRL__PF_CFG_FLR_EXC_EN__MASK                                                    0x00000002L
#define DEV0_PF5_D3HOTD0_RST_CTRL__PF_CFG_STICKY_EN__MASK                                                     0x00000004L
#define DEV0_PF5_D3HOTD0_RST_CTRL__PF_PRV_EN__MASK                                                            0x00000008L
#define DEV0_PF5_D3HOTD0_RST_CTRL__PF_PRV_STICKY_EN__MASK                                                     0x00000010L
//DEV0_PF6_D3HOTD0_RST_CTRL
#define DEV0_PF6_D3HOTD0_RST_CTRL__PF_CFG_EN__MASK                                                            0x00000001L
#define DEV0_PF6_D3HOTD0_RST_CTRL__PF_CFG_FLR_EXC_EN__MASK                                                    0x00000002L
#define DEV0_PF6_D3HOTD0_RST_CTRL__PF_CFG_STICKY_EN__MASK                                                     0x00000004L
#define DEV0_PF6_D3HOTD0_RST_CTRL__PF_PRV_EN__MASK                                                            0x00000008L
#define DEV0_PF6_D3HOTD0_RST_CTRL__PF_PRV_STICKY_EN__MASK                                                     0x00000010L
//DEV0_PF7_D3HOTD0_RST_CTRL
#define DEV0_PF7_D3HOTD0_RST_CTRL__PF_CFG_EN__MASK                                                            0x00000001L
#define DEV0_PF7_D3HOTD0_RST_CTRL__PF_CFG_FLR_EXC_EN__MASK                                                    0x00000002L
#define DEV0_PF7_D3HOTD0_RST_CTRL__PF_CFG_STICKY_EN__MASK                                                     0x00000004L
#define DEV0_PF7_D3HOTD0_RST_CTRL__PF_PRV_EN__MASK                                                            0x00000008L
#define DEV0_PF7_D3HOTD0_RST_CTRL__PF_PRV_STICKY_EN__MASK                                                     0x00000010L
//BIF_PORT0_DSTATE_VALUE
#define BIF_PORT0_DSTATE_VALUE__PORT0_DSTATE_TGT_VALUE__MASK                                                  0x00000003L
#define BIF_PORT0_DSTATE_VALUE__PORT0_DSTATE_ACK_VALUE__MASK                                                  0x00030000L


// addressBlock: bif_misc_bif_misc_regblk
//MISC_SCRATCH
#define MISC_SCRATCH__MISC_SCRATCH0__MASK                                                                     0xFFFFFFFFL
//INTR_LINE_POLARITY
#define INTR_LINE_POLARITY__INTR_LINE_POLARITY_DEV0__MASK                                                     0x000000FFL
//INTR_LINE_ENABLE
#define INTR_LINE_ENABLE__INTR_LINE_ENABLE_DEV0__MASK                                                         0x000000FFL
//OUTSTANDING_VC_ALLOC
#define OUTSTANDING_VC_ALLOC__DMA_OUTSTANDING_VC0_ALLOC__MASK                                                 0x00000003L
#define OUTSTANDING_VC_ALLOC__DMA_OUTSTANDING_VC1_ALLOC__MASK                                                 0x0000000CL
#define OUTSTANDING_VC_ALLOC__DMA_OUTSTANDING_VC2_ALLOC__MASK                                                 0x00000030L
#define OUTSTANDING_VC_ALLOC__DMA_OUTSTANDING_VC3_ALLOC__MASK                                                 0x000000C0L
#define OUTSTANDING_VC_ALLOC__DMA_OUTSTANDING_VC4_ALLOC__MASK                                                 0x00000300L
#define OUTSTANDING_VC_ALLOC__DMA_OUTSTANDING_VC5_ALLOC__MASK                                                 0x00000C00L
#define OUTSTANDING_VC_ALLOC__DMA_OUTSTANDING_VC6_ALLOC__MASK                                                 0x00003000L
#define OUTSTANDING_VC_ALLOC__DMA_OUTSTANDING_VC7_ALLOC__MASK                                                 0x0000C000L
#define OUTSTANDING_VC_ALLOC__DMA_OUTSTANDING_THRD__MASK                                                      0x000F0000L
#define OUTSTANDING_VC_ALLOC__HST_OUTSTANDING_VC0_ALLOC__MASK                                                 0x03000000L
#define OUTSTANDING_VC_ALLOC__HST_OUTSTANDING_VC1_ALLOC__MASK                                                 0x0C000000L
#define OUTSTANDING_VC_ALLOC__HST_OUTSTANDING_THRD__MASK                                                      0xF0000000L
//BIFC_MISC_CTRL0
#define BIFC_MISC_CTRL0__VWIRE_TARG_UNITID_CHECK_EN__MASK                                                     0x00000001L
#define BIFC_MISC_CTRL0__VWIRE_SRC_UNITID_CHECK_EN__MASK                                                      0x00000006L
#define BIFC_MISC_CTRL0__DMA_CHAIN_BREAK_IN_RCMODE__MASK                                                      0x00000100L
#define BIFC_MISC_CTRL0__HST_ARB_CHAIN_LOCK__MASK                                                             0x00000200L
#define BIFC_MISC_CTRL0__GSI_SST_ARB_CHAIN_LOCK__MASK                                                         0x00000400L
#define BIFC_MISC_CTRL0__DMA_ATOMIC_LENGTH_CHK_DIS__MASK                                                      0x00010000L
#define BIFC_MISC_CTRL0__DMA_ATOMIC_FAILED_STS_SEL__MASK                                                      0x00020000L
#define BIFC_MISC_CTRL0__PCIE_CAPABILITY_PROT_DIS__MASK                                                       0x01000000L
#define BIFC_MISC_CTRL0__VC7_DMA_IOCFG_DIS__MASK                                                              0x02000000L
#define BIFC_MISC_CTRL0__DMA_2ND_REQ_DIS__MASK                                                                0x04000000L
#define BIFC_MISC_CTRL0__PORT_DSTATE_BYPASS_MODE__MASK                                                        0x08000000L
#define BIFC_MISC_CTRL0__PME_TURNOFF_MODE__MASK                                                               0x10000000L
#define BIFC_MISC_CTRL0__PCIESWUS_SELECTION__MASK                                                             0x80000000L
//BIFC_MISC_CTRL1
#define BIFC_MISC_CTRL1__THT_HST_CPLD_POISON_REPORT__MASK                                                     0x00000001L
#define BIFC_MISC_CTRL1__DMA_REQ_POISON_REPORT__MASK                                                          0x00000002L
#define BIFC_MISC_CTRL1__DMA_REQ_ACSVIO_REPORT__MASK                                                          0x00000004L
#define BIFC_MISC_CTRL1__DMA_RSP_POISON_CPLD_REPORT__MASK                                                     0x00000008L
#define BIFC_MISC_CTRL1__GSI_SMN_WORST_ERR_STSTUS__MASK                                                       0x00000010L
#define BIFC_MISC_CTRL1__GSI_SDP_RDRSP_DATA_FORCE1_FOR_ERROR__MASK                                            0x00000020L
#define BIFC_MISC_CTRL1__GSI_RDWR_BALANCE_DIS__MASK                                                           0x00000040L
#define BIFC_MISC_CTRL1__GMI_MSG_BLOCKLVL_SEL__MASK                                                           0x00000080L
#define BIFC_MISC_CTRL1__HST_UNSUPPORT_SDPCMD_STS__MASK                                                       0x00000300L
#define BIFC_MISC_CTRL1__HST_UNSUPPORT_SDPCMD_DATASTS__MASK                                                   0x00000C00L
#define BIFC_MISC_CTRL1__DROP_OTHER_HT_ADDR_REQ__MASK                                                         0x00001000L
#define BIFC_MISC_CTRL1__DMAWRREQ_HSTRDRSP_ORDER_FORCE__MASK                                                  0x00002000L
#define BIFC_MISC_CTRL1__DMAWRREQ_HSTRDRSP_ORDER_FORCE_VALUE__MASK                                            0x00004000L
#define BIFC_MISC_CTRL1__UPS_SDP_RDY_TIE1__MASK                                                               0x00008000L
#define BIFC_MISC_CTRL1__GMI_RCC_DN_BME_DROP_DIS__MASK                                                        0x00010000L
#define BIFC_MISC_CTRL1__GMI_RCC_EP_BME_DROP_DIS__MASK                                                        0x00020000L
#define BIFC_MISC_CTRL1__GMI_BIH_DN_BME_DROP_DIS__MASK                                                        0x00040000L
#define BIFC_MISC_CTRL1__GMI_BIH_EP_BME_DROP_DIS__MASK                                                        0x00080000L
//BIFC_BME_ERR_LOG
#define BIFC_BME_ERR_LOG__DMA_ON_BME_LOW_DEV0_F0__MASK                                                        0x00000001L
#define BIFC_BME_ERR_LOG__DMA_ON_BME_LOW_DEV0_F1__MASK                                                        0x00000002L
#define BIFC_BME_ERR_LOG__DMA_ON_BME_LOW_DEV0_F2__MASK                                                        0x00000004L
#define BIFC_BME_ERR_LOG__DMA_ON_BME_LOW_DEV0_F3__MASK                                                        0x00000008L
#define BIFC_BME_ERR_LOG__DMA_ON_BME_LOW_DEV0_F4__MASK                                                        0x00000010L
#define BIFC_BME_ERR_LOG__DMA_ON_BME_LOW_DEV0_F5__MASK                                                        0x00000020L
#define BIFC_BME_ERR_LOG__DMA_ON_BME_LOW_DEV0_F6__MASK                                                        0x00000040L
#define BIFC_BME_ERR_LOG__DMA_ON_BME_LOW_DEV0_F7__MASK                                                        0x00000080L
#define BIFC_BME_ERR_LOG__CLEAR_DMA_ON_BME_LOW_DEV0_F0__MASK                                                  0x00010000L
#define BIFC_BME_ERR_LOG__CLEAR_DMA_ON_BME_LOW_DEV0_F1__MASK                                                  0x00020000L
#define BIFC_BME_ERR_LOG__CLEAR_DMA_ON_BME_LOW_DEV0_F2__MASK                                                  0x00040000L
#define BIFC_BME_ERR_LOG__CLEAR_DMA_ON_BME_LOW_DEV0_F3__MASK                                                  0x00080000L
#define BIFC_BME_ERR_LOG__CLEAR_DMA_ON_BME_LOW_DEV0_F4__MASK                                                  0x00100000L
#define BIFC_BME_ERR_LOG__CLEAR_DMA_ON_BME_LOW_DEV0_F5__MASK                                                  0x00200000L
#define BIFC_BME_ERR_LOG__CLEAR_DMA_ON_BME_LOW_DEV0_F6__MASK                                                  0x00400000L
#define BIFC_BME_ERR_LOG__CLEAR_DMA_ON_BME_LOW_DEV0_F7__MASK                                                  0x00800000L
//BIFC_RCCBIH_BME_ERR_LOG
#define BIFC_RCCBIH_BME_ERR_LOG__RCCBIH_ON_BME_LOW_DEV0_F0__MASK                                              0x00000001L
#define BIFC_RCCBIH_BME_ERR_LOG__RCCBIH_ON_BME_LOW_DEV0_F1__MASK                                              0x00000002L
#define BIFC_RCCBIH_BME_ERR_LOG__RCCBIH_ON_BME_LOW_DEV0_F2__MASK                                              0x00000004L
#define BIFC_RCCBIH_BME_ERR_LOG__RCCBIH_ON_BME_LOW_DEV0_F3__MASK                                              0x00000008L
#define BIFC_RCCBIH_BME_ERR_LOG__RCCBIH_ON_BME_LOW_DEV0_F4__MASK                                              0x00000010L
#define BIFC_RCCBIH_BME_ERR_LOG__RCCBIH_ON_BME_LOW_DEV0_F5__MASK                                              0x00000020L
#define BIFC_RCCBIH_BME_ERR_LOG__RCCBIH_ON_BME_LOW_DEV0_F6__MASK                                              0x00000040L
#define BIFC_RCCBIH_BME_ERR_LOG__RCCBIH_ON_BME_LOW_DEV0_F7__MASK                                              0x00000080L
#define BIFC_RCCBIH_BME_ERR_LOG__CLEAR_RCCBIH_ON_BME_LOW_DEV0_F0__MASK                                        0x00010000L
#define BIFC_RCCBIH_BME_ERR_LOG__CLEAR_RCCBIH_ON_BME_LOW_DEV0_F1__MASK                                        0x00020000L
#define BIFC_RCCBIH_BME_ERR_LOG__CLEAR_RCCBIH_ON_BME_LOW_DEV0_F2__MASK                                        0x00040000L
#define BIFC_RCCBIH_BME_ERR_LOG__CLEAR_RCCBIH_ON_BME_LOW_DEV0_F3__MASK                                        0x00080000L
#define BIFC_RCCBIH_BME_ERR_LOG__CLEAR_RCCBIH_ON_BME_LOW_DEV0_F4__MASK                                        0x00100000L
#define BIFC_RCCBIH_BME_ERR_LOG__CLEAR_RCCBIH_ON_BME_LOW_DEV0_F5__MASK                                        0x00200000L
#define BIFC_RCCBIH_BME_ERR_LOG__CLEAR_RCCBIH_ON_BME_LOW_DEV0_F6__MASK                                        0x00400000L
#define BIFC_RCCBIH_BME_ERR_LOG__CLEAR_RCCBIH_ON_BME_LOW_DEV0_F7__MASK                                        0x00800000L
//BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_IDO_OVERIDE_P_DEV0_F0__MASK                                     0x00000003L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_IDO_OVERIDE_NP_DEV0_F0__MASK                                    0x0000000CL
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_RO_OVERIDE_P_DEV0_F0__MASK                                      0x000000C0L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_RO_OVERIDE_NP_DEV0_F0__MASK                                     0x00000300L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_SNR_OVERIDE_P_DEV0_F0__MASK                                     0x00000C00L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_SNR_OVERIDE_NP_DEV0_F0__MASK                                    0x00003000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_IDO_OVERIDE_P_DEV0_F1__MASK                                     0x00030000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_IDO_OVERIDE_NP_DEV0_F1__MASK                                    0x000C0000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_RO_OVERIDE_P_DEV0_F1__MASK                                      0x00C00000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_RO_OVERIDE_NP_DEV0_F1__MASK                                     0x03000000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_SNR_OVERIDE_P_DEV0_F1__MASK                                     0x0C000000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F0_F1__TX_SNR_OVERIDE_NP_DEV0_F1__MASK                                    0x30000000L
//BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_IDO_OVERIDE_P_DEV0_F2__MASK                                     0x00000003L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_IDO_OVERIDE_NP_DEV0_F2__MASK                                    0x0000000CL
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_RO_OVERIDE_P_DEV0_F2__MASK                                      0x000000C0L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_RO_OVERIDE_NP_DEV0_F2__MASK                                     0x00000300L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_SNR_OVERIDE_P_DEV0_F2__MASK                                     0x00000C00L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_SNR_OVERIDE_NP_DEV0_F2__MASK                                    0x00003000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_IDO_OVERIDE_P_DEV0_F3__MASK                                     0x00030000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_IDO_OVERIDE_NP_DEV0_F3__MASK                                    0x000C0000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_RO_OVERIDE_P_DEV0_F3__MASK                                      0x00C00000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_RO_OVERIDE_NP_DEV0_F3__MASK                                     0x03000000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_SNR_OVERIDE_P_DEV0_F3__MASK                                     0x0C000000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F2_F3__TX_SNR_OVERIDE_NP_DEV0_F3__MASK                                    0x30000000L
//BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_IDO_OVERIDE_P_DEV0_F4__MASK                                     0x00000003L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_IDO_OVERIDE_NP_DEV0_F4__MASK                                    0x0000000CL
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_RO_OVERIDE_P_DEV0_F4__MASK                                      0x000000C0L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_RO_OVERIDE_NP_DEV0_F4__MASK                                     0x00000300L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_SNR_OVERIDE_P_DEV0_F4__MASK                                     0x00000C00L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_SNR_OVERIDE_NP_DEV0_F4__MASK                                    0x00003000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_IDO_OVERIDE_P_DEV0_F5__MASK                                     0x00030000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_IDO_OVERIDE_NP_DEV0_F5__MASK                                    0x000C0000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_RO_OVERIDE_P_DEV0_F5__MASK                                      0x00C00000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_RO_OVERIDE_NP_DEV0_F5__MASK                                     0x03000000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_SNR_OVERIDE_P_DEV0_F5__MASK                                     0x0C000000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F4_F5__TX_SNR_OVERIDE_NP_DEV0_F5__MASK                                    0x30000000L
//BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_IDO_OVERIDE_P_DEV0_F6__MASK                                     0x00000003L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_IDO_OVERIDE_NP_DEV0_F6__MASK                                    0x0000000CL
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_RO_OVERIDE_P_DEV0_F6__MASK                                      0x000000C0L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_RO_OVERIDE_NP_DEV0_F6__MASK                                     0x00000300L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_SNR_OVERIDE_P_DEV0_F6__MASK                                     0x00000C00L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_SNR_OVERIDE_NP_DEV0_F6__MASK                                    0x00003000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_IDO_OVERIDE_P_DEV0_F7__MASK                                     0x00030000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_IDO_OVERIDE_NP_DEV0_F7__MASK                                    0x000C0000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_RO_OVERIDE_P_DEV0_F7__MASK                                      0x00C00000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_RO_OVERIDE_NP_DEV0_F7__MASK                                     0x03000000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_SNR_OVERIDE_P_DEV0_F7__MASK                                     0x0C000000L
#define BIFC_DMA_ATTR_OVERRIDE_DEV0_F6_F7__TX_SNR_OVERIDE_NP_DEV0_F7__MASK                                    0x30000000L
//NBIF_VWIRE_CTRL
#define NBIF_VWIRE_CTRL__SMN_VWR_RESET_DELAY_CNT__MASK                                                        0x000000F0L
#define NBIF_VWIRE_CTRL__SMN_VWR_POSTED__MASK                                                                 0x00000100L
#define NBIF_VWIRE_CTRL__SDP_VWR_RESET_DELAY_CNT__MASK                                                        0x00F00000L
#define NBIF_VWIRE_CTRL__SDP_VWR_BLOCKLVL__MASK                                                               0x0C000000L
//NBIF_SMN_VWR_VCHG_DIS_CTRL
#define NBIF_SMN_VWR_VCHG_DIS_CTRL__SMN_VWR_VCHG_SET0_DIS__MASK                                               0x00000001L
#define NBIF_SMN_VWR_VCHG_DIS_CTRL__SMN_VWR_VCHG_SET1_DIS__MASK                                               0x00000002L
#define NBIF_SMN_VWR_VCHG_DIS_CTRL__SMN_VWR_VCHG_SET2_DIS__MASK                                               0x00000004L
#define NBIF_SMN_VWR_VCHG_DIS_CTRL__SMN_VWR_VCHG_SET3_DIS__MASK                                               0x00000008L
#define NBIF_SMN_VWR_VCHG_DIS_CTRL__SMN_VWR_VCHG_SET4_DIS__MASK                                               0x00000010L
#define NBIF_SMN_VWR_VCHG_DIS_CTRL__SMN_VWR_VCHG_SET5_DIS__MASK                                               0x00000020L
#define NBIF_SMN_VWR_VCHG_DIS_CTRL__SMN_VWR_VCHG_SET6_DIS__MASK                                               0x00000040L
//NBIF_SMN_VWR_VCHG_RST_CTRL0
#define NBIF_SMN_VWR_VCHG_RST_CTRL0__SMN_VWR_VCHG_SET0_RST_DEF_REV__MASK                                      0x00000001L
#define NBIF_SMN_VWR_VCHG_RST_CTRL0__SMN_VWR_VCHG_SET1_RST_DEF_REV__MASK                                      0x00000002L
#define NBIF_SMN_VWR_VCHG_RST_CTRL0__SMN_VWR_VCHG_SET2_RST_DEF_REV__MASK                                      0x00000004L
#define NBIF_SMN_VWR_VCHG_RST_CTRL0__SMN_VWR_VCHG_SET3_RST_DEF_REV__MASK                                      0x00000008L
#define NBIF_SMN_VWR_VCHG_RST_CTRL0__SMN_VWR_VCHG_SET4_RST_DEF_REV__MASK                                      0x00000010L
#define NBIF_SMN_VWR_VCHG_RST_CTRL0__SMN_VWR_VCHG_SET5_RST_DEF_REV__MASK                                      0x00000020L
#define NBIF_SMN_VWR_VCHG_RST_CTRL0__SMN_VWR_VCHG_SET6_RST_DEF_REV__MASK                                      0x00000040L
//NBIF_SMN_VWR_VCHG_TRIG
#define NBIF_SMN_VWR_VCHG_TRIG__SMN_VWR_VCHG_SET0_TRIG__MASK                                                  0x00000001L
#define NBIF_SMN_VWR_VCHG_TRIG__SMN_VWR_VCHG_SET1_TRIG__MASK                                                  0x00000002L
#define NBIF_SMN_VWR_VCHG_TRIG__SMN_VWR_VCHG_SET2_TRIG__MASK                                                  0x00000004L
#define NBIF_SMN_VWR_VCHG_TRIG__SMN_VWR_VCHG_SET3_TRIG__MASK                                                  0x00000008L
#define NBIF_SMN_VWR_VCHG_TRIG__SMN_VWR_VCHG_SET4_TRIG__MASK                                                  0x00000010L
#define NBIF_SMN_VWR_VCHG_TRIG__SMN_VWR_VCHG_SET5_TRIG__MASK                                                  0x00000020L
#define NBIF_SMN_VWR_VCHG_TRIG__SMN_VWR_VCHG_SET6_TRIG__MASK                                                  0x00000040L
//NBIF_SMN_VWR_WTRIG_CNTL
#define NBIF_SMN_VWR_WTRIG_CNTL__SMN_VWR_WTRIG_SET0_DIS__MASK                                                 0x00000001L
#define NBIF_SMN_VWR_WTRIG_CNTL__SMN_VWR_WTRIG_SET1_DIS__MASK                                                 0x00000002L
#define NBIF_SMN_VWR_WTRIG_CNTL__SMN_VWR_WTRIG_SET2_DIS__MASK                                                 0x00000004L
#define NBIF_SMN_VWR_WTRIG_CNTL__SMN_VWR_WTRIG_SET3_DIS__MASK                                                 0x00000008L
#define NBIF_SMN_VWR_WTRIG_CNTL__SMN_VWR_WTRIG_SET4_DIS__MASK                                                 0x00000010L
#define NBIF_SMN_VWR_WTRIG_CNTL__SMN_VWR_WTRIG_SET5_DIS__MASK                                                 0x00000020L
#define NBIF_SMN_VWR_WTRIG_CNTL__SMN_VWR_WTRIG_SET6_DIS__MASK                                                 0x00000040L
//NBIF_SMN_VWR_VCHG_DIS_CTRL_1
#define NBIF_SMN_VWR_VCHG_DIS_CTRL_1__SMN_VWR_VCHG_SET0_DIFFDET_DEF_REV__MASK                                 0x00000001L
#define NBIF_SMN_VWR_VCHG_DIS_CTRL_1__SMN_VWR_VCHG_SET1_DIFFDET_DEF_REV__MASK                                 0x00000002L
#define NBIF_SMN_VWR_VCHG_DIS_CTRL_1__SMN_VWR_VCHG_SET2_DIFFDET_DEF_REV__MASK                                 0x00000004L
#define NBIF_SMN_VWR_VCHG_DIS_CTRL_1__SMN_VWR_VCHG_SET3_DIFFDET_DEF_REV__MASK                                 0x00000008L
#define NBIF_SMN_VWR_VCHG_DIS_CTRL_1__SMN_VWR_VCHG_SET4_DIFFDET_DEF_REV__MASK                                 0x00000010L
#define NBIF_SMN_VWR_VCHG_DIS_CTRL_1__SMN_VWR_VCHG_SET5_DIFFDET_DEF_REV__MASK                                 0x00000020L
#define NBIF_SMN_VWR_VCHG_DIS_CTRL_1__SMN_VWR_VCHG_SET6_DIFFDET_DEF_REV__MASK                                 0x00000040L
//NBIF_MGCG_CTRL
#define NBIF_MGCG_CTRL__NBIF_MGCG_EN__MASK                                                                    0x00000001L
#define NBIF_MGCG_CTRL__NBIF_MGCG_MODE__MASK                                                                  0x00000002L
#define NBIF_MGCG_CTRL__NBIF_MGCG_HYSTERESIS__MASK                                                            0x000003FCL
//NBIF_DS_CTRL_LCLK
#define NBIF_DS_CTRL_LCLK__NBIF_LCLK_DS_EN__MASK                                                              0x00000001L
#define NBIF_DS_CTRL_LCLK__NBIF_LCLK_DS_TIMER__MASK                                                           0xFFFF0000L
//SMN_MST_CNTL0
#define SMN_MST_CNTL0__SMN_ARB_MODE__MASK                                                                     0x00000003L
#define SMN_MST_CNTL0__SMN_ZERO_BE_WR_EN_UPS__MASK                                                            0x00000100L
#define SMN_MST_CNTL0__SMN_ZERO_BE_RD_EN_UPS__MASK                                                            0x00000200L
#define SMN_MST_CNTL0__SMN_POST_MASK_EN_UPS__MASK                                                             0x00000400L
#define SMN_MST_CNTL0__MULTI_SMN_TRANS_ID_DIS_UPS__MASK                                                       0x00000800L
#define SMN_MST_CNTL0__SMN_ZERO_BE_WR_EN_DNS_DEV0__MASK                                                       0x00010000L
#define SMN_MST_CNTL0__SMN_ZERO_BE_RD_EN_DNS_DEV0__MASK                                                       0x00100000L
#define SMN_MST_CNTL0__SMN_POST_MASK_EN_DNS_DEV0__MASK                                                        0x01000000L
#define SMN_MST_CNTL0__MULTI_SMN_TRANS_ID_DIS_DNS_DEV0__MASK                                                  0x10000000L
//SMN_MST_EP_CNTL1
#define SMN_MST_EP_CNTL1__SMN_POST_MASK_EN_EP_DEV0_PF0__MASK                                                  0x00000001L
#define SMN_MST_EP_CNTL1__SMN_POST_MASK_EN_EP_DEV0_PF1__MASK                                                  0x00000002L
#define SMN_MST_EP_CNTL1__SMN_POST_MASK_EN_EP_DEV0_PF2__MASK                                                  0x00000004L
#define SMN_MST_EP_CNTL1__SMN_POST_MASK_EN_EP_DEV0_PF3__MASK                                                  0x00000008L
#define SMN_MST_EP_CNTL1__SMN_POST_MASK_EN_EP_DEV0_PF4__MASK                                                  0x00000010L
#define SMN_MST_EP_CNTL1__SMN_POST_MASK_EN_EP_DEV0_PF5__MASK                                                  0x00000020L
#define SMN_MST_EP_CNTL1__SMN_POST_MASK_EN_EP_DEV0_PF6__MASK                                                  0x00000040L
#define SMN_MST_EP_CNTL1__SMN_POST_MASK_EN_EP_DEV0_PF7__MASK                                                  0x00000080L
//SMN_MST_EP_CNTL2
#define SMN_MST_EP_CNTL2__MULTI_SMN_TRANS_ID_DIS_EP_DEV0_PF0__MASK                                            0x00000001L
#define SMN_MST_EP_CNTL2__MULTI_SMN_TRANS_ID_DIS_EP_DEV0_PF1__MASK                                            0x00000002L
#define SMN_MST_EP_CNTL2__MULTI_SMN_TRANS_ID_DIS_EP_DEV0_PF2__MASK                                            0x00000004L
#define SMN_MST_EP_CNTL2__MULTI_SMN_TRANS_ID_DIS_EP_DEV0_PF3__MASK                                            0x00000008L
#define SMN_MST_EP_CNTL2__MULTI_SMN_TRANS_ID_DIS_EP_DEV0_PF4__MASK                                            0x00000010L
#define SMN_MST_EP_CNTL2__MULTI_SMN_TRANS_ID_DIS_EP_DEV0_PF5__MASK                                            0x00000020L
#define SMN_MST_EP_CNTL2__MULTI_SMN_TRANS_ID_DIS_EP_DEV0_PF6__MASK                                            0x00000040L
#define SMN_MST_EP_CNTL2__MULTI_SMN_TRANS_ID_DIS_EP_DEV0_PF7__MASK                                            0x00000080L
//NBIF_SDP_VWR_VCHG_DIS_CTRL
#define NBIF_SDP_VWR_VCHG_DIS_CTRL__SDP_VWR_VCHG_ENDP_F0_DIS__MASK                                            0x00000001L
#define NBIF_SDP_VWR_VCHG_DIS_CTRL__SDP_VWR_VCHG_ENDP_F1_DIS__MASK                                            0x00000002L
#define NBIF_SDP_VWR_VCHG_DIS_CTRL__SDP_VWR_VCHG_ENDP_F2_DIS__MASK                                            0x00000004L
#define NBIF_SDP_VWR_VCHG_DIS_CTRL__SDP_VWR_VCHG_ENDP_F3_DIS__MASK                                            0x00000008L
#define NBIF_SDP_VWR_VCHG_DIS_CTRL__SDP_VWR_VCHG_ENDP_F4_DIS__MASK                                            0x00000010L
#define NBIF_SDP_VWR_VCHG_DIS_CTRL__SDP_VWR_VCHG_ENDP_F5_DIS__MASK                                            0x00000020L
#define NBIF_SDP_VWR_VCHG_DIS_CTRL__SDP_VWR_VCHG_ENDP_F6_DIS__MASK                                            0x00000040L
#define NBIF_SDP_VWR_VCHG_DIS_CTRL__SDP_VWR_VCHG_ENDP_F7_DIS__MASK                                            0x00000080L
#define NBIF_SDP_VWR_VCHG_DIS_CTRL__SDP_VWR_VCHG_SWDS_P0_DIS__MASK                                            0x01000000L
//NBIF_SDP_VWR_VCHG_RST_CTRL0
#define NBIF_SDP_VWR_VCHG_RST_CTRL0__SDP_VWR_VCHG_ENDP_F0_RST_OVRD_EN__MASK                                   0x00000001L
#define NBIF_SDP_VWR_VCHG_RST_CTRL0__SDP_VWR_VCHG_ENDP_F1_RST_OVRD_EN__MASK                                   0x00000002L
#define NBIF_SDP_VWR_VCHG_RST_CTRL0__SDP_VWR_VCHG_ENDP_F2_RST_OVRD_EN__MASK                                   0x00000004L
#define NBIF_SDP_VWR_VCHG_RST_CTRL0__SDP_VWR_VCHG_ENDP_F3_RST_OVRD_EN__MASK                                   0x00000008L
#define NBIF_SDP_VWR_VCHG_RST_CTRL0__SDP_VWR_VCHG_ENDP_F4_RST_OVRD_EN__MASK                                   0x00000010L
#define NBIF_SDP_VWR_VCHG_RST_CTRL0__SDP_VWR_VCHG_ENDP_F5_RST_OVRD_EN__MASK                                   0x00000020L
#define NBIF_SDP_VWR_VCHG_RST_CTRL0__SDP_VWR_VCHG_ENDP_F6_RST_OVRD_EN__MASK                                   0x00000040L
#define NBIF_SDP_VWR_VCHG_RST_CTRL0__SDP_VWR_VCHG_ENDP_F7_RST_OVRD_EN__MASK                                   0x00000080L
#define NBIF_SDP_VWR_VCHG_RST_CTRL0__SDP_VWR_VCHG_SWDS_P0_RST_OVRD_EN__MASK                                   0x01000000L
//NBIF_SDP_VWR_VCHG_RST_CTRL1
#define NBIF_SDP_VWR_VCHG_RST_CTRL1__SDP_VWR_VCHG_ENDP_F0_RST_OVRD_VAL__MASK                                  0x00000001L
#define NBIF_SDP_VWR_VCHG_RST_CTRL1__SDP_VWR_VCHG_ENDP_F1_RST_OVRD_VAL__MASK                                  0x00000002L
#define NBIF_SDP_VWR_VCHG_RST_CTRL1__SDP_VWR_VCHG_ENDP_F2_RST_OVRD_VAL__MASK                                  0x00000004L
#define NBIF_SDP_VWR_VCHG_RST_CTRL1__SDP_VWR_VCHG_ENDP_F3_RST_OVRD_VAL__MASK                                  0x00000008L
#define NBIF_SDP_VWR_VCHG_RST_CTRL1__SDP_VWR_VCHG_ENDP_F4_RST_OVRD_VAL__MASK                                  0x00000010L
#define NBIF_SDP_VWR_VCHG_RST_CTRL1__SDP_VWR_VCHG_ENDP_F5_RST_OVRD_VAL__MASK                                  0x00000020L
#define NBIF_SDP_VWR_VCHG_RST_CTRL1__SDP_VWR_VCHG_ENDP_F6_RST_OVRD_VAL__MASK                                  0x00000040L
#define NBIF_SDP_VWR_VCHG_RST_CTRL1__SDP_VWR_VCHG_ENDP_F7_RST_OVRD_VAL__MASK                                  0x00000080L
#define NBIF_SDP_VWR_VCHG_RST_CTRL1__SDP_VWR_VCHG_SWDS_P0_RST_OVRD_VAL__MASK                                  0x01000000L
//NBIF_SDP_VWR_VCHG_TRIG
#define NBIF_SDP_VWR_VCHG_TRIG__SDP_VWR_VCHG_ENDP_F0_TRIG__MASK                                               0x00000001L
#define NBIF_SDP_VWR_VCHG_TRIG__SDP_VWR_VCHG_ENDP_F1_TRIG__MASK                                               0x00000002L
#define NBIF_SDP_VWR_VCHG_TRIG__SDP_VWR_VCHG_ENDP_F2_TRIG__MASK                                               0x00000004L
#define NBIF_SDP_VWR_VCHG_TRIG__SDP_VWR_VCHG_ENDP_F3_TRIG__MASK                                               0x00000008L
#define NBIF_SDP_VWR_VCHG_TRIG__SDP_VWR_VCHG_ENDP_F4_TRIG__MASK                                               0x00000010L
#define NBIF_SDP_VWR_VCHG_TRIG__SDP_VWR_VCHG_ENDP_F5_TRIG__MASK                                               0x00000020L
#define NBIF_SDP_VWR_VCHG_TRIG__SDP_VWR_VCHG_ENDP_F6_TRIG__MASK                                               0x00000040L
#define NBIF_SDP_VWR_VCHG_TRIG__SDP_VWR_VCHG_ENDP_F7_TRIG__MASK                                               0x00000080L
#define NBIF_SDP_VWR_VCHG_TRIG__SDP_VWR_VCHG_SWDS_P0_TRIG__MASK                                               0x01000000L
//BME_DUMMY_CNTL_0
#define BME_DUMMY_CNTL_0__BME_DUMMY_RSPSTS_DEV0_F0__MASK                                                      0x00000003L
#define BME_DUMMY_CNTL_0__BME_DUMMY_RSPSTS_DEV0_F1__MASK                                                      0x0000000CL
#define BME_DUMMY_CNTL_0__BME_DUMMY_RSPSTS_DEV0_F2__MASK                                                      0x00000030L
#define BME_DUMMY_CNTL_0__BME_DUMMY_RSPSTS_DEV0_F3__MASK                                                      0x000000C0L
#define BME_DUMMY_CNTL_0__BME_DUMMY_RSPSTS_DEV0_F4__MASK                                                      0x00000300L
#define BME_DUMMY_CNTL_0__BME_DUMMY_RSPSTS_DEV0_F5__MASK                                                      0x00000C00L
#define BME_DUMMY_CNTL_0__BME_DUMMY_RSPSTS_DEV0_F6__MASK                                                      0x00003000L
#define BME_DUMMY_CNTL_0__BME_DUMMY_RSPSTS_DEV0_F7__MASK                                                      0x0000C000L
//BIFC_THT_CNTL
#define BIFC_THT_CNTL__CREDIT_ALLOC_THT_RD_VC0__MASK                                                          0x0000000FL
#define BIFC_THT_CNTL__CREDIT_ALLOC_THT_WR_VC0__MASK                                                          0x000000F0L
#define BIFC_THT_CNTL__CREDIT_ALLOC_THT_WR_VC1__MASK                                                          0x00000F00L
//BIFC_HSTARB_CNTL
#define BIFC_HSTARB_CNTL__SLVARB_MODE__MASK                                                                   0x00000003L
//BIFC_GSI_CNTL
#define BIFC_GSI_CNTL__GSI_SDP_RSP_ARB_MODE__MASK                                                             0x00000003L
#define BIFC_GSI_CNTL__GSI_CPL_RSP_ARB_MODE__MASK                                                             0x0000001CL
#define BIFC_GSI_CNTL__GSI_CPL_INTERLEAVING_EN__MASK                                                          0x00000020L
#define BIFC_GSI_CNTL__GSI_CPL_PCR_EP_CAUSE_UR_EN__MASK                                                       0x00000040L
#define BIFC_GSI_CNTL__GSI_CPL_SMN_P_EP_CAUSE_UR_EN__MASK                                                     0x00000080L
#define BIFC_GSI_CNTL__GSI_CPL_SMN_NP_EP_CAUSE_UR_EN__MASK                                                    0x00000100L
#define BIFC_GSI_CNTL__GSI_CPL_SST_EP_CAUSE_UR_EN__MASK                                                       0x00000200L
#define BIFC_GSI_CNTL__GSI_SDP_REQ_ARB_MODE__MASK                                                             0x00000C00L
#define BIFC_GSI_CNTL__GSI_SMN_REQ_ARB_MODE__MASK                                                             0x00003000L
//BIFC_PCIEFUNC_CNTL
#define BIFC_PCIEFUNC_CNTL__DMA_NON_PCIEFUNC_BUSDEVFUNC__MASK                                                 0x0000FFFFL
#define BIFC_PCIEFUNC_CNTL__MP1SYSHUBDATA_DRAM_IS_PCIEFUNC__MASK                                              0x00010000L
//BIFC_SDP_CNTL_0
#define BIFC_SDP_CNTL_0__HRP_SDP_DISCON_HYSTERESIS__MASK                                                      0x0000003FL
#define BIFC_SDP_CNTL_0__GSI_SDP_DISCON_HYSTERESIS__MASK                                                      0x00000FC0L
#define BIFC_SDP_CNTL_0__GMI_DNS_SDP_DISCON_HYSTERESIS__MASK                                                  0x0003F000L
#define BIFC_SDP_CNTL_0__GMI_UPS_SDP_DISCON_HYSTERESIS__MASK                                                  0x00FC0000L
//BIFC_PERF_CNTL_0
#define BIFC_PERF_CNTL_0__PERF_CNT_MMIO_RD_EN__MASK                                                           0x00000001L
#define BIFC_PERF_CNTL_0__PERF_CNT_MMIO_WR_EN__MASK                                                           0x00000002L
#define BIFC_PERF_CNTL_0__PERF_CNT_MMIO_RD_RESET__MASK                                                        0x00000100L
#define BIFC_PERF_CNTL_0__PERF_CNT_MMIO_WR_RESET__MASK                                                        0x00000200L
#define BIFC_PERF_CNTL_0__PERF_CNT_MMIO_RD_SEL__MASK                                                          0x001F0000L
#define BIFC_PERF_CNTL_0__PERF_CNT_MMIO_WR_SEL__MASK                                                          0x1F000000L
//BIFC_PERF_CNTL_1
#define BIFC_PERF_CNTL_1__PERF_CNT_DMA_RD_EN__MASK                                                            0x00000001L
#define BIFC_PERF_CNTL_1__PERF_CNT_DMA_WR_EN__MASK                                                            0x00000002L
#define BIFC_PERF_CNTL_1__PERF_CNT_DMA_RD_RESET__MASK                                                         0x00000100L
#define BIFC_PERF_CNTL_1__PERF_CNT_DMA_WR_RESET__MASK                                                         0x00000200L
#define BIFC_PERF_CNTL_1__PERF_CNT_DMA_RD_SEL__MASK                                                           0x003F0000L
#define BIFC_PERF_CNTL_1__PERF_CNT_DMA_WR_SEL__MASK                                                           0x7F000000L
//BIFC_PERF_CNT_MMIO_RD
#define BIFC_PERF_CNT_MMIO_RD__PERF_CNT_MMIO_RD_VALUE__MASK                                                   0xFFFFFFFFL
//BIFC_PERF_CNT_MMIO_WR
#define BIFC_PERF_CNT_MMIO_WR__PERF_CNT_MMIO_WR_VALUE__MASK                                                   0xFFFFFFFFL
//BIFC_PERF_CNT_DMA_RD
#define BIFC_PERF_CNT_DMA_RD__PERF_CNT_DMA_RD_VALUE__MASK                                                     0xFFFFFFFFL
//BIFC_PERF_CNT_DMA_WR
#define BIFC_PERF_CNT_DMA_WR__PERF_CNT_DMA_WR_VALUE__MASK                                                     0xFFFFFFFFL
//NBIF_REGIF_ERRSET_CTRL
#define NBIF_REGIF_ERRSET_CTRL__DROP_NONPF_MMREGREQ_SETERR_DIS__MASK                                          0x00000001L
//SMN_MST_EP_CNTL3
#define SMN_MST_EP_CNTL3__SMN_ZERO_BE_WR_EN_EP_DEV0_PF0__MASK                                                 0x00000001L
#define SMN_MST_EP_CNTL3__SMN_ZERO_BE_WR_EN_EP_DEV0_PF1__MASK                                                 0x00000002L
#define SMN_MST_EP_CNTL3__SMN_ZERO_BE_WR_EN_EP_DEV0_PF2__MASK                                                 0x00000004L
#define SMN_MST_EP_CNTL3__SMN_ZERO_BE_WR_EN_EP_DEV0_PF3__MASK                                                 0x00000008L
#define SMN_MST_EP_CNTL3__SMN_ZERO_BE_WR_EN_EP_DEV0_PF4__MASK                                                 0x00000010L
#define SMN_MST_EP_CNTL3__SMN_ZERO_BE_WR_EN_EP_DEV0_PF5__MASK                                                 0x00000020L
#define SMN_MST_EP_CNTL3__SMN_ZERO_BE_WR_EN_EP_DEV0_PF6__MASK                                                 0x00000040L
#define SMN_MST_EP_CNTL3__SMN_ZERO_BE_WR_EN_EP_DEV0_PF7__MASK                                                 0x00000080L
//SMN_MST_EP_CNTL4
#define SMN_MST_EP_CNTL4__SMN_ZERO_BE_RD_EN_EP_DEV0_PF0__MASK                                                 0x00000001L
#define SMN_MST_EP_CNTL4__SMN_ZERO_BE_RD_EN_EP_DEV0_PF1__MASK                                                 0x00000002L
#define SMN_MST_EP_CNTL4__SMN_ZERO_BE_RD_EN_EP_DEV0_PF2__MASK                                                 0x00000004L
#define SMN_MST_EP_CNTL4__SMN_ZERO_BE_RD_EN_EP_DEV0_PF3__MASK                                                 0x00000008L
#define SMN_MST_EP_CNTL4__SMN_ZERO_BE_RD_EN_EP_DEV0_PF4__MASK                                                 0x00000010L
#define SMN_MST_EP_CNTL4__SMN_ZERO_BE_RD_EN_EP_DEV0_PF5__MASK                                                 0x00000020L
#define SMN_MST_EP_CNTL4__SMN_ZERO_BE_RD_EN_EP_DEV0_PF6__MASK                                                 0x00000040L
#define SMN_MST_EP_CNTL4__SMN_ZERO_BE_RD_EN_EP_DEV0_PF7__MASK                                                 0x00000080L
//BIF_SELFRING_BUFFER_VID
#define BIF_SELFRING_BUFFER_VID__DOORBELL_MONITOR_CID__MASK                                                   0x000000FFL
#define BIF_SELFRING_BUFFER_VID__IOHUB_RAS_INTR_CID__MASK                                                     0x0000FF00L
//BIF_SELFRING_VECTOR_CNTL
#define BIF_SELFRING_VECTOR_CNTL__MISC_DB_MNTR_INTR_DIS__MASK                                                 0x00000001L


// addressBlock: bif_ras_bif_ras_regblk
//BIF_RAS_LEAF0_CTRL
#define BIF_RAS_LEAF0_CTRL__POISON_DET_EN__MASK                                                               0x00000001L
#define BIF_RAS_LEAF0_CTRL__POISON_ERREVENT_EN__MASK                                                          0x00000002L
#define BIF_RAS_LEAF0_CTRL__POISON_STALL_EN__MASK                                                             0x00000004L
#define BIF_RAS_LEAF0_CTRL__PARITY_DET_EN__MASK                                                               0x00000010L
#define BIF_RAS_LEAF0_CTRL__PARITY_ERREVENT_EN__MASK                                                          0x00000020L
#define BIF_RAS_LEAF0_CTRL__PARITY_STALL_EN__MASK                                                             0x00000040L
#define BIF_RAS_LEAF0_CTRL__ERR_EVENT_RECV__MASK                                                              0x00010000L
#define BIF_RAS_LEAF0_CTRL__LINK_DIS_RECV__MASK                                                               0x00020000L
#define BIF_RAS_LEAF0_CTRL__POISON_ERR_DET__MASK                                                              0x00040000L
#define BIF_RAS_LEAF0_CTRL__PARITY_ERR_DET__MASK                                                              0x00080000L
#define BIF_RAS_LEAF0_CTRL__ERR_EVENT_SENT__MASK                                                              0x00100000L
#define BIF_RAS_LEAF0_CTRL__EGRESS_STALLED__MASK                                                              0x00200000L
//BIF_RAS_LEAF1_CTRL
#define BIF_RAS_LEAF1_CTRL__POISON_DET_EN__MASK                                                               0x00000001L
#define BIF_RAS_LEAF1_CTRL__POISON_ERREVENT_EN__MASK                                                          0x00000002L
#define BIF_RAS_LEAF1_CTRL__POISON_STALL_EN__MASK                                                             0x00000004L
#define BIF_RAS_LEAF1_CTRL__PARITY_DET_EN__MASK                                                               0x00000010L
#define BIF_RAS_LEAF1_CTRL__PARITY_ERREVENT_EN__MASK                                                          0x00000020L
#define BIF_RAS_LEAF1_CTRL__PARITY_STALL_EN__MASK                                                             0x00000040L
#define BIF_RAS_LEAF1_CTRL__ERR_EVENT_RECV__MASK                                                              0x00010000L
#define BIF_RAS_LEAF1_CTRL__LINK_DIS_RECV__MASK                                                               0x00020000L
#define BIF_RAS_LEAF1_CTRL__POISON_ERR_DET__MASK                                                              0x00040000L
#define BIF_RAS_LEAF1_CTRL__PARITY_ERR_DET__MASK                                                              0x00080000L
#define BIF_RAS_LEAF1_CTRL__ERR_EVENT_SENT__MASK                                                              0x00100000L
#define BIF_RAS_LEAF1_CTRL__EGRESS_STALLED__MASK                                                              0x00200000L
//BIF_RAS_LEAF2_CTRL
#define BIF_RAS_LEAF2_CTRL__POISON_DET_EN__MASK                                                               0x00000001L
#define BIF_RAS_LEAF2_CTRL__POISON_ERREVENT_EN__MASK                                                          0x00000002L
#define BIF_RAS_LEAF2_CTRL__POISON_STALL_EN__MASK                                                             0x00000004L
#define BIF_RAS_LEAF2_CTRL__PARITY_DET_EN__MASK                                                               0x00000010L
#define BIF_RAS_LEAF2_CTRL__PARITY_ERREVENT_EN__MASK                                                          0x00000020L
#define BIF_RAS_LEAF2_CTRL__PARITY_STALL_EN__MASK                                                             0x00000040L
#define BIF_RAS_LEAF2_CTRL__ERR_EVENT_RECV__MASK                                                              0x00010000L
#define BIF_RAS_LEAF2_CTRL__LINK_DIS_RECV__MASK                                                               0x00020000L
#define BIF_RAS_LEAF2_CTRL__POISON_ERR_DET__MASK                                                              0x00040000L
#define BIF_RAS_LEAF2_CTRL__PARITY_ERR_DET__MASK                                                              0x00080000L
#define BIF_RAS_LEAF2_CTRL__ERR_EVENT_SENT__MASK                                                              0x00100000L
#define BIF_RAS_LEAF2_CTRL__EGRESS_STALLED__MASK                                                              0x00200000L
//BIF_RAS_MISC_CTRL
#define BIF_RAS_MISC_CTRL__LINKDIS_TRIG_ERREVENT_EN__MASK                                                     0x00000001L
//BIF_IOHUB_RAS_IH_CNTL
#define BIF_IOHUB_RAS_IH_CNTL__RAS_IH_INTR_EN__MASK                                                           0x00000001L
//BIF_RAS_VWR_FROM_IOHUB
#define BIF_RAS_VWR_FROM_IOHUB__RAS_IH_INTR_TRIG__MASK                                                        0x00000001L


// addressBlock: rcc_pfc_amdgfx_RCCPFCDEC
//RCC_PFC_LTR_CNTL
#define RCC_PFC_LTR_CNTL__SNOOP_LATENCY_VALUE__MASK                                                           0x000003FFL
#define RCC_PFC_LTR_CNTL__SNOOP_LATENCY_SCALE__MASK                                                           0x00001C00L
#define RCC_PFC_LTR_CNTL__SNOOP_REQUIREMENT__MASK                                                             0x00008000L
#define RCC_PFC_LTR_CNTL__NONSNOOP_LATENCY_VALUE__MASK                                                        0x03FF0000L
#define RCC_PFC_LTR_CNTL__NONSNOOP_LATENCY_SCALE__MASK                                                        0x1C000000L
#define RCC_PFC_LTR_CNTL__NONSNOOP_REQUIREMENT__MASK                                                          0x80000000L
//RCC_PFC_PME_RESTORE
#define RCC_PFC_PME_RESTORE__PME_RESTORE_PME_EN__MASK                                                         0x00000001L
#define RCC_PFC_PME_RESTORE__PME_RESTORE_PME_STATUS__MASK                                                     0x00000100L
//RCC_PFC_STICKY_RESTORE_0
#define RCC_PFC_STICKY_RESTORE_0__RESTORE_PSN_ERR_STATUS__MASK                                                0x00000001L
#define RCC_PFC_STICKY_RESTORE_0__RESTORE_CPL_TIMEOUT_STATUS__MASK                                            0x00000002L
#define RCC_PFC_STICKY_RESTORE_0__RESTORE_CPL_ABORT_ERR_STATUS__MASK                                          0x00000004L
#define RCC_PFC_STICKY_RESTORE_0__RESTORE_UNEXP_CPL_STATUS__MASK                                              0x00000008L
#define RCC_PFC_STICKY_RESTORE_0__RESTORE_MAL_TLP_STATUS__MASK                                                0x00000010L
#define RCC_PFC_STICKY_RESTORE_0__RESTORE_ECRC_ERR_STATUS__MASK                                               0x00000020L
#define RCC_PFC_STICKY_RESTORE_0__RESTORE_UNSUPP_REQ_ERR_STATUS__MASK                                         0x00000040L
#define RCC_PFC_STICKY_RESTORE_0__RESTORE_ADVISORY_NONFATAL_ERR_STATUS__MASK                                  0x00000080L
//RCC_PFC_STICKY_RESTORE_1
#define RCC_PFC_STICKY_RESTORE_1__RESTORE_TLP_HDR_0__MASK                                                     0xFFFFFFFFL
//RCC_PFC_STICKY_RESTORE_2
#define RCC_PFC_STICKY_RESTORE_2__RESTORE_TLP_HDR_1__MASK                                                     0xFFFFFFFFL
//RCC_PFC_STICKY_RESTORE_3
#define RCC_PFC_STICKY_RESTORE_3__RESTORE_TLP_HDR_2__MASK                                                     0xFFFFFFFFL
//RCC_PFC_STICKY_RESTORE_4
#define RCC_PFC_STICKY_RESTORE_4__RESTORE_TLP_HDR_3__MASK                                                     0xFFFFFFFFL
//RCC_PFC_STICKY_RESTORE_5
#define RCC_PFC_STICKY_RESTORE_5__RESTORE_TLP_PREFIX__MASK                                                    0xFFFFFFFFL
//RCC_PFC_AUXPWR_CNTL
#define RCC_PFC_AUXPWR_CNTL__AUX_CURRENT_OVERRIDE__MASK                                                       0x00000007L
#define RCC_PFC_AUXPWR_CNTL__AUX_POWER_DETECTED_OVERRIDE__MASK                                                0x00000008L


// addressBlock: rcc_pfc_amdgfxaz_RCCPFCDEC
//RCCPFCAMDGFXAZ_RCC_PFC_LTR_CNTL
#define RCCPFCAMDGFXAZ_RCC_PFC_LTR_CNTL__SNOOP_LATENCY_VALUE__MASK                                            0x000003FFL
#define RCCPFCAMDGFXAZ_RCC_PFC_LTR_CNTL__SNOOP_LATENCY_SCALE__MASK                                            0x00001C00L
#define RCCPFCAMDGFXAZ_RCC_PFC_LTR_CNTL__SNOOP_REQUIREMENT__MASK                                              0x00008000L
#define RCCPFCAMDGFXAZ_RCC_PFC_LTR_CNTL__NONSNOOP_LATENCY_VALUE__MASK                                         0x03FF0000L
#define RCCPFCAMDGFXAZ_RCC_PFC_LTR_CNTL__NONSNOOP_LATENCY_SCALE__MASK                                         0x1C000000L
#define RCCPFCAMDGFXAZ_RCC_PFC_LTR_CNTL__NONSNOOP_REQUIREMENT__MASK                                           0x80000000L
//RCCPFCAMDGFXAZ_RCC_PFC_PME_RESTORE
#define RCCPFCAMDGFXAZ_RCC_PFC_PME_RESTORE__PME_RESTORE_PME_EN__MASK                                          0x00000001L
#define RCCPFCAMDGFXAZ_RCC_PFC_PME_RESTORE__PME_RESTORE_PME_STATUS__MASK                                      0x00000100L
//RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_0
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_0__RESTORE_PSN_ERR_STATUS__MASK                                 0x00000001L
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_0__RESTORE_CPL_TIMEOUT_STATUS__MASK                             0x00000002L
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_0__RESTORE_CPL_ABORT_ERR_STATUS__MASK                           0x00000004L
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_0__RESTORE_UNEXP_CPL_STATUS__MASK                               0x00000008L
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_0__RESTORE_MAL_TLP_STATUS__MASK                                 0x00000010L
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_0__RESTORE_ECRC_ERR_STATUS__MASK                                0x00000020L
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_0__RESTORE_UNSUPP_REQ_ERR_STATUS__MASK                          0x00000040L
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_0__RESTORE_ADVISORY_NONFATAL_ERR_STATUS__MASK                   0x00000080L
//RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_1
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_1__RESTORE_TLP_HDR_0__MASK                                      0xFFFFFFFFL
//RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_2
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_2__RESTORE_TLP_HDR_1__MASK                                      0xFFFFFFFFL
//RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_3
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_3__RESTORE_TLP_HDR_2__MASK                                      0xFFFFFFFFL
//RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_4
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_4__RESTORE_TLP_HDR_3__MASK                                      0xFFFFFFFFL
//RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_5
#define RCCPFCAMDGFXAZ_RCC_PFC_STICKY_RESTORE_5__RESTORE_TLP_PREFIX__MASK                                     0xFFFFFFFFL
//RCCPFCAMDGFXAZ_RCC_PFC_AUXPWR_CNTL
#define RCCPFCAMDGFXAZ_RCC_PFC_AUXPWR_CNTL__AUX_CURRENT_OVERRIDE__MASK                                        0x00000007L
#define RCCPFCAMDGFXAZ_RCC_PFC_AUXPWR_CNTL__AUX_POWER_DETECTED_OVERRIDE__MASK                                 0x00000008L


// addressBlock: pciemsix_amdgfx_MSIXTDEC
//PCIEMSIX_VECT0_ADDR_LO
#define PCIEMSIX_VECT0_ADDR_LO__MSG_ADDR_LO__MASK                                                             0xFFFFFFFCL
//PCIEMSIX_VECT0_ADDR_HI
#define PCIEMSIX_VECT0_ADDR_HI__MSG_ADDR_HI__MASK                                                             0xFFFFFFFFL
//PCIEMSIX_VECT0_MSG_DATA
#define PCIEMSIX_VECT0_MSG_DATA__MSG_DATA__MASK                                                               0xFFFFFFFFL
//PCIEMSIX_VECT0_CONTROL
#define PCIEMSIX_VECT0_CONTROL__MASK_BIT__MASK                                                                0x00000001L
//PCIEMSIX_VECT1_ADDR_LO
#define PCIEMSIX_VECT1_ADDR_LO__MSG_ADDR_LO__MASK                                                             0xFFFFFFFCL
//PCIEMSIX_VECT1_ADDR_HI
#define PCIEMSIX_VECT1_ADDR_HI__MSG_ADDR_HI__MASK                                                             0xFFFFFFFFL
//PCIEMSIX_VECT1_MSG_DATA
#define PCIEMSIX_VECT1_MSG_DATA__MSG_DATA__MASK                                                               0xFFFFFFFFL
//PCIEMSIX_VECT1_CONTROL
#define PCIEMSIX_VECT1_CONTROL__MASK_BIT__MASK                                                                0x00000001L
//PCIEMSIX_VECT2_ADDR_LO
#define PCIEMSIX_VECT2_ADDR_LO__MSG_ADDR_LO__MASK                                                             0xFFFFFFFCL
//PCIEMSIX_VECT2_ADDR_HI
#define PCIEMSIX_VECT2_ADDR_HI__MSG_ADDR_HI__MASK                                                             0xFFFFFFFFL
//PCIEMSIX_VECT2_MSG_DATA
#define PCIEMSIX_VECT2_MSG_DATA__MSG_DATA__MASK                                                               0xFFFFFFFFL
//PCIEMSIX_VECT2_CONTROL
#define PCIEMSIX_VECT2_CONTROL__MASK_BIT__MASK                                                                0x00000001L
//PCIEMSIX_VECT3_ADDR_LO
#define PCIEMSIX_VECT3_ADDR_LO__MSG_ADDR_LO__MASK                                                             0xFFFFFFFCL
//PCIEMSIX_VECT3_ADDR_HI
#define PCIEMSIX_VECT3_ADDR_HI__MSG_ADDR_HI__MASK                                                             0xFFFFFFFFL
//PCIEMSIX_VECT3_MSG_DATA
#define PCIEMSIX_VECT3_MSG_DATA__MSG_DATA__MASK                                                               0xFFFFFFFFL
//PCIEMSIX_VECT3_CONTROL
#define PCIEMSIX_VECT3_CONTROL__MASK_BIT__MASK                                                                0x00000001L
//PCIEMSIX_VECT4_ADDR_LO
#define PCIEMSIX_VECT4_ADDR_LO__MSG_ADDR_LO__MASK                                                             0xFFFFFFFCL
//PCIEMSIX_VECT4_ADDR_HI
#define PCIEMSIX_VECT4_ADDR_HI__MSG_ADDR_HI__MASK                                                             0xFFFFFFFFL
//PCIEMSIX_VECT4_MSG_DATA
#define PCIEMSIX_VECT4_MSG_DATA__MSG_DATA__MASK                                                               0xFFFFFFFFL
//PCIEMSIX_VECT4_CONTROL
#define PCIEMSIX_VECT4_CONTROL__MASK_BIT__MASK                                                                0x00000001L
//PCIEMSIX_VECT5_ADDR_LO
#define PCIEMSIX_VECT5_ADDR_LO__MSG_ADDR_LO__MASK                                                             0xFFFFFFFCL
//PCIEMSIX_VECT5_ADDR_HI
#define PCIEMSIX_VECT5_ADDR_HI__MSG_ADDR_HI__MASK                                                             0xFFFFFFFFL
//PCIEMSIX_VECT5_MSG_DATA
#define PCIEMSIX_VECT5_MSG_DATA__MSG_DATA__MASK                                                               0xFFFFFFFFL
//PCIEMSIX_VECT5_CONTROL
#define PCIEMSIX_VECT5_CONTROL__MASK_BIT__MASK                                                                0x00000001L
//PCIEMSIX_VECT6_ADDR_LO
#define PCIEMSIX_VECT6_ADDR_LO__MSG_ADDR_LO__MASK                                                             0xFFFFFFFCL
//PCIEMSIX_VECT6_ADDR_HI
#define PCIEMSIX_VECT6_ADDR_HI__MSG_ADDR_HI__MASK                                                             0xFFFFFFFFL
//PCIEMSIX_VECT6_MSG_DATA
#define PCIEMSIX_VECT6_MSG_DATA__MSG_DATA__MASK                                                               0xFFFFFFFFL
//PCIEMSIX_VECT6_CONTROL
#define PCIEMSIX_VECT6_CONTROL__MASK_BIT__MASK                                                                0x00000001L
//PCIEMSIX_VECT7_ADDR_LO
#define PCIEMSIX_VECT7_ADDR_LO__MSG_ADDR_LO__MASK                                                             0xFFFFFFFCL
//PCIEMSIX_VECT7_ADDR_HI
#define PCIEMSIX_VECT7_ADDR_HI__MSG_ADDR_HI__MASK                                                             0xFFFFFFFFL
//PCIEMSIX_VECT7_MSG_DATA
#define PCIEMSIX_VECT7_MSG_DATA__MSG_DATA__MASK                                                               0xFFFFFFFFL
//PCIEMSIX_VECT7_CONTROL
#define PCIEMSIX_VECT7_CONTROL__MASK_BIT__MASK                                                                0x00000001L
//PCIEMSIX_VECT8_ADDR_LO
#define PCIEMSIX_VECT8_ADDR_LO__MSG_ADDR_LO__MASK                                                             0xFFFFFFFCL
//PCIEMSIX_VECT8_ADDR_HI
#define PCIEMSIX_VECT8_ADDR_HI__MSG_ADDR_HI__MASK                                                             0xFFFFFFFFL
//PCIEMSIX_VECT8_MSG_DATA
#define PCIEMSIX_VECT8_MSG_DATA__MSG_DATA__MASK                                                               0xFFFFFFFFL
//PCIEMSIX_VECT8_CONTROL
#define PCIEMSIX_VECT8_CONTROL__MASK_BIT__MASK                                                                0x00000001L
//PCIEMSIX_VECT9_ADDR_LO
#define PCIEMSIX_VECT9_ADDR_LO__MSG_ADDR_LO__MASK                                                             0xFFFFFFFCL
//PCIEMSIX_VECT9_ADDR_HI
#define PCIEMSIX_VECT9_ADDR_HI__MSG_ADDR_HI__MASK                                                             0xFFFFFFFFL
//PCIEMSIX_VECT9_MSG_DATA
#define PCIEMSIX_VECT9_MSG_DATA__MSG_DATA__MASK                                                               0xFFFFFFFFL
//PCIEMSIX_VECT9_CONTROL
#define PCIEMSIX_VECT9_CONTROL__MASK_BIT__MASK                                                                0x00000001L
//PCIEMSIX_VECT10_ADDR_LO
#define PCIEMSIX_VECT10_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT10_ADDR_HI
#define PCIEMSIX_VECT10_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT10_MSG_DATA
#define PCIEMSIX_VECT10_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT10_CONTROL
#define PCIEMSIX_VECT10_CONTROL__MASK_BIT__MASK                                                               0x00000001L
//PCIEMSIX_VECT11_ADDR_LO
#define PCIEMSIX_VECT11_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT11_ADDR_HI
#define PCIEMSIX_VECT11_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT11_MSG_DATA
#define PCIEMSIX_VECT11_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT11_CONTROL
#define PCIEMSIX_VECT11_CONTROL__MASK_BIT__MASK                                                               0x00000001L
//PCIEMSIX_VECT12_ADDR_LO
#define PCIEMSIX_VECT12_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT12_ADDR_HI
#define PCIEMSIX_VECT12_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT12_MSG_DATA
#define PCIEMSIX_VECT12_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT12_CONTROL
#define PCIEMSIX_VECT12_CONTROL__MASK_BIT__MASK                                                               0x00000001L
//PCIEMSIX_VECT13_ADDR_LO
#define PCIEMSIX_VECT13_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT13_ADDR_HI
#define PCIEMSIX_VECT13_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT13_MSG_DATA
#define PCIEMSIX_VECT13_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT13_CONTROL
#define PCIEMSIX_VECT13_CONTROL__MASK_BIT__MASK                                                               0x00000001L
//PCIEMSIX_VECT14_ADDR_LO
#define PCIEMSIX_VECT14_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT14_ADDR_HI
#define PCIEMSIX_VECT14_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT14_MSG_DATA
#define PCIEMSIX_VECT14_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT14_CONTROL
#define PCIEMSIX_VECT14_CONTROL__MASK_BIT__MASK                                                               0x00000001L
//PCIEMSIX_VECT15_ADDR_LO
#define PCIEMSIX_VECT15_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT15_ADDR_HI
#define PCIEMSIX_VECT15_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT15_MSG_DATA
#define PCIEMSIX_VECT15_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT15_CONTROL
#define PCIEMSIX_VECT15_CONTROL__MASK_BIT__MASK                                                               0x00000001L
//PCIEMSIX_VECT16_ADDR_LO
#define PCIEMSIX_VECT16_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT16_ADDR_HI
#define PCIEMSIX_VECT16_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT16_MSG_DATA
#define PCIEMSIX_VECT16_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT16_CONTROL
#define PCIEMSIX_VECT16_CONTROL__MASK_BIT__MASK                                                               0x00000001L
//PCIEMSIX_VECT17_ADDR_LO
#define PCIEMSIX_VECT17_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT17_ADDR_HI
#define PCIEMSIX_VECT17_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT17_MSG_DATA
#define PCIEMSIX_VECT17_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT17_CONTROL
#define PCIEMSIX_VECT17_CONTROL__MASK_BIT__MASK                                                               0x00000001L
//PCIEMSIX_VECT18_ADDR_LO
#define PCIEMSIX_VECT18_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT18_ADDR_HI
#define PCIEMSIX_VECT18_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT18_MSG_DATA
#define PCIEMSIX_VECT18_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT18_CONTROL
#define PCIEMSIX_VECT18_CONTROL__MASK_BIT__MASK                                                               0x00000001L
//PCIEMSIX_VECT19_ADDR_LO
#define PCIEMSIX_VECT19_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT19_ADDR_HI
#define PCIEMSIX_VECT19_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT19_MSG_DATA
#define PCIEMSIX_VECT19_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT19_CONTROL
#define PCIEMSIX_VECT19_CONTROL__MASK_BIT__MASK                                                               0x00000001L
//PCIEMSIX_VECT20_ADDR_LO
#define PCIEMSIX_VECT20_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT20_ADDR_HI
#define PCIEMSIX_VECT20_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT20_MSG_DATA
#define PCIEMSIX_VECT20_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT20_CONTROL
#define PCIEMSIX_VECT20_CONTROL__MASK_BIT__MASK                                                               0x00000001L
//PCIEMSIX_VECT21_ADDR_LO
#define PCIEMSIX_VECT21_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT21_ADDR_HI
#define PCIEMSIX_VECT21_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT21_MSG_DATA
#define PCIEMSIX_VECT21_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT21_CONTROL
#define PCIEMSIX_VECT21_CONTROL__MASK_BIT__MASK                                                               0x00000001L
//PCIEMSIX_VECT22_ADDR_LO
#define PCIEMSIX_VECT22_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT22_ADDR_HI
#define PCIEMSIX_VECT22_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT22_MSG_DATA
#define PCIEMSIX_VECT22_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT22_CONTROL
#define PCIEMSIX_VECT22_CONTROL__MASK_BIT__MASK                                                               0x00000001L
//PCIEMSIX_VECT23_ADDR_LO
#define PCIEMSIX_VECT23_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT23_ADDR_HI
#define PCIEMSIX_VECT23_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT23_MSG_DATA
#define PCIEMSIX_VECT23_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT23_CONTROL
#define PCIEMSIX_VECT23_CONTROL__MASK_BIT__MASK                                                               0x00000001L
//PCIEMSIX_VECT24_ADDR_LO
#define PCIEMSIX_VECT24_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT24_ADDR_HI
#define PCIEMSIX_VECT24_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT24_MSG_DATA
#define PCIEMSIX_VECT24_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT24_CONTROL
#define PCIEMSIX_VECT24_CONTROL__MASK_BIT__MASK                                                               0x00000001L
//PCIEMSIX_VECT25_ADDR_LO
#define PCIEMSIX_VECT25_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT25_ADDR_HI
#define PCIEMSIX_VECT25_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT25_MSG_DATA
#define PCIEMSIX_VECT25_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT25_CONTROL
#define PCIEMSIX_VECT25_CONTROL__MASK_BIT__MASK                                                               0x00000001L
//PCIEMSIX_VECT26_ADDR_LO
#define PCIEMSIX_VECT26_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT26_ADDR_HI
#define PCIEMSIX_VECT26_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT26_MSG_DATA
#define PCIEMSIX_VECT26_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT26_CONTROL
#define PCIEMSIX_VECT26_CONTROL__MASK_BIT__MASK                                                               0x00000001L
//PCIEMSIX_VECT27_ADDR_LO
#define PCIEMSIX_VECT27_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT27_ADDR_HI
#define PCIEMSIX_VECT27_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT27_MSG_DATA
#define PCIEMSIX_VECT27_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT27_CONTROL
#define PCIEMSIX_VECT27_CONTROL__MASK_BIT__MASK                                                               0x00000001L
//PCIEMSIX_VECT28_ADDR_LO
#define PCIEMSIX_VECT28_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT28_ADDR_HI
#define PCIEMSIX_VECT28_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT28_MSG_DATA
#define PCIEMSIX_VECT28_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT28_CONTROL
#define PCIEMSIX_VECT28_CONTROL__MASK_BIT__MASK                                                               0x00000001L
//PCIEMSIX_VECT29_ADDR_LO
#define PCIEMSIX_VECT29_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT29_ADDR_HI
#define PCIEMSIX_VECT29_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT29_MSG_DATA
#define PCIEMSIX_VECT29_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT29_CONTROL
#define PCIEMSIX_VECT29_CONTROL__MASK_BIT__MASK                                                               0x00000001L
//PCIEMSIX_VECT30_ADDR_LO
#define PCIEMSIX_VECT30_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT30_ADDR_HI
#define PCIEMSIX_VECT30_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT30_MSG_DATA
#define PCIEMSIX_VECT30_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT30_CONTROL
#define PCIEMSIX_VECT30_CONTROL__MASK_BIT__MASK                                                               0x00000001L
//PCIEMSIX_VECT31_ADDR_LO
#define PCIEMSIX_VECT31_ADDR_LO__MSG_ADDR_LO__MASK                                                            0xFFFFFFFCL
//PCIEMSIX_VECT31_ADDR_HI
#define PCIEMSIX_VECT31_ADDR_HI__MSG_ADDR_HI__MASK                                                            0xFFFFFFFFL
//PCIEMSIX_VECT31_MSG_DATA
#define PCIEMSIX_VECT31_MSG_DATA__MSG_DATA__MASK                                                              0xFFFFFFFFL
//PCIEMSIX_VECT31_CONTROL
#define PCIEMSIX_VECT31_CONTROL__MASK_BIT__MASK                                                               0x00000001L


// addressBlock: pciemsix_amdgfx_MSIXPDEC
//PCIEMSIX_PBA
#define PCIEMSIX_PBA__MSIX_PENDING_BITS__MASK                                                                 0xFFFFFFFFL


// addressBlock: syshub_mmreg_ind_syshubind
//SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__HST_CL0_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                     0x00000001L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__HST_CL1_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                     0x00000002L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__HST_CL2_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                     0x00000004L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__HST_CL3_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                     0x00000008L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__HST_CL4_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                     0x00000010L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__HST_CL5_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                     0x00000020L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__HST_CL6_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                     0x00000040L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__HST_CL7_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                     0x00000080L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__DMA_CL0_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                     0x00010000L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__DMA_CL1_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                     0x00020000L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__DMA_CL2_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                     0x00040000L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__DMA_CL3_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                     0x00080000L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__DMA_CL4_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                     0x00100000L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__DMA_CL5_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                     0x00200000L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__DMA_CL6_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                     0x00400000L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__DMA_CL7_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                     0x00800000L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__SYSHUB_SOCCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                      0x10000000L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SOCCLK__SYSHUB_SOCCLK_DS_EN__MASK                                       0x80000000L
//SYSHUBMMREGIND_SYSHUB_DS_CTRL2_SOCCLK
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL2_SOCCLK__SYSHUB_SOCCLK_DS_TIMER__MASK                                   0x0000FFFFL
//SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK__SYSHUB_bgen_socclk_HST_SW0_bypass_en__MASK   0x00000001L
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK__SYSHUB_bgen_socclk_HST_SW1_bypass_en__MASK   0x00000002L
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW0_bypass_en__MASK   0x00008000L
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW1_bypass_en__MASK   0x00010000L
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW2_bypass_en__MASK   0x00020000L
//SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK__SYSHUB_bgen_socclk_HST_SW0_imm_en__MASK         0x00000001L
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK__SYSHUB_bgen_socclk_HST_SW1_imm_en__MASK         0x00000002L
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW0_imm_en__MASK         0x00008000L
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW1_imm_en__MASK         0x00010000L
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SOCCLK__SYSHUB_bgen_socclk_DMA_SW2_imm_en__MASK         0x00020000L
//SYSHUBMMREGIND_DMA_CLK0_SW0_SYSHUB_QOS_CNTL
#define SYSHUBMMREGIND_DMA_CLK0_SW0_SYSHUB_QOS_CNTL__QOS_CNTL_MODE__MASK                                      0x00000001L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_SYSHUB_QOS_CNTL__QOS_MAX_VALUE__MASK                                      0x0000001EL
#define SYSHUBMMREGIND_DMA_CLK0_SW0_SYSHUB_QOS_CNTL__QOS_MIN_VALUE__MASK                                      0x000001E0L
//SYSHUBMMREGIND_DMA_CLK0_SW1_SYSHUB_QOS_CNTL
#define SYSHUBMMREGIND_DMA_CLK0_SW1_SYSHUB_QOS_CNTL__QOS_CNTL_MODE__MASK                                      0x00000001L
#define SYSHUBMMREGIND_DMA_CLK0_SW1_SYSHUB_QOS_CNTL__QOS_MAX_VALUE__MASK                                      0x0000001EL
#define SYSHUBMMREGIND_DMA_CLK0_SW1_SYSHUB_QOS_CNTL__QOS_MIN_VALUE__MASK                                      0x000001E0L
//SYSHUBMMREGIND_DMA_CLK0_SW0_CL0_CNTL
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL0_CNTL__FLR_ON_RS_RESET_EN__MASK                                        0x00000001L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL0_CNTL__LKRST_ON_RS_RESET_EN__MASK                                      0x00000002L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL0_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                    0x00000100L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL0_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                 0x00001E00L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL0_CNTL__READ_WRR_WEIGHT__MASK                                           0x00FF0000L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL0_CNTL__WRITE_WRR_WEIGHT__MASK                                          0xFF000000L
//SYSHUBMMREGIND_DMA_CLK0_SW0_CL1_CNTL
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL1_CNTL__FLR_ON_RS_RESET_EN__MASK                                        0x00000001L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL1_CNTL__LKRST_ON_RS_RESET_EN__MASK                                      0x00000002L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL1_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                    0x00000100L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL1_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                 0x00001E00L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL1_CNTL__READ_WRR_WEIGHT__MASK                                           0x00FF0000L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL1_CNTL__WRITE_WRR_WEIGHT__MASK                                          0xFF000000L
//SYSHUBMMREGIND_DMA_CLK0_SW0_CL2_CNTL
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL2_CNTL__FLR_ON_RS_RESET_EN__MASK                                        0x00000001L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL2_CNTL__LKRST_ON_RS_RESET_EN__MASK                                      0x00000002L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL2_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                    0x00000100L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL2_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                 0x00001E00L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL2_CNTL__READ_WRR_WEIGHT__MASK                                           0x00FF0000L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL2_CNTL__WRITE_WRR_WEIGHT__MASK                                          0xFF000000L
//SYSHUBMMREGIND_DMA_CLK0_SW0_CL3_CNTL
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL3_CNTL__FLR_ON_RS_RESET_EN__MASK                                        0x00000001L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL3_CNTL__LKRST_ON_RS_RESET_EN__MASK                                      0x00000002L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL3_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                    0x00000100L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL3_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                 0x00001E00L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL3_CNTL__READ_WRR_WEIGHT__MASK                                           0x00FF0000L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL3_CNTL__WRITE_WRR_WEIGHT__MASK                                          0xFF000000L
//SYSHUBMMREGIND_DMA_CLK0_SW0_CL4_CNTL
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL4_CNTL__FLR_ON_RS_RESET_EN__MASK                                        0x00000001L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL4_CNTL__LKRST_ON_RS_RESET_EN__MASK                                      0x00000002L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL4_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                    0x00000100L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL4_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                 0x00001E00L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL4_CNTL__READ_WRR_WEIGHT__MASK                                           0x00FF0000L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL4_CNTL__WRITE_WRR_WEIGHT__MASK                                          0xFF000000L
//SYSHUBMMREGIND_DMA_CLK0_SW0_CL5_CNTL
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL5_CNTL__FLR_ON_RS_RESET_EN__MASK                                        0x00000001L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL5_CNTL__LKRST_ON_RS_RESET_EN__MASK                                      0x00000002L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL5_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                    0x00000100L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL5_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                 0x00001E00L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL5_CNTL__READ_WRR_WEIGHT__MASK                                           0x00FF0000L
#define SYSHUBMMREGIND_DMA_CLK0_SW0_CL5_CNTL__WRITE_WRR_WEIGHT__MASK                                          0xFF000000L
//SYSHUBMMREGIND_DMA_CLK0_SW1_CL0_CNTL
#define SYSHUBMMREGIND_DMA_CLK0_SW1_CL0_CNTL__FLR_ON_RS_RESET_EN__MASK                                        0x00000001L
#define SYSHUBMMREGIND_DMA_CLK0_SW1_CL0_CNTL__LKRST_ON_RS_RESET_EN__MASK                                      0x00000002L
#define SYSHUBMMREGIND_DMA_CLK0_SW1_CL0_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                    0x00000100L
#define SYSHUBMMREGIND_DMA_CLK0_SW1_CL0_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                 0x00001E00L
#define SYSHUBMMREGIND_DMA_CLK0_SW1_CL0_CNTL__READ_WRR_WEIGHT__MASK                                           0x00FF0000L
#define SYSHUBMMREGIND_DMA_CLK0_SW1_CL0_CNTL__WRITE_WRR_WEIGHT__MASK                                          0xFF000000L
//SYSHUBMMREGIND_DMA_CLK0_SW2_CL0_CNTL
#define SYSHUBMMREGIND_DMA_CLK0_SW2_CL0_CNTL__FLR_ON_RS_RESET_EN__MASK                                        0x00000001L
#define SYSHUBMMREGIND_DMA_CLK0_SW2_CL0_CNTL__LKRST_ON_RS_RESET_EN__MASK                                      0x00000002L
#define SYSHUBMMREGIND_DMA_CLK0_SW2_CL0_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                    0x00000100L
#define SYSHUBMMREGIND_DMA_CLK0_SW2_CL0_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                 0x00001E00L
#define SYSHUBMMREGIND_DMA_CLK0_SW2_CL0_CNTL__READ_WRR_WEIGHT__MASK                                           0x00FF0000L
#define SYSHUBMMREGIND_DMA_CLK0_SW2_CL0_CNTL__WRITE_WRR_WEIGHT__MASK                                          0xFF000000L
//SYSHUBMMREGIND_SYSHUB_CG_CNTL
#define SYSHUBMMREGIND_SYSHUB_CG_CNTL__SYSHUB_CG_EN__MASK                                                     0x00000001L
#define SYSHUBMMREGIND_SYSHUB_CG_CNTL__SYSHUB_CG_IDLE_TIMER__MASK                                             0x0000FF00L
#define SYSHUBMMREGIND_SYSHUB_CG_CNTL__SYSHUB_CG_WAKEUP_TIMER__MASK                                           0x00FF0000L
//SYSHUBMMREGIND_SYSHUB_TRANS_IDLE
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF0__MASK                                         0x00000001L
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF1__MASK                                         0x00000002L
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF2__MASK                                         0x00000004L
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF3__MASK                                         0x00000008L
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF4__MASK                                         0x00000010L
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF5__MASK                                         0x00000020L
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF6__MASK                                         0x00000040L
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF7__MASK                                         0x00000080L
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF8__MASK                                         0x00000100L
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF9__MASK                                         0x00000200L
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF10__MASK                                        0x00000400L
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF11__MASK                                        0x00000800L
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF12__MASK                                        0x00001000L
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF13__MASK                                        0x00002000L
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF14__MASK                                        0x00004000L
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_VF15__MASK                                        0x00008000L
#define SYSHUBMMREGIND_SYSHUB_TRANS_IDLE__SYSHUB_TRANS_IDLE_PF__MASK                                          0x00010000L
//SYSHUBMMREGIND_SYSHUB_HP_TIMER
#define SYSHUBMMREGIND_SYSHUB_HP_TIMER__SYSHUB_HP_TIMER__MASK                                                 0xFFFFFFFFL
//SYSHUBMMREGIND_SYSHUB_SCRATCH
#define SYSHUBMMREGIND_SYSHUB_SCRATCH__SCRATCH__MASK                                                          0xFFFFFFFFL
//SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__HST_CL0_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                   0x00000001L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__HST_CL1_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                   0x00000002L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__HST_CL2_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                   0x00000004L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__HST_CL3_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                   0x00000008L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__HST_CL4_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                   0x00000010L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__HST_CL5_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                   0x00000020L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__HST_CL6_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                   0x00000040L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__HST_CL7_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                   0x00000080L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__DMA_CL0_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                   0x00010000L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__DMA_CL1_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                   0x00020000L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__DMA_CL2_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                   0x00040000L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__DMA_CL3_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                   0x00080000L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__DMA_CL4_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                   0x00100000L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__DMA_CL5_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                   0x00200000L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__DMA_CL6_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                   0x00400000L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__DMA_CL7_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                   0x00800000L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__SYSHUB_SHUBCLK_DEEPSLEEP_ALLOW_ENABLE__MASK                    0x10000000L
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL_SHUBCLK__SYSHUB_SHUBCLK_DS_EN__MASK                                     0x80000000L
//SYSHUBMMREGIND_SYSHUB_DS_CTRL2_SHUBCLK
#define SYSHUBMMREGIND_SYSHUB_DS_CTRL2_SHUBCLK__SYSHUB_SHUBCLK_DS_TIMER__MASK                                 0x0000FFFFL
//SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SHUBCLK
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SHUBCLK__SYSHUB_bgen_shubclk_DMA_SW0_bypass_en__MASK  0x00008000L
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SHUBCLK__SYSHUB_bgen_shubclk_DMA_SW1_bypass_en__MASK  0x00010000L
//SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SHUBCLK
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SHUBCLK__SYSHUB_bgen_shubclk_DMA_SW0_imm_en__MASK       0x00008000L
#define SYSHUBMMREGIND_SYSHUB_BGEN_ENHANCEMENT_IMM_EN_SHUBCLK__SYSHUB_bgen_shubclk_DMA_SW1_imm_en__MASK       0x00010000L
//SYSHUBMMREGIND_DMA_CLK1_SW0_SYSHUB_QOS_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW0_SYSHUB_QOS_CNTL__QOS_CNTL_MODE__MASK                                      0x00000001L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_SYSHUB_QOS_CNTL__QOS_MAX_VALUE__MASK                                      0x0000001EL
#define SYSHUBMMREGIND_DMA_CLK1_SW0_SYSHUB_QOS_CNTL__QOS_MIN_VALUE__MASK                                      0x000001E0L
//SYSHUBMMREGIND_DMA_CLK1_SW1_SYSHUB_QOS_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW1_SYSHUB_QOS_CNTL__QOS_CNTL_MODE__MASK                                      0x00000001L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_SYSHUB_QOS_CNTL__QOS_MAX_VALUE__MASK                                      0x0000001EL
#define SYSHUBMMREGIND_DMA_CLK1_SW1_SYSHUB_QOS_CNTL__QOS_MIN_VALUE__MASK                                      0x000001E0L
//SYSHUBMMREGIND_DMA_CLK1_SW0_CL0_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL0_CNTL__FLR_ON_RS_RESET_EN__MASK                                        0x00000001L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL0_CNTL__LKRST_ON_RS_RESET_EN__MASK                                      0x00000002L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL0_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                    0x00000100L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL0_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                 0x00001E00L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL0_CNTL__READ_WRR_WEIGHT__MASK                                           0x00FF0000L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL0_CNTL__WRITE_WRR_WEIGHT__MASK                                          0xFF000000L
//SYSHUBMMREGIND_DMA_CLK1_SW0_CL1_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL1_CNTL__FLR_ON_RS_RESET_EN__MASK                                        0x00000001L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL1_CNTL__LKRST_ON_RS_RESET_EN__MASK                                      0x00000002L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL1_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                    0x00000100L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL1_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                 0x00001E00L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL1_CNTL__READ_WRR_WEIGHT__MASK                                           0x00FF0000L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL1_CNTL__WRITE_WRR_WEIGHT__MASK                                          0xFF000000L
//SYSHUBMMREGIND_DMA_CLK1_SW0_CL2_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL2_CNTL__FLR_ON_RS_RESET_EN__MASK                                        0x00000001L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL2_CNTL__LKRST_ON_RS_RESET_EN__MASK                                      0x00000002L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL2_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                    0x00000100L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL2_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                 0x00001E00L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL2_CNTL__READ_WRR_WEIGHT__MASK                                           0x00FF0000L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL2_CNTL__WRITE_WRR_WEIGHT__MASK                                          0xFF000000L
//SYSHUBMMREGIND_DMA_CLK1_SW0_CL3_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL3_CNTL__FLR_ON_RS_RESET_EN__MASK                                        0x00000001L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL3_CNTL__LKRST_ON_RS_RESET_EN__MASK                                      0x00000002L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL3_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                    0x00000100L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL3_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                 0x00001E00L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL3_CNTL__READ_WRR_WEIGHT__MASK                                           0x00FF0000L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL3_CNTL__WRITE_WRR_WEIGHT__MASK                                          0xFF000000L
//SYSHUBMMREGIND_DMA_CLK1_SW0_CL4_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL4_CNTL__FLR_ON_RS_RESET_EN__MASK                                        0x00000001L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL4_CNTL__LKRST_ON_RS_RESET_EN__MASK                                      0x00000002L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL4_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                    0x00000100L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL4_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                 0x00001E00L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL4_CNTL__READ_WRR_WEIGHT__MASK                                           0x00FF0000L
#define SYSHUBMMREGIND_DMA_CLK1_SW0_CL4_CNTL__WRITE_WRR_WEIGHT__MASK                                          0xFF000000L
//SYSHUBMMREGIND_DMA_CLK1_SW1_CL0_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL0_CNTL__FLR_ON_RS_RESET_EN__MASK                                        0x00000001L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL0_CNTL__LKRST_ON_RS_RESET_EN__MASK                                      0x00000002L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL0_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                    0x00000100L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL0_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                 0x00001E00L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL0_CNTL__READ_WRR_WEIGHT__MASK                                           0x00FF0000L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL0_CNTL__WRITE_WRR_WEIGHT__MASK                                          0xFF000000L
//SYSHUBMMREGIND_DMA_CLK1_SW1_CL1_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL1_CNTL__FLR_ON_RS_RESET_EN__MASK                                        0x00000001L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL1_CNTL__LKRST_ON_RS_RESET_EN__MASK                                      0x00000002L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL1_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                    0x00000100L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL1_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                 0x00001E00L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL1_CNTL__READ_WRR_WEIGHT__MASK                                           0x00FF0000L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL1_CNTL__WRITE_WRR_WEIGHT__MASK                                          0xFF000000L
//SYSHUBMMREGIND_DMA_CLK1_SW1_CL2_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL2_CNTL__FLR_ON_RS_RESET_EN__MASK                                        0x00000001L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL2_CNTL__LKRST_ON_RS_RESET_EN__MASK                                      0x00000002L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL2_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                    0x00000100L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL2_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                 0x00001E00L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL2_CNTL__READ_WRR_WEIGHT__MASK                                           0x00FF0000L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL2_CNTL__WRITE_WRR_WEIGHT__MASK                                          0xFF000000L
//SYSHUBMMREGIND_DMA_CLK1_SW1_CL3_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL3_CNTL__FLR_ON_RS_RESET_EN__MASK                                        0x00000001L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL3_CNTL__LKRST_ON_RS_RESET_EN__MASK                                      0x00000002L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL3_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                    0x00000100L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL3_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                 0x00001E00L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL3_CNTL__READ_WRR_WEIGHT__MASK                                           0x00FF0000L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL3_CNTL__WRITE_WRR_WEIGHT__MASK                                          0xFF000000L
//SYSHUBMMREGIND_DMA_CLK1_SW1_CL4_CNTL
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL4_CNTL__FLR_ON_RS_RESET_EN__MASK                                        0x00000001L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL4_CNTL__LKRST_ON_RS_RESET_EN__MASK                                      0x00000002L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL4_CNTL__QOS_STATIC_OVERRIDE_EN__MASK                                    0x00000100L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL4_CNTL__QOS_STATIC_OVERRIDE_VALUE__MASK                                 0x00001E00L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL4_CNTL__READ_WRR_WEIGHT__MASK                                           0x00FF0000L
#define SYSHUBMMREGIND_DMA_CLK1_SW1_CL4_CNTL__WRITE_WRR_WEIGHT__MASK                                          0xFF000000L

#endif
