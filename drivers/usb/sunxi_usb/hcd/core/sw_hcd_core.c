/*
 * drivers/usb/sunxi_usb/hcd/core/sw_hcd_core.c
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include  "../include/sw_hcd_config.h"
#include  "../include/sw_hcd_core.h"
#include  "../include/sw_hcd_dma.h"

/* for high speed test mode; see USB 2.0 spec 7.1.20 */
static const u8 sw_hcd_test_packet[53] = {
	/* implicit SYNC then DATA0 to start */

	/* JKJKJKJK x9 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* JJKKJJKK x8 */
	0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
	/* JJJJKKKK x8 */
	0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee,
	/* JJJJJJJKKKKKKK x8 */
	0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	/* JJJJJJJK x8 */
	0x7f, 0xbf, 0xdf, 0xef, 0xf7, 0xfb, 0xfd,
	/* JKKKKKKK x10, JK */
	0xfc, 0x7e, 0xbf, 0xdf, 0xef, 0xf7, 0xfb, 0xfd, 0x7e

	/* implicit CRC16 then EOP to end */
};

/*
 * Interrupt Service Routine to record USB "global" interrupts.
 * Since these do not happen often and signify things of
 * paramount importance, it seems OK to check them individually;
 * the order of the tests is specified in the manual
 *
 * @param sw_hcd instance pointer
 * @param int_usb register contents
 * @param devctl
 * @param power
 */

#define STAGE0_MASK ((1 << USBC_BP_INTUSB_RESUME) \
                    | (1 << USBC_BP_INTUSB_SESSION_REQ) \
            		| (1 << USBC_BP_INTUSB_VBUS_ERROR) \
            		| (1 << USBC_BP_INTUSB_CONNECT) \
            		| (1 << USBC_BP_INTUSB_RESET) \
            		| (1 << USBC_BP_INTUSB_SOF))

/*
*******************************************************************************
*                     sw_hcd_write_fifo
*
* Description:
*    Load an endpoint's FIFO
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
void sw_hcd_write_fifo(struct sw_hcd_hw_ep *hw_ep, u16 len, const u8 *src)
{
	void __iomem *fifo = hw_ep->fifo;
	__u32 old_ep_index = 0;

	prefetch((u8 *)src);

	DMSG_DBG_HCD("sw_hcd_write_fifo: %cX ep%d fifo %p count %d buf %p\n",
			     'T', hw_ep->epnum, fifo, len, src);

	old_ep_index = USBC_GetActiveEp(hw_ep->sw_hcd->sw_hcd_io->usb_bsp_hdle);
	USBC_SelectActiveEp(hw_ep->sw_hcd->sw_hcd_io->usb_bsp_hdle, hw_ep->epnum);

	/* we can't assume unaligned reads work */
	if (likely((0x01 & (unsigned long) src) == 0)) {
		u16	index = 0;

		/* best case is 32bit-aligned source address */
		if ((0x02 & (unsigned long) src) == 0) {
			if (len >= 4) {
				sw_hcd_writesl(fifo, src + index, len >> 2);
				index += len & ~0x03;
			}

			if (len & 0x02) {
				USBC_Writew(*(u16 *)&src[index], fifo);
				index += 2;
			}
		} else {
			if (len >= 2) {
				sw_hcd_writesw(fifo, src + index, len >> 1);
				index += len & ~0x01;
			}
		}

		if (len & 0x01) {
			USBC_Writeb(src[index], fifo);
		}
	} else  {
		/* byte aligned */
		sw_hcd_writesb(fifo, src, len);
	}

	USBC_SelectActiveEp(hw_ep->sw_hcd->sw_hcd_io->usb_bsp_hdle, old_ep_index);

	return;
}
EXPORT_SYMBOL(sw_hcd_write_fifo);

