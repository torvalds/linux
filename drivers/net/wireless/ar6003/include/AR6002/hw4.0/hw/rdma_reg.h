// ------------------------------------------------------------------
// Copyright (c) 2004-2007 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
// ------------------------------------------------------------------
//===================================================================
// Author(s): ="Atheros"
//===================================================================


#ifndef _RDMA_REG_REG_H_
#define _RDMA_REG_REG_H_

#define DMA_CONFIG_ADDRESS                       0x00000000
#define DMA_CONFIG_OFFSET                        0x00000000
#define DMA_CONFIG_WLBB_PWD_EN_MSB               4
#define DMA_CONFIG_WLBB_PWD_EN_LSB               4
#define DMA_CONFIG_WLBB_PWD_EN_MASK              0x00000010
#define DMA_CONFIG_WLBB_PWD_EN_GET(x)            (((x) & DMA_CONFIG_WLBB_PWD_EN_MASK) >> DMA_CONFIG_WLBB_PWD_EN_LSB)
#define DMA_CONFIG_WLBB_PWD_EN_SET(x)            (((x) << DMA_CONFIG_WLBB_PWD_EN_LSB) & DMA_CONFIG_WLBB_PWD_EN_MASK)
#define DMA_CONFIG_WLMAC_PWD_EN_MSB              3
#define DMA_CONFIG_WLMAC_PWD_EN_LSB              3
#define DMA_CONFIG_WLMAC_PWD_EN_MASK             0x00000008
#define DMA_CONFIG_WLMAC_PWD_EN_GET(x)           (((x) & DMA_CONFIG_WLMAC_PWD_EN_MASK) >> DMA_CONFIG_WLMAC_PWD_EN_LSB)
#define DMA_CONFIG_WLMAC_PWD_EN_SET(x)           (((x) << DMA_CONFIG_WLMAC_PWD_EN_LSB) & DMA_CONFIG_WLMAC_PWD_EN_MASK)
#define DMA_CONFIG_ENABLE_RETENTION_MSB          2
#define DMA_CONFIG_ENABLE_RETENTION_LSB          2
#define DMA_CONFIG_ENABLE_RETENTION_MASK         0x00000004
#define DMA_CONFIG_ENABLE_RETENTION_GET(x)       (((x) & DMA_CONFIG_ENABLE_RETENTION_MASK) >> DMA_CONFIG_ENABLE_RETENTION_LSB)
#define DMA_CONFIG_ENABLE_RETENTION_SET(x)       (((x) << DMA_CONFIG_ENABLE_RETENTION_LSB) & DMA_CONFIG_ENABLE_RETENTION_MASK)
#define DMA_CONFIG_RTC_PRIORITY_MSB              1
#define DMA_CONFIG_RTC_PRIORITY_LSB              1
#define DMA_CONFIG_RTC_PRIORITY_MASK             0x00000002
#define DMA_CONFIG_RTC_PRIORITY_GET(x)           (((x) & DMA_CONFIG_RTC_PRIORITY_MASK) >> DMA_CONFIG_RTC_PRIORITY_LSB)
#define DMA_CONFIG_RTC_PRIORITY_SET(x)           (((x) << DMA_CONFIG_RTC_PRIORITY_LSB) & DMA_CONFIG_RTC_PRIORITY_MASK)
#define DMA_CONFIG_DMA_TYPE_MSB                  0
#define DMA_CONFIG_DMA_TYPE_LSB                  0
#define DMA_CONFIG_DMA_TYPE_MASK                 0x00000001
#define DMA_CONFIG_DMA_TYPE_GET(x)               (((x) & DMA_CONFIG_DMA_TYPE_MASK) >> DMA_CONFIG_DMA_TYPE_LSB)
#define DMA_CONFIG_DMA_TYPE_SET(x)               (((x) << DMA_CONFIG_DMA_TYPE_LSB) & DMA_CONFIG_DMA_TYPE_MASK)

#define DMA_CONTROL_ADDRESS                      0x00000004
#define DMA_CONTROL_OFFSET                       0x00000004
#define DMA_CONTROL_START_MSB                    1
#define DMA_CONTROL_START_LSB                    1
#define DMA_CONTROL_START_MASK                   0x00000002
#define DMA_CONTROL_START_GET(x)                 (((x) & DMA_CONTROL_START_MASK) >> DMA_CONTROL_START_LSB)
#define DMA_CONTROL_START_SET(x)                 (((x) << DMA_CONTROL_START_LSB) & DMA_CONTROL_START_MASK)
#define DMA_CONTROL_STOP_MSB                     0
#define DMA_CONTROL_STOP_LSB                     0
#define DMA_CONTROL_STOP_MASK                    0x00000001
#define DMA_CONTROL_STOP_GET(x)                  (((x) & DMA_CONTROL_STOP_MASK) >> DMA_CONTROL_STOP_LSB)
#define DMA_CONTROL_STOP_SET(x)                  (((x) << DMA_CONTROL_STOP_LSB) & DMA_CONTROL_STOP_MASK)

#define DMA_SRC_ADDRESS                          0x00000008
#define DMA_SRC_OFFSET                           0x00000008
#define DMA_SRC_ADDR_MSB                         31
#define DMA_SRC_ADDR_LSB                         2
#define DMA_SRC_ADDR_MASK                        0xfffffffc
#define DMA_SRC_ADDR_GET(x)                      (((x) & DMA_SRC_ADDR_MASK) >> DMA_SRC_ADDR_LSB)
#define DMA_SRC_ADDR_SET(x)                      (((x) << DMA_SRC_ADDR_LSB) & DMA_SRC_ADDR_MASK)

#define DMA_DEST_ADDRESS                         0x0000000c
#define DMA_DEST_OFFSET                          0x0000000c
#define DMA_DEST_ADDR_MSB                        31
#define DMA_DEST_ADDR_LSB                        2
#define DMA_DEST_ADDR_MASK                       0xfffffffc
#define DMA_DEST_ADDR_GET(x)                     (((x) & DMA_DEST_ADDR_MASK) >> DMA_DEST_ADDR_LSB)
#define DMA_DEST_ADDR_SET(x)                     (((x) << DMA_DEST_ADDR_LSB) & DMA_DEST_ADDR_MASK)

#define DMA_LENGTH_ADDRESS                       0x00000010
#define DMA_LENGTH_OFFSET                        0x00000010
#define DMA_LENGTH_WORDS_MSB                     11
#define DMA_LENGTH_WORDS_LSB                     0
#define DMA_LENGTH_WORDS_MASK                    0x00000fff
#define DMA_LENGTH_WORDS_GET(x)                  (((x) & DMA_LENGTH_WORDS_MASK) >> DMA_LENGTH_WORDS_LSB)
#define DMA_LENGTH_WORDS_SET(x)                  (((x) << DMA_LENGTH_WORDS_LSB) & DMA_LENGTH_WORDS_MASK)

