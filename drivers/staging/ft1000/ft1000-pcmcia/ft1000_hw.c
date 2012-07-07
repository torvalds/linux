/*---------------------------------------------------------------------------
   FT1000 driver for Flarion Flash OFDM NIC Device

   Copyright (C) 2002 Flarion Technologies, All rights reserved.
   Copyright (C) 2006 Patrik Ostrihon, All rights reserved.
   Copyright (C) 2006 ProWeb Consulting, a.s, All rights reserved.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2 of the License, or (at your option) any
   later version. This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details. You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place -
   Suite 330, Boston, MA 02111-1307, USA.
-----------------------------------------------------------------------------*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>

#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <asm/io.h>
#include <asm/bitops.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>
#include <linux/wait.h>
#include <linux/vmalloc.h>

#include <linux/firmware.h>
#include <linux/ethtool.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

#ifdef FT_DEBUG
#define DEBUG(n, args...) printk(KERN_DEBUG args);
#else
#define DEBUG(n, args...)
#endif

#include <linux/delay.h>
#include "ft1000.h"

static const struct firmware *fw_entry;

static void ft1000_hbchk(u_long data);
static struct timer_list poll_timer = {
      .function = ft1000_hbchk
};

static u16 cmdbuffer[1024];
static u8 tempbuffer[1600];
static u8 ft1000_card_present = 0;
static u8 flarion_ft1000_cnt = 0;

static irqreturn_t ft1000_interrupt(int irq, void *dev_id);
static void ft1000_enable_interrupts(struct net_device *dev);
static void ft1000_disable_interrupts(struct net_device *dev);

/* new kernel */
MODULE_AUTHOR("");
MODULE_DESCRIPTION
    ("Support for Flarion Flash OFDM NIC Device. Support for PCMCIA when used with ft1000_cs.");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("FT1000");

#define MAX_RCV_LOOP   100