/*
*******************************************************************************
*                     sw_hcd_read_fifo
*
* Description:
*    Unload an endpoint's FIFO
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
void sw_hcd_read_fifo(struct sw_hcd_hw_ep *hw_ep, u16 len, u8 *dst)
{
	void __iomem *fifo = hw_ep->fifo;
	__u32 old_ep_index = 0;

	DMSG_DBG_HCD("sw_hcd_read_fifo: %cX ep%d fifo %p count %d buf %p\n",
			    'R', hw_ep->epnum, fifo, len, dst);

	old_ep_index = USBC_GetActiveEp(hw_ep->sw_hcd->sw_hcd_io->usb_bsp_hdle);
	USBC_SelectActiveEp(hw_ep->sw_hcd->sw_hcd_io->usb_bsp_hdle, hw_ep->epnum);

	/* we can't assume unaligned writes work */
	if (likely((0x01 & (unsigned long) dst) == 0)) {
		u16	index = 0;

		/* best case is 32bit-aligned destination address */
		if ((0x02 & (unsigned long) dst) == 0) {
			if (len >= 4) {
				sw_hcd_readsl(fifo, dst, len >> 2);
				index = len & ~0x03;
			}
			if (len & 0x02) {
				*(u16 *)&dst[index] = USBC_Readw(fifo);
				index += 2;
			}
		} else {
			if (len >= 2) {
				sw_hcd_readsw(fifo, dst, len >> 1);
				index = len & ~0x01;
			}
		}

		if(len & 0x01){
			dst[index] = USBC_Readb(fifo);
		}
	} else  {
		/* byte aligned */
		sw_hcd_readsb(fifo, dst, len);
	}

	USBC_SelectActiveEp(hw_ep->sw_hcd->sw_hcd_io->usb_bsp_hdle, old_ep_index);

	return;
}
EXPORT_SYMBOL(sw_hcd_read_fifo);

/*
*******************************************************************************
*                     sw_hcd_load_testpacket
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
void sw_hcd_load_testpacket(struct sw_hcd *sw_hcd)
{
	void __iomem *usbc_base = sw_hcd->endpoints[0].regs;

	sw_hcd_ep_select(sw_hcd->mregs, 0);

	sw_hcd_write_fifo(sw_hcd->control_ep, sizeof(sw_hcd_test_packet), sw_hcd_test_packet);

	USBC_Writew(USBC_BP_CSR0_H_TxPkRdy, USBC_REG_CSR0(usbc_base));

	return;
}
EXPORT_SYMBOL(sw_hcd_load_testpacket);

/*
*******************************************************************************
*                     sw_hcd_start
*
* Description:
*    Program the to start (enable interrupts, dma, etc.).
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
void sw_hcd_start(struct sw_hcd *sw_hcd)
{
	void __iomem    *usbc_base  = NULL;
	u8              devctl      = 0;

    /* check argment */
    if(sw_hcd == NULL){
        DMSG_PANIC("ERR: invalid argment\n");
	    return ;
    }

    sw_hcd->is_active = 0;

	if(!sw_hcd->enable){
		DMSG_INFO("wrn: hcd is not enable, need not start hcd\n");
		return;
	}

    /* initialize parameter */
    usbc_base = sw_hcd->mregs;

    DMSG_DBG_HCD("sw_hcd_start: devctl = 0x%x, epmask = 0x%x\n",
		         USBC_Readb(USBC_REG_DEVCTL(usbc_base)), sw_hcd->epmask);

	/*  Set INT enable registers, enable interrupts */
	USBC_Writew(sw_hcd->epmask, USBC_REG_INTTxE(usbc_base));
	USBC_Writew((sw_hcd->epmask & 0xfe), USBC_REG_INTRxE(usbc_base));
	USBC_Writeb(0xff, USBC_REG_INTUSBE(usbc_base));

    USBC_Writeb(0x00, USBC_REG_TMCTL(usbc_base));

	/* put into basic highspeed mode and start session */
    USBC_Writeb((1 << USBC_BP_POWER_H_HIGH_SPEED_EN), USBC_REG_PCTL(usbc_base));

    devctl = USBC_Readb(USBC_REG_DEVCTL(usbc_base));
	devctl &= ~(1 << USBC_BP_DEVCTL_SESSION);
	USBC_Writeb(devctl, USBC_REG_DEVCTL(usbc_base));

	USBC_SelectBus(sw_hcd->sw_hcd_io->usb_bsp_hdle, USBC_IO_TYPE_PIO, 0, 0);

	/* assume ID pin is hard-wired to ground */
	sw_hcd_platform_enable(sw_hcd);

	/* port power on */
	sw_hcd_set_vbus(sw_hcd, 1);

    return;
}
EXPORT_SYMBOL(sw_hcd_start);