#define VMC_BASE_ADDRESS                         0x00000014
#define VMC_BASE_OFFSET                          0x00000014
#define VMC_BASE_ADDR_MSB                        31
#define VMC_BASE_ADDR_LSB                        2
#define VMC_BASE_ADDR_MASK                       0xfffffffc
#define VMC_BASE_ADDR_GET(x)                     (((x) & VMC_BASE_ADDR_MASK) >> VMC_BASE_ADDR_LSB)
#define VMC_BASE_ADDR_SET(x)                     (((x) << VMC_BASE_ADDR_LSB) & VMC_BASE_ADDR_MASK)

#define INDIRECT_REG_ADDRESS                     0x00000018
#define INDIRECT_REG_OFFSET                      0x00000018
#define INDIRECT_REG_ID_MSB                      31
#define INDIRECT_REG_ID_LSB                      2
#define INDIRECT_REG_ID_MASK                     0xfffffffc
#define INDIRECT_REG_ID_GET(x)                   (((x) & INDIRECT_REG_ID_MASK) >> INDIRECT_REG_ID_LSB)
#define INDIRECT_REG_ID_SET(x)                   (((x) << INDIRECT_REG_ID_LSB) & INDIRECT_REG_ID_MASK)

#define INDIRECT_RETURN_ADDRESS                  0x0000001c
#define INDIRECT_RETURN_OFFSET                   0x0000001c
#define INDIRECT_RETURN_ADDR_MSB                 31
#define INDIRECT_RETURN_ADDR_LSB                 2
#define INDIRECT_RETURN_ADDR_MASK                0xfffffffc
#define INDIRECT_RETURN_ADDR_GET(x)              (((x) & INDIRECT_RETURN_ADDR_MASK) >> INDIRECT_RETURN_ADDR_LSB)
#define INDIRECT_RETURN_ADDR_SET(x)              (((x) << INDIRECT_RETURN_ADDR_LSB) & INDIRECT_RETURN_ADDR_MASK)

#define RDMA_REGION_0__ADDRESS                   0x00000020
#define RDMA_REGION_0__OFFSET                    0x00000020
#define RDMA_REGION_0__ADDR_MSB                  31
#define RDMA_REGION_0__ADDR_LSB                  13
#define RDMA_REGION_0__ADDR_MASK                 0xffffe000
#define RDMA_REGION_0__ADDR_GET(x)               (((x) & RDMA_REGION_0__ADDR_MASK) >> RDMA_REGION_0__ADDR_LSB)
#define RDMA_REGION_0__ADDR_SET(x)               (((x) << RDMA_REGION_0__ADDR_LSB) & RDMA_REGION_0__ADDR_MASK)
#define RDMA_REGION_0__LENGTH_MSB                12
#define RDMA_REGION_0__LENGTH_LSB                2
#define RDMA_REGION_0__LENGTH_MASK               0x00001ffc
#define RDMA_REGION_0__LENGTH_GET(x)             (((x) & RDMA_REGION_0__LENGTH_MASK) >> RDMA_REGION_0__LENGTH_LSB)
#define RDMA_REGION_0__LENGTH_SET(x)             (((x) << RDMA_REGION_0__LENGTH_LSB) & RDMA_REGION_0__LENGTH_MASK)
#define RDMA_REGION_0__INDI_MSB                  1
#define RDMA_REGION_0__INDI_LSB                  1
#define RDMA_REGION_0__INDI_MASK                 0x00000002
#define RDMA_REGION_0__INDI_GET(x)               (((x) & RDMA_REGION_0__INDI_MASK) >> RDMA_REGION_0__INDI_LSB)
#define RDMA_REGION_0__INDI_SET(x)               (((x) << RDMA_REGION_0__INDI_LSB) & RDMA_REGION_0__INDI_MASK)
#define RDMA_REGION_0__NEXT_MSB                  0
#define RDMA_REGION_0__NEXT_LSB                  0
#define RDMA_REGION_0__NEXT_MASK                 0x00000001
#define RDMA_REGION_0__NEXT_GET(x)               (((x) & RDMA_REGION_0__NEXT_MASK) >> RDMA_REGION_0__NEXT_LSB)
#define RDMA_REGION_0__NEXT_SET(x)               (((x) << RDMA_REGION_0__NEXT_LSB) & RDMA_REGION_0__NEXT_MASK)

#define RDMA_REGION_1__ADDRESS                   0x00000024
#define RDMA_REGION_1__OFFSET                    0x00000024
#define RDMA_REGION_1__ADDR_MSB                  31
#define RDMA_REGION_1__ADDR_LSB                  13
#define RDMA_REGION_1__ADDR_MASK                 0xffffe000
#define RDMA_REGION_1__ADDR_GET(x)               (((x) & RDMA_REGION_1__ADDR_MASK) >> RDMA_REGION_1__ADDR_LSB)
#define RDMA_REGION_1__ADDR_SET(x)               (((x) << RDMA_REGION_1__ADDR_LSB) & RDMA_REGION_1__ADDR_MASK)
#define RDMA_REGION_1__LENGTH_MSB                12
#define RDMA_REGION_1__LENGTH_LSB                2
#define RDMA_REGION_1__LENGTH_MASK               0x00001ffc
#define RDMA_REGION_1__LENGTH_GET(x)             (((x) & RDMA_REGION_1__LENGTH_MASK) >> RDMA_REGION_1__LENGTH_LSB)
#define RDMA_REGION_1__LENGTH_SET(x)             (((x) << RDMA_REGION_1__LENGTH_LSB) & RDMA_REGION_1__LENGTH_MASK)
#define RDMA_REGION_1__INDI_MSB                  1
#define RDMA_REGION_1__INDI_LSB                  1
#define RDMA_REGION_1__INDI_MASK                 0x00000002
#define RDMA_REGION_1__INDI_GET(x)               (((x) & RDMA_REGION_1__INDI_MASK) >> RDMA_REGION_1__INDI_LSB)
#define RDMA_REGION_1__INDI_SET(x)               (((x) << RDMA_REGION_1__INDI_LSB) & RDMA_REGION_1__INDI_MASK)
#define RDMA_REGION_1__NEXT_MSB                  0
#define RDMA_REGION_1__NEXT_LSB                  0
#define RDMA_REGION_1__NEXT_MASK                 0x00000001
#define RDMA_REGION_1__NEXT_GET(x)               (((x) & RDMA_REGION_1__NEXT_MASK) >> RDMA_REGION_1__NEXT_LSB)
#define RDMA_REGION_1__NEXT_SET(x)               (((x) << RDMA_REGION_1__NEXT_LSB) & RDMA_REGION_1__NEXT_MASK)

