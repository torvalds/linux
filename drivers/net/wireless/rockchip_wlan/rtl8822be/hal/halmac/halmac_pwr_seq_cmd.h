/* SPDX-License-Identifier: GPL-2.0 */
#ifndef HALMAC_POWER_SEQUENCE_CMD
#define HALMAC_POWER_SEQUENCE_CMD

#include "halmac_2_platform.h"
#include "halmac_type.h"

#define HALMAC_POLLING_READY_TIMEOUT_COUNT 10000

/*
* The value of cmd : 4 bits
*/

/*
* offset : the read register offset
* msk : the mask of the read value
* value : N/A, left by 0
* Note : dirver shall implement this function by read & msk
*/
#define	HALMAC_PWR_CMD_READ		0x00
/*
* offset: the read register offset
* msk: the mask of the write bits
* value: write value
* Note: driver shall implement this cmd by read & msk after write
*/
#define	HALMAC_PWR_CMD_WRITE	0x01
/*
* offset: the read register offset
* msk: the mask of the polled value
* value: the value to be polled, masked by the msd field.
* Note: driver shall implement this cmd by
* do{
* if( (Read(offset) & msk) == (value & msk) )
* break;
* } while(not timeout);
*/
#define	HALMAC_PWR_CMD_POLLING	0x02
/*
* offset: the value to delay
* msk: N/A
* value: the unit of delay, 0: us, 1: ms
*/
#define	HALMAC_PWR_CMD_DELAY	0x03
/*
* offset: N/A
* msk: N/A
* value: N/A
*/
#define	HALMAC_PWR_CMD_END		0x04

/*
* The value of base : 4 bits
*/

/* define the base address of each block */
#define   HALMAC_PWR_BASEADDR_MAC	0x00
#define   HALMAC_PWR_BASEADDR_USB	0x01
#define   HALMAC_PWR_BASEADDR_PCIE	0x02
#define   HALMAC_PWR_BASEADDR_SDIO	0x03

/*
* The value of interface_msk : 4 bits
*/
#define	HALMAC_PWR_INTF_SDIO_MSK	BIT(0)
#define	HALMAC_PWR_INTF_USB_MSK		BIT(1)
#define	HALMAC_PWR_INTF_PCI_MSK		BIT(2)
#define	HALMAC_PWR_INTF_ALL_MSK		(BIT(0)|BIT(1)|BIT(2)|BIT(3))

/*
* The value of fab_msk : 4 bits
*/
#define	HALMAC_PWR_FAB_TSMC_MSK		BIT(0)
#define	HALMAC_PWR_FAB_UMC_MSK		BIT(1)
#define	HALMAC_PWR_FAB_ALL_MSK		(BIT(0)|BIT(1)|BIT(2)|BIT(3))

/*
* The value of cut_msk : 8 bits
*/
#define	HALMAC_PWR_CUT_TESTCHIP_MSK		BIT(0)
#define	HALMAC_PWR_CUT_A_MSK			BIT(1)
#define	HALMAC_PWR_CUT_B_MSK			BIT(2)
#define	HALMAC_PWR_CUT_C_MSK			BIT(3)
#define	HALMAC_PWR_CUT_D_MSK			BIT(4)
#define	HALMAC_PWR_CUT_E_MSK			BIT(5)
#define	HALMAC_PWR_CUT_F_MSK			BIT(6)
#define	HALMAC_PWR_CUT_G_MSK			BIT(7)
#define	HALMAC_PWR_CUT_ALL_MSK			0xFF

typedef enum _HALMAC_PWRSEQ_CMD_DELAY_UNIT_ {
	HALMAC_PWRSEQ_DELAY_US,
	HALMAC_PWRSEQ_DELAY_MS,
} HALMAC_PWRSEQ_DELAY_UNIT;

/* Don't care endian issue, because element of pwer seq vector is fixed address */
typedef struct _HALMAC_WL_PWR_CFG_ {
	u16 offset;
	u8	cut_msk;
	u8	fab_msk:4;
	u8	interface_msk:4;
	u8	base:4;
	u8	cmd:4;
	u8	msk;
	u8	value;
} HALMAC_WLAN_PWR_CFG, *PHALMAC_WLAN_PWR_CFG;

#endif
