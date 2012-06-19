/*
 * i.MX31 SDHC definitions.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef _SLOT_IMX31_H
#define _SLOT_IMX31_H

/*
 * i.MX31 SDHC registers.
 */

#define SDHC_STR_STP_CLK  0x00
#  define STR_STP_CLK_MMCSD_RESET 0x0008
#  define STR_STP_CLK_START_CLK   0x0002
#  define STR_STP_CLK_STOP_CLK    0x0001

#define SDHC_STATUS       0x04
#  define STATUS_CARD_PRESENCE    0x8000
#  define STATUS_SDIO_INT_ACTIVE  0x4000
#  define STATUS_END_CMD_RESP     0x2000
#  define STATUS_WRITE_OP_DONE    0x1000
#  define STATUS_READ_OP_DONE     0x0800
#  define STATUS_CARD_BUS_CLK_RUN 0x0100
#  define STATUS_APPL_BUFF_FF     0x0080
#  define STATUS_APPL_BUFF_FE     0x0040
#  define STATUS_RESP_CRC_ERR     0x0020
#  define STATUS_CRC_READ_ERR     0x0008
#  define STATUS_CRC_WRITE_ERR    0x0004
#  define STATUS_TIME_OUT_RESP    0x0002
#  define STATUS_TIME_OUT_READ    0x0001
#  define STATUS_ERR_CMD_MASK     (STATUS_RESP_CRC_ERR | STATUS_TIME_OUT_RESP)
#  define STATUS_ERR_DATA_MASK    (STATUS_CRC_READ_ERR | STATUS_CRC_WRITE_ERR | STATUS_TIME_OUT_READ)
#  define STATUS_ERR_MASK         (STATUS_ERR_CMD_MASK | STATUS_ERR_DATA_MASK)

#define SDHC_CLK_RATE     0x08

#define SDHC_CMD_DAT_CTRL 0x0c /* CMD_DAT_CONT */
#  define CMD_DAT_CTRL_CMD_RESUME        0x8000
#  define CMD_DAT_CTRL_CMD_RESP_LONG_OFF 0x1000
#  define CMD_DAT_CTRL_STOP_READ_WAIT    0x0800
#  define CMD_DAT_CTRL_START_READ_WAIT   0x0400
#  define CMD_DAT_CTRL_BUS_WIDTH_4       0x0200
#  define CMD_DAT_CTRL_INIT              0x0080
#  define CMD_DAT_CTRL_WRITE             0x0010
#  define CMD_DAT_CTRL_DATA_ENABLE       0x0008
#  define CMD_DAT_CTRL_RESP_NONE         0x0000
#  define CMD_DAT_CTRL_RESP_R1_R5_R6     0x0001
#  define CMD_DAT_CTRL_RESP_R2           0x0002
#  define CMD_DAT_CTRL_RESP_R3_R4        0x0003

#define SDHC_RES_TO       0x10

#define SDHC_READ_TO      0x14
#  define READ_TO_RECOMMENDED 0x2db4

#define SDHC_BLK_LEN      0x18

#define SDHC_NOB          0x1c

#define SDHC_REV_NO       0x20

#define SDHC_INT_CTRL     0x24 /* INT_CNTR */
#  define INT_CTRL_CARD_INSERTION_EN 0x8000
#  define INT_CTRL_SDIO_REMOVAL_EN   0x4000
#  define INT_CTRL_SDIO_IRQ_EN       0x2000
#  define INT_CTRL_DAT0_EN           0x1000
#  define INT_CTRL_BUF_READ_EN       0x0010
#  define INT_CTRL_BUF_WRITE_EN      0x0008
#  define INT_CTRL_END_CMD_RES       0x0004
#  define INT_CTRL_WRITE_OP_DONE     0x0002
#  define INT_CTRL_READ_OP_DONE      0x0001
#  define INT_CTRL_INT_EN_MASK       0xe01f

#define SDHC_CMD          0x28

#define SDHC_ARG          0x2c

#define SDHC_RES_FIFO     0x34

#define SDHC_BUFFER_ACCESS 0x38

#endif /* #ifndef _SLOT_IMX31_H */