#define RDMA_REGION_2__ADDRESS                   0x00000028
#define RDMA_REGION_2__OFFSET                    0x00000028
#define RDMA_REGION_2__ADDR_MSB                  31
#define RDMA_REGION_2__ADDR_LSB                  13
#define RDMA_REGION_2__ADDR_MASK                 0xffffe000
#define RDMA_REGION_2__ADDR_GET(x)               (((x) & RDMA_REGION_2__ADDR_MASK) >> RDMA_REGION_2__ADDR_LSB)
#define RDMA_REGION_2__ADDR_SET(x)               (((x) << RDMA_REGION_2__ADDR_LSB) & RDMA_REGION_2__ADDR_MASK)
#define RDMA_REGION_2__LENGTH_MSB                12
#define RDMA_REGION_2__LENGTH_LSB                2
#define RDMA_REGION_2__LENGTH_MASK               0x00001ffc
#define RDMA_REGION_2__LENGTH_GET(x)             (((x) & RDMA_REGION_2__LENGTH_MASK) >> RDMA_REGION_2__LENGTH_LSB)
#define RDMA_REGION_2__LENGTH_SET(x)             (((x) << RDMA_REGION_2__LENGTH_LSB) & RDMA_REGION_2__LENGTH_MASK)
#define RDMA_REGION_2__INDI_MSB                  1
#define RDMA_REGION_2__INDI_LSB                  1
#define RDMA_REGION_2__INDI_MASK                 0x00000002
#define RDMA_REGION_2__INDI_GET(x)               (((x) & RDMA_REGION_2__INDI_MASK) >> RDMA_REGION_2__INDI_LSB)
#define RDMA_REGION_2__INDI_SET(x)               (((x) << RDMA_REGION_2__INDI_LSB) & RDMA_REGION_2__INDI_MASK)
#define RDMA_REGION_2__NEXT_MSB                  0
#define RDMA_REGION_2__NEXT_LSB                  0
#define RDMA_REGION_2__NEXT_MASK                 0x00000001
#define RDMA_REGION_2__NEXT_GET(x)               (((x) & RDMA_REGION_2__NEXT_MASK) >> RDMA_REGION_2__NEXT_LSB)
#define RDMA_REGION_2__NEXT_SET(x)               (((x) << RDMA_REGION_2__NEXT_LSB) & RDMA_REGION_2__NEXT_MASK)

#define RDMA_REGION_3__ADDRESS                   0x0000002c
#define RDMA_REGION_3__OFFSET                    0x0000002c
#define RDMA_REGION_3__ADDR_MSB                  31
#define RDMA_REGION_3__ADDR_LSB                  13
#define RDMA_REGION_3__ADDR_MASK                 0xffffe000
#define RDMA_REGION_3__ADDR_GET(x)               (((x) & RDMA_REGION_3__ADDR_MASK) >> RDMA_REGION_3__ADDR_LSB)
#define RDMA_REGION_3__ADDR_SET(x)               (((x) << RDMA_REGION_3__ADDR_LSB) & RDMA_REGION_3__ADDR_MASK)
#define RDMA_REGION_3__LENGTH_MSB                12
#define RDMA_REGION_3__LENGTH_LSB                2
#define RDMA_REGION_3__LENGTH_MASK               0x00001ffc
#define RDMA_REGION_3__LENGTH_GET(x)             (((x) & RDMA_REGION_3__LENGTH_MASK) >> RDMA_REGION_3__LENGTH_LSB)
#define RDMA_REGION_3__LENGTH_SET(x)             (((x) << RDMA_REGION_3__LENGTH_LSB) & RDMA_REGION_3__LENGTH_MASK)
#define RDMA_REGION_3__INDI_MSB                  1
#define RDMA_REGION_3__INDI_LSB                  1
#define RDMA_REGION_3__INDI_MASK                 0x00000002
#define RDMA_REGION_3__INDI_GET(x)               (((x) & RDMA_REGION_3__INDI_MASK) >> RDMA_REGION_3__INDI_LSB)
#define RDMA_REGION_3__INDI_SET(x)               (((x) << RDMA_REGION_3__INDI_LSB) & RDMA_REGION_3__INDI_MASK)
#define RDMA_REGION_3__NEXT_MSB                  0
#define RDMA_REGION_3__NEXT_LSB                  0
#define RDMA_REGION_3__NEXT_MASK                 0x00000001
#define RDMA_REGION_3__NEXT_GET(x)               (((x) & RDMA_REGION_3__NEXT_MASK) >> RDMA_REGION_3__NEXT_LSB)
#define RDMA_REGION_3__NEXT_SET(x)               (((x) << RDMA_REGION_3__NEXT_LSB) & RDMA_REGION_3__NEXT_MASK)

#define RDMA_REGION_4__ADDRESS                   0x00000030
#define RDMA_REGION_4__OFFSET                    0x00000030
#define RDMA_REGION_4__ADDR_MSB                  31
#define RDMA_REGION_4__ADDR_LSB                  13
#define RDMA_REGION_4__ADDR_MASK                 0xffffe000
#define RDMA_REGION_4__ADDR_GET(x)               (((x) & RDMA_REGION_4__ADDR_MASK) >> RDMA_REGION_4__ADDR_LSB)
#define RDMA_REGION_4__ADDR_SET(x)               (((x) << RDMA_REGION_4__ADDR_LSB) & RDMA_REGION_4__ADDR_MASK)
#define RDMA_REGION_4__LENGTH_MSB                12
#define RDMA_REGION_4__LENGTH_LSB                2
#define RDMA_REGION_4__LENGTH_MASK               0x00001ffc
#define RDMA_REGION_4__LENGTH_GET(x)             (((x) & RDMA_REGION_4__LENGTH_MASK) >> RDMA_REGION_4__LENGTH_LSB)
#define RDMA_REGION_4__LENGTH_SET(x)             (((x) << RDMA_REGION_4__LENGTH_LSB) & RDMA_REGION_4__LENGTH_MASK)
#define RDMA_REGION_4__INDI_MSB                  1
#define RDMA_REGION_4__INDI_LSB                  1
#define RDMA_REGION_4__INDI_MASK                 0x00000002
#define RDMA_REGION_4__INDI_GET(x)               (((x) & RDMA_REGION_4__INDI_MASK) >> RDMA_REGION_4__INDI_LSB)
#define RDMA_REGION_4__INDI_SET(x)               (((x) << RDMA_REGION_4__INDI_LSB) & RDMA_REGION_4__INDI_MASK)
#define RDMA_REGION_4__NEXT_MSB                  0
#define RDMA_REGION_4__NEXT_LSB                  0
#define RDMA_REGION_4__NEXT_MASK                 0x00000001
#define RDMA_REGION_4__NEXT_GET(x)               (((x) & RDMA_REGION_4__NEXT_MASK) >> RDMA_REGION_4__NEXT_LSB)
#define RDMA_REGION_4__NEXT_SET(x)               (((x) << RDMA_REGION_4__NEXT_LSB) & RDMA_REGION_4__NEXT_MASK)

