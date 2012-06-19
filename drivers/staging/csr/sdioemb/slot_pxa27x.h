/*
 * PXA27x MMC/SD controller definitions.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef _SLOT_PXA27X_H
#define _SLOT_PXA27X_H

#define PXA27X_MMC_MMCLK_BASE_FREQ 19500000
#define PXA27X_MMC_FIFO_SIZE 32

#define STOP_CLOCK              (1 << 0)
#define START_CLOCK             (2 << 0)

#define STAT_END_CMD_RES                (1 << 13)
#define STAT_PRG_DONE                   (1 << 12)
#define STAT_DATA_TRAN_DONE             (1 << 11)
#define STAT_CLK_EN                     (1 << 8)
#define STAT_RECV_FIFO_FULL             (1 << 7)
#define STAT_XMIT_FIFO_EMPTY            (1 << 6)
#define STAT_RES_CRC_ERR                (1 << 5)
#define STAT_SPI_READ_ERROR_TOKEN       (1 << 4)
#define STAT_CRC_READ_ERROR             (1 << 3)
#define STAT_CRC_WRITE_ERROR            (1 << 2)
#define STAT_TIME_OUT_RESPONSE          (1 << 1)
#define STAT_READ_TIME_OUT              (1 << 0)

#define SPI_CS_ADDRESS          (1 << 3)
#define SPI_CS_EN               (1 << 2)
#define CRC_ON                  (1 << 1)
#define SPI_EN                  (1 << 0)

#define CMDAT_SDIO_INT_EN       (1 << 11)
#define CMDAT_STOP_TRAN         (1 << 10)
#define CMDAT_SD_4DAT           (1 << 8)
#define CMDAT_DMAEN             (1 << 7)
#define CMDAT_INIT              (1 << 6)
#define CMDAT_BUSY              (1 << 5)
#define CMDAT_STREAM            (1 << 4)        /* 1 = stream */
#define CMDAT_WRITE             (1 << 3)        /* 1 = write */
#define CMDAT_DATAEN            (1 << 2)
#define CMDAT_RESP_NONE         (0 << 0)
#define CMDAT_RESP_SHORT        (1 << 0)
#define CMDAT_RESP_R2           (2 << 0)
#define CMDAT_RESP_R3           (3 << 0)

#define RDTO_MAX                0xffff

#define BUF_PART_FULL           (1 << 0)

#define SDIO_SUSPEND_ACK        (1 << 12)
#define SDIO_INT                (1 << 11)
#define RD_STALLED              (1 << 10)
#define RES_ERR                 (1 << 9)
#define DAT_ERR                 (1 << 8)
#define TINT                    (1 << 7)
#define TXFIFO_WR_REQ           (1 << 6)
#define RXFIFO_RD_REQ           (1 << 5)
#define CLK_IS_OFF              (1 << 4)
#define STOP_CMD                (1 << 3)
#define END_CMD_RES             (1 << 2)
#define PRG_DONE                (1 << 1)
#define DATA_TRAN_DONE          (1 << 0)

#define MMC_I_MASK_ALL          0x00001fff

#endif /* #ifndef _SLOT_PXA27X_H */