/*
*******************************************************************************
*                     sw_hcd_generic_disable
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
void sw_hcd_generic_disable(struct sw_hcd *sw_hcd)
{
	void __iomem    *usbc_base  = sw_hcd->mregs;

	/* disable interrupts */
	USBC_Writeb(0x00, USBC_REG_INTUSBE(usbc_base));
	USBC_Writew(0x00, USBC_REG_INTTxE(usbc_base));
	USBC_Writew(0x00, USBC_REG_INTRxE(usbc_base));

	/* off */
	USBC_Writew(0x00, USBC_REG_DEVCTL(usbc_base));

	/*  flush pending interrupts */
	USBC_Writeb(0xff, USBC_REG_INTUSB(usbc_base));
	USBC_Writew(0x3f, USBC_REG_INTTx(usbc_base));
	USBC_Writew(0x3f, USBC_REG_INTRx(usbc_base));

	return;
}
EXPORT_SYMBOL(sw_hcd_generic_disable);

/*
*******************************************************************************
*                     sw_hcd_stop
*
* Description:
*    Make the stop (disable interrupts, etc.);
* reversible by sw_hcd_start called on gadget driver unregister
* with controller locked, irqs blocked
* acts as a NOP unless some role activated the hardware
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
void sw_hcd_stop(struct sw_hcd *sw_hcd)
{
	if(!sw_hcd->enable){
		DMSG_INFO("wrn: hcd is not enable, need not stop hcd\n");
		return;
	}

	/* stop IRQs, timers, ... */
	sw_hcd_platform_disable(sw_hcd);
	sw_hcd_generic_disable(sw_hcd);

	DMSG_INFO("sw_hcd_stop: sw_hcd disabled\n");

	/* FIXME
	 *  - mark host and/or peripheral drivers unusable/inactive
	 *  - disable DMA (and enable it in sw_hcd Start)
	 *  - make sure we can sw_hcd_start() after sw_hcd_stop(); with
	 *    OTG mode, gadget driver module rmmod/modprobe cycles that
	 *  - ...
	 */
	sw_hcd_platform_try_idle(sw_hcd, 0);

	return;
}
EXPORT_SYMBOL(sw_hcd_stop);

/*
*******************************************************************************
*                     sw_hcd_platform_try_idle
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
void sw_hcd_platform_try_idle(struct sw_hcd *sw_hcd, unsigned long timeout)
{

}
EXPORT_SYMBOL(sw_hcd_platform_try_idle);

/*
*******************************************************************************
*                     sw_hcd_platform_enable
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
void sw_hcd_platform_enable(struct sw_hcd *sw_hcd)
{

}
EXPORT_SYMBOL(sw_hcd_platform_enable);

/*
*******************************************************************************
*                     sw_hcd_platform_disable
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
void sw_hcd_platform_disable(struct sw_hcd *sw_hcd)
{

}
EXPORT_SYMBOL(sw_hcd_platform_disable);

/*
*******************************************************************************
*                     sw_hcd_platform_set_mode
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
int sw_hcd_platform_set_mode(struct sw_hcd *sw_hcd, u8 sw_hcd_mode)
{
	DMSG_PANIC("ERR: sw_hcd_platform_set_mode not support\n");

	return 0;
}
EXPORT_SYMBOL(sw_hcd_platform_set_mode);

/*
*******************************************************************************
*                     sw_hcd_platform_init
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
int sw_hcd_platform_init(struct sw_hcd *sw_hcd)
{
	USBC_EnhanceSignal(sw_hcd->sw_hcd_io->usb_bsp_hdle);
	USBC_EnableDpDmPullUp(sw_hcd->sw_hcd_io->usb_bsp_hdle);
    USBC_EnableIdPullUp(sw_hcd->sw_hcd_io->usb_bsp_hdle);
	USBC_ForceId(sw_hcd->sw_hcd_io->usb_bsp_hdle, USBC_ID_TYPE_HOST);

	return 0;
}
EXPORT_SYMBOL(sw_hcd_platform_init);

/*
*******************************************************************************
*                     sw_hcd_platform_suspend
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
int sw_hcd_platform_suspend(struct sw_hcd *sw_hcd)
{
	DMSG_PANIC("ERR: sw_hcd_platform_suspend not support\n");

	return 0;
}
EXPORT_SYMBOL(sw_hcd_platform_suspend);

/*
*******************************************************************************
*                     sw_hcd_platform_resume
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
int sw_hcd_platform_resume(struct sw_hcd *sw_hcd)
{
	DMSG_PANIC("ERR: sw_hcd_platform_resume not support\n");

	return 0;
}
EXPORT_SYMBOL(sw_hcd_platform_resume);

/*
*******************************************************************************
*                     sw_hcd_platform_exit
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
int sw_hcd_platform_exit(struct sw_hcd *sw_hcd)
{
	USBC_DisableDpDmPullUp(sw_hcd->sw_hcd_io->usb_bsp_hdle);
    USBC_DisableIdPullUp(sw_hcd->sw_hcd_io->usb_bsp_hdle);
	USBC_ForceId(sw_hcd->sw_hcd_io->usb_bsp_hdle, USBC_ID_TYPE_DISABLE);

	return 0;
}
EXPORT_SYMBOL(sw_hcd_platform_exit);

/* "modprobe ... use_dma=0" etc */