#define RDMA_REGION_5__ADDRESS                   0x00000034
#define RDMA_REGION_5__OFFSET                    0x00000034
#define RDMA_REGION_5__ADDR_MSB                  31
#define RDMA_REGION_5__ADDR_LSB                  13
#define RDMA_REGION_5__ADDR_MASK                 0xffffe000
#define RDMA_REGION_5__ADDR_GET(x)               (((x) & RDMA_REGION_5__ADDR_MASK) >> RDMA_REGION_5__ADDR_LSB)
#define RDMA_REGION_5__ADDR_SET(x)               (((x) << RDMA_REGION_5__ADDR_LSB) & RDMA_REGION_5__ADDR_MASK)
#define RDMA_REGION_5__LENGTH_MSB                12
#define RDMA_REGION_5__LENGTH_LSB                2
#define RDMA_REGION_5__LENGTH_MASK               0x00001ffc
#define RDMA_REGION_5__LENGTH_GET(x)             (((x) & RDMA_REGION_5__LENGTH_MASK) >> RDMA_REGION_5__LENGTH_LSB)
#define RDMA_REGION_5__LENGTH_SET(x)             (((x) << RDMA_REGION_5__LENGTH_LSB) & RDMA_REGION_5__LENGTH_MASK)
#define RDMA_REGION_5__INDI_MSB                  1
#define RDMA_REGION_5__INDI_LSB                  1
#define RDMA_REGION_5__INDI_MASK                 0x00000002
#define RDMA_REGION_5__INDI_GET(x)               (((x) & RDMA_REGION_5__INDI_MASK) >> RDMA_REGION_5__INDI_LSB)
#define RDMA_REGION_5__INDI_SET(x)               (((x) << RDMA_REGION_5__INDI_LSB) & RDMA_REGION_5__INDI_MASK)
#define RDMA_REGION_5__NEXT_MSB                  0
#define RDMA_REGION_5__NEXT_LSB                  0
#define RDMA_REGION_5__NEXT_MASK                 0x00000001
#define RDMA_REGION_5__NEXT_GET(x)               (((x) & RDMA_REGION_5__NEXT_MASK) >> RDMA_REGION_5__NEXT_LSB)
#define RDMA_REGION_5__NEXT_SET(x)               (((x) << RDMA_REGION_5__NEXT_LSB) & RDMA_REGION_5__NEXT_MASK)

#define RDMA_REGION_6__ADDRESS                   0x00000038
#define RDMA_REGION_6__OFFSET                    0x00000038
#define RDMA_REGION_6__ADDR_MSB                  31
#define RDMA_REGION_6__ADDR_LSB                  13
#define RDMA_REGION_6__ADDR_MASK                 0xffffe000
#define RDMA_REGION_6__ADDR_GET(x)               (((x) & RDMA_REGION_6__ADDR_MASK) >> RDMA_REGION_6__ADDR_LSB)
#define RDMA_REGION_6__ADDR_SET(x)               (((x) << RDMA_REGION_6__ADDR_LSB) & RDMA_REGION_6__ADDR_MASK)
#define RDMA_REGION_6__LENGTH_MSB                12
#define RDMA_REGION_6__LENGTH_LSB                2
#define RDMA_REGION_6__LENGTH_MASK               0x00001ffc
#define RDMA_REGION_6__LENGTH_GET(x)             (((x) & RDMA_REGION_6__LENGTH_MASK) >> RDMA_REGION_6__LENGTH_LSB)
#define RDMA_REGION_6__LENGTH_SET(x)             (((x) << RDMA_REGION_6__LENGTH_LSB) & RDMA_REGION_6__LENGTH_MASK)
#define RDMA_REGION_6__INDI_MSB                  1
#define RDMA_REGION_6__INDI_LSB                  1
#define RDMA_REGION_6__INDI_MASK                 0x00000002
#define RDMA_REGION_6__INDI_GET(x)               (((x) & RDMA_REGION_6__INDI_MASK) >> RDMA_REGION_6__INDI_LSB)
#define RDMA_REGION_6__INDI_SET(x)               (((x) << RDMA_REGION_6__INDI_LSB) & RDMA_REGION_6__INDI_MASK)
#define RDMA_REGION_6__NEXT_MSB                  0
#define RDMA_REGION_6__NEXT_LSB                  0
#define RDMA_REGION_6__NEXT_MASK                 0x00000001
#define RDMA_REGION_6__NEXT_GET(x)               (((x) & RDMA_REGION_6__NEXT_MASK) >> RDMA_REGION_6__NEXT_LSB)
#define RDMA_REGION_6__NEXT_SET(x)               (((x) << RDMA_REGION_6__NEXT_LSB) & RDMA_REGION_6__NEXT_MASK)

