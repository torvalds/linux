/*
 * arch/ppc/platforms/katana.h
 *
 * Definitions for Artesyn Katana750i/3750 board.
 *
 * Author: Tim Montgomery <timm@artesyncp.com>
 * Maintained by: Mark A. Greer <mgreer@mvista.com>
 *
 * Based on code done by Rabeeh Khoury - rabeeh@galileo.co.il
 * Based on code done by Mark A. Greer <mgreer@mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

/*
 * The MV64360 has 2 PCI buses each with 1 window from the CPU bus to
 * PCI I/O space and 4 windows from the CPU bus to PCI MEM space.
 * We'll only use one PCI MEM window on each PCI bus.
 *
 * This is the CPU physical memory map (windows must be at least 64 KB and start
 * on a boundary that is a multiple of the window size):
 *
 *    0xff800000-0xffffffff      - Boot window
 *    0xf8400000-0xf843ffff      - Internal SRAM
 *    0xf8200000-0xf83fffff      - CPLD
 *    0xf8100000-0xf810ffff      - MV64360 Registers (CONFIG_MV64X60_NEW_BASE)
 *    0xf8000000-0xf80fffff      - Socketed FLASH
 *    0xe0000000-0xefffffff      - Soldered FLASH
 *    0xc0000000-0xc3ffffff      - PCI I/O (second hose)
 *    0x80000000-0xbfffffff      - PCI MEM (second hose)
 */

#ifndef __PPC_PLATFORMS_KATANA_H
#define __PPC_PLATFORMS_KATANA_H

/* CPU Physical Memory Map setup. */
#define KATANA_BOOT_WINDOW_BASE			0xff800000
#define KATANA_BOOT_WINDOW_SIZE			0x00800000 /* 8 MB */
#define KATANA_INTERNAL_SRAM_BASE		0xf8400000
#define KATANA_CPLD_BASE			0xf8200000
#define KATANA_CPLD_SIZE			0x00200000 /* 2 MB */
#define KATANA_SOCKET_BASE			0xf8000000
#define KATANA_SOCKETED_FLASH_SIZE		0x00100000 /* 1 MB */
#define KATANA_SOLDERED_FLASH_BASE		0xe0000000
#define KATANA_SOLDERED_FLASH_SIZE		0x10000000 /* 256 MB */

#define KATANA_PCI1_MEM_START_PROC_ADDR         0x80000000
#define KATANA_PCI1_MEM_START_PCI_HI_ADDR       0x00000000
#define KATANA_PCI1_MEM_START_PCI_LO_ADDR       0x80000000
#define KATANA_PCI1_MEM_SIZE                    0x40000000 /* 1 GB */
#define KATANA_PCI1_IO_START_PROC_ADDR          0xc0000000
#define KATANA_PCI1_IO_START_PCI_ADDR           0x00000000
#define KATANA_PCI1_IO_SIZE                     0x04000000 /* 64 MB */

/* Board-specific IRQ info */
#define  KATANA_PCI_INTA_IRQ_3750		(64+8)
#define  KATANA_PCI_INTB_IRQ_3750		(64+9)
#define  KATANA_PCI_INTC_IRQ_3750		(64+10)

#define  KATANA_PCI_INTA_IRQ_750i		(64+8)
#define  KATANA_PCI_INTB_IRQ_750i		(64+9)
#define  KATANA_PCI_INTC_IRQ_750i		(64+10)
#define  KATANA_PCI_INTD_IRQ_750i		(64+14)

#define KATANA_CPLD_RST_EVENT			0x00000000
#define KATANA_CPLD_RST_CMD			0x00001000
#define KATANA_CPLD_PCI_ERR_INT_EN		0x00002000
#define KATANA_CPLD_PCI_ERR_INT_PEND		0x00003000
#define KATANA_CPLD_PRODUCT_ID			0x00004000
#define KATANA_CPLD_EREADY			0x00005000

#define KATANA_CPLD_HARDWARE_VER		0x00007000
#define KATANA_CPLD_PLD_VER			0x00008000
#define KATANA_CPLD_BD_CFG_0			0x00009000
#define KATANA_CPLD_BD_CFG_1			0x0000a000
#define KATANA_CPLD_BD_CFG_3			0x0000c000
#define KATANA_CPLD_LED				0x0000d000
#define KATANA_CPLD_RESET_OUT			0x0000e000

