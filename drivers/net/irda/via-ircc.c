/********************************************************************
 Filename:      via-ircc.c
 Version:       1.0 
 Description:   Driver for the VIA VT8231/VT8233 IrDA chipsets
 Author:        VIA Technologies,inc
 Date  :	08/06/2003

Copyright (c) 1998-2003 VIA Technologies, Inc.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; either version 2, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTIES OR REPRESENTATIONS; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

F01 Oct/02/02: Modify code for V0.11(move out back to back transfer)
F02 Oct/28/02: Add SB device ID for 3147 and 3177.
 Comment :
       jul/09/2002 : only implement two kind of dongle currently.
       Oct/02/2002 : work on VT8231 and VT8233 .
       Aug/06/2003 : change driver format to pci driver .

2004-02-16: <sda@bdit.de>
- Removed unneeded 'legacy' pci stuff.
- Make sure SIR mode is set (hw_init()) before calling mode-dependent stuff.
- On speed change from core, don't send SIR frame with new speed. 
  Use current speed and change speeds later.
- Make module-param dongle_id actually work.
- New dongle_id 17 (0x11): TDFS4500. Single-ended SIR only. 
  Tested with home-grown PCB on EPIA boards.
- Code cleanup.
       
 ********************************************************************/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/rtnetlink.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/gfp.h>

#include <asm/io.h>
#include <asm/dma.h>
#include <asm/byteorder.h>

#include <linux/pm.h>

#include <net/irda/wrapper.h>
#include <net/irda/irda.h>
#include <net/irda/irda_device.h>

#include "via-ircc.h"

#define VIA_MODULE_NAME "via-ircc"
#define CHIP_IO_EXTENT 0x40

static char *driver_name = VIA_MODULE_NAME;

/* Module parameters */
static int qos_mtt_bits = 0x07;	/* 1 ms or more */
static int dongle_id = 0;	/* default: probe */

/* We can't guess the type of connected dongle, user *must* supply it. */
module_param(dongle_id, int, 0);

/* Some prototypes */
static int via_ircc_open(struct pci_dev *pdev, chipio_t * info,
			 unsigned int id);
static int via_ircc_dma_receive(struct via_ircc_cb *self);
static int via_ircc_dma_receive_complete(struct via_ircc_cb *self,
					 int iobase);
static netdev_tx_t via_ircc_hard_xmit_sir(struct sk_buff *skb,
						struct net_device *dev);
static netdev_tx_t via_ircc_hard_xmit_fir(struct sk_buff *skb,
						struct net_device *dev);
static void via_hw_init(struct via_ircc_cb *self);
static void via_ircc_change_speed(struct via_ircc_cb *self, __u32 baud);
static irqreturn_t via_ircc_interrupt(int irq, void *dev_id);
static int via_ircc_is_receiving(struct via_ircc_cb *self);
static int via_ircc_read_dongle_id(int iobase);

static int via_ircc_net_open(struct net_device *dev);
static int via_ircc_net_close(struct net_device *dev);
static int via_ircc_net_ioctl(struct net_device *dev, struct ifreq *rq,
			      int cmd);
static void via_ircc_change_dongle_speed(int iobase, int speed,
					 int dongle_id);
static int RxTimerHandler(struct via_ircc_cb *self, int iobase);
static void hwreset(struct via_ircc_cb *self);
static int via_ircc_dma_xmit(struct via_ircc_cb *self, u16 iobase);
static int upload_rxdata(struct via_ircc_cb *self, int iobase);
static int __devinit via_init_one (struct pci_dev *pcidev, const struct pci_device_id *id);
static void __devexit via_remove_one (struct pci_dev *pdev);

/* FIXME : Should use udelay() instead, even if we are x86 only - Jean II */
static void iodelay(int udelay)
{
	u8 data;
	int i;

	for (i = 0; i < udelay; i++) {
		data = inb(0x80);
	}
}

static DEFINE_PCI_DEVICE_TABLE(via_pci_tbl) = {
	{ PCI_VENDOR_ID_VIA, 0x8231, PCI_ANY_ID, PCI_ANY_ID,0,0,0 },
	{ PCI_VENDOR_ID_VIA, 0x3109, PCI_ANY_ID, PCI_ANY_ID,0,0,1 },
	{ PCI_VENDOR_ID_VIA, 0x3074, PCI_ANY_ID, PCI_ANY_ID,0,0,2 },
	{ PCI_VENDOR_ID_VIA, 0x3147, PCI_ANY_ID, PCI_ANY_ID,0,0,3 },
	{ PCI_VENDOR_ID_VIA, 0x3177, PCI_ANY_ID, PCI_ANY_ID,0,0,4 },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci,via_pci_tbl);


static struct pci_driver via_driver = {
	.name		= VIA_MODULE_NAME,
	.id_table	= via_pci_tbl,
	.probe		= via_init_one,
	.remove		= __devexit_p(via_remove_one),
};


/*
 * Function via_ircc_init ()
 *
 *    Initialize chip. Just find out chip type and resource.
 */
static int __init via_ircc_init(void)
{
	int rc;

	IRDA_DEBUG(3, "%s()\n", __func__);

	rc = pci_register_driver(&via_driver);
	if (rc < 0) {
		IRDA_DEBUG(0, "%s(): error rc = %d, returning  -ENODEV...\n",
			   __func__, rc);
		return -ENODEV;
	}
	return 0;
}