#define RDMA_REGION_7__ADDRESS                   0x0000003c
#define RDMA_REGION_7__OFFSET                    0x0000003c
#define RDMA_REGION_7__ADDR_MSB                  31
#define RDMA_REGION_7__ADDR_LSB                  13
#define RDMA_REGION_7__ADDR_MASK                 0xffffe000
#define RDMA_REGION_7__ADDR_GET(x)               (((x) & RDMA_REGION_7__ADDR_MASK) >> RDMA_REGION_7__ADDR_LSB)
#define RDMA_REGION_7__ADDR_SET(x)               (((x) << RDMA_REGION_7__ADDR_LSB) & RDMA_REGION_7__ADDR_MASK)
#define RDMA_REGION_7__LENGTH_MSB                12
#define RDMA_REGION_7__LENGTH_LSB                2
#define RDMA_REGION_7__LENGTH_MASK               0x00001ffc
#define RDMA_REGION_7__LENGTH_GET(x)             (((x) & RDMA_REGION_7__LENGTH_MASK) >> RDMA_REGION_7__LENGTH_LSB)
#define RDMA_REGION_7__LENGTH_SET(x)             (((x) << RDMA_REGION_7__LENGTH_LSB) & RDMA_REGION_7__LENGTH_MASK)
#define RDMA_REGION_7__INDI_MSB                  1
#define RDMA_REGION_7__INDI_LSB                  1
#define RDMA_REGION_7__INDI_MASK                 0x00000002
#define RDMA_REGION_7__INDI_GET(x)               (((x) & RDMA_REGION_7__INDI_MASK) >> RDMA_REGION_7__INDI_LSB)
#define RDMA_REGION_7__INDI_SET(x)               (((x) << RDMA_REGION_7__INDI_LSB) & RDMA_REGION_7__INDI_MASK)
#define RDMA_REGION_7__NEXT_MSB                  0
#define RDMA_REGION_7__NEXT_LSB                  0
#define RDMA_REGION_7__NEXT_MASK                 0x00000001
#define RDMA_REGION_7__NEXT_GET(x)               (((x) & RDMA_REGION_7__NEXT_MASK) >> RDMA_REGION_7__NEXT_LSB)
#define RDMA_REGION_7__NEXT_SET(x)               (((x) << RDMA_REGION_7__NEXT_LSB) & RDMA_REGION_7__NEXT_MASK)

#define RDMA_REGION_8__ADDRESS                   0x00000040
#define RDMA_REGION_8__OFFSET                    0x00000040
#define RDMA_REGION_8__ADDR_MSB                  31
#define RDMA_REGION_8__ADDR_LSB                  13
#define RDMA_REGION_8__ADDR_MASK                 0xffffe000
#define RDMA_REGION_8__ADDR_GET(x)               (((x) & RDMA_REGION_8__ADDR_MASK) >> RDMA_REGION_8__ADDR_LSB)
#define RDMA_REGION_8__ADDR_SET(x)               (((x) << RDMA_REGION_8__ADDR_LSB) & RDMA_REGION_8__ADDR_MASK)
#define RDMA_REGION_8__LENGTH_MSB                12
#define RDMA_REGION_8__LENGTH_LSB                2
#define RDMA_REGION_8__LENGTH_MASK               0x00001ffc
#define RDMA_REGION_8__LENGTH_GET(x)             (((x) & RDMA_REGION_8__LENGTH_MASK) >> RDMA_REGION_8__LENGTH_LSB)
#define RDMA_REGION_8__LENGTH_SET(x)             (((x) << RDMA_REGION_8__LENGTH_LSB) & RDMA_REGION_8__LENGTH_MASK)
#define RDMA_REGION_8__INDI_MSB                  1
#define RDMA_REGION_8__INDI_LSB                  1
#define RDMA_REGION_8__INDI_MASK                 0x00000002
#define RDMA_REGION_8__INDI_GET(x)               (((x) & RDMA_REGION_8__INDI_MASK) >> RDMA_REGION_8__INDI_LSB)
#define RDMA_REGION_8__INDI_SET(x)               (((x) << RDMA_REGION_8__INDI_LSB) & RDMA_REGION_8__INDI_MASK)
#define RDMA_REGION_8__NEXT_MSB                  0
#define RDMA_REGION_8__NEXT_LSB                  0
#define RDMA_REGION_8__NEXT_MASK                 0x00000001
#define RDMA_REGION_8__NEXT_GET(x)               (((x) & RDMA_REGION_8__NEXT_MASK) >> RDMA_REGION_8__NEXT_LSB)
#define RDMA_REGION_8__NEXT_SET(x)               (((x) << RDMA_REGION_8__NEXT_LSB) & RDMA_REGION_8__NEXT_MASK)

#define RDMA_REGION_9__ADDRESS                   0x00000044
#define RDMA_REGION_9__OFFSET                    0x00000044
#define RDMA_REGION_9__ADDR_MSB                  31
#define RDMA_REGION_9__ADDR_LSB                  13
#define RDMA_REGION_9__ADDR_MASK                 0xffffe000
#define RDMA_REGION_9__ADDR_GET(x)               (((x) & RDMA_REGION_9__ADDR_MASK) >> RDMA_REGION_9__ADDR_LSB)
#define RDMA_REGION_9__ADDR_SET(x)               (((x) << RDMA_REGION_9__ADDR_LSB) & RDMA_REGION_9__ADDR_MASK)
#define RDMA_REGION_9__LENGTH_MSB                12
#define RDMA_REGION_9__LENGTH_LSB                2
#define RDMA_REGION_9__LENGTH_MASK               0x00001ffc
#define RDMA_REGION_9__LENGTH_GET(x)             (((x) & RDMA_REGION_9__LENGTH_MASK) >> RDMA_REGION_9__LENGTH_LSB)
#define RDMA_REGION_9__LENGTH_SET(x)             (((x) << RDMA_REGION_9__LENGTH_LSB) & RDMA_REGION_9__LENGTH_MASK)
#define RDMA_REGION_9__INDI_MSB                  1
#define RDMA_REGION_9__INDI_LSB                  1
#define RDMA_REGION_9__INDI_MASK                 0x00000002
#define RDMA_REGION_9__INDI_GET(x)               (((x) & RDMA_REGION_9__INDI_MASK) >> RDMA_REGION_9__INDI_LSB)
#define RDMA_REGION_9__INDI_SET(x)               (((x) << RDMA_REGION_9__INDI_LSB) & RDMA_REGION_9__INDI_MASK)
#define RDMA_REGION_9__NEXT_MSB                  0
#define RDMA_REGION_9__NEXT_LSB                  0
#define RDMA_REGION_9__NEXT_MASK                 0x00000001
#define RDMA_REGION_9__NEXT_GET(x)               (((x) & RDMA_REGION_9__NEXT_MASK) >> RDMA_REGION_9__NEXT_LSB)
#define RDMA_REGION_9__NEXT_SET(x)               (((x) << RDMA_REGION_9__NEXT_LSB) & RDMA_REGION_9__NEXT_MASK)