#define KATANA_CPLD_RST_EVENT_INITACT		0x80
#define KATANA_CPLD_RST_EVENT_SW		0x40
#define KATANA_CPLD_RST_EVENT_WD		0x20
#define KATANA_CPLD_RST_EVENT_COPS		0x10
#define KATANA_CPLD_RST_EVENT_COPH		0x08
#define KATANA_CPLD_RST_EVENT_CPCI		0x02
#define KATANA_CPLD_RST_EVENT_FP		0x01

#define KATANA_CPLD_RST_CMD_SCL			0x80
#define KATANA_CPLD_RST_CMD_SDA			0x40
#define KATANA_CPLD_RST_CMD_I2C			0x10
#define KATANA_CPLD_RST_CMD_FR			0x08
#define KATANA_CPLD_RST_CMD_SR			0x04
#define KATANA_CPLD_RST_CMD_HR			0x01

#define KATANA_CPLD_BD_CFG_0_SYSCLK_MASK	0xc0
#define KATANA_CPLD_BD_CFG_0_SYSCLK_200		0x00
#define KATANA_CPLD_BD_CFG_0_SYSCLK_166		0x80
#define KATANA_CPLD_BD_CFG_0_SYSCLK_133		0xc0
#define KATANA_CPLD_BD_CFG_0_SYSCLK_100		0x40

#define KATANA_CPLD_BD_CFG_1_FL_BANK_MASK	0x03
#define KATANA_CPLD_BD_CFG_1_FL_BANK_16MB	0x00
#define KATANA_CPLD_BD_CFG_1_FL_BANK_32MB	0x01
#define KATANA_CPLD_BD_CFG_1_FL_BANK_64MB	0x02
#define KATANA_CPLD_BD_CFG_1_FL_BANK_128MB	0x03

#define KATANA_CPLD_BD_CFG_1_FL_NUM_BANKS_MASK	0x04
#define KATANA_CPLD_BD_CFG_1_FL_NUM_BANKS_ONE	0x00
#define KATANA_CPLD_BD_CFG_1_FL_NUM_BANKS_TWO	0x04

#define KATANA_CPLD_BD_CFG_3_MONARCH		0x04

#define KATANA_CPLD_RESET_OUT_PORTSEL		0x80
#define KATANA_CPLD_RESET_OUT_WD		0x20
#define KATANA_CPLD_RESET_OUT_COPH		0x08
#define KATANA_CPLD_RESET_OUT_PCI_RST_PCI	0x02
#define KATANA_CPLD_RESET_OUT_PCI_RST_FP	0x01

#define KATANA_MBOX_RESET_REQUEST		0xC83A
#define KATANA_MBOX_RESET_ACK			0xE430
#define KATANA_MBOX_RESET_DONE			0x32E5

#define HSL_PLD_BASE				0x00010000
#define HSL_PLD_J4SGA_REG_OFF			0
#define HSL_PLD_J4GA_REG_OFF			1
#define HSL_PLD_J2GA_REG_OFF			2
#define HSL_PLD_HOT_SWAP_OFF			6
#define HSL_PLD_HOT_SWAP_LED_BIT		0x1
#define GA_MASK					0x1f
#define HSL_PLD_SIZE				0x1000
#define K3750_GPP_GEO_ADDR_PINS			0xf8000000
#define K3750_GPP_GEO_ADDR_SHIFT		27

#define K3750_GPP_EVENT_PROC_0			(1 << 21)
#define K3750_GPP_EVENT_PROC_1_2		(1 << 2)

#define PCI_VENDOR_ID_ARTESYN			0x1223
#define PCI_DEVICE_ID_KATANA_3750_PROC0		0x0041
#define PCI_DEVICE_ID_KATANA_3750_PROC1		0x0042
#define PCI_DEVICE_ID_KATANA_3750_PROC2		0x0043

#define COPROC_MEM_FUNCTION			0
#define COPROC_MEM_BAR				0
#define COPROC_REGS_FUNCTION			0
#define COPROC_REGS_BAR				4
#define COPROC_FLASH_FUNCTION			2
#define COPROC_FLASH_BAR			4

#define KATANA_IPMB_LOCAL_I2C_ADDR		0x08

#define	KATANA_DEFAULT_BAUD			9600
#define	KATANA_MPSC_CLK_SRC			8	  /* TCLK */

#define	KATANA_MTD_MONITOR_SIZE			(1 << 20) /* 1 MB */

