/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2008 Marvell Semiconductors, All Rights Reserved.
 */

#ifndef __MVSDIO_H
#define __MVSDIO_H

/*
 * Clock rates
 */

#define MVSD_CLOCKRATE_MAX			50000000
#define MVSD_BASE_DIV_MAX			0x7ff


/*
 * Register offsets
 */

#define MVSD_SYS_ADDR_LOW			0x000
#define MVSD_SYS_ADDR_HI			0x004
#define MVSD_BLK_SIZE				0x008
#define MVSD_BLK_COUNT				0x00c
#define MVSD_ARG_LOW				0x010
#define MVSD_ARG_HI				0x014
#define MVSD_XFER_MODE				0x018
#define MVSD_CMD				0x01c
#define MVSD_RSP(i)				(0x020 + ((i)<<2))
#define MVSD_RSP0				0x020
#define MVSD_RSP1				0x024
#define MVSD_RSP2				0x028
#define MVSD_RSP3				0x02c
#define MVSD_RSP4				0x030
#define MVSD_RSP5				0x034
#define MVSD_RSP6				0x038
#define MVSD_RSP7				0x03c
#define MVSD_FIFO				0x040
#define MVSD_RSP_CRC7				0x044
#define MVSD_HW_STATE				0x048
#define MVSD_HOST_CTRL				0x050
#define MVSD_BLK_GAP_CTRL			0x054
#define MVSD_CLK_CTRL				0x058
#define MVSD_SW_RESET				0x05c
#define MVSD_NOR_INTR_STATUS			0x060
#define MVSD_ERR_INTR_STATUS			0x064
#define MVSD_NOR_STATUS_EN			0x068
#define MVSD_ERR_STATUS_EN			0x06c
#define MVSD_NOR_INTR_EN			0x070
#define MVSD_ERR_INTR_EN			0x074
#define MVSD_AUTOCMD12_ERR_STATUS		0x078
#define MVSD_CURR_BYTE_LEFT			0x07c
#define MVSD_CURR_BLK_LEFT			0x080
#define MVSD_AUTOCMD12_ARG_LOW			0x084
#define MVSD_AUTOCMD12_ARG_HI			0x088
#define MVSD_AUTOCMD12_CMD			0x08c
#define MVSD_AUTO_RSP(i)			(0x090 + ((i)<<2))
#define MVSD_AUTO_RSP0				0x090
#define MVSD_AUTO_RSP1				0x094
#define MVSD_AUTO_RSP2				0x098
#define MVSD_CLK_DIV				0x128

#define MVSD_WINDOW_CTRL(i)			(0x108 + ((i) << 3))
#define MVSD_WINDOW_BASE(i)			(0x10c + ((i) << 3))


/*
 * MVSD_CMD
 */

#define MVSD_CMD_RSP_NONE			(0 << 0)
#define MVSD_CMD_RSP_136			(1 << 0)
#define MVSD_CMD_RSP_48				(2 << 0)
#define MVSD_CMD_RSP_48BUSY			(3 << 0)

#define MVSD_CMD_CHECK_DATACRC16		(1 << 2)
#define MVSD_CMD_CHECK_CMDCRC			(1 << 3)
#define MVSD_CMD_INDX_CHECK			(1 << 4)
#define MVSD_CMD_DATA_PRESENT			(1 << 5)
#define MVSD_UNEXPECTED_RESP			(1 << 7)
#define MVSD_CMD_INDEX(x)			((x) << 8)


/*
 * MVSD_AUTOCMD12_CMD
 */

#define MVSD_AUTOCMD12_BUSY			(1 << 0)
#define MVSD_AUTOCMD12_INDX_CHECK		(1 << 1)
#define MVSD_AUTOCMD12_INDEX(x)			((x) << 8)

/*
 * MVSD_XFER_MODE
 */

#define MVSD_XFER_MODE_WR_DATA_START		(1 << 0)
#define MVSD_XFER_MODE_HW_WR_DATA_EN		(1 << 1)
#define MVSD_XFER_MODE_AUTO_CMD12		(1 << 2)
#define MVSD_XFER_MODE_INT_CHK_EN		(1 << 3)
#define MVSD_XFER_MODE_TO_HOST			(1 << 4)
#define MVSD_XFER_MODE_STOP_CLK			(1 << 5)
#define MVSD_XFER_MODE_PIO			(1 << 6)