//---------------------------------------------------------------------------
//
// Function:   ft1000_read_fifo_len
// Description: This function will read the ASIC Uplink FIFO status register
//             which will return the number of bytes remaining in the Uplink FIFO.
//             Sixteen bytes are subtracted to make sure that the ASIC does not
//             reach its threshold.
// Input:
//     dev    - network device structure
// Output:
//     value  - number of bytes available in the ASIC Uplink FIFO.
//
//---------------------------------------------------------------------------
static inline u16 ft1000_read_fifo_len(struct net_device *dev)
{
	struct ft1000_info *info = netdev_priv(dev);

	if (info->AsicID == ELECTRABUZZ_ID)
		return (ft1000_read_reg(dev, FT1000_REG_UFIFO_STAT) - 16);
	else
		return (ft1000_read_reg(dev, FT1000_REG_MAG_UFSR) - 16);
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_read_dpram
// Description: This function will read the specific area of dpram
//             (Electrabuzz ASIC only)
// Input:
//     dev    - device structure
//     offset - index of dpram
// Output:
//     value  - value of dpram
//
//---------------------------------------------------------------------------
u16 ft1000_read_dpram(struct net_device * dev, int offset)
{
	struct ft1000_info *info = netdev_priv(dev);
	unsigned long flags;
	u16 data;

	// Provide mutual exclusive access while reading ASIC registers.
	spin_lock_irqsave(&info->dpram_lock, flags);
	ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR, offset);
	data = ft1000_read_reg(dev, FT1000_REG_DPRAM_DATA);
	spin_unlock_irqrestore(&info->dpram_lock, flags);

	return (data);
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_write_dpram
// Description: This function will write to a specific area of dpram
//             (Electrabuzz ASIC only)
// Input:
//     dev    - device structure
//     offset - index of dpram
//     value  - value to write
// Output:
//     none.
//
//---------------------------------------------------------------------------
static inline void ft1000_write_dpram(struct net_device *dev,
					  int offset, u16 value)
{
	struct ft1000_info *info = netdev_priv(dev);
	unsigned long flags;

	// Provide mutual exclusive access while reading ASIC registers.
	spin_lock_irqsave(&info->dpram_lock, flags);
	ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR, offset);
	ft1000_write_reg(dev, FT1000_REG_DPRAM_DATA, value);
	spin_unlock_irqrestore(&info->dpram_lock, flags);
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_read_dpram_mag_16
// Description: This function will read the specific area of dpram
//             (Magnemite ASIC only)
// Input:
//     dev    - device structure
//     offset - index of dpram
// Output:
//     value  - value of dpram
//
//---------------------------------------------------------------------------
u16 ft1000_read_dpram_mag_16(struct net_device *dev, int offset, int Index)
{
	struct ft1000_info *info = netdev_priv(dev);
	unsigned long flags;
	u16 data;

	// Provide mutual exclusive access while reading ASIC registers.
	spin_lock_irqsave(&info->dpram_lock, flags);
	ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR, offset);
	// check if we want to read upper or lower 32-bit word
	if (Index) {
		data = ft1000_read_reg(dev, FT1000_REG_MAG_DPDATAL);
	} else {
		data = ft1000_read_reg(dev, FT1000_REG_MAG_DPDATAH);
	}
	spin_unlock_irqrestore(&info->dpram_lock, flags);

	return (data);
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_write_dpram_mag_16
// Description: This function will write to a specific area of dpram
//             (Magnemite ASIC only)
// Input:
//     dev    - device structure
//     offset - index of dpram
//     value  - value to write
// Output:
//     none.
//
//---------------------------------------------------------------------------
static inline void ft1000_write_dpram_mag_16(struct net_device *dev,
						 int offset, u16 value, int Index)
{
	struct ft1000_info *info = netdev_priv(dev);
	unsigned long flags;

	// Provide mutual exclusive access while reading ASIC registers.
	spin_lock_irqsave(&info->dpram_lock, flags);
	ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR, offset);
	if (Index) {
		ft1000_write_reg(dev, FT1000_REG_MAG_DPDATAL, value);
	} else {
		ft1000_write_reg(dev, FT1000_REG_MAG_DPDATAH, value);
	}
	spin_unlock_irqrestore(&info->dpram_lock, flags);
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_read_dpram_mag_32
// Description: This function will read the specific area of dpram
//             (Magnemite ASIC only)
// Input:
//     dev    - device structure
//     offset - index of dpram
// Output:
//     value  - value of dpram
//
//---------------------------------------------------------------------------
u32 ft1000_read_dpram_mag_32(struct net_device *dev, int offset)
{
	struct ft1000_info *info = netdev_priv(dev);
	unsigned long flags;
	u32 data;

	// Provide mutual exclusive access while reading ASIC registers.
	spin_lock_irqsave(&info->dpram_lock, flags);
	ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR, offset);
	data = inl(dev->base_addr + FT1000_REG_MAG_DPDATAL);
	spin_unlock_irqrestore(&info->dpram_lock, flags);

	return (data);
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_write_dpram_mag_32
// Description: This function will write to a specific area of dpram
//             (Magnemite ASIC only)
// Input:
//     dev    - device structure
//     offset - index of dpram
//     value  - value to write
// Output:
//     none.
//
//---------------------------------------------------------------------------
void ft1000_write_dpram_mag_32(struct net_device *dev, int offset, u32 value)
{
	struct ft1000_info *info = netdev_priv(dev);
	unsigned long flags;

	// Provide mutual exclusive access while reading ASIC registers.
	spin_lock_irqsave(&info->dpram_lock, flags);
	ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR, offset);
	outl(value, dev->base_addr + FT1000_REG_MAG_DPDATAL);
	spin_unlock_irqrestore(&info->dpram_lock, flags);
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_enable_interrupts
// Description: This function will enable interrupts base on the current interrupt mask.
// Input:
//     dev    - device structure
// Output:
//     None.
//
//---------------------------------------------------------------------------
static void ft1000_enable_interrupts(struct net_device *dev)
{
	u16 tempword;

	DEBUG(1, "ft1000_hw:ft1000_enable_interrupts()\n");
	ft1000_write_reg(dev, FT1000_REG_SUP_IMASK, ISR_DEFAULT_MASK);
	tempword = ft1000_read_reg(dev, FT1000_REG_SUP_IMASK);
	DEBUG(1,
		  "ft1000_hw:ft1000_enable_interrupts:current interrupt enable mask = 0x%x\n",
		  tempword);
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_disable_interrupts
// Description: This function will disable all interrupts.
// Input:
//     dev    - device structure
// Output:
//     None.
//
//---------------------------------------------------------------------------
static void ft1000_disable_interrupts(struct net_device *dev)
{
	u16 tempword;

	DEBUG(1, "ft1000_hw: ft1000_disable_interrupts()\n");
	ft1000_write_reg(dev, FT1000_REG_SUP_IMASK, ISR_MASK_ALL);
	tempword = ft1000_read_reg(dev, FT1000_REG_SUP_IMASK);
	DEBUG(1,
		  "ft1000_hw:ft1000_disable_interrupts:current interrupt enable mask = 0x%x\n",
		  tempword);
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_reset_asic
// Description: This function will call the Card Service function to reset the
//             ASIC.
// Input:
//     dev    - device structure
// Output:
//     none
//
//---------------------------------------------------------------------------
static void ft1000_reset_asic(struct net_device *dev)
{
	struct ft1000_info *info = netdev_priv(dev);
	u16 tempword;

	DEBUG(1, "ft1000_hw:ft1000_reset_asic called\n");

	(*info->ft1000_reset) (info->link);

	// Let's use the register provided by the Magnemite ASIC to reset the
	// ASIC and DSP.
	if (info->AsicID == MAGNEMITE_ID) {
		ft1000_write_reg(dev, FT1000_REG_RESET,
				 (DSP_RESET_BIT | ASIC_RESET_BIT));
	}
	mdelay(1);
	if (info->AsicID == ELECTRABUZZ_ID) {
		// set watermark to -1 in order to not generate an interrupt
		ft1000_write_reg(dev, FT1000_REG_WATERMARK, 0xffff);
	} else {
		// set watermark to -1 in order to not generate an interrupt
		ft1000_write_reg(dev, FT1000_REG_MAG_WATERMARK, 0xffff);
	}
	// clear interrupts
	tempword = ft1000_read_reg(dev, FT1000_REG_SUP_ISR);
	DEBUG(1, "ft1000_hw: interrupt status register = 0x%x\n", tempword);
	ft1000_write_reg(dev, FT1000_REG_SUP_ISR, tempword);
	tempword = ft1000_read_reg(dev, FT1000_REG_SUP_ISR);
	DEBUG(1, "ft1000_hw: interrupt status register = 0x%x\n", tempword);

}

//---------------------------------------------------------------------------
//
// Function:   ft1000_reset_card
// Description: This function will reset the card
// Input:
//     dev    - device structure
// Output:
//     status - false (card reset fail)
//              true  (card reset successful)
//
//---------------------------------------------------------------------------
static int ft1000_reset_card(struct net_device *dev)
{
	struct ft1000_info *info = netdev_priv(dev);
	u16 tempword;
	int i;
	unsigned long flags;
	struct prov_record *ptr;

	DEBUG(1, "ft1000_hw:ft1000_reset_card called.....\n");

	info->CardReady = 0;
	info->ProgConStat = 0;
	info->squeseqnum = 0;
	ft1000_disable_interrupts(dev);

//	del_timer(&poll_timer);

	// Make sure we free any memory reserve for provisioning
	while (list_empty(&info->prov_list) == 0) {
		DEBUG(0,
			  "ft1000_hw:ft1000_reset_card:deleting provisioning record\n");
		ptr = list_entry(info->prov_list.next, struct prov_record, list);
		list_del(&ptr->list);
		kfree(ptr->pprov_data);
		kfree(ptr);
	}

	if (info->AsicID == ELECTRABUZZ_ID) {
		DEBUG(1, "ft1000_hw:ft1000_reset_card:resetting DSP\n");
		ft1000_write_reg(dev, FT1000_REG_RESET, DSP_RESET_BIT);
	} else {
		DEBUG(1,
			  "ft1000_hw:ft1000_reset_card:resetting ASIC and DSP\n");
		ft1000_write_reg(dev, FT1000_REG_RESET,
				 (DSP_RESET_BIT | ASIC_RESET_BIT));
	}

	// Copy DSP session record into info block if this is not a coldstart
	if (ft1000_card_present == 1) {
		spin_lock_irqsave(&info->dpram_lock, flags);
		if (info->AsicID == ELECTRABUZZ_ID) {
			ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR,
					 FT1000_DPRAM_RX_BASE);
			for (i = 0; i < MAX_DSP_SESS_REC; i++) {
				info->DSPSess.Rec[i] =
					ft1000_read_reg(dev,
							FT1000_REG_DPRAM_DATA);
			}
		} else {
			ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR,
					 FT1000_DPRAM_MAG_RX_BASE);
			for (i = 0; i < MAX_DSP_SESS_REC / 2; i++) {
				info->DSPSess.MagRec[i] =
					inl(dev->base_addr + FT1000_REG_MAG_DPDATA);
			}
		}
		spin_unlock_irqrestore(&info->dpram_lock, flags);
	}

	DEBUG(1, "ft1000_hw:ft1000_reset_card:resetting ASIC\n");
	mdelay(10);
	//reset ASIC
	ft1000_reset_asic(dev);

	DEBUG(1, "ft1000_hw:ft1000_reset_card:downloading dsp image\n");

	if (info->AsicID == MAGNEMITE_ID) {
		// Put dsp in reset and take ASIC out of reset
		DEBUG(0,
			  "ft1000_hw:ft1000_reset_card:Put DSP in reset and take ASIC out of reset\n");
		ft1000_write_reg(dev, FT1000_REG_RESET, DSP_RESET_BIT);

		// Setting MAGNEMITE ASIC to big endian mode
		ft1000_write_reg(dev, FT1000_REG_SUP_CTRL, HOST_INTF_BE);
		// Download bootloader
		card_bootload(dev);

		// Take DSP out of reset
		ft1000_write_reg(dev, FT1000_REG_RESET, 0);
		// FLARION_DSP_ACTIVE;
		mdelay(10);
		DEBUG(0, "ft1000_hw:ft1000_reset_card:Take DSP out of reset\n");

		// Wait for 0xfefe indicating dsp ready before starting download
		for (i = 0; i < 50; i++) {
			tempword =
				ft1000_read_dpram_mag_16(dev, FT1000_MAG_DPRAM_FEFE,
							 FT1000_MAG_DPRAM_FEFE_INDX);
			if (tempword == 0xfefe) {
				break;
			}
			mdelay(20);
		}

		if (i == 50) {
			DEBUG(0,
				  "ft1000_hw:ft1000_reset_card:No FEFE detected from DSP\n");
			return false;
		}

	} else {
		// Take DSP out of reset
		ft1000_write_reg(dev, FT1000_REG_RESET, ~DSP_RESET_BIT);
		mdelay(10);
	}

	if (card_download(dev, fw_entry->data, fw_entry->size)) {
		DEBUG(1, "card download unsuccessful\n");
		return false;
	} else {
		DEBUG(1, "card download successful\n");
	}

	mdelay(10);

	if (info->AsicID == ELECTRABUZZ_ID) {
		// Need to initialize the FIFO length counter to zero in order to sync up
		// with the DSP
		info->fifo_cnt = 0;
		ft1000_write_dpram(dev, FT1000_FIFO_LEN, info->fifo_cnt);
		// Initialize DSP heartbeat area to ho
		ft1000_write_dpram(dev, FT1000_HI_HO, ho);
		tempword = ft1000_read_dpram(dev, FT1000_HI_HO);
		DEBUG(1, "ft1000_hw:ft1000_reset_asic:hi_ho value = 0x%x\n",
			  tempword);
	} else {
		// Initialize DSP heartbeat area to ho
		ft1000_write_dpram_mag_16(dev, FT1000_MAG_HI_HO, ho_mag,
					  FT1000_MAG_HI_HO_INDX);
		tempword =
			ft1000_read_dpram_mag_16(dev, FT1000_MAG_HI_HO,
						 FT1000_MAG_HI_HO_INDX);
		DEBUG(1, "ft1000_hw:ft1000_reset_card:hi_ho value = 0x%x\n",
			  tempword);
	}

	info->CardReady = 1;
	ft1000_enable_interrupts(dev);

	/* Schedule heartbeat process to run every 2 seconds */
//	poll_timer.expires = jiffies + (2*HZ);
//	poll_timer.data = (u_long)dev;
//	add_timer(&poll_timer);

	return true;

}

//---------------------------------------------------------------------------
//
// Function:   ft1000_chkcard
// Description: This function will check if the device is presently available on
//             the system.
// Input:
//     dev    - device structure
// Output:
//     status - false (device is not present)
//              true  (device is present)
//
//---------------------------------------------------------------------------
static int ft1000_chkcard(struct net_device *dev)
{
	u16 tempword;

	// Mask register is used to check for device presence since it is never
	// set to zero.
	tempword = ft1000_read_reg(dev, FT1000_REG_SUP_IMASK);
	if (tempword == 0) {
		DEBUG(1,
			  "ft1000_hw:ft1000_chkcard: IMASK = 0 Card not detected\n");
		return false;
	}
	// The system will return the value of 0xffff for the version register
	// if the device is not present.
	tempword = ft1000_read_reg(dev, FT1000_REG_ASIC_ID);
	if (tempword == 0xffff) {
		DEBUG(1,
			  "ft1000_hw:ft1000_chkcard: Version = 0xffff Card not detected\n");
		return false;
	}
	return true;
}


//---------------------------------------------------------------------------
//
// Function:   ft1000_hbchk
// Description: This function will perform the heart beat check of the DSP as
//             well as the ASIC.
// Input:
//     dev    - device structure
// Output:
//     none
//
//---------------------------------------------------------------------------
static void ft1000_hbchk(u_long data)
{
	struct net_device *dev = (struct net_device *)data;

	struct ft1000_info *info;
	u16 tempword;

	info = netdev_priv(dev);

	if (info->CardReady == 1) {
		// Perform dsp heartbeat check
		if (info->AsicID == ELECTRABUZZ_ID) {
			tempword = ft1000_read_dpram(dev, FT1000_HI_HO);
		} else {
			tempword =
				ntohs(ft1000_read_dpram_mag_16
				  (dev, FT1000_MAG_HI_HO,
				   FT1000_MAG_HI_HO_INDX));
		}
		DEBUG(1, "ft1000_hw:ft1000_hbchk:hi_ho value = 0x%x\n",
			  tempword);
		// Let's perform another check if ho is not detected
		if (tempword != ho) {
			if (info->AsicID == ELECTRABUZZ_ID) {
				tempword = ft1000_read_dpram(dev, FT1000_HI_HO);
			}
			else {
				tempword = ntohs(ft1000_read_dpram_mag_16(dev, FT1000_MAG_HI_HO, FT1000_MAG_HI_HO_INDX));
			}
		}
		if (tempword != ho) {
			printk(KERN_INFO
				   "ft1000: heartbeat failed - no ho detected\n");
			if (info->AsicID == ELECTRABUZZ_ID) {
				info->DSP_TIME[0] =
					ft1000_read_dpram(dev, FT1000_DSP_TIMER0);
				info->DSP_TIME[1] =
					ft1000_read_dpram(dev, FT1000_DSP_TIMER1);
				info->DSP_TIME[2] =
					ft1000_read_dpram(dev, FT1000_DSP_TIMER2);
				info->DSP_TIME[3] =
					ft1000_read_dpram(dev, FT1000_DSP_TIMER3);
			} else {
				info->DSP_TIME[0] =
					ft1000_read_dpram_mag_16(dev,
								 FT1000_MAG_DSP_TIMER0,
								 FT1000_MAG_DSP_TIMER0_INDX);
				info->DSP_TIME[1] =
					ft1000_read_dpram_mag_16(dev,
								 FT1000_MAG_DSP_TIMER1,
								 FT1000_MAG_DSP_TIMER1_INDX);
				info->DSP_TIME[2] =
					ft1000_read_dpram_mag_16(dev,
								 FT1000_MAG_DSP_TIMER2,
								 FT1000_MAG_DSP_TIMER2_INDX);
				info->DSP_TIME[3] =
					ft1000_read_dpram_mag_16(dev,
								 FT1000_MAG_DSP_TIMER3,
								 FT1000_MAG_DSP_TIMER3_INDX);
			}
			info->DrvErrNum = DSP_HB_INFO;
			if (ft1000_reset_card(dev) == 0) {
				printk(KERN_INFO
					   "ft1000: Hardware Failure Detected - PC Card disabled\n");
				info->ProgConStat = 0xff;
				return;
			}
			/* Schedule this module to run every 2 seconds */
			poll_timer.expires = jiffies + (2*HZ);
			poll_timer.data = (u_long)dev;
			add_timer(&poll_timer);
			return;
		}

		tempword = ft1000_read_reg(dev, FT1000_REG_DOORBELL);
		// Let's check doorbell again if fail
		if (tempword & FT1000_DB_HB) {
			tempword = ft1000_read_reg(dev, FT1000_REG_DOORBELL);
		}
		if (tempword & FT1000_DB_HB) {
			printk(KERN_INFO
				   "ft1000: heartbeat doorbell not clear by firmware\n");
			if (info->AsicID == ELECTRABUZZ_ID) {
				info->DSP_TIME[0] =
					ft1000_read_dpram(dev, FT1000_DSP_TIMER0);
				info->DSP_TIME[1] =
					ft1000_read_dpram(dev, FT1000_DSP_TIMER1);
				info->DSP_TIME[2] =
					ft1000_read_dpram(dev, FT1000_DSP_TIMER2);
				info->DSP_TIME[3] =
					ft1000_read_dpram(dev, FT1000_DSP_TIMER3);
			} else {
				info->DSP_TIME[0] =
					ft1000_read_dpram_mag_16(dev,
								 FT1000_MAG_DSP_TIMER0,
								 FT1000_MAG_DSP_TIMER0_INDX);
				info->DSP_TIME[1] =
					ft1000_read_dpram_mag_16(dev,
								 FT1000_MAG_DSP_TIMER1,
								 FT1000_MAG_DSP_TIMER1_INDX);
				info->DSP_TIME[2] =
					ft1000_read_dpram_mag_16(dev,
								 FT1000_MAG_DSP_TIMER2,
								 FT1000_MAG_DSP_TIMER2_INDX);
				info->DSP_TIME[3] =
					ft1000_read_dpram_mag_16(dev,
								 FT1000_MAG_DSP_TIMER3,
								 FT1000_MAG_DSP_TIMER3_INDX);
			}
			info->DrvErrNum = DSP_HB_INFO;
			if (ft1000_reset_card(dev) == 0) {
				printk(KERN_INFO
					   "ft1000: Hardware Failure Detected - PC Card disabled\n");
				info->ProgConStat = 0xff;
				return;
			}
			/* Schedule this module to run every 2 seconds */
			poll_timer.expires = jiffies + (2*HZ);
			poll_timer.data = (u_long)dev;
			add_timer(&poll_timer);
			return;
		}
		// Set dedicated area to hi and ring appropriate doorbell according
		// to hi/ho heartbeat protocol
		if (info->AsicID == ELECTRABUZZ_ID) {
			ft1000_write_dpram(dev, FT1000_HI_HO, hi);
		} else {
			ft1000_write_dpram_mag_16(dev, FT1000_MAG_HI_HO, hi_mag,
						  FT1000_MAG_HI_HO_INDX);
		}

		if (info->AsicID == ELECTRABUZZ_ID) {
			tempword = ft1000_read_dpram(dev, FT1000_HI_HO);
		} else {
			tempword =
				ntohs(ft1000_read_dpram_mag_16
				  (dev, FT1000_MAG_HI_HO,
				   FT1000_MAG_HI_HO_INDX));
		}
        // Let's write hi again if fail
		if (tempword != hi) {
			if (info->AsicID == ELECTRABUZZ_ID) {
				ft1000_write_dpram(dev, FT1000_HI_HO, hi);
			}
			else {
				ft1000_write_dpram_mag_16(dev, FT1000_MAG_HI_HO, hi_mag, FT1000_MAG_HI_HO_INDX);
			}

			if (info->AsicID == ELECTRABUZZ_ID) {
				tempword = ft1000_read_dpram(dev, FT1000_HI_HO);
			}
			else {
				tempword = ntohs(ft1000_read_dpram_mag_16(dev, FT1000_MAG_HI_HO, FT1000_MAG_HI_HO_INDX));
			}

		}

		if (tempword != hi) {
			printk(KERN_INFO
				   "ft1000: heartbeat failed - cannot write hi into DPRAM\n");
			if (info->AsicID == ELECTRABUZZ_ID) {
				info->DSP_TIME[0] =
					ft1000_read_dpram(dev, FT1000_DSP_TIMER0);
				info->DSP_TIME[1] =
					ft1000_read_dpram(dev, FT1000_DSP_TIMER1);
				info->DSP_TIME[2] =
					ft1000_read_dpram(dev, FT1000_DSP_TIMER2);
				info->DSP_TIME[3] =
					ft1000_read_dpram(dev, FT1000_DSP_TIMER3);
			} else {
				info->DSP_TIME[0] =
					ft1000_read_dpram_mag_16(dev,
								 FT1000_MAG_DSP_TIMER0,
								 FT1000_MAG_DSP_TIMER0_INDX);
				info->DSP_TIME[1] =
					ft1000_read_dpram_mag_16(dev,
								 FT1000_MAG_DSP_TIMER1,
								 FT1000_MAG_DSP_TIMER1_INDX);
				info->DSP_TIME[2] =
					ft1000_read_dpram_mag_16(dev,
								 FT1000_MAG_DSP_TIMER2,
								 FT1000_MAG_DSP_TIMER2_INDX);
				info->DSP_TIME[3] =
					ft1000_read_dpram_mag_16(dev,
								 FT1000_MAG_DSP_TIMER3,
								 FT1000_MAG_DSP_TIMER3_INDX);
			}
			info->DrvErrNum = DSP_HB_INFO;
			if (ft1000_reset_card(dev) == 0) {
				printk(KERN_INFO
					   "ft1000: Hardware Failure Detected - PC Card disabled\n");
				info->ProgConStat = 0xff;
				return;
			}
			/* Schedule this module to run every 2 seconds */
			poll_timer.expires = jiffies + (2*HZ);
			poll_timer.data = (u_long)dev;
			add_timer(&poll_timer);
			return;
		}
		ft1000_write_reg(dev, FT1000_REG_DOORBELL, FT1000_DB_HB);

	}

	/* Schedule this module to run every 2 seconds */
	poll_timer.expires = jiffies + (2 * HZ);
	poll_timer.data = (u_long) dev;
	add_timer(&poll_timer);
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_send_cmd
// Description:
// Input:
// Output:
//
//---------------------------------------------------------------------------
static void ft1000_send_cmd (struct net_device *dev, u16 *ptempbuffer, int size, u16 qtype)
{
	struct ft1000_info *info = netdev_priv(dev);
	int i;
	u16 tempword;
	unsigned long flags;

	size += sizeof(struct pseudo_hdr);
	// check for odd byte and increment to 16-bit word align value
	if ((size & 0x0001)) {
		size++;
	}
	DEBUG(1, "FT1000:ft1000_send_cmd:total length = %d\n", size);
	DEBUG(1, "FT1000:ft1000_send_cmd:length = %d\n", ntohs(*ptempbuffer));
	// put message into slow queue area
	// All messages are in the form total_len + pseudo header + message body
	spin_lock_irqsave(&info->dpram_lock, flags);

    // Make sure SLOWQ doorbell is clear
    tempword = ft1000_read_reg(dev, FT1000_REG_DOORBELL);
    i=0;
    while (tempword & FT1000_DB_DPRAM_TX) {
        mdelay(10);
        i++;
        if (i==10) {
            spin_unlock_irqrestore(&info->dpram_lock, flags);
            return;
        }
        tempword = ft1000_read_reg(dev, FT1000_REG_DOORBELL);
    }

	if (info->AsicID == ELECTRABUZZ_ID) {
		ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR,
				 FT1000_DPRAM_TX_BASE);
		// Write total length to dpram
		ft1000_write_reg(dev, FT1000_REG_DPRAM_DATA, size);
		// Write pseudo header and messgae body
		for (i = 0; i < (size >> 1); i++) {
			DEBUG(1, "FT1000:ft1000_send_cmd:data %d = 0x%x\n", i,
				  *ptempbuffer);
			tempword = htons(*ptempbuffer++);
			ft1000_write_reg(dev, FT1000_REG_DPRAM_DATA, tempword);
		}
	} else {
		ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR,
				 FT1000_DPRAM_MAG_TX_BASE);
		// Write total length to dpram
		ft1000_write_reg(dev, FT1000_REG_MAG_DPDATAH, htons(size));
		// Write pseudo header and messgae body
		ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR,
				 FT1000_DPRAM_MAG_TX_BASE + 1);
		for (i = 0; i < (size >> 2); i++) {
			DEBUG(1, "FT1000:ft1000_send_cmd:data = 0x%x\n",
				  *ptempbuffer);
			outw(*ptempbuffer++,
				 dev->base_addr + FT1000_REG_MAG_DPDATAL);
			DEBUG(1, "FT1000:ft1000_send_cmd:data = 0x%x\n",
				  *ptempbuffer);
			outw(*ptempbuffer++,
				 dev->base_addr + FT1000_REG_MAG_DPDATAH);
		}
		DEBUG(1, "FT1000:ft1000_send_cmd:data = 0x%x\n", *ptempbuffer);
		outw(*ptempbuffer++, dev->base_addr + FT1000_REG_MAG_DPDATAL);
		DEBUG(1, "FT1000:ft1000_send_cmd:data = 0x%x\n", *ptempbuffer);
		outw(*ptempbuffer++, dev->base_addr + FT1000_REG_MAG_DPDATAH);
	}
	spin_unlock_irqrestore(&info->dpram_lock, flags);

	// ring doorbell to notify DSP that we have a message ready
	ft1000_write_reg(dev, FT1000_REG_DOORBELL, FT1000_DB_DPRAM_TX);
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_receive_cmd
// Description: This function will read a message from the dpram area.
// Input:
//    dev - network device structure
//    pbuffer - caller supply address to buffer
//    pnxtph - pointer to next pseudo header
// Output:
//   Status = 0 (unsuccessful)
//          = 1 (successful)
//
//---------------------------------------------------------------------------
static bool ft1000_receive_cmd(struct net_device *dev, u16 *pbuffer,
				int maxsz, u16 *pnxtph)
{
	struct ft1000_info *info = netdev_priv(dev);
	u16 size;
	u16 *ppseudohdr;
	int i;
	u16 tempword;
	unsigned long flags;

	if (info->AsicID == ELECTRABUZZ_ID) {
		size = ( ft1000_read_dpram(dev, *pnxtph) ) + sizeof(struct pseudo_hdr);
	} else {
		size =
			ntohs(ft1000_read_dpram_mag_16
			  (dev, FT1000_MAG_PH_LEN,
			   FT1000_MAG_PH_LEN_INDX)) + sizeof(struct pseudo_hdr);
	}
	if (size > maxsz) {
		DEBUG(1,
			  "FT1000:ft1000_receive_cmd:Invalid command length = %d\n",
			  size);
		return false;
	} else {
		ppseudohdr = (u16 *) pbuffer;
		spin_lock_irqsave(&info->dpram_lock, flags);
		if (info->AsicID == ELECTRABUZZ_ID) {
			ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR,
					 FT1000_DPRAM_RX_BASE + 2);
			for (i = 0; i <= (size >> 1); i++) {
				tempword =
					ft1000_read_reg(dev, FT1000_REG_DPRAM_DATA);
				*pbuffer++ = ntohs(tempword);
			}
		} else {
			ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR,
					 FT1000_DPRAM_MAG_RX_BASE);
			*pbuffer = inw(dev->base_addr + FT1000_REG_MAG_DPDATAH);
			DEBUG(1, "ft1000_hw:received data = 0x%x\n", *pbuffer);
			pbuffer++;
			ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR,
					 FT1000_DPRAM_MAG_RX_BASE + 1);
			for (i = 0; i <= (size >> 2); i++) {
				*pbuffer =
					inw(dev->base_addr +
					FT1000_REG_MAG_DPDATAL);
				pbuffer++;
				*pbuffer =
					inw(dev->base_addr +
					FT1000_REG_MAG_DPDATAH);
				pbuffer++;
			}
			//copy odd aligned word
			*pbuffer = inw(dev->base_addr + FT1000_REG_MAG_DPDATAL);
			DEBUG(1, "ft1000_hw:received data = 0x%x\n", *pbuffer);
			pbuffer++;
			*pbuffer = inw(dev->base_addr + FT1000_REG_MAG_DPDATAH);
			DEBUG(1, "ft1000_hw:received data = 0x%x\n", *pbuffer);
			pbuffer++;
		}
		if (size & 0x0001) {
			//copy odd byte from fifo
			tempword = ft1000_read_reg(dev, FT1000_REG_DPRAM_DATA);
			*pbuffer = ntohs(tempword);
		}
		spin_unlock_irqrestore(&info->dpram_lock, flags);

		// Check if pseudo header checksum is good
		// Calculate pseudo header checksum
		tempword = *ppseudohdr++;
		for (i = 1; i < 7; i++) {
			tempword ^= *ppseudohdr++;
		}
		if ((tempword != *ppseudohdr)) {
			DEBUG(1,
				  "FT1000:ft1000_receive_cmd:Pseudo header checksum mismatch\n");
			// Drop this message
			return false;
		}
		return true;
	}
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_proc_drvmsg
// Description: This function will process the various driver messages.
// Input:
//     dev    - device structure
//     pnxtph - pointer to next pseudo header
// Output:
//     none
//
//---------------------------------------------------------------------------
static void ft1000_proc_drvmsg(struct net_device *dev)
{
	struct ft1000_info *info = netdev_priv(dev);
	u16 msgtype;
	u16 tempword;
	struct media_msg *pmediamsg;
	struct dsp_init_msg *pdspinitmsg;
	struct drv_msg *pdrvmsg;
	u16 len;
	u16 i;
	struct prov_record *ptr;
	struct pseudo_hdr *ppseudo_hdr;
	u16 *pmsg;
	struct timeval tv;
	union {
		u8 byte[2];
		u16 wrd;
	} convert;

    if (info->AsicID == ELECTRABUZZ_ID) {
        tempword = FT1000_DPRAM_RX_BASE+2;
    }
    else {
        tempword = FT1000_DPRAM_MAG_RX_BASE;
    }
    if ( ft1000_receive_cmd(dev, &cmdbuffer[0], MAX_CMD_SQSIZE, &tempword) ) {

		// Get the message type which is total_len + PSEUDO header + msgtype + message body
		pdrvmsg = (struct drv_msg *) & cmdbuffer[0];
		msgtype = ntohs(pdrvmsg->type);
		DEBUG(1, "Command message type = 0x%x\n", msgtype);
		switch (msgtype) {
		case DSP_PROVISION:
			DEBUG(0,
				  "Got a provisioning request message from DSP\n");
			mdelay(25);
			while (list_empty(&info->prov_list) == 0) {
				DEBUG(0, "Sending a provisioning message\n");
				// Make sure SLOWQ doorbell is clear
				tempword =
					ft1000_read_reg(dev, FT1000_REG_DOORBELL);
				i = 0;
				while (tempword & FT1000_DB_DPRAM_TX) {
					mdelay(5);
					i++;
					if (i == 10) {
						break;
					}
				}
				ptr =
					list_entry(info->prov_list.next,
						   struct prov_record, list);
				len = *(u16 *) ptr->pprov_data;
				len = htons(len);

				pmsg = (u16 *) ptr->pprov_data;
				ppseudo_hdr = (struct pseudo_hdr *) pmsg;
				// Insert slow queue sequence number
				ppseudo_hdr->seq_num = info->squeseqnum++;
				ppseudo_hdr->portsrc = 0;
				// Calculate new checksum
				ppseudo_hdr->checksum = *pmsg++;
				DEBUG(1, "checksum = 0x%x\n",
					  ppseudo_hdr->checksum);
				for (i = 1; i < 7; i++) {
					ppseudo_hdr->checksum ^= *pmsg++;
					DEBUG(1, "checksum = 0x%x\n",
						  ppseudo_hdr->checksum);
				}

				ft1000_send_cmd (dev, (u16 *)ptr->pprov_data, len, SLOWQ_TYPE);
				list_del(&ptr->list);
				kfree(ptr->pprov_data);
				kfree(ptr);
			}
			// Indicate adapter is ready to take application messages after all
			// provisioning messages are sent
			info->CardReady = 1;
			break;
		case MEDIA_STATE:
			pmediamsg = (struct media_msg *) & cmdbuffer[0];
			if (info->ProgConStat != 0xFF) {
			if (pmediamsg->state) {
				DEBUG(1, "Media is up\n");
				if (info->mediastate == 0) {
					netif_carrier_on(dev);
					netif_wake_queue(dev);
					info->mediastate = 1;
					do_gettimeofday(&tv);
					info->ConTm = tv.tv_sec;
				}
			} else {
				DEBUG(1, "Media is down\n");
				if (info->mediastate == 1) {
					info->mediastate = 0;
					netif_carrier_off(dev);
					netif_stop_queue(dev);
					info->ConTm = 0;
				}
			}
            }
            else {
                DEBUG(1,"Media is down\n");
                if (info->mediastate == 1) {
                    info->mediastate = 0;
                    netif_carrier_off(dev);
                    netif_stop_queue(dev);
                    info->ConTm = 0;
                }
            }
			break;
		case DSP_INIT_MSG:
			pdspinitmsg = (struct dsp_init_msg *) & cmdbuffer[0];
			memcpy(info->DspVer, pdspinitmsg->DspVer, DSPVERSZ);
			DEBUG(1, "DSPVER = 0x%2x 0x%2x 0x%2x 0x%2x\n",
				  info->DspVer[0], info->DspVer[1], info->DspVer[2],
				   info->DspVer[3]);
			memcpy(info->HwSerNum, pdspinitmsg->HwSerNum,
				   HWSERNUMSZ);
			memcpy(info->Sku, pdspinitmsg->Sku, SKUSZ);
			memcpy(info->eui64, pdspinitmsg->eui64, EUISZ);
			dev->dev_addr[0] = info->eui64[0];
			dev->dev_addr[1] = info->eui64[1];
			dev->dev_addr[2] = info->eui64[2];
			dev->dev_addr[3] = info->eui64[5];
			dev->dev_addr[4] = info->eui64[6];
			dev->dev_addr[5] = info->eui64[7];

			if (ntohs(pdspinitmsg->length) ==
				(sizeof(struct dsp_init_msg) - 20)) {
				memcpy(info->ProductMode,
					   pdspinitmsg->ProductMode, MODESZ);
				memcpy(info->RfCalVer, pdspinitmsg->RfCalVer,
					   CALVERSZ);
				memcpy(info->RfCalDate, pdspinitmsg->RfCalDate,
					   CALDATESZ);
				DEBUG(1, "RFCalVer = 0x%2x 0x%2x\n",
					  info->RfCalVer[0], info->RfCalVer[1]);
			}

			break ;
		case DSP_STORE_INFO:
			DEBUG(1, "FT1000:drivermsg:Got DSP_STORE_INFO\n");
			tempword = ntohs(pdrvmsg->length);
			info->DSPInfoBlklen = tempword;
			if (tempword < (MAX_DSP_SESS_REC - 4)) {
				pmsg = (u16 *) & pdrvmsg->data[0];
				for (i = 0; i < ((tempword + 1) / 2); i++) {
					DEBUG(1,
						  "FT1000:drivermsg:dsp info data = 0x%x\n",
						  *pmsg);
					info->DSPInfoBlk[i + 10] = *pmsg++;
				}
			}
			break;
		case DSP_GET_INFO:
			DEBUG(1, "FT1000:drivermsg:Got DSP_GET_INFO\n");
			// copy dsp info block to dsp
			// allow any outstanding ioctl to finish
			mdelay(10);
			tempword = ft1000_read_reg(dev, FT1000_REG_DOORBELL);
			if (tempword & FT1000_DB_DPRAM_TX) {
				mdelay(10);
				tempword =
					ft1000_read_reg(dev, FT1000_REG_DOORBELL);
				if (tempword & FT1000_DB_DPRAM_TX) {
					mdelay(10);
				}
			}

			if ((tempword & FT1000_DB_DPRAM_TX) == 0) {
				// Put message into Slow Queue
				// Form Pseudo header
				pmsg = (u16 *) info->DSPInfoBlk;
				ppseudo_hdr = (struct pseudo_hdr *) pmsg;
				ppseudo_hdr->length =
					htons(info->DSPInfoBlklen + 4);
				ppseudo_hdr->source = 0x10;
				ppseudo_hdr->destination = 0x20;
				ppseudo_hdr->portdest = 0;
				ppseudo_hdr->portsrc = 0;
				ppseudo_hdr->sh_str_id = 0;
				ppseudo_hdr->control = 0;
				ppseudo_hdr->rsvd1 = 0;
				ppseudo_hdr->rsvd2 = 0;
				ppseudo_hdr->qos_class = 0;
				// Insert slow queue sequence number
				ppseudo_hdr->seq_num = info->squeseqnum++;
				// Insert application id
				ppseudo_hdr->portsrc = 0;
				// Calculate new checksum
				ppseudo_hdr->checksum = *pmsg++;
				for (i = 1; i < 7; i++) {
					ppseudo_hdr->checksum ^= *pmsg++;
				}
				info->DSPInfoBlk[8] = 0x7200;
				info->DSPInfoBlk[9] =
					htons(info->DSPInfoBlklen);
				ft1000_send_cmd (dev, (u16 *)info->DSPInfoBlk, (u16)(info->DSPInfoBlklen+4), 0);
			}

			break;
		case GET_DRV_ERR_RPT_MSG:
			DEBUG(1, "FT1000:drivermsg:Got GET_DRV_ERR_RPT_MSG\n");
			// copy driver error message to dsp
			// allow any outstanding ioctl to finish
			mdelay(10);
			tempword = ft1000_read_reg(dev, FT1000_REG_DOORBELL);
			if (tempword & FT1000_DB_DPRAM_TX) {
				mdelay(10);
				tempword =
					ft1000_read_reg(dev, FT1000_REG_DOORBELL);
				if (tempword & FT1000_DB_DPRAM_TX) {
					mdelay(10);
				}
			}

			if ((tempword & FT1000_DB_DPRAM_TX) == 0) {
				// Put message into Slow Queue
				// Form Pseudo header
				pmsg = (u16 *) & tempbuffer[0];
				ppseudo_hdr = (struct pseudo_hdr *) pmsg;
				ppseudo_hdr->length = htons(0x0012);
				ppseudo_hdr->source = 0x10;
				ppseudo_hdr->destination = 0x20;
				ppseudo_hdr->portdest = 0;
				ppseudo_hdr->portsrc = 0;
				ppseudo_hdr->sh_str_id = 0;
				ppseudo_hdr->control = 0;
				ppseudo_hdr->rsvd1 = 0;
				ppseudo_hdr->rsvd2 = 0;
				ppseudo_hdr->qos_class = 0;
				// Insert slow queue sequence number
				ppseudo_hdr->seq_num = info->squeseqnum++;
				// Insert application id
				ppseudo_hdr->portsrc = 0;
				// Calculate new checksum
                ppseudo_hdr->checksum = *pmsg++;
                for (i=1; i<7; i++) {
                    ppseudo_hdr->checksum ^= *pmsg++;
                }
				pmsg = (u16 *) & tempbuffer[16];
				*pmsg++ = htons(RSP_DRV_ERR_RPT_MSG);
				*pmsg++ = htons(0x000e);
				*pmsg++ = htons(info->DSP_TIME[0]);
				*pmsg++ = htons(info->DSP_TIME[1]);
				*pmsg++ = htons(info->DSP_TIME[2]);
				*pmsg++ = htons(info->DSP_TIME[3]);
				convert.byte[0] = info->DspVer[0];
				convert.byte[1] = info->DspVer[1];
				*pmsg++ = convert.wrd;
				convert.byte[0] = info->DspVer[2];
				convert.byte[1] = info->DspVer[3];
				*pmsg++ = convert.wrd;
				*pmsg++ = htons(info->DrvErrNum);

				ft1000_send_cmd (dev, (u16 *)&tempbuffer[0], (u16)(0x0012), 0);
				info->DrvErrNum = 0;
			}

			break;
		default:
			break;
		}
	}
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_parse_dpram_msg
// Description: This function will parse the message received from the DSP
//             via the DPRAM interface.
// Input:
//     dev    - device structure
// Output:
//     status - FAILURE
//              SUCCESS
//
//---------------------------------------------------------------------------
static int ft1000_parse_dpram_msg(struct net_device *dev)
{
	struct ft1000_info *info = netdev_priv(dev);
	u16 doorbell;
	u16 portid;
	u16 nxtph;
	u16 total_len;
	int i = 0;
	int cnt;
	unsigned long flags;

	doorbell = ft1000_read_reg(dev, FT1000_REG_DOORBELL);
	DEBUG(1, "Doorbell = 0x%x\n", doorbell);

	if (doorbell & FT1000_ASIC_RESET_REQ) {
		// Copy DSP session record from info block
		spin_lock_irqsave(&info->dpram_lock, flags);
		if (info->AsicID == ELECTRABUZZ_ID) {
			ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR,
					 FT1000_DPRAM_RX_BASE);
			for (i = 0; i < MAX_DSP_SESS_REC; i++) {
				ft1000_write_reg(dev, FT1000_REG_DPRAM_DATA,
						 info->DSPSess.Rec[i]);
			}
		} else {
			ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR,
					 FT1000_DPRAM_MAG_RX_BASE);
			for (i = 0; i < MAX_DSP_SESS_REC / 2; i++) {
				outl(info->DSPSess.MagRec[i],
					 dev->base_addr + FT1000_REG_MAG_DPDATA);
			}
		}
		spin_unlock_irqrestore(&info->dpram_lock, flags);

		// clear ASIC RESET request
		ft1000_write_reg(dev, FT1000_REG_DOORBELL,
				 FT1000_ASIC_RESET_REQ);
		DEBUG(1, "Got an ASIC RESET Request\n");
		ft1000_write_reg(dev, FT1000_REG_DOORBELL,
				 FT1000_ASIC_RESET_DSP);

		if (info->AsicID == MAGNEMITE_ID) {
			// Setting MAGNEMITE ASIC to big endian mode
			ft1000_write_reg(dev, FT1000_REG_SUP_CTRL,
					 HOST_INTF_BE);
		}
	}

	if (doorbell & FT1000_DSP_ASIC_RESET) {
		DEBUG(0,
			  "FT1000:ft1000_parse_dpram_msg: Got a dsp ASIC reset message\n");
		ft1000_write_reg(dev, FT1000_REG_DOORBELL,
				 FT1000_DSP_ASIC_RESET);
		udelay(200);
		return SUCCESS;
	}

	if (doorbell & FT1000_DB_DPRAM_RX) {
		DEBUG(1,
			  "FT1000:ft1000_parse_dpram_msg: Got a slow queue message\n");
		nxtph = FT1000_DPRAM_RX_BASE + 2;
		if (info->AsicID == ELECTRABUZZ_ID) {
			total_len =
				ft1000_read_dpram(dev, FT1000_DPRAM_RX_BASE);
		} else {
			total_len =
				ntohs(ft1000_read_dpram_mag_16
				  (dev, FT1000_MAG_TOTAL_LEN,
				   FT1000_MAG_TOTAL_LEN_INDX));
		}
		DEBUG(1, "FT1000:ft1000_parse_dpram_msg:total length = %d\n",
			  total_len);
		if ((total_len < MAX_CMD_SQSIZE) && (total_len > sizeof(struct pseudo_hdr))) {
            total_len += nxtph;
            cnt = 0;
            // ft1000_read_reg will return a value that needs to be byteswap
            // in order to get DSP_QID_OFFSET.
			if (info->AsicID == ELECTRABUZZ_ID) {
				portid =
					(ft1000_read_dpram
					 (dev,
					  DSP_QID_OFFSET + FT1000_DPRAM_RX_BASE +
					  2) >> 8) & 0xff;
			} else {
				portid =
					(ft1000_read_dpram_mag_16
					 (dev, FT1000_MAG_PORT_ID,
					  FT1000_MAG_PORT_ID_INDX) & 0xff);
			}
			DEBUG(1, "DSP_QID = 0x%x\n", portid);

			if (portid == DRIVERID) {
				// We are assumming one driver message from the DSP at a time.
				ft1000_proc_drvmsg(dev);
			}
		}
		ft1000_write_reg(dev, FT1000_REG_DOORBELL, FT1000_DB_DPRAM_RX);
	}

	if (doorbell & FT1000_DB_COND_RESET) {
		// Reset ASIC and DSP
		if (info->AsicID == ELECTRABUZZ_ID) {
			info->DSP_TIME[0] =
				ft1000_read_dpram(dev, FT1000_DSP_TIMER0);
			info->DSP_TIME[1] =
				ft1000_read_dpram(dev, FT1000_DSP_TIMER1);
			info->DSP_TIME[2] =
				ft1000_read_dpram(dev, FT1000_DSP_TIMER2);
			info->DSP_TIME[3] =
				ft1000_read_dpram(dev, FT1000_DSP_TIMER3);
		} else {
			info->DSP_TIME[0] =
				ft1000_read_dpram_mag_16(dev, FT1000_MAG_DSP_TIMER0,
							 FT1000_MAG_DSP_TIMER0_INDX);
			info->DSP_TIME[1] =
				ft1000_read_dpram_mag_16(dev, FT1000_MAG_DSP_TIMER1,
							 FT1000_MAG_DSP_TIMER1_INDX);
			info->DSP_TIME[2] =
				ft1000_read_dpram_mag_16(dev, FT1000_MAG_DSP_TIMER2,
							 FT1000_MAG_DSP_TIMER2_INDX);
			info->DSP_TIME[3] =
				ft1000_read_dpram_mag_16(dev, FT1000_MAG_DSP_TIMER3,
							 FT1000_MAG_DSP_TIMER3_INDX);
		}
		info->DrvErrNum = DSP_CONDRESET_INFO;
		DEBUG(1, "ft1000_hw:DSP conditional reset requested\n");
		ft1000_reset_card(dev);
		ft1000_write_reg(dev, FT1000_REG_DOORBELL,
				 FT1000_DB_COND_RESET);
	}
	// let's clear any unexpected doorbells from DSP
	doorbell =
		doorbell & ~(FT1000_DB_DPRAM_RX | FT1000_ASIC_RESET_REQ |
			 FT1000_DB_COND_RESET | 0xff00);
	if (doorbell) {
		DEBUG(1, "Clearing unexpected doorbell = 0x%x\n", doorbell);
		ft1000_write_reg(dev, FT1000_REG_DOORBELL, doorbell);
	}

	return SUCCESS;

}