static int __devinit via_init_one (struct pci_dev *pcidev, const struct pci_device_id *id)
{
	int rc;
        u8 temp,oldPCI_40,oldPCI_44,bTmp,bTmp1;
	u16 Chipset,FirDRQ1,FirDRQ0,FirIRQ,FirIOBase;
	chipio_t info;

	IRDA_DEBUG(2, "%s(): Device ID=(0X%X)\n", __func__, id->device);

	rc = pci_enable_device (pcidev);
	if (rc) {
		IRDA_DEBUG(0, "%s(): error rc = %d\n", __func__, rc);
		return -ENODEV;
	}

	// South Bridge exist
        if ( ReadLPCReg(0x20) != 0x3C )
		Chipset=0x3096;
	else
		Chipset=0x3076;

	if (Chipset==0x3076) {
		IRDA_DEBUG(2, "%s(): Chipset = 3076\n", __func__);

		WriteLPCReg(7,0x0c );
		temp=ReadLPCReg(0x30);//check if BIOS Enable Fir
		if((temp&0x01)==1) {   // BIOS close or no FIR
			WriteLPCReg(0x1d, 0x82 );
			WriteLPCReg(0x23,0x18);
			temp=ReadLPCReg(0xF0);
			if((temp&0x01)==0) {
				temp=(ReadLPCReg(0x74)&0x03);    //DMA
				FirDRQ0=temp + 4;
				temp=(ReadLPCReg(0x74)&0x0C) >> 2;
				FirDRQ1=temp + 4;
			} else {
				temp=(ReadLPCReg(0x74)&0x0C) >> 2;    //DMA
				FirDRQ0=temp + 4;
				FirDRQ1=FirDRQ0;
			}
			FirIRQ=(ReadLPCReg(0x70)&0x0f);		//IRQ
			FirIOBase=ReadLPCReg(0x60 ) << 8;	//IO Space :high byte
			FirIOBase=FirIOBase| ReadLPCReg(0x61) ;	//low byte
			FirIOBase=FirIOBase  ;
			info.fir_base=FirIOBase;
			info.irq=FirIRQ;
			info.dma=FirDRQ1;
			info.dma2=FirDRQ0;
			pci_read_config_byte(pcidev,0x40,&bTmp);
			pci_write_config_byte(pcidev,0x40,((bTmp | 0x08) & 0xfe));
			pci_read_config_byte(pcidev,0x42,&bTmp);
			pci_write_config_byte(pcidev,0x42,(bTmp | 0xf0));
			pci_write_config_byte(pcidev,0x5a,0xc0);
			WriteLPCReg(0x28, 0x70 );
			if (via_ircc_open(pcidev, &info, 0x3076) == 0)
				rc=0;
		} else
			rc = -ENODEV; //IR not turn on	 
	} else { //Not VT1211
		IRDA_DEBUG(2, "%s(): Chipset = 3096\n", __func__);

		pci_read_config_byte(pcidev,0x67,&bTmp);//check if BIOS Enable Fir
		if((bTmp&0x01)==1) {  // BIOS enable FIR
			//Enable Double DMA clock
			pci_read_config_byte(pcidev,0x42,&oldPCI_40);
			pci_write_config_byte(pcidev,0x42,oldPCI_40 | 0x80);
			pci_read_config_byte(pcidev,0x40,&oldPCI_40);
			pci_write_config_byte(pcidev,0x40,oldPCI_40 & 0xf7);
			pci_read_config_byte(pcidev,0x44,&oldPCI_44);
			pci_write_config_byte(pcidev,0x44,0x4e);
  //---------- read configuration from Function0 of south bridge
			if((bTmp&0x02)==0) {
				pci_read_config_byte(pcidev,0x44,&bTmp1); //DMA
				FirDRQ0 = (bTmp1 & 0x30) >> 4;
				pci_read_config_byte(pcidev,0x44,&bTmp1);
				FirDRQ1 = (bTmp1 & 0xc0) >> 6;
			} else  {
				pci_read_config_byte(pcidev,0x44,&bTmp1);    //DMA
				FirDRQ0 = (bTmp1 & 0x30) >> 4 ;
				FirDRQ1=0;
			}
			pci_read_config_byte(pcidev,0x47,&bTmp1);  //IRQ
			FirIRQ = bTmp1 & 0x0f;

			pci_read_config_byte(pcidev,0x69,&bTmp);
			FirIOBase = bTmp << 8;//hight byte
			pci_read_config_byte(pcidev,0x68,&bTmp);
			FirIOBase = (FirIOBase | bTmp ) & 0xfff0;
  //-------------------------
			info.fir_base=FirIOBase;
			info.irq=FirIRQ;
			info.dma=FirDRQ1;
			info.dma2=FirDRQ0;
			if (via_ircc_open(pcidev, &info, 0x3096) == 0)
				rc=0;
		} else
			rc = -ENODEV; //IR not turn on !!!!!
	}//Not VT1211

	IRDA_DEBUG(2, "%s(): End - rc = %d\n", __func__, rc);
	return rc;
}

static void __exit via_ircc_cleanup(void)
{
	IRDA_DEBUG(3, "%s()\n", __func__);

	/* Cleanup all instances of the driver */
	pci_unregister_driver (&via_driver); 
}

static const struct net_device_ops via_ircc_sir_ops = {
	.ndo_start_xmit = via_ircc_hard_xmit_sir,
	.ndo_open = via_ircc_net_open,
	.ndo_stop = via_ircc_net_close,
	.ndo_do_ioctl = via_ircc_net_ioctl,
};
static const struct net_device_ops via_ircc_fir_ops = {
	.ndo_start_xmit = via_ircc_hard_xmit_fir,
	.ndo_open = via_ircc_net_open,
	.ndo_stop = via_ircc_net_close,
	.ndo_do_ioctl = via_ircc_net_ioctl,
};

/*
 * Function via_ircc_open(pdev, iobase, irq)
 *
 *    Open driver instance
 *
 */
static __devinit int via_ircc_open(struct pci_dev *pdev, chipio_t * info,
				   unsigned int id)
{
	struct net_device *dev;
	struct via_ircc_cb *self;
	int err;

	IRDA_DEBUG(3, "%s()\n", __func__);

	/* Allocate new instance of the driver */
	dev = alloc_irdadev(sizeof(struct via_ircc_cb));
	if (dev == NULL) 
		return -ENOMEM;

	self = netdev_priv(dev);
	self->netdev = dev;
	spin_lock_init(&self->lock);

	pci_set_drvdata(pdev, self);

	/* Initialize Resource */
	self->io.cfg_base = info->cfg_base;
	self->io.fir_base = info->fir_base;
	self->io.irq = info->irq;
	self->io.fir_ext = CHIP_IO_EXTENT;
	self->io.dma = info->dma;
	self->io.dma2 = info->dma2;
	self->io.fifo_size = 32;
	self->chip_id = id;
	self->st_fifo.len = 0;
	self->RxDataReady = 0;

	/* Reserve the ioports that we need */
	if (!request_region(self->io.fir_base, self->io.fir_ext, driver_name)) {
		IRDA_DEBUG(0, "%s(), can't get iobase of 0x%03x\n",
			   __func__, self->io.fir_base);
		err = -ENODEV;
		goto err_out1;
	}
	
	/* Initialize QoS for this device */
	irda_init_max_qos_capabilies(&self->qos);

	/* Check if user has supplied the dongle id or not */
	if (!dongle_id)
		dongle_id = via_ircc_read_dongle_id(self->io.fir_base);
	self->io.dongle_id = dongle_id;

	/* The only value we must override it the baudrate */
	/* Maximum speeds and capabilities are dongle-dependent. */
	switch( self->io.dongle_id ){
	case 0x0d:
		self->qos.baud_rate.bits =
		    IR_9600 | IR_19200 | IR_38400 | IR_57600 | IR_115200 |
		    IR_576000 | IR_1152000 | (IR_4000000 << 8);
		break;
	default:
		self->qos.baud_rate.bits =
		    IR_9600 | IR_19200 | IR_38400 | IR_57600 | IR_115200;
		break;
	}

	/* Following was used for testing:
	 *
	 *   self->qos.baud_rate.bits = IR_9600;
	 *
	 * Is is no good, as it prohibits (error-prone) speed-changes.
	 */

	self->qos.min_turn_time.bits = qos_mtt_bits;
	irda_qos_bits_to_value(&self->qos);

	/* Max DMA buffer size needed = (data_size + 6) * (window_size) + 6; */
	self->rx_buff.truesize = 14384 + 2048;
	self->tx_buff.truesize = 14384 + 2048;

	/* Allocate memory if needed */
	self->rx_buff.head =
		dma_alloc_coherent(&pdev->dev, self->rx_buff.truesize,
				   &self->rx_buff_dma, GFP_KERNEL);
	if (self->rx_buff.head == NULL) {
		err = -ENOMEM;
		goto err_out2;
	}
	memset(self->rx_buff.head, 0, self->rx_buff.truesize);

	self->tx_buff.head =
		dma_alloc_coherent(&pdev->dev, self->tx_buff.truesize,
				   &self->tx_buff_dma, GFP_KERNEL);
	if (self->tx_buff.head == NULL) {
		err = -ENOMEM;
		goto err_out3;
	}
	memset(self->tx_buff.head, 0, self->tx_buff.truesize);

	self->rx_buff.in_frame = FALSE;
	self->rx_buff.state = OUTSIDE_FRAME;
	self->tx_buff.data = self->tx_buff.head;
	self->rx_buff.data = self->rx_buff.head;

	/* Reset Tx queue info */
	self->tx_fifo.len = self->tx_fifo.ptr = self->tx_fifo.free = 0;
	self->tx_fifo.tail = self->tx_buff.head;

	/* Override the network functions we need to use */
	dev->netdev_ops = &via_ircc_sir_ops;

	err = register_netdev(dev);
	if (err)
		goto err_out4;

	IRDA_MESSAGE("IrDA: Registered device %s (via-ircc)\n", dev->name);

	/* Initialise the hardware..
	*/
	self->io.speed = 9600;
	via_hw_init(self);
	return 0;
 err_out4:
	dma_free_coherent(&pdev->dev, self->tx_buff.truesize,
			  self->tx_buff.head, self->tx_buff_dma);
 err_out3:
	dma_free_coherent(&pdev->dev, self->rx_buff.truesize,
			  self->rx_buff.head, self->rx_buff_dma);
 err_out2:
	release_region(self->io.fir_base, self->io.fir_ext);
 err_out1:
	pci_set_drvdata(pdev, NULL);
	free_netdev(dev);
	return err;
}