/*
 * MVSD_HOST_CTRL
 */

#define MVSD_HOST_CTRL_PUSH_PULL_EN 		(1 << 0)

#define MVSD_HOST_CTRL_CARD_TYPE_MEM_ONLY 	(0 << 1)
#define MVSD_HOST_CTRL_CARD_TYPE_IO_ONLY 	(1 << 1)
#define MVSD_HOST_CTRL_CARD_TYPE_IO_MEM_COMBO 	(2 << 1)
#define MVSD_HOST_CTRL_CARD_TYPE_IO_MMC 	(3 << 1)
#define MVSD_HOST_CTRL_CARD_TYPE_MASK	 	(3 << 1)

#define MVSD_HOST_CTRL_BIG_ENDIAN 		(1 << 3)
#define MVSD_HOST_CTRL_LSB_FIRST 		(1 << 4)
#define MVSD_HOST_CTRL_DATA_WIDTH_4_BITS 	(1 << 9)
#define MVSD_HOST_CTRL_HI_SPEED_EN 		(1 << 10)

#define MVSD_HOST_CTRL_TMOUT_MAX 		0xf
#define MVSD_HOST_CTRL_TMOUT_MASK 		(0xf << 11)
#define MVSD_HOST_CTRL_TMOUT(x) 		((x) << 11)
#define MVSD_HOST_CTRL_TMOUT_EN 		(1 << 15)


/*
 * MVSD_SW_RESET
 */

#define MVSD_SW_RESET_NOW			(1 << 8)


/*
 * Normal interrupt status bits
 */

#define MVSD_NOR_CMD_DONE			(1 << 0)
#define MVSD_NOR_XFER_DONE			(1 << 1)
#define MVSD_NOR_BLK_GAP_EVT			(1 << 2)
#define MVSD_NOR_DMA_DONE			(1 << 3)
#define MVSD_NOR_TX_AVAIL			(1 << 4)
#define MVSD_NOR_RX_READY			(1 << 5)
#define MVSD_NOR_CARD_INT			(1 << 8)
#define MVSD_NOR_READ_WAIT_ON			(1 << 9)
#define MVSD_NOR_RX_FIFO_8W			(1 << 10)
#define MVSD_NOR_TX_FIFO_8W			(1 << 11)
#define MVSD_NOR_SUSPEND_ON			(1 << 12)
#define MVSD_NOR_AUTOCMD12_DONE			(1 << 13)
#define MVSD_NOR_UNEXP_RSP			(1 << 14)
#define MVSD_NOR_ERROR				(1 << 15)


/*
 * Error status bits
 */

#define MVSD_ERR_CMD_TIMEOUT			(1 << 0)
#define MVSD_ERR_CMD_CRC			(1 << 1)
#define MVSD_ERR_CMD_ENDBIT			(1 << 2)
#define MVSD_ERR_CMD_INDEX			(1 << 3)
#define MVSD_ERR_DATA_TIMEOUT			(1 << 4)
#define MVSD_ERR_DATA_CRC			(1 << 5)
#define MVSD_ERR_DATA_ENDBIT			(1 << 6)
#define MVSD_ERR_AUTOCMD12			(1 << 8)
#define MVSD_ERR_CMD_STARTBIT			(1 << 9)
#define MVSD_ERR_XFER_SIZE			(1 << 10)
#define MVSD_ERR_RESP_T_BIT			(1 << 11)
#define MVSD_ERR_CRC_ENDBIT			(1 << 12)
#define MVSD_ERR_CRC_STARTBIT			(1 << 13)
#define MVSD_ERR_CRC_STATUS			(1 << 14)


/*
 * CMD12 error status bits
 */

#define MVSD_AUTOCMD12_ERR_NOTEXE		(1 << 0)
#define MVSD_AUTOCMD12_ERR_TIMEOUT		(1 << 1)
#define MVSD_AUTOCMD12_ERR_CRC			(1 << 2)
#define MVSD_AUTOCMD12_ERR_ENDBIT		(1 << 3)
#define MVSD_AUTOCMD12_ERR_INDEX		(1 << 4)
#define MVSD_AUTOCMD12_ERR_RESP_T_BIT		(1 << 5)
#define MVSD_AUTOCMD12_ERR_RESP_STARTBIT	(1 << 6)

#endif