/*
*******************************************************************************
*                     sw_hcd_dma_completion
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
void sw_hcd_dma_completion(struct sw_hcd *sw_hcd, u8 epnum, u8 transmit)
{
	u8	devctl = USBC_Readb(USBC_REG_DEVCTL(sw_hcd->mregs));

	/* called with controller lock already held */

	if (!epnum) {
        DMSG_PANIC("ERR: sw_hcd_dma_completion, not support ep0\n");
	} else {
		/* endpoints 1..15 */
		if (transmit) {
			if (devctl & (1 << USBC_BP_DEVCTL_HOST_MODE)) {
				if (is_host_capable()) {
					sw_hcd_host_tx(sw_hcd, epnum);
				}
			}
		} else {
			/* receive */
			if (devctl & (1 << USBC_BP_DEVCTL_HOST_MODE)) {
				if (is_host_capable()) {
					sw_hcd_host_rx(sw_hcd, epnum);
				}
			}
		}
	}

    return;
}
EXPORT_SYMBOL(sw_hcd_dma_completion);
/*
*******************************************************************************
*                     sw_hcd_soft_disconnect
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
void sw_hcd_soft_disconnect(struct sw_hcd *sw_hcd)
{
	DMSG_INFO("-------sw_hcd%d_soft_disconnect---------\n", sw_hcd->usbc_no);

	usb_hcd_resume_root_hub(sw_hcd_to_hcd(sw_hcd));
	sw_hcd_root_disconnect(sw_hcd);

	return;
}

/*
*******************************************************************************
*                     sw_hcd_stage0_irq
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
static irqreturn_t sw_hcd_stage0_irq(struct sw_hcd *sw_hcd, u8 int_usb, u8 devctl, u8 power)
{
	irqreturn_t handled = IRQ_NONE;
	void __iomem *usbc_base = sw_hcd->mregs;

	DMSG_DBG_HCD("sw_hcd_stage0_irq: Power=%02x, DevCtl=%02x, int_usb=0x%x\n", power, devctl, int_usb);

	if(int_usb & (1 << USBC_BP_INTUSB_SOF)){
		//DMSG_INFO("\n\n------------IRQ SOF-------------\n\n");

		USBC_INT_ClearMiscPending(sw_hcd->sw_hcd_io->usb_bsp_hdle, (1 << USBC_BP_INTUSB_SOF));

		USBC_INT_DisableUsbMiscUint(sw_hcd->sw_hcd_io->usb_bsp_hdle, (1 << USBC_BP_INTUSB_SOF));
	}

	/* in host mode, the peripheral may issue remote wakeup.
	 * in peripheral mode, the host may resume the link.
	 * spurious RESUME irqs happen too, paired with SUSPEND.
	 */
	if (int_usb & (1 << USBC_BP_INTUSB_RESUME)) {
		DMSG_INFO("\n------------IRQ RESUME-------------\n\n");

		USBC_INT_ClearMiscPending(sw_hcd->sw_hcd_io->usb_bsp_hdle, (1 << USBC_BP_INTUSB_RESUME));

		handled = IRQ_HANDLED;

		if (devctl & (1 << USBC_BP_DEVCTL_HOST_MODE)) {
			if (power & (1 << USBC_BP_POWER_H_SUSPEND)) {
				/* spurious */
				sw_hcd->int_usb &= ~(1 << USBC_BP_INTUSBE_EN_SUSPEND);

				DMSG_INFO("sw_hcd_stage0_irq, Spurious SUSPENDM\n");

				//break;
			}

			power &= ~(1 << USBC_BP_POWER_H_SUSPEND);
			power |= (1 << USBC_BP_POWER_H_RESUME);
			USBC_Writeb(power, USBC_REG_PCTL(usbc_base));

			sw_hcd->port1_status |= (USB_PORT_STAT_C_SUSPEND << 16) | SW_HCD_PORT_STAT_RESUME;
			sw_hcd->rh_timer = jiffies + msecs_to_jiffies(20);
			sw_hcd->is_active = 1;
			usb_hcd_resume_root_hub(sw_hcd_to_hcd(sw_hcd));
		} else {
				usb_hcd_resume_root_hub(sw_hcd_to_hcd(sw_hcd));
		}
    }

	/* see manual for the order of the tests */
	if (int_usb & (1 << USBC_BP_INTUSB_SESSION_REQ)) {
		DMSG_INFO("\n------------IRQ SESSION_REQ-------------\n\n");

		USBC_INT_ClearMiscPending(sw_hcd->sw_hcd_io->usb_bsp_hdle, (1 << USBC_BP_INTUSB_SESSION_REQ));

        /* power down */
        devctl = USBC_Readb(USBC_REG_DEVCTL(usbc_base));
    	devctl &= ~(1 << USBC_BP_DEVCTL_SESSION);
    	USBC_Writeb(devctl, USBC_REG_DEVCTL(usbc_base));

        USBC_ForceVbusValid(sw_hcd->sw_hcd_io->usb_bsp_hdle, USBC_VBUS_TYPE_LOW);

        sw_hcd_set_vbus(sw_hcd, 0);

        /* delay */
        mdelay(100);

        /* power on */
        devctl = USBC_Readb(USBC_REG_DEVCTL(usbc_base));
    	devctl |= (1 << USBC_BP_DEVCTL_SESSION);
    	USBC_Writeb(devctl, USBC_REG_DEVCTL(usbc_base));

        USBC_ForceVbusValid(sw_hcd->sw_hcd_io->usb_bsp_hdle, USBC_VBUS_TYPE_HIGH);

        sw_hcd_set_vbus(sw_hcd, 1);

		sw_hcd->ep0_stage = SW_HCD_EP0_START;

		handled = IRQ_HANDLED;
	}

	if (int_usb & (1 << USBC_BP_INTUSB_VBUS_ERROR)) {
		int	ignore = 0;

		DMSG_INFO("\n------------IRQ VBUS_ERROR-------------\n\n");

		USBC_INT_ClearMiscPending(sw_hcd->sw_hcd_io->usb_bsp_hdle, (1 << USBC_BP_INTUSB_VBUS_ERROR));

        /* recovery is dicey once we've gotten past the
		 * initial stages of enumeration, but if VBUS
		 * stayed ok at the other end of the link, and
		 * another reset is due (at least for high speed,
		 * to redo the chirp etc), it might work OK...
		 */
		if (sw_hcd->vbuserr_retry) {
			sw_hcd->vbuserr_retry--;
			ignore  = 1;

			devctl |= (1 << USBC_BP_DEVCTL_SESSION);
			USBC_Writeb(devctl, USBC_REG_DEVCTL(usbc_base));
		} else {
			sw_hcd->port1_status |= (1 << USB_PORT_FEAT_OVER_CURRENT)
				               | (1 << USB_PORT_FEAT_C_OVER_CURRENT);
		}

        /* go through A_WAIT_VFALL then start a new session */
		if (!ignore){
			sw_hcd_set_vbus(sw_hcd, 0);
			sw_hcd->ep0_stage = SW_HCD_EP0_START;
		}

		handled = IRQ_HANDLED;
    }

	if (int_usb & (1 << USBC_BP_INTUSB_CONNECT)) {
		struct usb_hcd *hcd = sw_hcd_to_hcd(sw_hcd);

		DMSG_INFO("\n------------IRQ CONNECT-------------\n\n");

		USBC_INT_ClearMiscPending(sw_hcd->sw_hcd_io->usb_bsp_hdle, (1 << USBC_BP_INTUSB_CONNECT));

		handled = IRQ_HANDLED;
		sw_hcd->is_active = 1;

		sw_hcd->ep0_stage = SW_HCD_EP0_START;

        sw_hcd->port1_status &= ~(USB_PORT_STAT_LOW_SPEED
        					|USB_PORT_STAT_HIGH_SPEED
        					|USB_PORT_STAT_ENABLE
        					);
		sw_hcd->port1_status |= USB_PORT_STAT_CONNECTION
					        |(USB_PORT_STAT_C_CONNECTION << 16);

        /* high vs full speed is just a guess until after reset */
		if (devctl & (1 << USBC_BP_DEVCTL_LS_DEV)){
			sw_hcd->port1_status |= USB_PORT_STAT_LOW_SPEED;
		}

		if(hcd->status_urb){
			usb_hcd_poll_rh_status(hcd);
		}else{
			usb_hcd_resume_root_hub(hcd);
		}

		SW_HCD_HST_MODE(sw_hcd);
    }

	/* mentor saves a bit: bus reset and babble share the same irq.
	 * only host sees babble; only peripheral sees bus reset.
	 */
	if (int_usb & (1 << USBC_BP_INTUSB_RESET)) {
	    DMSG_INFO("\n------------IRQ Reset or Babble-------------\n\n");

		USBC_INT_ClearMiscPending(sw_hcd->sw_hcd_io->usb_bsp_hdle, (1 << USBC_BP_INTUSB_RESET));

        //把babble当作disconnect处理
		USBC_Host_SetFunctionAddress_Deafult(sw_hcd->sw_hcd_io->usb_bsp_hdle, USBC_EP_TYPE_TX, 0);
		{
		    u32 i = 1;

			for( i = 1 ; i <= 5; i++){
				USBC_Host_SetFunctionAddress_Deafult(sw_hcd->sw_hcd_io->usb_bsp_hdle, USBC_EP_TYPE_TX, i);
				USBC_Host_SetFunctionAddress_Deafult(sw_hcd->sw_hcd_io->usb_bsp_hdle, USBC_EP_TYPE_RX, i);
			}
		}

		/* 清除关于拔出设备的所有中断, 目前没有hub, 所以可以清除所有中断 */
		USBC_INT_ClearMiscPendingAll(sw_hcd->sw_hcd_io->usb_bsp_hdle);
		USBC_INT_ClearEpPendingAll(sw_hcd->sw_hcd_io->usb_bsp_hdle, USBC_EP_TYPE_TX);
		USBC_INT_ClearEpPendingAll(sw_hcd->sw_hcd_io->usb_bsp_hdle, USBC_EP_TYPE_RX);

        /* power down */
        devctl = USBC_Readb(USBC_REG_DEVCTL(usbc_base));
    	devctl &= ~(1 << USBC_BP_DEVCTL_SESSION);
    	USBC_Writeb(devctl, USBC_REG_DEVCTL(usbc_base));

        USBC_ForceVbusValid(sw_hcd->sw_hcd_io->usb_bsp_hdle, USBC_VBUS_TYPE_LOW);

        sw_hcd_set_vbus(sw_hcd, 0);

        /* delay */
        mdelay(100);

        /* power on */
        devctl = USBC_Readb(USBC_REG_DEVCTL(usbc_base));
    	devctl |= (1 << USBC_BP_DEVCTL_SESSION);
    	USBC_Writeb(devctl, USBC_REG_DEVCTL(usbc_base));

        USBC_ForceVbusValid(sw_hcd->sw_hcd_io->usb_bsp_hdle, USBC_VBUS_TYPE_HIGH);

        sw_hcd_set_vbus(sw_hcd, 1);

        /* disconnect */
		sw_hcd->ep0_stage = SW_HCD_EP0_START;
		usb_hcd_resume_root_hub(sw_hcd_to_hcd(sw_hcd));
		sw_hcd_root_disconnect(sw_hcd);

	    handled = IRQ_HANDLED;
    }

    schedule_work(&sw_hcd->irq_work);

	return handled;
}