//---------------------------------------------------------------------------
//
// Function:   ft1000_flush_fifo
// Description: This function will flush one packet from the downlink
//             FIFO.
// Input:
//     dev      - device structure
//     drv_err  - driver error causing the flush fifo
// Output:
//     None.
//
//---------------------------------------------------------------------------
static void ft1000_flush_fifo(struct net_device *dev, u16 DrvErrNum)
{
	struct ft1000_info *info = netdev_priv(dev);
	u16 i;
	u32 templong;
	u16 tempword;

	DEBUG(1, "ft1000:ft1000_hw:ft1000_flush_fifo called\n");
	if (info->PktIntfErr > MAX_PH_ERR) {
		if (info->AsicID == ELECTRABUZZ_ID) {
			info->DSP_TIME[0] =
				ft1000_read_dpram(dev, FT1000_DSP_TIMER0);
			info->DSP_TIME[1] =
				ft1000_read_dpram(dev, FT1000_DSP_TIMER1);
			info->DSP_TIME[2] =
				ft1000_read_dpram(dev, FT1000_DSP_TIMER2);
			info->DSP_TIME[3] =
				ft1000_read_dpram(dev, FT1000_DSP_TIMER3);
		} else {
			info->DSP_TIME[0] =
				ft1000_read_dpram_mag_16(dev, FT1000_MAG_DSP_TIMER0,
							 FT1000_MAG_DSP_TIMER0_INDX);
			info->DSP_TIME[1] =
				ft1000_read_dpram_mag_16(dev, FT1000_MAG_DSP_TIMER1,
							 FT1000_MAG_DSP_TIMER1_INDX);
			info->DSP_TIME[2] =
				ft1000_read_dpram_mag_16(dev, FT1000_MAG_DSP_TIMER2,
							 FT1000_MAG_DSP_TIMER2_INDX);
			info->DSP_TIME[3] =
				ft1000_read_dpram_mag_16(dev, FT1000_MAG_DSP_TIMER3,
							 FT1000_MAG_DSP_TIMER3_INDX);
		}
		info->DrvErrNum = DrvErrNum;
		ft1000_reset_card(dev);
		return;
	} else {
		// Flush corrupted pkt from FIFO
		i = 0;
		do {
			if (info->AsicID == ELECTRABUZZ_ID) {
				tempword =
					ft1000_read_reg(dev, FT1000_REG_DFIFO);
				tempword =
					ft1000_read_reg(dev, FT1000_REG_DFIFO_STAT);
			} else {
				templong =
					inl(dev->base_addr + FT1000_REG_MAG_DFR);
				tempword =
					inw(dev->base_addr + FT1000_REG_MAG_DFSR);
			}
			i++;
			// This should never happen unless the ASIC is broken.
			// We must reset to recover.
			if ((i > 2048) || (tempword == 0)) {
				if (info->AsicID == ELECTRABUZZ_ID) {
					info->DSP_TIME[0] =
						ft1000_read_dpram(dev,
								  FT1000_DSP_TIMER0);
					info->DSP_TIME[1] =
						ft1000_read_dpram(dev,
								  FT1000_DSP_TIMER1);
					info->DSP_TIME[2] =
						ft1000_read_dpram(dev,
								  FT1000_DSP_TIMER2);
					info->DSP_TIME[3] =
						ft1000_read_dpram(dev,
								  FT1000_DSP_TIMER3);
				} else {
					info->DSP_TIME[0] =
						ft1000_read_dpram_mag_16(dev,
									 FT1000_MAG_DSP_TIMER0,
									 FT1000_MAG_DSP_TIMER0_INDX);
					info->DSP_TIME[1] =
						ft1000_read_dpram_mag_16(dev,
									 FT1000_MAG_DSP_TIMER1,
									 FT1000_MAG_DSP_TIMER1_INDX);
					info->DSP_TIME[2] =
						ft1000_read_dpram_mag_16(dev,
									 FT1000_MAG_DSP_TIMER2,
									 FT1000_MAG_DSP_TIMER2_INDX);
					info->DSP_TIME[3] =
						ft1000_read_dpram_mag_16(dev,
									 FT1000_MAG_DSP_TIMER3,
									 FT1000_MAG_DSP_TIMER3_INDX);
				}
				if (tempword == 0) {
					// Let's check if ASIC reads are still ok by reading the Mask register
					// which is never zero at this point of the code.
					tempword =
						inw(dev->base_addr +
						FT1000_REG_SUP_IMASK);
					if (tempword == 0) {
						// This indicates that we can not communicate with the ASIC
						info->DrvErrNum =
							FIFO_FLUSH_BADCNT;
					} else {
						// Let's assume that we really flush the FIFO
						info->PktIntfErr++;
						return;
					}
				} else {
					info->DrvErrNum = FIFO_FLUSH_MAXLIMIT;
				}
				return;
			}
			tempword = inw(dev->base_addr + FT1000_REG_SUP_STAT);
		} while ((tempword & 0x03) != 0x03);
		if (info->AsicID == ELECTRABUZZ_ID) {
			i++;
			DEBUG(0, "Flushing FIFO complete = %x\n", tempword);
			// Flush last word in FIFO.
			tempword = ft1000_read_reg(dev, FT1000_REG_DFIFO);
			// Update FIFO counter for DSP
			i = i * 2;
			DEBUG(0, "Flush Data byte count to dsp = %d\n", i);
			info->fifo_cnt += i;
			ft1000_write_dpram(dev, FT1000_FIFO_LEN,
					   info->fifo_cnt);
		} else {
			DEBUG(0, "Flushing FIFO complete = %x\n", tempword);
			// Flush last word in FIFO
			templong = inl(dev->base_addr + FT1000_REG_MAG_DFR);
			tempword = inw(dev->base_addr + FT1000_REG_SUP_STAT);
			DEBUG(0, "FT1000_REG_SUP_STAT = 0x%x\n", tempword);
			tempword = inw(dev->base_addr + FT1000_REG_MAG_DFSR);
			DEBUG(0, "FT1000_REG_MAG_DFSR = 0x%x\n", tempword);
		}
		if (DrvErrNum) {
			info->PktIntfErr++;
		}
	}
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_copy_up_pkt
// Description: This function will pull Flarion packets out of the Downlink
//             FIFO and convert it to an ethernet packet.  The ethernet packet will
//             then be deliver to the TCP/IP stack.
// Input:
//     dev    - device structure
// Output:
//     status - FAILURE
//              SUCCESS
//
//---------------------------------------------------------------------------
static int ft1000_copy_up_pkt(struct net_device *dev)
{
	u16 tempword;
	struct ft1000_info *info = netdev_priv(dev);
	u16 len;
	struct sk_buff *skb;
	u16 i;
	u8 *pbuffer = NULL;
	u8 *ptemp = NULL;
	u16 chksum;
	u32 *ptemplong;
	u32 templong;

	DEBUG(1, "ft1000_copy_up_pkt\n");
	// Read length
	if (info->AsicID == ELECTRABUZZ_ID) {
		tempword = ft1000_read_reg(dev, FT1000_REG_DFIFO);
		len = tempword;
	} else {
		tempword = ft1000_read_reg(dev, FT1000_REG_MAG_DFRL);
		len = ntohs(tempword);
	}
	chksum = tempword;
	DEBUG(1, "Number of Bytes in FIFO = %d\n", len);

	if (len > ENET_MAX_SIZE) {
		DEBUG(0, "size of ethernet packet invalid\n");
		if (info->AsicID == MAGNEMITE_ID) {
			// Read High word to complete 32 bit access
			tempword = ft1000_read_reg(dev, FT1000_REG_MAG_DFRH);
		}
		ft1000_flush_fifo(dev, DSP_PKTLEN_INFO);
		info->stats.rx_errors++;
		return FAILURE;
	}

	skb = dev_alloc_skb(len + 12 + 2);

	if (skb == NULL) {
		DEBUG(0, "No Network buffers available\n");
		// Read High word to complete 32 bit access
		if (info->AsicID == MAGNEMITE_ID) {
			tempword = ft1000_read_reg(dev, FT1000_REG_MAG_DFRH);
		}
		ft1000_flush_fifo(dev, 0);
		info->stats.rx_errors++;
		return FAILURE;
	}
	pbuffer = (u8 *) skb_put(skb, len + 12);

	// Pseudo header
	if (info->AsicID == ELECTRABUZZ_ID) {
		for (i = 1; i < 7; i++) {
			tempword = ft1000_read_reg(dev, FT1000_REG_DFIFO);
			chksum ^= tempword;
		}
		// read checksum value
		tempword = ft1000_read_reg(dev, FT1000_REG_DFIFO);
	} else {
		tempword = ft1000_read_reg(dev, FT1000_REG_MAG_DFRH);
		DEBUG(1, "Pseudo = 0x%x\n", tempword);
		chksum ^= tempword;

		tempword = ft1000_read_reg(dev, FT1000_REG_MAG_DFRL);
		DEBUG(1, "Pseudo = 0x%x\n", tempword);
		chksum ^= tempword;

		tempword = ft1000_read_reg(dev, FT1000_REG_MAG_DFRH);
		DEBUG(1, "Pseudo = 0x%x\n", tempword);
		chksum ^= tempword;

		tempword = ft1000_read_reg(dev, FT1000_REG_MAG_DFRL);
		DEBUG(1, "Pseudo = 0x%x\n", tempword);
		chksum ^= tempword;

		tempword = ft1000_read_reg(dev, FT1000_REG_MAG_DFRH);
		DEBUG(1, "Pseudo = 0x%x\n", tempword);
		chksum ^= tempword;

		tempword = ft1000_read_reg(dev, FT1000_REG_MAG_DFRL);
		DEBUG(1, "Pseudo = 0x%x\n", tempword);
		chksum ^= tempword;

		// read checksum value
		tempword = ft1000_read_reg(dev, FT1000_REG_MAG_DFRH);
		DEBUG(1, "Pseudo = 0x%x\n", tempword);
	}

	if (chksum != tempword) {
		DEBUG(0, "Packet checksum mismatch 0x%x 0x%x\n", chksum,
			  tempword);
		ft1000_flush_fifo(dev, DSP_PKTPHCKSUM_INFO);
		info->stats.rx_errors++;
		kfree_skb(skb);
		return FAILURE;
	}
	//subtract the number of bytes read already
	ptemp = pbuffer;

	// fake MAC address
	*pbuffer++ = dev->dev_addr[0];
	*pbuffer++ = dev->dev_addr[1];
	*pbuffer++ = dev->dev_addr[2];
	*pbuffer++ = dev->dev_addr[3];
	*pbuffer++ = dev->dev_addr[4];
	*pbuffer++ = dev->dev_addr[5];
	*pbuffer++ = 0x00;
	*pbuffer++ = 0x07;
	*pbuffer++ = 0x35;
	*pbuffer++ = 0xff;
	*pbuffer++ = 0xff;
	*pbuffer++ = 0xfe;

	if (info->AsicID == ELECTRABUZZ_ID) {
		for (i = 0; i < len / 2; i++) {
			tempword = ft1000_read_reg(dev, FT1000_REG_DFIFO);
			*pbuffer++ = (u8) (tempword >> 8);
			*pbuffer++ = (u8) tempword;
			if (ft1000_chkcard(dev) == false) {
				kfree_skb(skb);
				return FAILURE;
			}
		}

		// Need to read one more word if odd byte
		if (len & 0x0001) {
			tempword = ft1000_read_reg(dev, FT1000_REG_DFIFO);
			*pbuffer++ = (u8) (tempword >> 8);
		}
	} else {
		ptemplong = (u32 *) pbuffer;
		for (i = 0; i < len / 4; i++) {
			templong = inl(dev->base_addr + FT1000_REG_MAG_DFR);
			DEBUG(1, "Data = 0x%8x\n", templong);
			*ptemplong++ = templong;
		}

		// Need to read one more word if odd align.
		if (len & 0x0003) {
			templong = inl(dev->base_addr + FT1000_REG_MAG_DFR);
			DEBUG(1, "Data = 0x%8x\n", templong);
			*ptemplong++ = templong;
		}

	}

	DEBUG(1, "Data passed to Protocol layer:\n");
	for (i = 0; i < len + 12; i++) {
		DEBUG(1, "Protocol Data: 0x%x\n ", *ptemp++);
	}

	skb->dev = dev;
	skb->protocol = eth_type_trans(skb, dev);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	netif_rx(skb);

	info->stats.rx_packets++;
	// Add on 12 bytes for MAC address which was removed
	info->stats.rx_bytes += (len + 12);

	if (info->AsicID == ELECTRABUZZ_ID) {
		// track how many bytes have been read from FIFO - round up to 16 bit word
		tempword = len + 16;
		if (tempword & 0x01)
			tempword++;
		info->fifo_cnt += tempword;
		ft1000_write_reg(dev, FT1000_REG_DPRAM_ADDR, FT1000_FIFO_LEN);
		ft1000_write_reg(dev, FT1000_REG_DPRAM_DATA, info->fifo_cnt);
	}

	return SUCCESS;
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_copy_down_pkt
// Description: This function will take an ethernet packet and convert it to
//             a Flarion packet prior to sending it to the ASIC Downlink
//             FIFO.
// Input:
//     dev    - device structure
//     packet - address of ethernet packet
//     len    - length of IP packet
// Output:
//     status - FAILURE
//              SUCCESS
//
//---------------------------------------------------------------------------
static int ft1000_copy_down_pkt(struct net_device *dev, u16 * packet, u16 len)
{
	struct ft1000_info *info = netdev_priv(dev);
	union {
		struct pseudo_hdr blk;
		u16 buff[sizeof(struct pseudo_hdr) >> 1];
		u8 buffc[sizeof(struct pseudo_hdr)];
	} pseudo;
	int i;
	u32 *plong;

	DEBUG(1, "ft1000_hw: copy_down_pkt()\n");

	// Check if there is room on the FIFO
	if (len > ft1000_read_fifo_len(dev)) {
		udelay(10);
		if (len > ft1000_read_fifo_len(dev)) {
			udelay(20);
		}
		if (len > ft1000_read_fifo_len(dev)) {
			udelay(20);
		}
		if (len > ft1000_read_fifo_len(dev)) {
			udelay(20);
		}
		if (len > ft1000_read_fifo_len(dev)) {
			udelay(20);
		}
		if (len > ft1000_read_fifo_len(dev)) {
			udelay(20);
		}
		if (len > ft1000_read_fifo_len(dev)) {
			DEBUG(1,
				  "ft1000_hw:ft1000_copy_down_pkt:Transmit FIFO is fulli - pkt drop\n");
			info->stats.tx_errors++;
			return SUCCESS;
		}
	}
	// Create pseudo header and send pseudo/ip to hardware
	if (info->AsicID == ELECTRABUZZ_ID) {
		pseudo.blk.length = len;
	} else {
		pseudo.blk.length = ntohs(len);
	}
	pseudo.blk.source = DSPID;	// Need to swap to get in correct order
	pseudo.blk.destination = HOSTID;
	pseudo.blk.portdest = NETWORKID;	// Need to swap to get in correct order
	pseudo.blk.portsrc = DSPAIRID;
	pseudo.blk.sh_str_id = 0;
	pseudo.blk.control = 0;
	pseudo.blk.rsvd1 = 0;
	pseudo.blk.seq_num = 0;
	pseudo.blk.rsvd2 = info->packetseqnum++;
	pseudo.blk.qos_class = 0;
	/* Calculate pseudo header checksum */
	pseudo.blk.checksum = pseudo.buff[0];
	for (i = 1; i < 7; i++) {
		pseudo.blk.checksum ^= pseudo.buff[i];
	}

	// Production Mode
	if (info->AsicID == ELECTRABUZZ_ID) {
		// copy first word to UFIFO_BEG reg
		ft1000_write_reg(dev, FT1000_REG_UFIFO_BEG, pseudo.buff[0]);
		DEBUG(1, "ft1000_hw:ft1000_copy_down_pkt:data 0 BEG = 0x%04x\n",
			  pseudo.buff[0]);

		// copy subsequent words to UFIFO_MID reg
		ft1000_write_reg(dev, FT1000_REG_UFIFO_MID, pseudo.buff[1]);
		DEBUG(1, "ft1000_hw:ft1000_copy_down_pkt:data 1 MID = 0x%04x\n",
			  pseudo.buff[1]);
		ft1000_write_reg(dev, FT1000_REG_UFIFO_MID, pseudo.buff[2]);
		DEBUG(1, "ft1000_hw:ft1000_copy_down_pkt:data 2 MID = 0x%04x\n",
			  pseudo.buff[2]);
		ft1000_write_reg(dev, FT1000_REG_UFIFO_MID, pseudo.buff[3]);
		DEBUG(1, "ft1000_hw:ft1000_copy_down_pkt:data 3 MID = 0x%04x\n",
			  pseudo.buff[3]);
		ft1000_write_reg(dev, FT1000_REG_UFIFO_MID, pseudo.buff[4]);
		DEBUG(1, "ft1000_hw:ft1000_copy_down_pkt:data 4 MID = 0x%04x\n",
			  pseudo.buff[4]);
		ft1000_write_reg(dev, FT1000_REG_UFIFO_MID, pseudo.buff[5]);
		DEBUG(1, "ft1000_hw:ft1000_copy_down_pkt:data 5 MID = 0x%04x\n",
			  pseudo.buff[5]);
		ft1000_write_reg(dev, FT1000_REG_UFIFO_MID, pseudo.buff[6]);
		DEBUG(1, "ft1000_hw:ft1000_copy_down_pkt:data 6 MID = 0x%04x\n",
			  pseudo.buff[6]);
		ft1000_write_reg(dev, FT1000_REG_UFIFO_MID, pseudo.buff[7]);
		DEBUG(1, "ft1000_hw:ft1000_copy_down_pkt:data 7 MID = 0x%04x\n",
			  pseudo.buff[7]);

		// Write PPP type + IP Packet into Downlink FIFO
		for (i = 0; i < (len >> 1) - 1; i++) {
			ft1000_write_reg(dev, FT1000_REG_UFIFO_MID,
					 htons(*packet));
			DEBUG(1,
				  "ft1000_hw:ft1000_copy_down_pkt:data %d MID = 0x%04x\n",
				  i + 8, htons(*packet));
			packet++;
		}

		// Check for odd byte
		if (len & 0x0001) {
			ft1000_write_reg(dev, FT1000_REG_UFIFO_MID,
					 htons(*packet));
			DEBUG(1,
				  "ft1000_hw:ft1000_copy_down_pkt:data MID = 0x%04x\n",
				  htons(*packet));
			packet++;
			ft1000_write_reg(dev, FT1000_REG_UFIFO_END,
					 htons(*packet));
			DEBUG(1,
				  "ft1000_hw:ft1000_copy_down_pkt:data %d MID = 0x%04x\n",
				  i + 8, htons(*packet));
		} else {
			ft1000_write_reg(dev, FT1000_REG_UFIFO_END,
					 htons(*packet));
			DEBUG(1,
				  "ft1000_hw:ft1000_copy_down_pkt:data %d MID = 0x%04x\n",
				  i + 8, htons(*packet));
		}
	} else {
		outl(*(u32 *) & pseudo.buff[0],
			 dev->base_addr + FT1000_REG_MAG_UFDR);
		DEBUG(1, "ft1000_copy_down_pkt: Pseudo = 0x%8x\n",
			  *(u32 *) & pseudo.buff[0]);
		outl(*(u32 *) & pseudo.buff[2],
			 dev->base_addr + FT1000_REG_MAG_UFDR);
		DEBUG(1, "ft1000_copy_down_pkt: Pseudo = 0x%8x\n",
			  *(u32 *) & pseudo.buff[2]);
		outl(*(u32 *) & pseudo.buff[4],
			 dev->base_addr + FT1000_REG_MAG_UFDR);
		DEBUG(1, "ft1000_copy_down_pkt: Pseudo = 0x%8x\n",
			  *(u32 *) & pseudo.buff[4]);
		outl(*(u32 *) & pseudo.buff[6],
			 dev->base_addr + FT1000_REG_MAG_UFDR);
		DEBUG(1, "ft1000_copy_down_pkt: Pseudo = 0x%8x\n",
			  *(u32 *) & pseudo.buff[6]);

		plong = (u32 *) packet;
		// Write PPP type + IP Packet into Downlink FIFO
		for (i = 0; i < (len >> 2); i++) {
			outl(*plong++, dev->base_addr + FT1000_REG_MAG_UFDR);
		}

		// Check for odd alignment
		if (len & 0x0003) {
			DEBUG(1,
				  "ft1000_hw:ft1000_copy_down_pkt:data = 0x%8x\n",
				  *plong);
			outl(*plong++, dev->base_addr + FT1000_REG_MAG_UFDR);
		}
		outl(1, dev->base_addr + FT1000_REG_MAG_UFER);
	}

	info->stats.tx_packets++;
	// Add 14 bytes for MAC address plus ethernet type
	info->stats.tx_bytes += (len + 14);
	return SUCCESS;
}

static struct net_device_stats *ft1000_stats(struct net_device *dev)
{
	struct ft1000_info *info = netdev_priv(dev);
	return (&info->stats);
}

static int ft1000_open(struct net_device *dev)
{

	DEBUG(0, "ft1000_hw: ft1000_open is called\n");

	ft1000_reset_card(dev);
	DEBUG(0, "ft1000_hw: ft1000_open is ended\n");

	/* schedule ft1000_hbchk to perform periodic heartbeat checks on DSP and ASIC */
	init_timer(&poll_timer);
	poll_timer.expires = jiffies + (2 * HZ);
	poll_timer.data = (u_long) dev;
	add_timer(&poll_timer);

	DEBUG(0, "ft1000_hw: ft1000_open is ended2\n");
	return 0;
}

static int ft1000_close(struct net_device *dev)
{
	struct ft1000_info *info = netdev_priv(dev);

	DEBUG(0, "ft1000_hw: ft1000_close()\n");

	info->CardReady = 0;
	del_timer(&poll_timer);

	if (ft1000_card_present == 1) {
		DEBUG(0, "Media is down\n");
		netif_stop_queue(dev);

		ft1000_disable_interrupts(dev);
		ft1000_write_reg(dev, FT1000_REG_RESET, DSP_RESET_BIT);

		//reset ASIC
		ft1000_reset_asic(dev);
	}
	return 0;
}

static int ft1000_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ft1000_info *info = netdev_priv(dev);
	u8 *pdata;

	DEBUG(1, "ft1000_hw: ft1000_start_xmit()\n");
	if (skb == NULL) {
		DEBUG(1, "ft1000_hw: ft1000_start_xmit:skb == NULL!!!\n");
		return 0;
	}

	DEBUG(1, "ft1000_hw: ft1000_start_xmit:length of packet = %d\n",
		  skb->len);

	pdata = (u8 *) skb->data;

	if (info->mediastate == 0) {
		/* Drop packet is mediastate is down */
		DEBUG(1, "ft1000_hw:ft1000_copy_down_pkt:mediastate is down\n");
		return SUCCESS;
	}

	if ((skb->len < ENET_HEADER_SIZE) || (skb->len > ENET_MAX_SIZE)) {
		/* Drop packet which has invalid size */
		DEBUG(1,
			  "ft1000_hw:ft1000_copy_down_pkt:invalid ethernet length\n");
		return SUCCESS;
	}
	ft1000_copy_down_pkt(dev, (u16 *) (pdata + ENET_HEADER_SIZE - 2),
				 skb->len - ENET_HEADER_SIZE + 2);

	dev_kfree_skb(skb);

	return 0;
}

static irqreturn_t ft1000_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct ft1000_info *info = netdev_priv(dev);
	u16 tempword;
	u16 inttype;
	int cnt;

	DEBUG(1, "ft1000_hw: ft1000_interrupt()\n");

	if (info->CardReady == 0) {
		ft1000_disable_interrupts(dev);
		return IRQ_HANDLED;
	}

	if (ft1000_chkcard(dev) == false) {
		ft1000_disable_interrupts(dev);
		return IRQ_HANDLED;
	}

	ft1000_disable_interrupts(dev);

	// Read interrupt type
	inttype = ft1000_read_reg(dev, FT1000_REG_SUP_ISR);

    // Make sure we process all interrupt before leaving the ISR due to the edge trigger interrupt type
    while (inttype) {
		if (inttype & ISR_DOORBELL_PEND)
			ft1000_parse_dpram_msg(dev);

	if (inttype & ISR_RCV) {
		DEBUG(1, "Data in FIFO\n");

		cnt = 0;
		do {
			// Check if we have packets in the Downlink FIFO
			if (info->AsicID == ELECTRABUZZ_ID) {
				tempword =
					ft1000_read_reg(dev, FT1000_REG_DFIFO_STAT);
			} else {
				tempword =
					ft1000_read_reg(dev, FT1000_REG_MAG_DFSR);
			}
			if (tempword & 0x1f) {
				ft1000_copy_up_pkt(dev);
			} else {
				break;
			}
			cnt++;
		} while (cnt < MAX_RCV_LOOP);

	}
	// clear interrupts
	tempword = ft1000_read_reg(dev, FT1000_REG_SUP_ISR);
	DEBUG(1, "ft1000_hw: interrupt status register = 0x%x\n", tempword);
	ft1000_write_reg(dev, FT1000_REG_SUP_ISR, tempword);

        // Read interrupt type
        inttype = ft1000_read_reg (dev, FT1000_REG_SUP_ISR);
        DEBUG(1,"ft1000_hw: interrupt status register after clear = 0x%x\n",inttype);
    }
	ft1000_enable_interrupts(dev);
	return IRQ_HANDLED;
}