/*
 * Function via_remove_one(pdev)
 *
 *    Close driver instance
 *
 */
static void __devexit via_remove_one(struct pci_dev *pdev)
{
	struct via_ircc_cb *self = pci_get_drvdata(pdev);
	int iobase;

	IRDA_DEBUG(3, "%s()\n", __func__);

	iobase = self->io.fir_base;

	ResetChip(iobase, 5);	//hardware reset.
	/* Remove netdevice */
	unregister_netdev(self->netdev);

	/* Release the PORT that this driver is using */
	IRDA_DEBUG(2, "%s(), Releasing Region %03x\n",
		   __func__, self->io.fir_base);
	release_region(self->io.fir_base, self->io.fir_ext);
	if (self->tx_buff.head)
		dma_free_coherent(&pdev->dev, self->tx_buff.truesize,
				  self->tx_buff.head, self->tx_buff_dma);
	if (self->rx_buff.head)
		dma_free_coherent(&pdev->dev, self->rx_buff.truesize,
				  self->rx_buff.head, self->rx_buff_dma);
	pci_set_drvdata(pdev, NULL);

	free_netdev(self->netdev);

	pci_disable_device(pdev);
}

/*
 * Function via_hw_init(self)
 *
 *    Returns non-negative on success.
 *
 * Formerly via_ircc_setup 
 */
static void via_hw_init(struct via_ircc_cb *self)
{
	int iobase = self->io.fir_base;

	IRDA_DEBUG(3, "%s()\n", __func__);

	SetMaxRxPacketSize(iobase, 0x0fff);	//set to max:4095
	// FIFO Init
	EnRXFIFOReadyInt(iobase, OFF);
	EnRXFIFOHalfLevelInt(iobase, OFF);
	EnTXFIFOHalfLevelInt(iobase, OFF);
	EnTXFIFOUnderrunEOMInt(iobase, ON);
	EnTXFIFOReadyInt(iobase, OFF);
	InvertTX(iobase, OFF);
	InvertRX(iobase, OFF);

	if (ReadLPCReg(0x20) == 0x3c)
		WriteLPCReg(0xF0, 0);	// for VT1211
	/* Int Init */
	EnRXSpecInt(iobase, ON);

	/* The following is basically hwreset */
	/* If this is the case, why not just call hwreset() ? Jean II */
	ResetChip(iobase, 5);
	EnableDMA(iobase, OFF);
	EnableTX(iobase, OFF);
	EnableRX(iobase, OFF);
	EnRXDMA(iobase, OFF);
	EnTXDMA(iobase, OFF);
	RXStart(iobase, OFF);
	TXStart(iobase, OFF);
	InitCard(iobase);
	CommonInit(iobase);
	SIRFilter(iobase, ON);
	SetSIR(iobase, ON);
	CRC16(iobase, ON);
	EnTXCRC(iobase, 0);
	WriteReg(iobase, I_ST_CT_0, 0x00);
	SetBaudRate(iobase, 9600);
	SetPulseWidth(iobase, 12);
	SetSendPreambleCount(iobase, 0);

	self->io.speed = 9600;
	self->st_fifo.len = 0;

	via_ircc_change_dongle_speed(iobase, self->io.speed,
				     self->io.dongle_id);

	WriteReg(iobase, I_ST_CT_0, 0x80);
}

/*
 * Function via_ircc_read_dongle_id (void)
 *
 */
static int via_ircc_read_dongle_id(int iobase)
{
	int dongle_id = 9;	/* Default to IBM */

	IRDA_ERROR("via-ircc: dongle probing not supported, please specify dongle_id module parameter.\n");
	return dongle_id;
}

/*
 * Function via_ircc_change_dongle_speed (iobase, speed, dongle_id)
 *    Change speed of the attach dongle
 *    only implement two type of dongle currently.
 */