#define RDMA_REGION_10__ADDRESS                  0x00000048
#define RDMA_REGION_10__OFFSET                   0x00000048
#define RDMA_REGION_10__ADDR_MSB                 31
#define RDMA_REGION_10__ADDR_LSB                 13
#define RDMA_REGION_10__ADDR_MASK                0xffffe000
#define RDMA_REGION_10__ADDR_GET(x)              (((x) & RDMA_REGION_10__ADDR_MASK) >> RDMA_REGION_10__ADDR_LSB)
#define RDMA_REGION_10__ADDR_SET(x)              (((x) << RDMA_REGION_10__ADDR_LSB) & RDMA_REGION_10__ADDR_MASK)
#define RDMA_REGION_10__LENGTH_MSB               12
#define RDMA_REGION_10__LENGTH_LSB               2
#define RDMA_REGION_10__LENGTH_MASK              0x00001ffc
#define RDMA_REGION_10__LENGTH_GET(x)            (((x) & RDMA_REGION_10__LENGTH_MASK) >> RDMA_REGION_10__LENGTH_LSB)
#define RDMA_REGION_10__LENGTH_SET(x)            (((x) << RDMA_REGION_10__LENGTH_LSB) & RDMA_REGION_10__LENGTH_MASK)
#define RDMA_REGION_10__INDI_MSB                 1
#define RDMA_REGION_10__INDI_LSB                 1
#define RDMA_REGION_10__INDI_MASK                0x00000002
#define RDMA_REGION_10__INDI_GET(x)              (((x) & RDMA_REGION_10__INDI_MASK) >> RDMA_REGION_10__INDI_LSB)
#define RDMA_REGION_10__INDI_SET(x)              (((x) << RDMA_REGION_10__INDI_LSB) & RDMA_REGION_10__INDI_MASK)
#define RDMA_REGION_10__NEXT_MSB                 0
#define RDMA_REGION_10__NEXT_LSB                 0
#define RDMA_REGION_10__NEXT_MASK                0x00000001
#define RDMA_REGION_10__NEXT_GET(x)              (((x) & RDMA_REGION_10__NEXT_MASK) >> RDMA_REGION_10__NEXT_LSB)
#define RDMA_REGION_10__NEXT_SET(x)              (((x) << RDMA_REGION_10__NEXT_LSB) & RDMA_REGION_10__NEXT_MASK)

#define RDMA_REGION_11__ADDRESS                  0x0000004c
#define RDMA_REGION_11__OFFSET                   0x0000004c
#define RDMA_REGION_11__ADDR_MSB                 31
#define RDMA_REGION_11__ADDR_LSB                 13
#define RDMA_REGION_11__ADDR_MASK                0xffffe000
#define RDMA_REGION_11__ADDR_GET(x)              (((x) & RDMA_REGION_11__ADDR_MASK) >> RDMA_REGION_11__ADDR_LSB)
#define RDMA_REGION_11__ADDR_SET(x)              (((x) << RDMA_REGION_11__ADDR_LSB) & RDMA_REGION_11__ADDR_MASK)
#define RDMA_REGION_11__LENGTH_MSB               12
#define RDMA_REGION_11__LENGTH_LSB               2
#define RDMA_REGION_11__LENGTH_MASK              0x00001ffc
#define RDMA_REGION_11__LENGTH_GET(x)            (((x) & RDMA_REGION_11__LENGTH_MASK) >> RDMA_REGION_11__LENGTH_LSB)
#define RDMA_REGION_11__LENGTH_SET(x)            (((x) << RDMA_REGION_11__LENGTH_LSB) & RDMA_REGION_11__LENGTH_MASK)
#define RDMA_REGION_11__INDI_MSB                 1
#define RDMA_REGION_11__INDI_LSB                 1
#define RDMA_REGION_11__INDI_MASK                0x00000002
#define RDMA_REGION_11__INDI_GET(x)              (((x) & RDMA_REGION_11__INDI_MASK) >> RDMA_REGION_11__INDI_LSB)
#define RDMA_REGION_11__INDI_SET(x)              (((x) << RDMA_REGION_11__INDI_LSB) & RDMA_REGION_11__INDI_MASK)
#define RDMA_REGION_11__NEXT_MSB                 0
#define RDMA_REGION_11__NEXT_LSB                 0
#define RDMA_REGION_11__NEXT_MASK                0x00000001
#define RDMA_REGION_11__NEXT_GET(x)              (((x) & RDMA_REGION_11__NEXT_MASK) >> RDMA_REGION_11__NEXT_LSB)
#define RDMA_REGION_11__NEXT_SET(x)              (((x) << RDMA_REGION_11__NEXT_LSB) & RDMA_REGION_11__NEXT_MASK)

#define RDMA_REGION_12__ADDRESS                  0x00000050
#define RDMA_REGION_12__OFFSET                   0x00000050
#define RDMA_REGION_12__ADDR_MSB                 31
#define RDMA_REGION_12__ADDR_LSB                 13
#define RDMA_REGION_12__ADDR_MASK                0xffffe000
#define RDMA_REGION_12__ADDR_GET(x)              (((x) & RDMA_REGION_12__ADDR_MASK) >> RDMA_REGION_12__ADDR_LSB)
#define RDMA_REGION_12__ADDR_SET(x)              (((x) << RDMA_REGION_12__ADDR_LSB) & RDMA_REGION_12__ADDR_MASK)
#define RDMA_REGION_12__LENGTH_MSB               12
#define RDMA_REGION_12__LENGTH_LSB               2
#define RDMA_REGION_12__LENGTH_MASK              0x00001ffc
#define RDMA_REGION_12__LENGTH_GET(x)            (((x) & RDMA_REGION_12__LENGTH_MASK) >> RDMA_REGION_12__LENGTH_LSB)
#define RDMA_REGION_12__LENGTH_SET(x)            (((x) << RDMA_REGION_12__LENGTH_LSB) & RDMA_REGION_12__LENGTH_MASK)
#define RDMA_REGION_12__INDI_MSB                 1
#define RDMA_REGION_12__INDI_LSB                 1
#define RDMA_REGION_12__INDI_MASK                0x00000002
#define RDMA_REGION_12__INDI_GET(x)              (((x) & RDMA_REGION_12__INDI_MASK) >> RDMA_REGION_12__INDI_LSB)
#define RDMA_REGION_12__INDI_SET(x)              (((x) << RDMA_REGION_12__INDI_LSB) & RDMA_REGION_12__INDI_MASK)
#define RDMA_REGION_12__NEXT_MSB                 0
#define RDMA_REGION_12__NEXT_LSB                 0
#define RDMA_REGION_12__NEXT_MASK                0x00000001
#define RDMA_REGION_12__NEXT_GET(x)              (((x) & RDMA_REGION_12__NEXT_MASK) >> RDMA_REGION_12__NEXT_LSB)
#define RDMA_REGION_12__NEXT_SET(x)              (((x) << RDMA_REGION_12__NEXT_LSB) & RDMA_REGION_12__NEXT_MASK)

