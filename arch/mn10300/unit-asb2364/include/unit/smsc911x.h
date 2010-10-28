/* Support for the SMSC911x NIC
 *
 * Copyright (C) 2006 Matsushita Electric Industrial Co., Ltd.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_UNIT_SMSC911X_H
#define _ASM_UNIT_SMSC911X_H

#include <linux/netdevice.h>
#include <proc/irq.h>
#include <unit/fpga-regs.h>

#define MN10300_USE_EXT_EEPROM


#define SMSC911X_BASE		0xA8000000UL
#define SMSC911X_BASE_END	0xA8000100UL
#define SMSC911X_IRQ		FPGA_LAN_IRQ

/*
 * Allow the FPGA to be initialised by the SMSC911x driver
 */
#undef SMSC_INITIALIZE
#define SMSC_INITIALIZE()					\
do {								\
	/* release reset */					\
	ASB2364_FPGA_REG_RESET_LAN = 0x0001;			\
	SyncExBus();						\
} while (0)

#ifdef MN10300_USE_EXT_EEPROM
#include <linux/delay.h>
#include <unit/clock.h>

#define EEPROM_ADDRESS	0xA0
#define MAC_OFFSET	0x0008
#define USE_IIC_CH	0	/* 0 or 1 */
#define IIC_OFFSET	(0x80000 * USE_IIC_CH)
#define IIC_DTRM	__SYSREG(0xd8400000 + IIC_OFFSET, u32)
#define IIC_DREC	__SYSREG(0xd8400004 + IIC_OFFSET, u32)
#define IIC_MYADD	__SYSREG(0xd8400008 + IIC_OFFSET, u32)
#define IIC_CLK		__SYSREG(0xd840000c + IIC_OFFSET, u32)
#define IIC_BRST	__SYSREG(0xd8400010 + IIC_OFFSET, u32)
#define IIC_HOLD	__SYSREG(0xd8400014 + IIC_OFFSET, u32)
#define IIC_BSTS	__SYSREG(0xd8400018 + IIC_OFFSET, u32)
#define IIC_ICR		__SYSREG(0xd4000080 + 4 * USE_IIC_CH, u16)

#define IIC_CLK_PLS	((unsigned short)(MN10300_IOCLK / 100000 - 1))
#define IIC_CLK_LOW	((unsigned short)(IIC_CLK_PLS / 2))

#define SYS_IIC_DTRM_Bit_STA	((unsigned short)0x0400)
#define SYS_IIC_DTRM_Bit_STO	((unsigned short)0x0200)
#define SYS_IIC_DTRM_Bit_ACK	((unsigned short)0x0100)
#define SYS_IIC_DTRM_Bit_DATA	((unsigned short)0x00FF)

static inline void POLL_INT_REQ(volatile u16 *icr)
{
	unsigned long flags;
	u16 tmp;

	while (!(*icr & GxICR_REQUEST))
		;
	flags = arch_local_cli_save();
	tmp = *icr;
	*icr = (tmp & GxICR_LEVEL) | GxICR_DETECT;
	tmp = *icr;
	arch_local_irq_restore(flags);
}

/*
 * Implement the SMSC911x hook for MAC address retrieval
 */
#undef smsc_get_mac
static inline int smsc_get_mac(struct net_device *dev)
{
	unsigned char *mac_buf = dev->dev_addr;
	int i;
	unsigned short value;
	unsigned int data;
	int mac_length = 6;
	int check;
	u16 orig_gicr, tmp;
	unsigned long flags;

	/* save original GnICR and clear GnICR.IE */
	flags = arch_local_cli_save();
	orig_gicr = IIC_ICR;
	IIC_ICR = orig_gicr & GxICR_LEVEL;
	tmp = IIC_ICR;
	arch_local_irq_restore(flags);

	IIC_MYADD = 0x00000008;
	IIC_CLK = (IIC_CLK_LOW << 16) + (IIC_CLK_PLS);
	/* bus hung recovery */

	while (1) {
		check = 0;
		for (i = 0; i < 3; i++) {
			if ((IIC_BSTS & 0x00000003) == 0x00000003)
				check++;
			udelay(3);
		}

		if (check == 3) {
			IIC_BRST = 0x00000003;
			break;
		} else {
			for (i = 0; i < 3; i++) {
				IIC_BRST = 0x00000002;
				udelay(8);
				IIC_BRST = 0x00000003;
				udelay(8);
			}
		}
	}

	IIC_BRST = 0x00000002;
	IIC_BRST = 0x00000003;

	value	=  SYS_IIC_DTRM_Bit_STA | SYS_IIC_DTRM_Bit_ACK;
	value	|= (((unsigned short)EEPROM_ADDRESS & SYS_IIC_DTRM_Bit_DATA) |
		    (unsigned short)0x0000);
	IIC_DTRM = value;
	POLL_INT_REQ(&IIC_ICR);

	/** send offset of MAC address in EEPROM **/
	IIC_DTRM = (unsigned char)((MAC_OFFSET & 0xFF00) >> 8);
	POLL_INT_REQ(&IIC_ICR);

	IIC_DTRM = (unsigned char)(MAC_OFFSET & 0x00FF);
	POLL_INT_REQ(&IIC_ICR);

	udelay(1000);

	value	=  SYS_IIC_DTRM_Bit_STA;
	value	|= (((unsigned short)EEPROM_ADDRESS & SYS_IIC_DTRM_Bit_DATA) |
		    (unsigned short)0x0001);
	IIC_DTRM = value;
	POLL_INT_REQ(&IIC_ICR);

	IIC_DTRM = 0x00000000;
	while (mac_length > 0) {
		POLL_INT_REQ(&IIC_ICR);

		data = IIC_DREC;
		mac_length--;
		if (mac_length == 0)
			value = 0x00000300;	/* stop IIC bus */
		else if (mac_length == 1)
			value = 0x00000100;	/* no ack */
		else
			value = 0x00000000;	/* ack */
		IIC_DTRM = value;
		*mac_buf++ = (unsigned char)(data & 0xff);
	}

	/* restore GnICR.LV and GnICR.IE */
	flags = arch_local_cli_save();
	IIC_ICR = (orig_gicr & (GxICR_LEVEL | GxICR_ENABLE));
	tmp = IIC_ICR;
	arch_local_irq_restore(flags);

	return 0;
}
#endif /* MN10300_USE_EXT_EEPROM */
#endif /* _ASM_UNIT_SMSC911X_H */