void stop_ft1000_card(struct net_device *dev)
{
	struct ft1000_info *info = netdev_priv(dev);
	struct prov_record *ptr;
//	int cnt;

	DEBUG(0, "ft1000_hw: stop_ft1000_card()\n");

	info->CardReady = 0;
	ft1000_card_present = 0;
	netif_stop_queue(dev);
	ft1000_disable_interrupts(dev);

	// Make sure we free any memory reserve for provisioning
	while (list_empty(&info->prov_list) == 0) {
		ptr = list_entry(info->prov_list.next, struct prov_record, list);
		list_del(&ptr->list);
		kfree(ptr->pprov_data);
		kfree(ptr);
	}

	if (info->registered) {
		unregister_netdev(dev);
		info->registered = 0;
	}

	free_irq(dev->irq, dev);
	release_region(dev->base_addr,256);
	release_firmware(fw_entry);
	flarion_ft1000_cnt--;
	ft1000CleanupProc(dev);

}

static void ft1000_get_drvinfo(struct net_device *dev,
				   struct ethtool_drvinfo *info)
{
	struct ft1000_info *ft_info;
	ft_info = netdev_priv(dev);

	snprintf(info->driver, 32, "ft1000");
	snprintf(info->bus_info, ETHTOOL_BUSINFO_LEN, "PCMCIA 0x%lx",
		 dev->base_addr);
	snprintf(info->fw_version, 32, "%d.%d.%d.%d", ft_info->DspVer[0],
		 ft_info->DspVer[1], ft_info->DspVer[2], ft_info->DspVer[3]);
}