#define RDMA_REGION_13__ADDRESS                  0x00000054
#define RDMA_REGION_13__OFFSET                   0x00000054
#define RDMA_REGION_13__ADDR_MSB                 31
#define RDMA_REGION_13__ADDR_LSB                 13
#define RDMA_REGION_13__ADDR_MASK                0xffffe000
#define RDMA_REGION_13__ADDR_GET(x)              (((x) & RDMA_REGION_13__ADDR_MASK) >> RDMA_REGION_13__ADDR_LSB)
#define RDMA_REGION_13__ADDR_SET(x)              (((x) << RDMA_REGION_13__ADDR_LSB) & RDMA_REGION_13__ADDR_MASK)
#define RDMA_REGION_13__LENGTH_MSB               12
#define RDMA_REGION_13__LENGTH_LSB               2
#define RDMA_REGION_13__LENGTH_MASK              0x00001ffc
#define RDMA_REGION_13__LENGTH_GET(x)            (((x) & RDMA_REGION_13__LENGTH_MASK) >> RDMA_REGION_13__LENGTH_LSB)
#define RDMA_REGION_13__LENGTH_SET(x)            (((x) << RDMA_REGION_13__LENGTH_LSB) & RDMA_REGION_13__LENGTH_MASK)
#define RDMA_REGION_13__INDI_MSB                 1
#define RDMA_REGION_13__INDI_LSB                 1
#define RDMA_REGION_13__INDI_MASK                0x00000002
#define RDMA_REGION_13__INDI_GET(x)              (((x) & RDMA_REGION_13__INDI_MASK) >> RDMA_REGION_13__INDI_LSB)
#define RDMA_REGION_13__INDI_SET(x)              (((x) << RDMA_REGION_13__INDI_LSB) & RDMA_REGION_13__INDI_MASK)
#define RDMA_REGION_13__NEXT_MSB                 0
#define RDMA_REGION_13__NEXT_LSB                 0
#define RDMA_REGION_13__NEXT_MASK                0x00000001
#define RDMA_REGION_13__NEXT_GET(x)              (((x) & RDMA_REGION_13__NEXT_MASK) >> RDMA_REGION_13__NEXT_LSB)
#define RDMA_REGION_13__NEXT_SET(x)              (((x) << RDMA_REGION_13__NEXT_LSB) & RDMA_REGION_13__NEXT_MASK)

#define RDMA_REGION_14__ADDRESS                  0x00000058
#define RDMA_REGION_14__OFFSET                   0x00000058
#define RDMA_REGION_14__ADDR_MSB                 31
#define RDMA_REGION_14__ADDR_LSB                 13
#define RDMA_REGION_14__ADDR_MASK                0xffffe000
#define RDMA_REGION_14__ADDR_GET(x)              (((x) & RDMA_REGION_14__ADDR_MASK) >> RDMA_REGION_14__ADDR_LSB)
#define RDMA_REGION_14__ADDR_SET(x)              (((x) << RDMA_REGION_14__ADDR_LSB) & RDMA_REGION_14__ADDR_MASK)
#define RDMA_REGION_14__LENGTH_MSB               12
#define RDMA_REGION_14__LENGTH_LSB               2
#define RDMA_REGION_14__LENGTH_MASK              0x00001ffc
#define RDMA_REGION_14__LENGTH_GET(x)            (((x) & RDMA_REGION_14__LENGTH_MASK) >> RDMA_REGION_14__LENGTH_LSB)
#define RDMA_REGION_14__LENGTH_SET(x)            (((x) << RDMA_REGION_14__LENGTH_LSB) & RDMA_REGION_14__LENGTH_MASK)
#define RDMA_REGION_14__INDI_MSB                 1
#define RDMA_REGION_14__INDI_LSB                 1
#define RDMA_REGION_14__INDI_MASK                0x00000002
#define RDMA_REGION_14__INDI_GET(x)              (((x) & RDMA_REGION_14__INDI_MASK) >> RDMA_REGION_14__INDI_LSB)
#define RDMA_REGION_14__INDI_SET(x)              (((x) << RDMA_REGION_14__INDI_LSB) & RDMA_REGION_14__INDI_MASK)
#define RDMA_REGION_14__NEXT_MSB                 0
#define RDMA_REGION_14__NEXT_LSB                 0
#define RDMA_REGION_14__NEXT_MASK                0x00000001
#define RDMA_REGION_14__NEXT_GET(x)              (((x) & RDMA_REGION_14__NEXT_MASK) >> RDMA_REGION_14__NEXT_LSB)
#define RDMA_REGION_14__NEXT_SET(x)              (((x) << RDMA_REGION_14__NEXT_LSB) & RDMA_REGION_14__NEXT_MASK)

#define RDMA_REGION_15__ADDRESS                  0x0000005c
#define RDMA_REGION_15__OFFSET                   0x0000005c
#define RDMA_REGION_15__ADDR_MSB                 31
#define RDMA_REGION_15__ADDR_LSB                 13
#define RDMA_REGION_15__ADDR_MASK                0xffffe000
#define RDMA_REGION_15__ADDR_GET(x)              (((x) & RDMA_REGION_15__ADDR_MASK) >> RDMA_REGION_15__ADDR_LSB)
#define RDMA_REGION_15__ADDR_SET(x)              (((x) << RDMA_REGION_15__ADDR_LSB) & RDMA_REGION_15__ADDR_MASK)
#define RDMA_REGION_15__LENGTH_MSB               12
#define RDMA_REGION_15__LENGTH_LSB               2
#define RDMA_REGION_15__LENGTH_MASK              0x00001ffc
#define RDMA_REGION_15__LENGTH_GET(x)            (((x) & RDMA_REGION_15__LENGTH_MASK) >> RDMA_REGION_15__LENGTH_LSB)
#define RDMA_REGION_15__LENGTH_SET(x)            (((x) << RDMA_REGION_15__LENGTH_LSB) & RDMA_REGION_15__LENGTH_MASK)
#define RDMA_REGION_15__INDI_MSB                 1
#define RDMA_REGION_15__INDI_LSB                 1
#define RDMA_REGION_15__INDI_MASK                0x00000002
#define RDMA_REGION_15__INDI_GET(x)              (((x) & RDMA_REGION_15__INDI_MASK) >> RDMA_REGION_15__INDI_LSB)
#define RDMA_REGION_15__INDI_SET(x)              (((x) << RDMA_REGION_15__INDI_LSB) & RDMA_REGION_15__INDI_MASK)
#define RDMA_REGION_15__NEXT_MSB                 0
#define RDMA_REGION_15__NEXT_LSB                 0
#define RDMA_REGION_15__NEXT_MASK                0x00000001
#define RDMA_REGION_15__NEXT_GET(x)              (((x) & RDMA_REGION_15__NEXT_MASK) >> RDMA_REGION_15__NEXT_LSB)
#define RDMA_REGION_15__NEXT_SET(x)              (((x) << RDMA_REGION_15__NEXT_LSB) & RDMA_REGION_15__NEXT_MASK)

