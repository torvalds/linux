/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 */

#ifndef _MSCC_OCELOT_QS_H_
#define _MSCC_OCELOT_QS_H_

/* TODO handle BE */
#define XTR_EOF_0          0x00000080U
#define XTR_EOF_1          0x01000080U
#define XTR_EOF_2          0x02000080U
#define XTR_EOF_3          0x03000080U
#define XTR_PRUNED         0x04000080U
#define XTR_ABORT          0x05000080U
#define XTR_ESCAPE         0x06000080U
#define XTR_NOT_READY      0x07000080U
#define XTR_VALID_BYTES(x) (4 - (((x) >> 24) & 3))

#define QS_XTR_GRP_CFG_RSZ                                0x4

#define QS_XTR_GRP_CFG_MODE(x)                            (((x) << 2) & GENMASK(3, 2))
#define QS_XTR_GRP_CFG_MODE_M                             GENMASK(3, 2)
#define QS_XTR_GRP_CFG_MODE_X(x)                          (((x) & GENMASK(3, 2)) >> 2)
#define QS_XTR_GRP_CFG_STATUS_WORD_POS                    BIT(1)
#define QS_XTR_GRP_CFG_BYTE_SWAP                          BIT(0)

#define QS_XTR_RD_RSZ                                     0x4

#define QS_XTR_FRM_PRUNING_RSZ                            0x4

#define QS_XTR_CFG_DP_WM(x)                               (((x) << 5) & GENMASK(7, 5))
#define QS_XTR_CFG_DP_WM_M                                GENMASK(7, 5)
#define QS_XTR_CFG_DP_WM_X(x)                             (((x) & GENMASK(7, 5)) >> 5)
#define QS_XTR_CFG_SCH_WM(x)                              (((x) << 2) & GENMASK(4, 2))
#define QS_XTR_CFG_SCH_WM_M                               GENMASK(4, 2)
#define QS_XTR_CFG_SCH_WM_X(x)                            (((x) & GENMASK(4, 2)) >> 2)
#define QS_XTR_CFG_OFLW_ERR_STICKY(x)                     ((x) & GENMASK(1, 0))
#define QS_XTR_CFG_OFLW_ERR_STICKY_M                      GENMASK(1, 0)

#define QS_INJ_GRP_CFG_RSZ                                0x4

#define QS_INJ_GRP_CFG_MODE(x)                            (((x) << 2) & GENMASK(3, 2))
#define QS_INJ_GRP_CFG_MODE_M                             GENMASK(3, 2)
#define QS_INJ_GRP_CFG_MODE_X(x)                          (((x) & GENMASK(3, 2)) >> 2)
#define QS_INJ_GRP_CFG_BYTE_SWAP                          BIT(0)

#define QS_INJ_WR_RSZ                                     0x4

#define QS_INJ_CTRL_RSZ                                   0x4

#define QS_INJ_CTRL_GAP_SIZE(x)                           (((x) << 21) & GENMASK(24, 21))
#define QS_INJ_CTRL_GAP_SIZE_M                            GENMASK(24, 21)
#define QS_INJ_CTRL_GAP_SIZE_X(x)                         (((x) & GENMASK(24, 21)) >> 21)
#define QS_INJ_CTRL_ABORT                                 BIT(20)
#define QS_INJ_CTRL_EOF                                   BIT(19)
#define QS_INJ_CTRL_SOF                                   BIT(18)
#define QS_INJ_CTRL_VLD_BYTES(x)                          (((x) << 16) & GENMASK(17, 16))
#define QS_INJ_CTRL_VLD_BYTES_M                           GENMASK(17, 16)
#define QS_INJ_CTRL_VLD_BYTES_X(x)                        (((x) & GENMASK(17, 16)) >> 16)

#define QS_INJ_STATUS_WMARK_REACHED(x)                    (((x) << 4) & GENMASK(5, 4))
#define QS_INJ_STATUS_WMARK_REACHED_M                     GENMASK(5, 4)
#define QS_INJ_STATUS_WMARK_REACHED_X(x)                  (((x) & GENMASK(5, 4)) >> 4)
#define QS_INJ_STATUS_FIFO_RDY(x)                         (((x) << 2) & GENMASK(3, 2))
#define QS_INJ_STATUS_FIFO_RDY_M                          GENMASK(3, 2)
#define QS_INJ_STATUS_FIFO_RDY_X(x)                       (((x) & GENMASK(3, 2)) >> 2)
#define QS_INJ_STATUS_INJ_IN_PROGRESS(x)                  ((x) & GENMASK(1, 0))
#define QS_INJ_STATUS_INJ_IN_PROGRESS_M                   GENMASK(1, 0)

#define QS_INJ_ERR_RSZ                                    0x4

#define QS_INJ_ERR_ABORT_ERR_STICKY                       BIT(1)
#define QS_INJ_ERR_WR_ERR_STICKY                          BIT(0)

#endif