static u32 ft1000_get_link(struct net_device *dev)
{
	struct ft1000_info *info;
	info = netdev_priv(dev);
	return info->mediastate;
}

static const struct ethtool_ops ops = {
	.get_drvinfo = ft1000_get_drvinfo,
	.get_link = ft1000_get_link
};

struct net_device *init_ft1000_card(struct pcmcia_device *link,
						void *ft1000_reset)
{
	struct ft1000_info *info;
	struct net_device *dev;

	static const struct net_device_ops ft1000ops =		// Slavius 21.10.2009 due to kernel changes
	{
		.ndo_open = &ft1000_open,
		.ndo_stop = &ft1000_close,
		.ndo_start_xmit = &ft1000_start_xmit,
		.ndo_get_stats = &ft1000_stats,
	};

	DEBUG(1, "ft1000_hw: init_ft1000_card()\n");
	DEBUG(1, "ft1000_hw: irq = %d\n", link->irq);
	DEBUG(1, "ft1000_hw: port = 0x%04x\n", link->resource[0]->start);

	flarion_ft1000_cnt++;

	if (flarion_ft1000_cnt > 1) {
		flarion_ft1000_cnt--;

		printk(KERN_INFO
			   "ft1000: This driver can not support more than one instance\n");
		return NULL;
	}

	dev = alloc_etherdev(sizeof(struct ft1000_info));
	if (!dev) {
		printk(KERN_ERR "ft1000: failed to allocate etherdev\n");
		return NULL;
	}

	SET_NETDEV_DEV(dev, &link->dev);
	info = netdev_priv(dev);

	memset(info, 0, sizeof(struct ft1000_info));

	DEBUG(1, "address of dev = 0x%8x\n", (u32) dev);
	DEBUG(1, "address of dev info = 0x%8x\n", (u32) info);
	DEBUG(0, "device name = %s\n", dev->name);

	memset(&info->stats, 0, sizeof(struct net_device_stats));

	spin_lock_init(&info->dpram_lock);
	info->DrvErrNum = 0;
	info->registered = 1;
	info->link = link;
	info->ft1000_reset = ft1000_reset;
	info->mediastate = 0;
	info->fifo_cnt = 0;
	info->CardReady = 0;
	info->DSP_TIME[0] = 0;
	info->DSP_TIME[1] = 0;
	info->DSP_TIME[2] = 0;
	info->DSP_TIME[3] = 0;
	flarion_ft1000_cnt = 0;

	INIT_LIST_HEAD(&info->prov_list);

	info->squeseqnum = 0;

//	dev->hard_start_xmit = &ft1000_start_xmit;
//	dev->get_stats = &ft1000_stats;
//	dev->open = &ft1000_open;
//	dev->stop = &ft1000_close;

	dev->netdev_ops = &ft1000ops;		// Slavius 21.10.2009 due to kernel changes

	DEBUG(0, "device name = %s\n", dev->name);

	dev->irq = link->irq;
	dev->base_addr = link->resource[0]->start;
	if (pcmcia_get_mac_from_cis(link, dev)) {
		printk(KERN_ERR "ft1000: Could not read mac address\n");
		goto err_dev;
	}

	if (request_irq(dev->irq, ft1000_interrupt, IRQF_SHARED, dev->name, dev)) {
		printk(KERN_ERR "ft1000: Could not request_irq\n");
		goto err_dev;
	}

	if (request_region(dev->base_addr, 256, dev->name) == NULL) {
		printk(KERN_ERR "ft1000: Could not request_region\n");
		goto err_irq;
	}

	if (register_netdev(dev) != 0) {
		DEBUG(0, "ft1000: Could not register netdev");
		goto err_reg;
	}

	info->AsicID = ft1000_read_reg(dev, FT1000_REG_ASIC_ID);
	if (info->AsicID == ELECTRABUZZ_ID) {
		DEBUG(0, "ft1000_hw: ELECTRABUZZ ASIC\n");
		if (request_firmware(&fw_entry, "ft1000.img", &link->dev) != 0) {
			printk(KERN_INFO "ft1000: Could not open ft1000.img\n");
			goto err_unreg;
		}
	} else {
		DEBUG(0, "ft1000_hw: MAGNEMITE ASIC\n");
		if (request_firmware(&fw_entry, "ft2000.img", &link->dev) != 0) {
			printk(KERN_INFO "ft1000: Could not open ft2000.img\n");
			goto err_unreg;
		}
	}

	ft1000_enable_interrupts(dev);

	ft1000InitProc(dev);
	ft1000_card_present = 1;
	SET_ETHTOOL_OPS(dev, &ops);
	printk(KERN_INFO "ft1000: %s: addr 0x%04lx irq %d, MAC addr %pM\n",
			dev->name, dev->base_addr, dev->irq, dev->dev_addr);
	return dev;

err_unreg:
	unregister_netdev(dev);
err_reg:
	release_region(dev->base_addr, 256);
err_irq:
	free_irq(dev->irq, dev);
err_dev:
	free_netdev(dev);
	return NULL;
}