#define DMA_STATUS_ADDRESS                       0x00000060
#define DMA_STATUS_OFFSET                        0x00000060
#define DMA_STATUS_ERROR_CODE_MSB                14
#define DMA_STATUS_ERROR_CODE_LSB                4
#define DMA_STATUS_ERROR_CODE_MASK               0x00007ff0
#define DMA_STATUS_ERROR_CODE_GET(x)             (((x) & DMA_STATUS_ERROR_CODE_MASK) >> DMA_STATUS_ERROR_CODE_LSB)
#define DMA_STATUS_ERROR_CODE_SET(x)             (((x) << DMA_STATUS_ERROR_CODE_LSB) & DMA_STATUS_ERROR_CODE_MASK)
#define DMA_STATUS_ERROR_MSB                     3
#define DMA_STATUS_ERROR_LSB                     3
#define DMA_STATUS_ERROR_MASK                    0x00000008
#define DMA_STATUS_ERROR_GET(x)                  (((x) & DMA_STATUS_ERROR_MASK) >> DMA_STATUS_ERROR_LSB)
#define DMA_STATUS_ERROR_SET(x)                  (((x) << DMA_STATUS_ERROR_LSB) & DMA_STATUS_ERROR_MASK)
#define DMA_STATUS_DONE_MSB                      2
#define DMA_STATUS_DONE_LSB                      2
#define DMA_STATUS_DONE_MASK                     0x00000004
#define DMA_STATUS_DONE_GET(x)                   (((x) & DMA_STATUS_DONE_MASK) >> DMA_STATUS_DONE_LSB)
#define DMA_STATUS_DONE_SET(x)                   (((x) << DMA_STATUS_DONE_LSB) & DMA_STATUS_DONE_MASK)
#define DMA_STATUS_STOPPED_MSB                   1
#define DMA_STATUS_STOPPED_LSB                   1
#define DMA_STATUS_STOPPED_MASK                  0x00000002
#define DMA_STATUS_STOPPED_GET(x)                (((x) & DMA_STATUS_STOPPED_MASK) >> DMA_STATUS_STOPPED_LSB)
#define DMA_STATUS_STOPPED_SET(x)                (((x) << DMA_STATUS_STOPPED_LSB) & DMA_STATUS_STOPPED_MASK)
#define DMA_STATUS_RUNNING_MSB                   0
#define DMA_STATUS_RUNNING_LSB                   0
#define DMA_STATUS_RUNNING_MASK                  0x00000001
#define DMA_STATUS_RUNNING_GET(x)                (((x) & DMA_STATUS_RUNNING_MASK) >> DMA_STATUS_RUNNING_LSB)
#define DMA_STATUS_RUNNING_SET(x)                (((x) << DMA_STATUS_RUNNING_LSB) & DMA_STATUS_RUNNING_MASK)

#define DMA_INT_EN_ADDRESS                       0x00000064
#define DMA_INT_EN_OFFSET                        0x00000064
#define DMA_INT_EN_ERROR_ENA_MSB                 3
#define DMA_INT_EN_ERROR_ENA_LSB                 3
#define DMA_INT_EN_ERROR_ENA_MASK                0x00000008
#define DMA_INT_EN_ERROR_ENA_GET(x)              (((x) & DMA_INT_EN_ERROR_ENA_MASK) >> DMA_INT_EN_ERROR_ENA_LSB)
#define DMA_INT_EN_ERROR_ENA_SET(x)              (((x) << DMA_INT_EN_ERROR_ENA_LSB) & DMA_INT_EN_ERROR_ENA_MASK)
#define DMA_INT_EN_DONE_ENA_MSB                  2
#define DMA_INT_EN_DONE_ENA_LSB                  2
#define DMA_INT_EN_DONE_ENA_MASK                 0x00000004
#define DMA_INT_EN_DONE_ENA_GET(x)               (((x) & DMA_INT_EN_DONE_ENA_MASK) >> DMA_INT_EN_DONE_ENA_LSB)
#define DMA_INT_EN_DONE_ENA_SET(x)               (((x) << DMA_INT_EN_DONE_ENA_LSB) & DMA_INT_EN_DONE_ENA_MASK)
#define DMA_INT_EN_STOPPED_ENA_MSB               1
#define DMA_INT_EN_STOPPED_ENA_LSB               1
#define DMA_INT_EN_STOPPED_ENA_MASK              0x00000002
#define DMA_INT_EN_STOPPED_ENA_GET(x)            (((x) & DMA_INT_EN_STOPPED_ENA_MASK) >> DMA_INT_EN_STOPPED_ENA_LSB)
#define DMA_INT_EN_STOPPED_ENA_SET(x)            (((x) << DMA_INT_EN_STOPPED_ENA_LSB) & DMA_INT_EN_STOPPED_ENA_MASK)


#ifndef __ASSEMBLER__

typedef struct rdma_reg_reg_s {
  volatile unsigned int dma_config;
  volatile unsigned int dma_control;
  volatile unsigned int dma_src;
  volatile unsigned int dma_dest;
  volatile unsigned int dma_length;
  volatile unsigned int vmc_base;
  volatile unsigned int indirect_reg;
  volatile unsigned int indirect_return;
  volatile unsigned int rdma_region_0_;
  volatile unsigned int rdma_region_1_;
  volatile unsigned int rdma_region_2_;
  volatile unsigned int rdma_region_3_;
  volatile unsigned int rdma_region_4_;
  volatile unsigned int rdma_region_5_;
  volatile unsigned int rdma_region_6_;
  volatile unsigned int rdma_region_7_;
  volatile unsigned int rdma_region_8_;
  volatile unsigned int rdma_region_9_;
  volatile unsigned int rdma_region_10_;
  volatile unsigned int rdma_region_11_;
  volatile unsigned int rdma_region_12_;
  volatile unsigned int rdma_region_13_;
  volatile unsigned int rdma_region_14_;
  volatile unsigned int rdma_region_15_;
  volatile unsigned int dma_status;
  volatile unsigned int dma_int_en;
} rdma_reg_reg_t;

#endif /* __ASSEMBLER__ */

#endif /* _RDMA_REG_H_ */
