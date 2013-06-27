/*
 * drivers/usb/sunxi_usb/misc/sw_usb_debug.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * javen <javen@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */


#include  "../include/sw_usb_config.h"

/*
*******************************************************************************
*                     print_usb_reg_by_ep
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void print_usb_reg_by_ep(spinlock_t *lock, __u32 usbc_base, __s32 ep_index, char *str)
{
//	__u32 i = 0;
	__u32 old_ep_index = 0;
	unsigned long flags = 0;

	if(lock){
		spin_lock_irqsave(lock, flags);
	}

    DMSG_INFO("\n");
    DMSG_INFO("--------------------------ep%d: %s--------------------------\n", ep_index, str);

	if(ep_index >= 0){
		old_ep_index = USBC_Readw(usbc_base + USBC_REG_o_EPIND);
		USBC_Writew(ep_index, (usbc_base + USBC_REG_o_EPIND));
		DMSG_INFO("old_ep_index = %d, ep_index = %d\n", old_ep_index, ep_index);
	}

    DMSG_INFO("USBC_REG_o_FADDR         = 0x%x\n", USBC_Readb(usbc_base + USBC_REG_o_FADDR));
    DMSG_INFO("USBC_REG_o_PCTL          = 0x%x\n", USBC_Readb(usbc_base + USBC_REG_o_PCTL));
    DMSG_INFO("USBC_REG_o_INTTx         = 0x%x\n", USBC_Readw(usbc_base + USBC_REG_o_INTTx));
    DMSG_INFO("USBC_REG_o_INTRx         = 0x%x\n", USBC_Readw(usbc_base + USBC_REG_o_INTRx));
    DMSG_INFO("USBC_REG_o_INTTxE        = 0x%x\n", USBC_Readw(usbc_base + USBC_REG_o_INTTxE));
    DMSG_INFO("USBC_REG_o_INTRxE        = 0x%x\n", USBC_Readw(usbc_base + USBC_REG_o_INTRxE));
    DMSG_INFO("USBC_REG_o_INTUSB        = 0x%x\n", USBC_Readb(usbc_base + USBC_REG_o_INTUSB));
    DMSG_INFO("USBC_REG_o_INTUSBE       = 0x%x\n", USBC_Readb(usbc_base + USBC_REG_o_INTUSBE));
    DMSG_INFO("USBC_REG_o_EPIND         = 0x%x\n", USBC_Readw(usbc_base + USBC_REG_o_EPIND));
    DMSG_INFO("USBC_REG_o_TXMAXP        = 0x%x\n", USBC_Readw(usbc_base + USBC_REG_o_TXMAXP));
    DMSG_INFO("USBC_REG_o_CSR0          = 0x%x\n", USBC_Readw(usbc_base + USBC_REG_o_CSR0));
    DMSG_INFO("USBC_REG_o_TXCSR         = 0x%x\n", USBC_Readw(usbc_base + USBC_REG_o_TXCSR));
    DMSG_INFO("USBC_REG_o_RXMAXP        = 0x%x\n", USBC_Readw(usbc_base + USBC_REG_o_RXMAXP));
    DMSG_INFO("USBC_REG_o_RXCSR         = 0x%x\n", USBC_Readw(usbc_base + USBC_REG_o_RXCSR));

    DMSG_INFO("USBC_REG_o_COUNT0        = 0x%x\n", USBC_Readw(usbc_base + USBC_REG_o_COUNT0));
    DMSG_INFO("USBC_REG_o_RXCOUNT       = 0x%x\n", USBC_Readw(usbc_base + USBC_REG_o_RXCOUNT));
    DMSG_INFO("USBC_REG_o_TXTYPE        = 0x%x\n", USBC_Readb(usbc_base + USBC_REG_o_TXTYPE));
    DMSG_INFO("USBC_REG_o_NAKLIMIT0     = 0x%x\n", USBC_Readb(usbc_base + USBC_REG_o_NAKLIMIT0));
    DMSG_INFO("USBC_REG_o_TXINTERVAL    = 0x%x\n", USBC_Readb(usbc_base + USBC_REG_o_TXINTERVAL));
    DMSG_INFO("USBC_REG_o_RXTYPE        = 0x%x\n", USBC_Readb(usbc_base + USBC_REG_o_RXTYPE));
    DMSG_INFO("USBC_REG_o_RXINTERVAL    = 0x%x\n", USBC_Readb(usbc_base + USBC_REG_o_RXINTERVAL));
    DMSG_INFO("USBC_REG_o_CONFIGDATA    = 0x%x\n", USBC_Readb(usbc_base + USBC_REG_o_CONFIGDATA));

    DMSG_INFO("USBC_REG_o_DEVCTL        = 0x%x\n", USBC_Readb(usbc_base + USBC_REG_o_DEVCTL));
    DMSG_INFO("USBC_REG_o_TXFIFOSZ      = 0x%x\n", USBC_Readb(usbc_base + USBC_REG_o_TXFIFOSZ));
    DMSG_INFO("USBC_REG_o_RXFIFOSZ      = 0x%x\n", USBC_Readb(usbc_base + USBC_REG_o_RXFIFOSZ));
    DMSG_INFO("USBC_REG_o_TXFIFOAD      = 0x%x\n", USBC_Readw(usbc_base + USBC_REG_o_TXFIFOAD));
    DMSG_INFO("USBC_REG_o_RXFIFOAD      = 0x%x\n", USBC_Readw(usbc_base + USBC_REG_o_RXFIFOAD));
    DMSG_INFO("USBC_REG_o_VEND0         = 0x%x\n", USBC_Readb(usbc_base + USBC_REG_o_VEND0));
    DMSG_INFO("USBC_REG_o_VEND1         = 0x%x\n", USBC_Readb(usbc_base + USBC_REG_o_VEND1));

	DMSG_INFO("TXFADDRx(%d)             = 0x%x\n", ep_index, USBC_Readb(usbc_base + USBC_REG_o_TXFADDRx));
	DMSG_INFO("TXHADDRx(%d              = 0x%x\n", ep_index, USBC_Readb(usbc_base + USBC_REG_o_TXHADDRx));
	DMSG_INFO("TXHPORTx(%d)             = 0x%x\n", ep_index, USBC_Readb(usbc_base + USBC_REG_o_TXHPORTx));
	DMSG_INFO("RXFADDRx(%d)             = 0x%x\n", ep_index, USBC_Readb(usbc_base + USBC_REG_o_RXFADDRx));
	DMSG_INFO("RXHADDRx(%d)             = 0x%x\n", ep_index, USBC_Readb(usbc_base + USBC_REG_o_RXHADDRx));
	DMSG_INFO("RXHPORTx(%d)             = 0x%x\n", ep_index, USBC_Readb(usbc_base + USBC_REG_o_RXHPORTx));
	DMSG_INFO("RPCOUNTx(%d)             = 0x%x\n", ep_index, (u32)USBC_Readl(usbc_base + USBC_REG_o_RPCOUNT));

    DMSG_INFO("USBC_REG_o_ISCR          = 0x%x\n", (u32)USBC_Readl(usbc_base + USBC_REG_o_ISCR));
    DMSG_INFO("USBC_REG_o_PHYCTL        = 0x%x\n", (u32)USBC_Readl(usbc_base + USBC_REG_o_PHYCTL));
    DMSG_INFO("USBC_REG_o_PHYBIST       = 0x%x\n", (u32)USBC_Readl(usbc_base + USBC_REG_o_PHYBIST));

	if(ep_index >= 0){
		USBC_Writew(old_ep_index, (usbc_base + USBC_REG_o_EPIND));
	}

	DMSG_INFO("---------------------------------------------------------------------------\n");
	DMSG_INFO("\n");

	if(lock){
		spin_unlock_irqrestore(lock, flags);
	}

    return;
}

/*
*******************************************************************************
*                     print_all_usb_reg
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void print_all_usb_reg(spinlock_t *lock, __u32 usbc_base, __s32 ep_start, __u32 ep_end, char *str)
{
	__u32 i = 0;
	__u32 old_ep_index = 0;
	unsigned long flags = 0;

	if(lock){
		spin_lock_irqsave(lock, flags);
	}

    DMSG_INFO("\n");
    DMSG_INFO("-------------------print_all_usb_reg: %s----------------\n", str);

	old_ep_index = USBC_Readw(usbc_base + USBC_REG_o_EPIND);

	for(i = ep_start; i <= ep_end; i++){
		print_usb_reg_by_ep(lock, usbc_base, i, str);
	}

	USBC_Writew(old_ep_index, (usbc_base + USBC_REG_o_EPIND));

	DMSG_INFO("---------------------------------------------------------------------------\n");
	DMSG_INFO("\n");

	if(lock){
		spin_unlock_irqrestore(lock, flags);
	}

    return;
}