/*
*******************************************************************************
*                     sw_hcd_stage2_irq
*
* Description:
*    Interrupt Service Routine to record USB "global" interrupts.
* Since these do not happen often and signify things of
* paramount importance, it seems OK to check them individually;
* the order of the tests is specified in the manual
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
static irqreturn_t sw_hcd_stage2_irq(struct sw_hcd *sw_hcd,
                                   u8 int_usb,
                                   u8 devctl,
                                   u8 power)
{
	irqreturn_t handled = IRQ_NONE;

	if ((int_usb & (1 << USBC_BP_INTUSB_DISCONNECT)) && !sw_hcd->ignore_disconnect) {
		DMSG_INFO("\n------------IRQ DISCONNECT-------------\n\n");

		USBC_INT_ClearMiscPending(sw_hcd->sw_hcd_io->usb_bsp_hdle, (1 << USBC_BP_INTUSB_DISCONNECT));

		handled = IRQ_HANDLED;

		usb_hcd_resume_root_hub(sw_hcd_to_hcd(sw_hcd));
		sw_hcd_root_disconnect(sw_hcd);

        schedule_work(&sw_hcd->irq_work);
    }

	if (int_usb & (1 << USBC_BP_INTUSB_SUSPEND)) {
		DMSG_INFO("\n------------IRQ SUSPEND-------------\n\n");

		USBC_INT_ClearMiscPending(sw_hcd->sw_hcd_io->usb_bsp_hdle, (1 << USBC_BP_INTUSB_SUSPEND));

		handled = IRQ_HANDLED;

        /* "should not happen" */
        sw_hcd->is_active = 0;

        schedule_work(&sw_hcd->irq_work);
    }

    return handled;
}


