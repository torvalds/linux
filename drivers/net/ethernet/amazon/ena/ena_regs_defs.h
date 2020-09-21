/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright 2015-2020 Amazon.com, Inc. or its affiliates. All rights reserved.
 */
#ifndef _ENA_REGS_H_
#define _ENA_REGS_H_

enum ena_regs_reset_reason_types {
	ENA_REGS_RESET_NORMAL                       = 0,
	ENA_REGS_RESET_KEEP_ALIVE_TO                = 1,
	ENA_REGS_RESET_ADMIN_TO                     = 2,
	ENA_REGS_RESET_MISS_TX_CMPL                 = 3,
	ENA_REGS_RESET_INV_RX_REQ_ID                = 4,
	ENA_REGS_RESET_INV_TX_REQ_ID                = 5,
	ENA_REGS_RESET_TOO_MANY_RX_DESCS            = 6,
	ENA_REGS_RESET_INIT_ERR                     = 7,
	ENA_REGS_RESET_DRIVER_INVALID_STATE         = 8,
	ENA_REGS_RESET_OS_TRIGGER                   = 9,
	ENA_REGS_RESET_OS_NETDEV_WD                 = 10,
	ENA_REGS_RESET_SHUTDOWN                     = 11,
	ENA_REGS_RESET_USER_TRIGGER                 = 12,
	ENA_REGS_RESET_GENERIC                      = 13,
	ENA_REGS_RESET_MISS_INTERRUPT               = 14,
};

/* ena_registers offsets */

/* 0 base */
#define ENA_REGS_VERSION_OFF                                0x0
#define ENA_REGS_CONTROLLER_VERSION_OFF                     0x4
#define ENA_REGS_CAPS_OFF                                   0x8
#define ENA_REGS_CAPS_EXT_OFF                               0xc
#define ENA_REGS_AQ_BASE_LO_OFF                             0x10
#define ENA_REGS_AQ_BASE_HI_OFF                             0x14
#define ENA_REGS_AQ_CAPS_OFF                                0x18
#define ENA_REGS_ACQ_BASE_LO_OFF                            0x20
#define ENA_REGS_ACQ_BASE_HI_OFF                            0x24
#define ENA_REGS_ACQ_CAPS_OFF                               0x28
#define ENA_REGS_AQ_DB_OFF                                  0x2c
#define ENA_REGS_ACQ_TAIL_OFF                               0x30
#define ENA_REGS_AENQ_CAPS_OFF                              0x34
#define ENA_REGS_AENQ_BASE_LO_OFF                           0x38
#define ENA_REGS_AENQ_BASE_HI_OFF                           0x3c
#define ENA_REGS_AENQ_HEAD_DB_OFF                           0x40
#define ENA_REGS_AENQ_TAIL_OFF                              0x44
#define ENA_REGS_INTR_MASK_OFF                              0x4c
#define ENA_REGS_DEV_CTL_OFF                                0x54
#define ENA_REGS_DEV_STS_OFF                                0x58
#define ENA_REGS_MMIO_REG_READ_OFF                          0x5c
#define ENA_REGS_MMIO_RESP_LO_OFF                           0x60
#define ENA_REGS_MMIO_RESP_HI_OFF                           0x64
#define ENA_REGS_RSS_IND_ENTRY_UPDATE_OFF                   0x68

/* version register */
#define ENA_REGS_VERSION_MINOR_VERSION_MASK                 0xff
#define ENA_REGS_VERSION_MAJOR_VERSION_SHIFT                8
#define ENA_REGS_VERSION_MAJOR_VERSION_MASK                 0xff00

/* controller_version register */
#define ENA_REGS_CONTROLLER_VERSION_SUBMINOR_VERSION_MASK   0xff
#define ENA_REGS_CONTROLLER_VERSION_MINOR_VERSION_SHIFT     8
#define ENA_REGS_CONTROLLER_VERSION_MINOR_VERSION_MASK      0xff00
#define ENA_REGS_CONTROLLER_VERSION_MAJOR_VERSION_SHIFT     16
#define ENA_REGS_CONTROLLER_VERSION_MAJOR_VERSION_MASK      0xff0000
#define ENA_REGS_CONTROLLER_VERSION_IMPL_ID_SHIFT           24
#define ENA_REGS_CONTROLLER_VERSION_IMPL_ID_MASK            0xff000000