#define	KATANA_ETH0_PHY_ADDR			12
#define	KATANA_ETH1_PHY_ADDR			11
#define	KATANA_ETH2_PHY_ADDR			4

#define KATANA_PRODUCT_ID_3750			0x01
#define KATANA_PRODUCT_ID_750i			0x02
#define KATANA_PRODUCT_ID_752i			0x04

#define KATANA_ETH_TX_QUEUE_SIZE		800
#define KATANA_ETH_RX_QUEUE_SIZE		400

#define	KATANA_ETH_PORT_CONFIG_VALUE			\
	ETH_UNICAST_NORMAL_MODE			|	\
	ETH_DEFAULT_RX_QUEUE_0			|	\
	ETH_DEFAULT_RX_ARP_QUEUE_0		|	\
	ETH_RECEIVE_BC_IF_NOT_IP_OR_ARP		|	\
	ETH_RECEIVE_BC_IF_IP			|	\
	ETH_RECEIVE_BC_IF_ARP			|	\
	ETH_CAPTURE_TCP_FRAMES_DIS		|	\
	ETH_CAPTURE_UDP_FRAMES_DIS		|	\
	ETH_DEFAULT_RX_TCP_QUEUE_0		|	\
	ETH_DEFAULT_RX_UDP_QUEUE_0		|	\
	ETH_DEFAULT_RX_BPDU_QUEUE_0

#define	KATANA_ETH_PORT_CONFIG_EXTEND_VALUE		\
	ETH_SPAN_BPDU_PACKETS_AS_NORMAL		|	\
	ETH_PARTITION_DISABLE

#define	GT_ETH_IPG_INT_RX(value)			\
	((value & 0x3fff) << 8)

#define	KATANA_ETH_PORT_SDMA_CONFIG_VALUE		\
	ETH_RX_BURST_SIZE_4_64BIT		|	\
	GT_ETH_IPG_INT_RX(0)			|	\
	ETH_TX_BURST_SIZE_4_64BIT

#define	KATANA_ETH_PORT_SERIAL_CONTROL_VALUE		\
	ETH_FORCE_LINK_PASS			|	\
	ETH_ENABLE_AUTO_NEG_FOR_DUPLX		|	\
	ETH_DISABLE_AUTO_NEG_FOR_FLOW_CTRL	|	\
	ETH_ADV_SYMMETRIC_FLOW_CTRL		|	\
	ETH_FORCE_FC_MODE_NO_PAUSE_DIS_TX	|	\
	ETH_FORCE_BP_MODE_NO_JAM		|	\
	BIT9					|	\
	ETH_DO_NOT_FORCE_LINK_FAIL		|	\
	ETH_RETRANSMIT_16_ATTEMPTS		|	\
	ETH_ENABLE_AUTO_NEG_SPEED_GMII		|	\
	ETH_DTE_ADV_0				|	\
	ETH_DISABLE_AUTO_NEG_BYPASS		|	\
	ETH_AUTO_NEG_NO_CHANGE			|	\
	ETH_MAX_RX_PACKET_9700BYTE		|	\
	ETH_CLR_EXT_LOOPBACK			|	\
	ETH_SET_FULL_DUPLEX_MODE		|	\
	ETH_ENABLE_FLOW_CTRL_TX_RX_IN_FULL_DUPLEX

#ifndef __ASSEMBLY__

typedef enum {
	KATANA_ID_3750,
	KATANA_ID_750I,
	KATANA_ID_752I,
	KATANA_ID_MAX
} katana_id_t;

#endif

static inline u32
katana_bus_freq(void __iomem *cpld_base)
{
	u8 bd_cfg_0;

	bd_cfg_0 = in_8(cpld_base + KATANA_CPLD_BD_CFG_0);

	switch (bd_cfg_0 & KATANA_CPLD_BD_CFG_0_SYSCLK_MASK) {
	case KATANA_CPLD_BD_CFG_0_SYSCLK_200:
		return 200000000;
		break;

	case KATANA_CPLD_BD_CFG_0_SYSCLK_166:
		return 166666666;
		break;

	case KATANA_CPLD_BD_CFG_0_SYSCLK_133:
		return 133333333;
		break;

	case KATANA_CPLD_BD_CFG_0_SYSCLK_100:
		return 100000000;
		break;

	default:
		return 133333333;
		break;
	}
}

#endif	/* __PPC_PLATFORMS_KATANA_H */