/*
*******************************************************************************
*                     sw_hcd_interrupt
*
* Description:
*    handle all the irqs defined by the sw_hcd core. for now we expect:  other
* irq sources //(phy, dma, etc) will be handled first, sw_hcd->int_* values
* will be assigned, and the irq will already have been acked.
*
* called in irq context with spinlock held, irqs blocked
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
static irqreturn_t sw_hcd_interrupt(struct sw_hcd *sw_hcd)
{
	irqreturn_t     retval      = IRQ_NONE;
	u8              devctl      = 0;
	u8              power       = 0;
	int             ep_num      = 0;
	u32             reg         = 0;
	void __iomem    *usbc_base  = NULL;

    /* check argment */
    if(sw_hcd == NULL){
        DMSG_PANIC("ERR: invalid argment\n");
	    return IRQ_NONE;
    }

    /* initialize parameter */
    usbc_base   = sw_hcd->mregs;

	devctl = USBC_Readb(USBC_REG_DEVCTL(usbc_base));
	power  = USBC_Readb(USBC_REG_PCTL(usbc_base));

	DMSG_DBG_HCD("irq: (0x%x, 0x%x, 0x%x)\n", sw_hcd->int_usb, sw_hcd->int_tx, sw_hcd->int_rx);

	/* the core can interrupt us for multiple reasons; docs have
	 * a generic interrupt flowchart to follow
	 */
	if (sw_hcd->int_usb & STAGE0_MASK){
		retval |= sw_hcd_stage0_irq(sw_hcd, sw_hcd->int_usb, devctl, power);
    }

	/* "stage 1" is handling endpoint irqs */

	/* handle endpoint 0 first */
	if (sw_hcd->int_tx & 1) {
		USBC_INT_ClearEpPending(sw_hcd->sw_hcd_io->usb_bsp_hdle, USBC_EP_TYPE_TX, 0);

		retval |= sw_hcd_h_ep0_irq(sw_hcd);
	}

	/* RX on endpoints 1-15 */
	reg = sw_hcd->int_rx >> 1;
	ep_num = 1;
	while (reg) {
		if (reg & 1) {
			USBC_INT_ClearEpPending(sw_hcd->sw_hcd_io->usb_bsp_hdle,
				                    USBC_EP_TYPE_RX,
				                    ep_num);

			/* sw_hcd_ep_select(sw_hcd->mregs, ep_num); */
			/* REVISIT just retval = ep->rx_irq(...) */
			retval = IRQ_HANDLED;
			if (devctl & (1 << USBC_BP_DEVCTL_HOST_MODE)) {
				if (is_host_capable()){
					sw_hcd_host_rx(sw_hcd, ep_num);
				}
			}
		}

		reg >>= 1;
		ep_num++;
	}

	/* TX on endpoints 1-15 */
	reg = sw_hcd->int_tx >> 1;
	ep_num = 1;
	while (reg) {
		if (reg & 1) {
			USBC_INT_ClearEpPending(sw_hcd->sw_hcd_io->usb_bsp_hdle,
		                    		USBC_EP_TYPE_TX,
		                    		ep_num);

			/* sw_hcd_ep_select(sw_hcd->mregs, ep_num); */
			/* REVISIT just retval |= ep->tx_irq(...) */
			retval = IRQ_HANDLED;
			if (devctl & (1 << USBC_BP_DEVCTL_HOST_MODE)) {
				if (is_host_capable()) {
					sw_hcd_host_tx(sw_hcd, ep_num);
				}
			}
		}

		reg >>= 1;
		ep_num++;
	}

	/* finish handling "global" interrupts after handling fifos */
	if(sw_hcd->int_usb){
		retval |= sw_hcd_stage2_irq(sw_hcd, sw_hcd->int_usb, devctl, power);
	}

	return retval;
}