static void via_ircc_change_dongle_speed(int iobase, int speed,
					 int dongle_id)
{
	u8 mode = 0;

	/* speed is unused, as we use IsSIROn()/IsMIROn() */
	speed = speed;

	IRDA_DEBUG(1, "%s(): change_dongle_speed to %d for 0x%x, %d\n",
		   __func__, speed, iobase, dongle_id);

	switch (dongle_id) {

		/* Note: The dongle_id's listed here are derived from
		 * nsc-ircc.c */ 

	case 0x08:		/* HP HSDL-2300, HP HSDL-3600/HSDL-3610 */
		UseOneRX(iobase, ON);	// use one RX pin   RX1,RX2
		InvertTX(iobase, OFF);
		InvertRX(iobase, OFF);

		EnRX2(iobase, ON);	//sir to rx2
		EnGPIOtoRX2(iobase, OFF);

		if (IsSIROn(iobase)) {	//sir
			// Mode select Off
			SlowIRRXLowActive(iobase, ON);
			udelay(1000);
			SlowIRRXLowActive(iobase, OFF);
		} else {
			if (IsMIROn(iobase)) {	//mir
				// Mode select On
				SlowIRRXLowActive(iobase, OFF);
				udelay(20);
			} else {	// fir
				if (IsFIROn(iobase)) {	//fir
					// Mode select On
					SlowIRRXLowActive(iobase, OFF);
					udelay(20);
				}
			}
		}
		break;

	case 0x09:		/* IBM31T1100 or Temic TFDS6000/TFDS6500 */
		UseOneRX(iobase, ON);	//use ONE RX....RX1
		InvertTX(iobase, OFF);
		InvertRX(iobase, OFF);	// invert RX pin

		EnRX2(iobase, ON);
		EnGPIOtoRX2(iobase, OFF);
		if (IsSIROn(iobase)) {	//sir
			// Mode select On
			SlowIRRXLowActive(iobase, ON);
			udelay(20);
			// Mode select Off
			SlowIRRXLowActive(iobase, OFF);
		}
		if (IsMIROn(iobase)) {	//mir
			// Mode select On
			SlowIRRXLowActive(iobase, OFF);
			udelay(20);
			// Mode select Off
			SlowIRRXLowActive(iobase, ON);
		} else {	// fir
			if (IsFIROn(iobase)) {	//fir
				// Mode select On
				SlowIRRXLowActive(iobase, OFF);
				// TX On
				WriteTX(iobase, ON);
				udelay(20);
				// Mode select OFF
				SlowIRRXLowActive(iobase, ON);
				udelay(20);
				// TX Off
				WriteTX(iobase, OFF);
			}
		}
		break;

	case 0x0d:
		UseOneRX(iobase, OFF);	// use two RX pin   RX1,RX2
		InvertTX(iobase, OFF);
		InvertRX(iobase, OFF);
		SlowIRRXLowActive(iobase, OFF);
		if (IsSIROn(iobase)) {	//sir
			EnGPIOtoRX2(iobase, OFF);
			WriteGIO(iobase, OFF);
			EnRX2(iobase, OFF);	//sir to rx2
		} else {	// fir mir
			EnGPIOtoRX2(iobase, OFF);
			WriteGIO(iobase, OFF);
			EnRX2(iobase, OFF);	//fir to rx
		}
		break;

	case 0x11:		/* Temic TFDS4500 */

		IRDA_DEBUG(2, "%s: Temic TFDS4500: One RX pin, TX normal, RX inverted.\n", __func__);

		UseOneRX(iobase, ON);	//use ONE RX....RX1
		InvertTX(iobase, OFF);
		InvertRX(iobase, ON);	// invert RX pin
	
		EnRX2(iobase, ON);	//sir to rx2
		EnGPIOtoRX2(iobase, OFF);

		if( IsSIROn(iobase) ){	//sir

			// Mode select On
			SlowIRRXLowActive(iobase, ON);
			udelay(20);
			// Mode select Off
			SlowIRRXLowActive(iobase, OFF);

		} else{
			IRDA_DEBUG(0, "%s: Warning: TFDS4500 not running in SIR mode !\n", __func__);
		}
		break;

	case 0x0ff:		/* Vishay */
		if (IsSIROn(iobase))
			mode = 0;
		else if (IsMIROn(iobase))
			mode = 1;
		else if (IsFIROn(iobase))
			mode = 2;
		else if (IsVFIROn(iobase))
			mode = 5;	//VFIR-16
		SI_SetMode(iobase, mode);
		break;

	default:
		IRDA_ERROR("%s: Error: dongle_id %d unsupported !\n",
			   __func__, dongle_id);
	}
}

/*
 * Function via_ircc_change_speed (self, baud)
 *
 *    Change the speed of the device
 *
 */
static void via_ircc_change_speed(struct via_ircc_cb *self, __u32 speed)
{
	struct net_device *dev = self->netdev;
	u16 iobase;
	u8 value = 0, bTmp;

	iobase = self->io.fir_base;
	/* Update accounting for new speed */
	self->io.speed = speed;
	IRDA_DEBUG(1, "%s: change_speed to %d bps.\n", __func__, speed);

	WriteReg(iobase, I_ST_CT_0, 0x0);

	/* Controller mode sellection */
	switch (speed) {
	case 2400:
	case 9600:
	case 19200:
	case 38400:
	case 57600:
	case 115200:
		value = (115200/speed)-1;
		SetSIR(iobase, ON);
		CRC16(iobase, ON);
		break;
	case 576000:
		/* FIXME: this can't be right, as it's the same as 115200,
		 * and 576000 is MIR, not SIR. */
		value = 0;
		SetSIR(iobase, ON);
		CRC16(iobase, ON);
		break;
	case 1152000:
		value = 0;
		SetMIR(iobase, ON);
		/* FIXME: CRC ??? */
		break;
	case 4000000:
		value = 0;
		SetFIR(iobase, ON);
		SetPulseWidth(iobase, 0);
		SetSendPreambleCount(iobase, 14);
		CRC16(iobase, OFF);
		EnTXCRC(iobase, ON);
		break;
	case 16000000:
		value = 0;
		SetVFIR(iobase, ON);
		/* FIXME: CRC ??? */
		break;
	default:
		value = 0;
		break;
	}

	/* Set baudrate to 0x19[2..7] */
	bTmp = (ReadReg(iobase, I_CF_H_1) & 0x03);
	bTmp |= value << 2;
	WriteReg(iobase, I_CF_H_1, bTmp);

	/* Some dongles may need to be informed about speed changes. */
	via_ircc_change_dongle_speed(iobase, speed, self->io.dongle_id);

	/* Set FIFO size to 64 */
	SetFIFO(iobase, 64);

	/* Enable IR */
	WriteReg(iobase, I_ST_CT_0, 0x80);

	// EnTXFIFOHalfLevelInt(iobase,ON);

	/* Enable some interrupts so we can receive frames */
	//EnAllInt(iobase,ON);

	if (IsSIROn(iobase)) {
		SIRFilter(iobase, ON);
		SIRRecvAny(iobase, ON);
	} else {
		SIRFilter(iobase, OFF);
		SIRRecvAny(iobase, OFF);
	}

	if (speed > 115200) {
		/* Install FIR xmit handler */
		dev->netdev_ops = &via_ircc_fir_ops;
		via_ircc_dma_receive(self);
	} else {
		/* Install SIR xmit handler */
		dev->netdev_ops = &via_ircc_sir_ops;
	}
	netif_wake_queue(dev);
}

/*
 * Function via_ircc_hard_xmit (skb, dev)
 *
 *    Transmit the frame!
 *
 */
static netdev_tx_t via_ircc_hard_xmit_sir(struct sk_buff *skb,
						struct net_device *dev)
{
	struct via_ircc_cb *self;
	unsigned long flags;
	u16 iobase;
	__u32 speed;

	self = netdev_priv(dev);
	IRDA_ASSERT(self != NULL, return NETDEV_TX_OK;);
	iobase = self->io.fir_base;

	netif_stop_queue(dev);
	/* Check if we need to change the speed */
	speed = irda_get_next_speed(skb);
	if ((speed != self->io.speed) && (speed != -1)) {
		/* Check for empty frame */
		if (!skb->len) {
			via_ircc_change_speed(self, speed);
			dev->trans_start = jiffies;
			dev_kfree_skb(skb);
			return NETDEV_TX_OK;
		} else
			self->new_speed = speed;
	}
	InitCard(iobase);
	CommonInit(iobase);
	SIRFilter(iobase, ON);
	SetSIR(iobase, ON);
	CRC16(iobase, ON);
	EnTXCRC(iobase, 0);
	WriteReg(iobase, I_ST_CT_0, 0x00);