/* caps register */
#define ENA_REGS_CAPS_CONTIGUOUS_QUEUE_REQUIRED_MASK        0x1
#define ENA_REGS_CAPS_RESET_TIMEOUT_SHIFT                   1
#define ENA_REGS_CAPS_RESET_TIMEOUT_MASK                    0x3e
#define ENA_REGS_CAPS_DMA_ADDR_WIDTH_SHIFT                  8
#define ENA_REGS_CAPS_DMA_ADDR_WIDTH_MASK                   0xff00
#define ENA_REGS_CAPS_ADMIN_CMD_TO_SHIFT                    16
#define ENA_REGS_CAPS_ADMIN_CMD_TO_MASK                     0xf0000

/* aq_caps register */
#define ENA_REGS_AQ_CAPS_AQ_DEPTH_MASK                      0xffff
#define ENA_REGS_AQ_CAPS_AQ_ENTRY_SIZE_SHIFT                16
#define ENA_REGS_AQ_CAPS_AQ_ENTRY_SIZE_MASK                 0xffff0000

/* acq_caps register */
#define ENA_REGS_ACQ_CAPS_ACQ_DEPTH_MASK                    0xffff
#define ENA_REGS_ACQ_CAPS_ACQ_ENTRY_SIZE_SHIFT              16
#define ENA_REGS_ACQ_CAPS_ACQ_ENTRY_SIZE_MASK               0xffff0000

/* aenq_caps register */
#define ENA_REGS_AENQ_CAPS_AENQ_DEPTH_MASK                  0xffff
#define ENA_REGS_AENQ_CAPS_AENQ_ENTRY_SIZE_SHIFT            16
#define ENA_REGS_AENQ_CAPS_AENQ_ENTRY_SIZE_MASK             0xffff0000

/* dev_ctl register */
#define ENA_REGS_DEV_CTL_DEV_RESET_MASK                     0x1
#define ENA_REGS_DEV_CTL_AQ_RESTART_SHIFT                   1
#define ENA_REGS_DEV_CTL_AQ_RESTART_MASK                    0x2
#define ENA_REGS_DEV_CTL_QUIESCENT_SHIFT                    2
#define ENA_REGS_DEV_CTL_QUIESCENT_MASK                     0x4
#define ENA_REGS_DEV_CTL_IO_RESUME_SHIFT                    3
#define ENA_REGS_DEV_CTL_IO_RESUME_MASK                     0x8
#define ENA_REGS_DEV_CTL_RESET_REASON_SHIFT                 28
#define ENA_REGS_DEV_CTL_RESET_REASON_MASK                  0xf0000000

/* dev_sts register */
#define ENA_REGS_DEV_STS_READY_MASK                         0x1
#define ENA_REGS_DEV_STS_AQ_RESTART_IN_PROGRESS_SHIFT       1
#define ENA_REGS_DEV_STS_AQ_RESTART_IN_PROGRESS_MASK        0x2
#define ENA_REGS_DEV_STS_AQ_RESTART_FINISHED_SHIFT          2
#define ENA_REGS_DEV_STS_AQ_RESTART_FINISHED_MASK           0x4
#define ENA_REGS_DEV_STS_RESET_IN_PROGRESS_SHIFT            3
#define ENA_REGS_DEV_STS_RESET_IN_PROGRESS_MASK             0x8
#define ENA_REGS_DEV_STS_RESET_FINISHED_SHIFT               4
#define ENA_REGS_DEV_STS_RESET_FINISHED_MASK                0x10
#define ENA_REGS_DEV_STS_FATAL_ERROR_SHIFT                  5
#define ENA_REGS_DEV_STS_FATAL_ERROR_MASK                   0x20
#define ENA_REGS_DEV_STS_QUIESCENT_STATE_IN_PROGRESS_SHIFT  6
#define ENA_REGS_DEV_STS_QUIESCENT_STATE_IN_PROGRESS_MASK   0x40
#define ENA_REGS_DEV_STS_QUIESCENT_STATE_ACHIEVED_SHIFT     7
#define ENA_REGS_DEV_STS_QUIESCENT_STATE_ACHIEVED_MASK      0x80

/* mmio_reg_read register */
#define ENA_REGS_MMIO_REG_READ_REQ_ID_MASK                  0xffff
#define ENA_REGS_MMIO_REG_READ_REG_OFF_SHIFT                16
#define ENA_REGS_MMIO_REG_READ_REG_OFF_MASK                 0xffff0000

/* rss_ind_entry_update register */
#define ENA_REGS_RSS_IND_ENTRY_UPDATE_INDEX_MASK            0xffff
#define ENA_REGS_RSS_IND_ENTRY_UPDATE_CQ_IDX_SHIFT          16
#define ENA_REGS_RSS_IND_ENTRY_UPDATE_CQ_IDX_MASK           0xffff0000

#endif /* _ENA_REGS_H_ */