/*
*******************************************************************************
*                     clear_all_irq
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
static void clear_all_irq(struct sw_hcd *sw_hcd)
{
    USBC_INT_ClearEpPendingAll(sw_hcd->sw_hcd_io->usb_bsp_hdle, USBC_EP_TYPE_TX);
    USBC_INT_ClearEpPendingAll(sw_hcd->sw_hcd_io->usb_bsp_hdle, USBC_EP_TYPE_RX);
    USBC_INT_ClearMiscPendingAll(sw_hcd->sw_hcd_io->usb_bsp_hdle);
}

/*
*******************************************************************************
*                     generic_interrupt
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
irqreturn_t generic_interrupt(int irq, void *__hci)
{
	unsigned long   flags       = 0;
	irqreturn_t     retval      = IRQ_NONE;
	struct sw_hcd     *sw_hcd       = NULL;
	void __iomem    *usbc_base  = NULL;

    /* check argment */
    if(__hci == NULL){
        DMSG_PANIC("ERR: invalid argment\n");
	    return IRQ_NONE;
    }

    /* initialize parameter */
    sw_hcd        = (struct sw_hcd *)__hci;
    usbc_base   = sw_hcd->mregs;

	/* host role must be active */
	if (!sw_hcd->enable){
	    DMSG_PANIC("ERR: usb generic_interrupt, host is not enable\n");
		clear_all_irq(sw_hcd);
		return IRQ_NONE;
    }

	spin_lock_irqsave(&sw_hcd->lock, flags);

	sw_hcd->int_usb = USBC_Readb(USBC_REG_INTUSB(usbc_base));
	sw_hcd->int_tx  = USBC_Readb(USBC_REG_INTTx(usbc_base));
	sw_hcd->int_rx  = USBC_Readb(USBC_REG_INTRx(usbc_base));

	if (sw_hcd->int_usb || sw_hcd->int_tx || sw_hcd->int_rx){
		retval = sw_hcd_interrupt(sw_hcd);
	}

	spin_unlock_irqrestore(&sw_hcd->lock, flags);

	/* REVISIT we sometimes get spurious IRQs on g_ep0
	 * not clear why...
	 */
	if (retval != IRQ_HANDLED){
		DMSG_INFO("spurious?\n");
    }

	return IRQ_HANDLED;
}
EXPORT_SYMBOL(generic_interrupt);




