	spin_lock_irqsave(&self->lock, flags);
	self->tx_buff.data = self->tx_buff.head;
	self->tx_buff.len =
	    async_wrap_skb(skb, self->tx_buff.data,
			   self->tx_buff.truesize);

	dev->stats.tx_bytes += self->tx_buff.len;
	/* Send this frame with old speed */
	SetBaudRate(iobase, self->io.speed);
	SetPulseWidth(iobase, 12);
	SetSendPreambleCount(iobase, 0);
	WriteReg(iobase, I_ST_CT_0, 0x80);

	EnableTX(iobase, ON);
	EnableRX(iobase, OFF);

	ResetChip(iobase, 0);
	ResetChip(iobase, 1);
	ResetChip(iobase, 2);
	ResetChip(iobase, 3);
	ResetChip(iobase, 4);

	EnAllInt(iobase, ON);
	EnTXDMA(iobase, ON);
	EnRXDMA(iobase, OFF);

	irda_setup_dma(self->io.dma, self->tx_buff_dma, self->tx_buff.len,
		       DMA_TX_MODE);

	SetSendByte(iobase, self->tx_buff.len);
	RXStart(iobase, OFF);
	TXStart(iobase, ON);

	dev->trans_start = jiffies;
	spin_unlock_irqrestore(&self->lock, flags);
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static netdev_tx_t via_ircc_hard_xmit_fir(struct sk_buff *skb,
						struct net_device *dev)
{
	struct via_ircc_cb *self;
	u16 iobase;
	__u32 speed;
	unsigned long flags;

	self = netdev_priv(dev);
	iobase = self->io.fir_base;

	if (self->st_fifo.len)
		return NETDEV_TX_OK;
	if (self->chip_id == 0x3076)
		iodelay(1500);
	else
		udelay(1500);
	netif_stop_queue(dev);
	speed = irda_get_next_speed(skb);
	if ((speed != self->io.speed) && (speed != -1)) {
		if (!skb->len) {
			via_ircc_change_speed(self, speed);
			dev->trans_start = jiffies;
			dev_kfree_skb(skb);
			return NETDEV_TX_OK;
		} else
			self->new_speed = speed;
	}
	spin_lock_irqsave(&self->lock, flags);
	self->tx_fifo.queue[self->tx_fifo.free].start = self->tx_fifo.tail;
	self->tx_fifo.queue[self->tx_fifo.free].len = skb->len;

