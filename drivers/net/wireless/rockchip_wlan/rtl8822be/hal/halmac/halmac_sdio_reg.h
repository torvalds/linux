#ifndef __HALMAC_SDIO_REG_H__
#define __HALMAC_SDIO_REG_H__

/* SDIO CMD address mapping */

#define HALMAC_SDIO_4BYTE_LEN_MASK      0x1FFF
#define HALMAC_SDIO_LOCAL_MSK           0x0FFF
#define HALMAC_WLAN_IOREG_MSK           0xFFFF

/* Sdio Address for SDIO Local Reg, TRX FIFO, MAC Reg */
typedef enum {
	HALMAC_SDIO_CMD_ADDR_SDIO_REG = 0,
	HALMAC_SDIO_CMD_ADDR_MAC_REG = 8,
	HALMAC_SDIO_CMD_ADDR_TXFF_HIGH = 4,
	HALMAC_SDIO_CMD_ADDR_TXFF_LOW = 6,
	HALMAC_SDIO_CMD_ADDR_TXFF_NORMAL = 5,
	HALMAC_SDIO_CMD_ADDR_TXFF_EXTRA = 7,
	HALMAC_SDIO_CMD_ADDR_RXFF = 7,
} HALMAC_SDIO_CMD_ADDR;


#if 1
#define SDIO_LOCAL_DEVICE_ID                            0       /* 0b[16], 000b[15:13] */
#define WLAN_TX_HIQ_DEVICE_ID                           4       /* 0b[16], 100b[15:13] */
#define WLAN_TX_MIQ_DEVICE_ID                           5       /* 0b[16], 101b[15:13] */
#define WLAN_TX_LOQ_DEVICE_ID                           6       /* 0b[16], 110b[15:13] */
#define WLAN_TX_EPQ_DEVICE_ID                           3       /* 0b[16], 110b[15:13] */
#define WLAN_RX0FF_DEVICE_ID                            7       /* 0b[16], 111b[15:13] */
#define WLAN_IOREG_DEVICE_ID                            8       /* 1b[16] */

/* IO Bus domain address mapping */
#define SDIO_LOCAL_OFFSET                                       0x10250000
#define WLAN_IOREG_OFFSET                               0x10260000
#define FW_FIFO_OFFSET                                  0x10270000
#define TX_HIQ_OFFSET                                           0x10310000
#define TX_MIQ_OFFSET                                           0x10320000
#define TX_LOQ_OFFSET                                           0x10330000
#define TX_EXQ_OFFSET                                           0x10350000
#define RX_RXOFF_OFFSET                                 0x10340000
#endif

#endif/* __HALMAC_SDIO_REG_H__ */