	self->tx_fifo.tail += skb->len;
	dev->stats.tx_bytes += skb->len;
	skb_copy_from_linear_data(skb,
		      self->tx_fifo.queue[self->tx_fifo.free].start, skb->len);
	self->tx_fifo.len++;
	self->tx_fifo.free++;
//F01   if (self->tx_fifo.len == 1) {
	via_ircc_dma_xmit(self, iobase);
//F01   }
//F01   if (self->tx_fifo.free < (MAX_TX_WINDOW -1 )) netif_wake_queue(self->netdev);
	dev->trans_start = jiffies;
	dev_kfree_skb(skb);
	spin_unlock_irqrestore(&self->lock, flags);
	return NETDEV_TX_OK;

}

static int via_ircc_dma_xmit(struct via_ircc_cb *self, u16 iobase)
{
	EnTXDMA(iobase, OFF);
	self->io.direction = IO_XMIT;
	EnPhys(iobase, ON);
	EnableTX(iobase, ON);
	EnableRX(iobase, OFF);
	ResetChip(iobase, 0);
	ResetChip(iobase, 1);
	ResetChip(iobase, 2);
	ResetChip(iobase, 3);
	ResetChip(iobase, 4);
	EnAllInt(iobase, ON);
	EnTXDMA(iobase, ON);
	EnRXDMA(iobase, OFF);
	irda_setup_dma(self->io.dma,
		       ((u8 *)self->tx_fifo.queue[self->tx_fifo.ptr].start -
			self->tx_buff.head) + self->tx_buff_dma,
		       self->tx_fifo.queue[self->tx_fifo.ptr].len, DMA_TX_MODE);
	IRDA_DEBUG(1, "%s: tx_fifo.ptr=%x,len=%x,tx_fifo.len=%x..\n",
		   __func__, self->tx_fifo.ptr,
		   self->tx_fifo.queue[self->tx_fifo.ptr].len,
		   self->tx_fifo.len);

	SetSendByte(iobase, self->tx_fifo.queue[self->tx_fifo.ptr].len);
	RXStart(iobase, OFF);
	TXStart(iobase, ON);
	return 0;

}

/*
 * Function via_ircc_dma_xmit_complete (self)
 *
 *    The transfer of a frame in finished. This function will only be called 
 *    by the interrupt handler
 *
 */
static int via_ircc_dma_xmit_complete(struct via_ircc_cb *self)
{
	int iobase;
	int ret = TRUE;
	u8 Tx_status;

	IRDA_DEBUG(3, "%s()\n", __func__);

	iobase = self->io.fir_base;
	/* Disable DMA */
//      DisableDmaChannel(self->io.dma);
	/* Check for underrrun! */
	/* Clear bit, by writing 1 into it */
	Tx_status = GetTXStatus(iobase);
	if (Tx_status & 0x08) {
		self->netdev->stats.tx_errors++;
		self->netdev->stats.tx_fifo_errors++;
		hwreset(self);
// how to clear underrrun ?
	} else {
		self->netdev->stats.tx_packets++;
		ResetChip(iobase, 3);
		ResetChip(iobase, 4);
	}
	/* Check if we need to change the speed */
	if (self->new_speed) {
		via_ircc_change_speed(self, self->new_speed);
		self->new_speed = 0;
	}

	/* Finished with this frame, so prepare for next */
	if (IsFIROn(iobase)) {
		if (self->tx_fifo.len) {
			self->tx_fifo.len--;
			self->tx_fifo.ptr++;
		}
	}
	IRDA_DEBUG(1,
		   "%s: tx_fifo.len=%x ,tx_fifo.ptr=%x,tx_fifo.free=%x...\n",
		   __func__,
		   self->tx_fifo.len, self->tx_fifo.ptr, self->tx_fifo.free);
/* F01_S
	// Any frames to be sent back-to-back? 
	if (self->tx_fifo.len) {
		// Not finished yet! 
	  	via_ircc_dma_xmit(self, iobase);
		ret = FALSE;
	} else { 
F01_E*/
	// Reset Tx FIFO info 
	self->tx_fifo.len = self->tx_fifo.ptr = self->tx_fifo.free = 0;
	self->tx_fifo.tail = self->tx_buff.head;
//F01   }

	// Make sure we have room for more frames 
//F01   if (self->tx_fifo.free < (MAX_TX_WINDOW -1 )) {
	// Not busy transmitting anymore 
	// Tell the network layer, that we can accept more frames 
	netif_wake_queue(self->netdev);
//F01   }
	return ret;
}

/*
 * Function via_ircc_dma_receive (self)
 *
 *    Set configuration for receive a frame.
 *
 */
static int via_ircc_dma_receive(struct via_ircc_cb *self)
{
	int iobase;

	iobase = self->io.fir_base;

	IRDA_DEBUG(3, "%s()\n", __func__);

	self->tx_fifo.len = self->tx_fifo.ptr = self->tx_fifo.free = 0;
	self->tx_fifo.tail = self->tx_buff.head;
	self->RxDataReady = 0;
	self->io.direction = IO_RECV;
	self->rx_buff.data = self->rx_buff.head;
	self->st_fifo.len = self->st_fifo.pending_bytes = 0;
	self->st_fifo.tail = self->st_fifo.head = 0;

	EnPhys(iobase, ON);
	EnableTX(iobase, OFF);
	EnableRX(iobase, ON);

	ResetChip(iobase, 0);
	ResetChip(iobase, 1);
	ResetChip(iobase, 2);
	ResetChip(iobase, 3);
	ResetChip(iobase, 4);

	EnAllInt(iobase, ON);
	EnTXDMA(iobase, OFF);
	EnRXDMA(iobase, ON);
	irda_setup_dma(self->io.dma2, self->rx_buff_dma,
		  self->rx_buff.truesize, DMA_RX_MODE);
	TXStart(iobase, OFF);
	RXStart(iobase, ON);

	return 0;
}

/*
 * Function via_ircc_dma_receive_complete (self)
 *
 *    Controller Finished with receiving frames,
 *    and this routine is call by ISR
 *    
 */
static int via_ircc_dma_receive_complete(struct via_ircc_cb *self,
					 int iobase)
{
	struct st_fifo *st_fifo;
	struct sk_buff *skb;
	int len, i;
	u8 status = 0;

	iobase = self->io.fir_base;
	st_fifo = &self->st_fifo;

	if (self->io.speed < 4000000) {	//Speed below FIR
		len = GetRecvByte(iobase, self);
		skb = dev_alloc_skb(len + 1);
		if (skb == NULL)
			return FALSE;
		// Make sure IP header gets aligned 
		skb_reserve(skb, 1);
		skb_put(skb, len - 2);
		if (self->chip_id == 0x3076) {
			for (i = 0; i < len - 2; i++)
				skb->data[i] = self->rx_buff.data[i * 2];
		} else {
			if (self->chip_id == 0x3096) {
				for (i = 0; i < len - 2; i++)
					skb->data[i] =
					    self->rx_buff.data[i];
			}
		}
		// Move to next frame 
		self->rx_buff.data += len;
		self->netdev->stats.rx_bytes += len;
		self->netdev->stats.rx_packets++;
		skb->dev = self->netdev;
		skb_reset_mac_header(skb);
		skb->protocol = htons(ETH_P_IRDA);
		netif_rx(skb);
		return TRUE;
	}

	else {			//FIR mode
		len = GetRecvByte(iobase, self);
		if (len == 0)
			return TRUE;	//interrupt only, data maybe move by RxT  
		if (((len - 4) < 2) || ((len - 4) > 2048)) {
			IRDA_DEBUG(1, "%s(): Trouble:len=%x,CurCount=%x,LastCount=%x..\n",
				   __func__, len, RxCurCount(iobase, self),
				   self->RxLastCount);
			hwreset(self);
			return FALSE;
		}
		IRDA_DEBUG(2, "%s(): fifo.len=%x,len=%x,CurCount=%x..\n",
			   __func__,
			   st_fifo->len, len - 4, RxCurCount(iobase, self));

		st_fifo->entries[st_fifo->tail].status = status;
		st_fifo->entries[st_fifo->tail].len = len;
		st_fifo->pending_bytes += len;
		st_fifo->tail++;
		st_fifo->len++;
		if (st_fifo->tail > MAX_RX_WINDOW)
			st_fifo->tail = 0;
		self->RxDataReady = 0;

		// It maybe have MAX_RX_WINDOW package receive by
		// receive_complete before Timer IRQ
/* F01_S
          if (st_fifo->len < (MAX_RX_WINDOW+2 )) { 
		  RXStart(iobase,ON);
	  	  SetTimer(iobase,4);
	  }
	  else	  { 
F01_E */
		EnableRX(iobase, OFF);
		EnRXDMA(iobase, OFF);
		RXStart(iobase, OFF);
//F01_S
		// Put this entry back in fifo 
		if (st_fifo->head > MAX_RX_WINDOW)
			st_fifo->head = 0;
		status = st_fifo->entries[st_fifo->head].status;
		len = st_fifo->entries[st_fifo->head].len;
		st_fifo->head++;
		st_fifo->len--;

		skb = dev_alloc_skb(len + 1 - 4);
		/*
		 * if frame size, data ptr, or skb ptr are wrong, then get next
		 * entry.
		 */
		if ((skb == NULL) || (skb->data == NULL) ||
		    (self->rx_buff.data == NULL) || (len < 6)) {
			self->netdev->stats.rx_dropped++;
			kfree_skb(skb);
			return TRUE;
		}
		skb_reserve(skb, 1);
		skb_put(skb, len - 4);

		skb_copy_to_linear_data(skb, self->rx_buff.data, len - 4);
		IRDA_DEBUG(2, "%s(): len=%x.rx_buff=%p\n", __func__,
			   len - 4, self->rx_buff.data);

		// Move to next frame 
		self->rx_buff.data += len;
		self->netdev->stats.rx_bytes += len;
		self->netdev->stats.rx_packets++;
		skb->dev = self->netdev;
		skb_reset_mac_header(skb);
		skb->protocol = htons(ETH_P_IRDA);
		netif_rx(skb);

//F01_E
	}			//FIR
	return TRUE;

}

/*
 * if frame is received , but no INT ,then use this routine to upload frame.
 */
static int upload_rxdata(struct via_ircc_cb *self, int iobase)
{
	struct sk_buff *skb;
	int len;
	struct st_fifo *st_fifo;
	st_fifo = &self->st_fifo;

	len = GetRecvByte(iobase, self);

	IRDA_DEBUG(2, "%s(): len=%x\n", __func__, len);

	if ((len - 4) < 2) {
		self->netdev->stats.rx_dropped++;
		return FALSE;
	}

	skb = dev_alloc_skb(len + 1);
	if (skb == NULL) {
		self->netdev->stats.rx_dropped++;
		return FALSE;
	}
	skb_reserve(skb, 1);
	skb_put(skb, len - 4 + 1);
	skb_copy_to_linear_data(skb, self->rx_buff.data, len - 4 + 1);
	st_fifo->tail++;
	st_fifo->len++;
	if (st_fifo->tail > MAX_RX_WINDOW)
		st_fifo->tail = 0;
	// Move to next frame 
	self->rx_buff.data += len;
	self->netdev->stats.rx_bytes += len;
	self->netdev->stats.rx_packets++;
	skb->dev = self->netdev;
	skb_reset_mac_header(skb);
	skb->protocol = htons(ETH_P_IRDA);
	netif_rx(skb);
	if (st_fifo->len < (MAX_RX_WINDOW + 2)) {
		RXStart(iobase, ON);
	} else {
		EnableRX(iobase, OFF);
		EnRXDMA(iobase, OFF);
		RXStart(iobase, OFF);
	}
	return TRUE;
}

/*
 * Implement back to back receive , use this routine to upload data.
 */

static int RxTimerHandler(struct via_ircc_cb *self, int iobase)
{
	struct st_fifo *st_fifo;
	struct sk_buff *skb;
	int len;
	u8 status;

	st_fifo = &self->st_fifo;

	if (CkRxRecv(iobase, self)) {
		// if still receiving ,then return ,don't upload frame 
		self->RetryCount = 0;
		SetTimer(iobase, 20);
		self->RxDataReady++;
		return FALSE;
	} else
		self->RetryCount++;

	if ((self->RetryCount >= 1) ||
	    ((st_fifo->pending_bytes + 2048) > self->rx_buff.truesize) ||
	    (st_fifo->len >= (MAX_RX_WINDOW))) {
		while (st_fifo->len > 0) {	//upload frame
			// Put this entry back in fifo 
			if (st_fifo->head > MAX_RX_WINDOW)
				st_fifo->head = 0;
			status = st_fifo->entries[st_fifo->head].status;
			len = st_fifo->entries[st_fifo->head].len;
			st_fifo->head++;
			st_fifo->len--;

			skb = dev_alloc_skb(len + 1 - 4);
			/*
			 * if frame size, data ptr, or skb ptr are wrong,
			 * then get next entry.
			 */
			if ((skb == NULL) || (skb->data == NULL) ||
			    (self->rx_buff.data == NULL) || (len < 6)) {
				self->netdev->stats.rx_dropped++;
				continue;
			}
			skb_reserve(skb, 1);
			skb_put(skb, len - 4);
			skb_copy_to_linear_data(skb, self->rx_buff.data, len - 4);

			IRDA_DEBUG(2, "%s(): len=%x.head=%x\n", __func__,
				   len - 4, st_fifo->head);

			// Move to next frame 
			self->rx_buff.data += len;
			self->netdev->stats.rx_bytes += len;
			self->netdev->stats.rx_packets++;
			skb->dev = self->netdev;
			skb_reset_mac_header(skb);
			skb->protocol = htons(ETH_P_IRDA);
			netif_rx(skb);
		}		//while
		self->RetryCount = 0;

		IRDA_DEBUG(2,
			   "%s(): End of upload HostStatus=%x,RxStatus=%x\n",
			   __func__,
			   GetHostStatus(iobase), GetRXStatus(iobase));

		/*
		 * if frame is receive complete at this routine ,then upload
		 * frame.
		 */
		if ((GetRXStatus(iobase) & 0x10) &&
		    (RxCurCount(iobase, self) != self->RxLastCount)) {
			upload_rxdata(self, iobase);
			if (irda_device_txqueue_empty(self->netdev))
				via_ircc_dma_receive(self);
		}
	}			// timer detect complete
	else
		SetTimer(iobase, 4);
	return TRUE;

}



/*
 * Function via_ircc_interrupt (irq, dev_id)
 *
 *    An interrupt from the chip has arrived. Time to do some work
 *
 */
static irqreturn_t via_ircc_interrupt(int dummy, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct via_ircc_cb *self = netdev_priv(dev);
	int iobase;
	u8 iHostIntType, iRxIntType, iTxIntType;

	iobase = self->io.fir_base;
	spin_lock(&self->lock);
	iHostIntType = GetHostStatus(iobase);

	IRDA_DEBUG(4, "%s(): iHostIntType %02x:  %s %s %s  %02x\n",
		   __func__, iHostIntType,
		   (iHostIntType & 0x40) ? "Timer" : "",
		   (iHostIntType & 0x20) ? "Tx" : "",
		   (iHostIntType & 0x10) ? "Rx" : "",
		   (iHostIntType & 0x0e) >> 1);

	if ((iHostIntType & 0x40) != 0) {	//Timer Event
		self->EventFlag.TimeOut++;
		ClearTimerInt(iobase, 1);
		if (self->io.direction == IO_XMIT) {
			via_ircc_dma_xmit(self, iobase);
		}
		if (self->io.direction == IO_RECV) {
			/*
			 * frame ready hold too long, must reset.
			 */
			if (self->RxDataReady > 30) {
				hwreset(self);
				if (irda_device_txqueue_empty(self->netdev)) {
					via_ircc_dma_receive(self);
				}
			} else {	// call this to upload frame.
				RxTimerHandler(self, iobase);
			}
		}		//RECV
	}			//Timer Event
	if ((iHostIntType & 0x20) != 0) {	//Tx Event
		iTxIntType = GetTXStatus(iobase);

		IRDA_DEBUG(4, "%s(): iTxIntType %02x:  %s %s %s %s\n",
			   __func__, iTxIntType,
			   (iTxIntType & 0x08) ? "FIFO underr." : "",
			   (iTxIntType & 0x04) ? "EOM" : "",
			   (iTxIntType & 0x02) ? "FIFO ready" : "",
			   (iTxIntType & 0x01) ? "Early EOM" : "");

		if (iTxIntType & 0x4) {
			self->EventFlag.EOMessage++;	// read and will auto clean
			if (via_ircc_dma_xmit_complete(self)) {
				if (irda_device_txqueue_empty
				    (self->netdev)) {
					via_ircc_dma_receive(self);
				}
			} else {
				self->EventFlag.Unknown++;
			}
		}		//EOP
	}			//Tx Event
	//----------------------------------------
	if ((iHostIntType & 0x10) != 0) {	//Rx Event
		/* Check if DMA has finished */
		iRxIntType = GetRXStatus(iobase);

		IRDA_DEBUG(4, "%s(): iRxIntType %02x:  %s %s %s %s %s %s %s\n",
			   __func__, iRxIntType,
			   (iRxIntType & 0x80) ? "PHY err."	: "",
			   (iRxIntType & 0x40) ? "CRC err"	: "",
			   (iRxIntType & 0x20) ? "FIFO overr."	: "",
			   (iRxIntType & 0x10) ? "EOF"		: "",
			   (iRxIntType & 0x08) ? "RxData"	: "",
			   (iRxIntType & 0x02) ? "RxMaxLen"	: "",
			   (iRxIntType & 0x01) ? "SIR bad"	: "");
		if (!iRxIntType)
			IRDA_DEBUG(3, "%s(): RxIRQ =0\n", __func__);

		if (iRxIntType & 0x10) {
			if (via_ircc_dma_receive_complete(self, iobase)) {
//F01       if(!(IsFIROn(iobase)))  via_ircc_dma_receive(self);
				via_ircc_dma_receive(self);
			}
		}		// No ERR     
		else {		//ERR
			IRDA_DEBUG(4, "%s(): RxIRQ ERR:iRxIntType=%x,HostIntType=%x,CurCount=%x,RxLastCount=%x_____\n",
				   __func__, iRxIntType, iHostIntType,
				   RxCurCount(iobase, self),
				   self->RxLastCount);

			if (iRxIntType & 0x20) {	//FIFO OverRun ERR
				ResetChip(iobase, 0);
				ResetChip(iobase, 1);
			} else {	//PHY,CRC ERR

				if (iRxIntType != 0x08)
					hwreset(self);	//F01
			}
			via_ircc_dma_receive(self);
		}		//ERR

	}			//Rx Event
	spin_unlock(&self->lock);
	return IRQ_RETVAL(iHostIntType);
}

static void hwreset(struct via_ircc_cb *self)
{
	int iobase;
	iobase = self->io.fir_base;

	IRDA_DEBUG(3, "%s()\n", __func__);

	ResetChip(iobase, 5);
	EnableDMA(iobase, OFF);
	EnableTX(iobase, OFF);
	EnableRX(iobase, OFF);
	EnRXDMA(iobase, OFF);
	EnTXDMA(iobase, OFF);
	RXStart(iobase, OFF);
	TXStart(iobase, OFF);
	InitCard(iobase);
	CommonInit(iobase);
	SIRFilter(iobase, ON);
	SetSIR(iobase, ON);
	CRC16(iobase, ON);
	EnTXCRC(iobase, 0);
	WriteReg(iobase, I_ST_CT_0, 0x00);
	SetBaudRate(iobase, 9600);
	SetPulseWidth(iobase, 12);
	SetSendPreambleCount(iobase, 0);
	WriteReg(iobase, I_ST_CT_0, 0x80);

	/* Restore speed. */
	via_ircc_change_speed(self, self->io.speed);

	self->st_fifo.len = 0;
}

/*
 * Function via_ircc_is_receiving (self)
 *
 *    Return TRUE is we are currently receiving a frame
 *
 */
static int via_ircc_is_receiving(struct via_ircc_cb *self)
{
	int status = FALSE;
	int iobase;

	IRDA_ASSERT(self != NULL, return FALSE;);

	iobase = self->io.fir_base;
	if (CkRxRecv(iobase, self))
		status = TRUE;

	IRDA_DEBUG(2, "%s(): status=%x....\n", __func__, status);

	return status;
}


/*
 * Function via_ircc_net_open (dev)
 *
 *    Start the device
 *
 */
static int via_ircc_net_open(struct net_device *dev)
{
	struct via_ircc_cb *self;
	int iobase;
	char hwname[32];

	IRDA_DEBUG(3, "%s()\n", __func__);

	IRDA_ASSERT(dev != NULL, return -1;);
	self = netdev_priv(dev);
	dev->stats.rx_packets = 0;
	IRDA_ASSERT(self != NULL, return 0;);
	iobase = self->io.fir_base;
	if (request_irq(self->io.irq, via_ircc_interrupt, 0, dev->name, dev)) {
		IRDA_WARNING("%s, unable to allocate irq=%d\n", driver_name,
			     self->io.irq);
		return -EAGAIN;
	}
	/*
	 * Always allocate the DMA channel after the IRQ, and clean up on 
	 * failure.
	 */
	if (request_dma(self->io.dma, dev->name)) {
		IRDA_WARNING("%s, unable to allocate dma=%d\n", driver_name,
			     self->io.dma);
		free_irq(self->io.irq, self);
		return -EAGAIN;
	}
	if (self->io.dma2 != self->io.dma) {
		if (request_dma(self->io.dma2, dev->name)) {
			IRDA_WARNING("%s, unable to allocate dma2=%d\n",
				     driver_name, self->io.dma2);
			free_irq(self->io.irq, self);
			free_dma(self->io.dma);
			return -EAGAIN;
		}
	}


	/* turn on interrupts */
	EnAllInt(iobase, ON);
	EnInternalLoop(iobase, OFF);
	EnExternalLoop(iobase, OFF);

	/* */
	via_ircc_dma_receive(self);

	/* Ready to play! */
	netif_start_queue(dev);

	/* 
	 * Open new IrLAP layer instance, now that everything should be
	 * initialized properly 
	 */
	sprintf(hwname, "VIA @ 0x%x", iobase);
	self->irlap = irlap_open(dev, &self->qos, hwname);

	self->RxLastCount = 0;

	return 0;
}

/*
 * Function via_ircc_net_close (dev)
 *
 *    Stop the device
 *
 */
static int via_ircc_net_close(struct net_device *dev)
{
	struct via_ircc_cb *self;
	int iobase;

	IRDA_DEBUG(3, "%s()\n", __func__);

	IRDA_ASSERT(dev != NULL, return -1;);
	self = netdev_priv(dev);
	IRDA_ASSERT(self != NULL, return 0;);

	/* Stop device */
	netif_stop_queue(dev);
	/* Stop and remove instance of IrLAP */
	if (self->irlap)
		irlap_close(self->irlap);
	self->irlap = NULL;
	iobase = self->io.fir_base;
	EnTXDMA(iobase, OFF);
	EnRXDMA(iobase, OFF);
	DisableDmaChannel(self->io.dma);

	/* Disable interrupts */
	EnAllInt(iobase, OFF);
	free_irq(self->io.irq, dev);
	free_dma(self->io.dma);
	if (self->io.dma2 != self->io.dma)
		free_dma(self->io.dma2);

	return 0;
}

/*
 * Function via_ircc_net_ioctl (dev, rq, cmd)
 *
 *    Process IOCTL commands for this device
 *
 */
static int via_ircc_net_ioctl(struct net_device *dev, struct ifreq *rq,
			      int cmd)
{
	struct if_irda_req *irq = (struct if_irda_req *) rq;
	struct via_ircc_cb *self;
	unsigned long flags;
	int ret = 0;

	IRDA_ASSERT(dev != NULL, return -1;);
	self = netdev_priv(dev);
	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_DEBUG(1, "%s(), %s, (cmd=0x%X)\n", __func__, dev->name,
		   cmd);
	/* Disable interrupts & save flags */
	spin_lock_irqsave(&self->lock, flags);
	switch (cmd) {
	case SIOCSBANDWIDTH:	/* Set bandwidth */
		if (!capable(CAP_NET_ADMIN)) {
			ret = -EPERM;
			goto out;
		}
		via_ircc_change_speed(self, irq->ifr_baudrate);
		break;
	case SIOCSMEDIABUSY:	/* Set media busy */
		if (!capable(CAP_NET_ADMIN)) {
			ret = -EPERM;
			goto out;
		}
		irda_device_set_media_busy(self->netdev, TRUE);
		break;
	case SIOCGRECEIVING:	/* Check if we are receiving right now */
		irq->ifr_receiving = via_ircc_is_receiving(self);
		break;
	default:
		ret = -EOPNOTSUPP;
	}
      out:
	spin_unlock_irqrestore(&self->lock, flags);
	return ret;
}

MODULE_AUTHOR("VIA Technologies,inc");
MODULE_DESCRIPTION("VIA IrDA Device Driver");
MODULE_LICENSE("GPL");

module_init(via_ircc_init);
module_exit(via_ircc_cleanup);
