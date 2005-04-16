/*********************************************************************
 *                
 * Filename:      toshoboe.h
 * Version:       2.16
 * Description:   Driver for the Toshiba OBOE (or type-O or 701)
 *                FIR Chipset, also supports the DONAUOBOE (type-DO
 *                or d01) FIR chipset which as far as I know is
 *                register compatible.
 * Status:        Experimental.
 * Author:        James McKenzie <james@fishsoup.dhs.org>
 * Created at:    Sat May 8  12:35:27 1999
 * Modified: 2.16 Martin Lucina <mato@kotelna.sk>
 * Modified: 2.16 Sat Jun 22 18:54:29 2002 (sync headers)
 * Modified: 2.17 Christian Gennerat <christian.gennerat@polytechnique.org>
 * Modified: 2.17 jeu sep 12 08:50:20 2002 (add lock to be used by spinlocks)
 * 
 *     Copyright (c) 1999 James McKenzie, All Rights Reserved.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither James McKenzie nor Cambridge University admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 * 
 *     Applicable Models : Libretto 100/110CT and many more.
 *     Toshiba refers to this chip as the type-O IR port,
 *     or the type-DO IR port.
 *
 * IrDA chip set list from Toshiba Computer Engineering Corp.
 * model			method	maker	controler		Version 
 * Portege 320CT	FIR,SIR Toshiba Oboe(Triangle) 
 * Portege 3010CT	FIR,SIR Toshiba Oboe(Sydney) 
 * Portege 3015CT	FIR,SIR Toshiba Oboe(Sydney) 
 * Portege 3020CT	FIR,SIR Toshiba Oboe(Sydney) 
 * Portege 7020CT	FIR,SIR ?		?
 * 
 * Satell. 4090XCDT	FIR,SIR ?		?
 * 
 * Libretto 100CT	FIR,SIR Toshiba Oboe 
 * Libretto 1000CT	FIR,SIR Toshiba Oboe 
 * 
 * TECRA750DVD		FIR,SIR Toshiba Oboe(Triangle)	REV ID=14h 
 * TECRA780			FIR,SIR Toshiba Oboe(Sandlot)	REV ID=32h,33h 
 * TECRA750CDT		FIR,SIR Toshiba Oboe(Triangle)	REV ID=13h,14h 
 * TECRA8000		FIR,SIR Toshiba Oboe(ISKUR)		REV ID=23h 
 * 
 ********************************************************************/

/* The documentation for this chip is allegedly released         */
/* However I have not seen it, not have I managed to contact     */
/* anyone who has. HOWEVER the chip bears a striking resemblence */
/* to the IrDA controller in the Toshiba RISC TMPR3922 chip      */
/* the documentation for this is freely available at             */
/* http://www.toshiba.com/taec/components/Generic/TMPR3922.shtml */
/* The mapping between the registers in that document and the    */
/* Registers in the 701 oboe chip are as follows    */


/* 3922 reg     701 regs, by bit numbers                        */
/*               7- 0  15- 8  24-16  31-25                      */
/* $28            0x0    0x1                                    */
/* $2c                                     SEE NOTE 1           */
/* $30            0x6    0x7                                    */
/* $34            0x8    0x9               SEE NOTE 2           */
/* $38           0x10   0x11                                    */
/* $3C                   0xe               SEE NOTE 3           */
/* $40           0x12   0x13                                    */
/* $44           0x14   0x15                                    */
/* $48           0x16   0x17                                    */
/* $4c           0x18   0x19                                    */
/* $50           0x1a   0x1b                                    */

/* FIXME: could be 0x1b 0x1a here */

/* $54           0x1d   0x1c                                    */
/* $5C           0xf                       SEE NOTE 4           */
/* $130                                    SEE NOTE 5           */
/* $134                                    SEE NOTE 6           */
/*                                                              */
/* NOTES:                                                       */
/* 1. The pointer to ring is packed in most unceremoniusly      */
/*    701 Register      Address bits    (A9-A0 must be zero)    */
/*            0x4:      A17 A16 A15 A14 A13 A12 A11 A10         */
/*            0x5:      A25 A24 A23 A22 A21 A20 A19 A18         */
/*            0x2:        0   0 A31 A30 A29 A28 A27 A26         */
/*                                                              */
/* 2. The M$ drivers do a write 0x1 to 0x9, however the 3922    */
/*    documentation would suggest that a write of 0x1 to 0x8    */
/*    would be more appropriate.                                */
/*                                                              */
/* 3. This assignment is tenuous at best, register 0xe seems to */
/*    have bits arranged 0 0 0 R/W R/W R/W R/W R/W              */
/*    if either of the lower two bits are set the chip seems to */
/*    switch off                                                */
/*                                                              */
/* 4. Bits 7-4 seem to be different 4 seems just to be generic  */
/*    receiver busy flag                                        */
/*                                                              */
/* 5. and 6. The IER and ISR have a different bit assignment    */
/*    The lower three bits of both read back as ones            */
/* ISR is register 0xc, IER is register 0xd                     */
/*           7      6      5      4      3      2      1      0 */
/* 0xc: TxDone RxDone TxUndr RxOver SipRcv      1      1      1 */
/* 0xd: TxDone RxDone TxUndr RxOver SipRcv      1      1      1 */
/* TxDone xmitt done (generated only if generate interrupt bit  */
/*   is set in the ring)                                        */
/* RxDone recv completed (or other recv condition if you set it */
/*   up                                                         */
/* TxUnder underflow in Transmit FIFO                           */
/* RxOver  overflow in Recv FIFO                                */
/* SipRcv  received serial gap  (or other condition you set)    */
/* Interrupts are enabled by writing a one to the IER register  */
/* Interrupts are cleared by writting a one to the ISR register */
/*                                                              */
/* 6. The remaining registers: 0x6 and 0x3 appear to be         */
/*    reserved parts of 16 or 32 bit registersthe remainder     */
/*    0xa 0xb 0x1e 0x1f could possibly be (by their behaviour)  */
/*    the Unicast Filter register at $58.                       */
/*                                                              */
/* 7. While the core obviously expects 32 bit accesses all the  */
/*    M$ drivers do 8 bit accesses, infact the Miniport ones    */
/*    write and read back the byte serveral times (why?)        */


#ifndef TOSHOBOE_H
#define TOSHOBOE_H

/* Registers */

#define OBOE_IO_EXTENT	0x1f

/*Receive and transmit slot pointers */
#define OBOE_REG(i)	(i+(self->base))
#define OBOE_RXSLOT	OBOE_REG(0x0)
#define OBOE_TXSLOT	OBOE_REG(0x1)
#define OBOE_SLOT_MASK	0x3f

#define OBOE_TXRING_OFFSET		0x200
#define OBOE_TXRING_OFFSET_IN_SLOTS	0x40

/*pointer to the ring */
#define OBOE_RING_BASE0	OBOE_REG(0x4)
#define OBOE_RING_BASE1	OBOE_REG(0x5)
#define OBOE_RING_BASE2	OBOE_REG(0x2)
#define OBOE_RING_BASE3	OBOE_REG(0x3)

/*Number of slots in the ring */
#define OBOE_RING_SIZE  OBOE_REG(0x7)
#define OBOE_RING_SIZE_RX4	0x00
#define OBOE_RING_SIZE_RX8	0x01
#define OBOE_RING_SIZE_RX16	0x03
#define OBOE_RING_SIZE_RX32	0x07
#define OBOE_RING_SIZE_RX64	0x0f
#define OBOE_RING_SIZE_TX4	0x00
#define OBOE_RING_SIZE_TX8	0x10
#define OBOE_RING_SIZE_TX16	0x30
#define OBOE_RING_SIZE_TX32	0x70
#define OBOE_RING_SIZE_TX64	0xf0

#define OBOE_RING_MAX_SIZE	64

/*Causes the gubbins to re-examine the ring */
#define OBOE_PROMPT	OBOE_REG(0x9)
#define OBOE_PROMPT_BIT		0x1

/* Interrupt Status Register */
#define OBOE_ISR	OBOE_REG(0xc)
/* Interrupt Enable Register */
#define OBOE_IER	OBOE_REG(0xd)
/* Interrupt bits for IER and ISR */
#define OBOE_INT_TXDONE		0x80
#define OBOE_INT_RXDONE		0x40
#define OBOE_INT_TXUNDER	0x20
#define OBOE_INT_RXOVER		0x10
#define OBOE_INT_SIP		0x08
#define OBOE_INT_MASK		0xf8

/*Reset Register */
#define OBOE_CONFIG1	OBOE_REG(0xe)
#define OBOE_CONFIG1_RST	0x01
#define OBOE_CONFIG1_DISABLE	0x02
#define OBOE_CONFIG1_4		0x08
#define OBOE_CONFIG1_8		0x08

#define OBOE_CONFIG1_ON		0x8
#define OBOE_CONFIG1_RESET	0xf
#define OBOE_CONFIG1_OFF	0xe

#define OBOE_STATUS	OBOE_REG(0xf)
#define OBOE_STATUS_RXBUSY	0x10
#define OBOE_STATUS_FIRRX	0x04
#define OBOE_STATUS_MIRRX	0x02
#define OBOE_STATUS_SIRRX	0x01


/*Speed control registers */
#define OBOE_CONFIG0L	OBOE_REG(0x10)
#define OBOE_CONFIG0H	OBOE_REG(0x11)

#define OBOE_CONFIG0H_TXONLOOP  0x80 /*Transmit when looping (dangerous) */
#define OBOE_CONFIG0H_LOOP	0x40 /*Loopback Tx->Rx */
#define OBOE_CONFIG0H_ENTX	0x10 /*Enable Tx */
#define OBOE_CONFIG0H_ENRX	0x08 /*Enable Rx */
#define OBOE_CONFIG0H_ENDMAC	0x04 /*Enable/reset* the DMA controller */
#define OBOE_CONFIG0H_RCVANY	0x02 /*DMA mode 1=bytes, 0=dwords */

#define OBOE_CONFIG0L_CRC16	0x80 /*CRC 1=16 bit 0=32 bit */
#define OBOE_CONFIG0L_ENFIR	0x40 /*Enable FIR */
#define OBOE_CONFIG0L_ENMIR	0x20 /*Enable MIR */
#define OBOE_CONFIG0L_ENSIR	0x10 /*Enable SIR */
#define OBOE_CONFIG0L_ENSIRF	0x08 /*Enable SIR framer */
#define OBOE_CONFIG0L_SIRTEST	0x04 /*Enable SIR framer in MIR and FIR */
#define OBOE_CONFIG0L_INVERTTX  0x02 /*Invert Tx Line */
#define OBOE_CONFIG0L_INVERTRX  0x01 /*Invert Rx Line */

#define OBOE_BOF	OBOE_REG(0x12)
#define OBOE_EOF	OBOE_REG(0x13)

#define OBOE_ENABLEL	OBOE_REG(0x14)
#define OBOE_ENABLEH	OBOE_REG(0x15)

#define OBOE_ENABLEH_PHYANDCLOCK	0x80 /*Toggle low to copy config in */
#define OBOE_ENABLEH_CONFIGERR		0x40
#define OBOE_ENABLEH_FIRON		0x20
#define OBOE_ENABLEH_MIRON		0x10
#define OBOE_ENABLEH_SIRON		0x08
#define OBOE_ENABLEH_ENTX		0x04
#define OBOE_ENABLEH_ENRX		0x02
#define OBOE_ENABLEH_CRC16		0x01

#define OBOE_ENABLEL_BROADCAST		0x01

#define OBOE_CURR_PCONFIGL		OBOE_REG(0x16) /*Current config */
#define OBOE_CURR_PCONFIGH		OBOE_REG(0x17)

#define OBOE_NEW_PCONFIGL		OBOE_REG(0x18)
#define OBOE_NEW_PCONFIGH		OBOE_REG(0x19)

#define OBOE_PCONFIGH_BAUDMASK		0xfc
#define OBOE_PCONFIGH_WIDTHMASK		0x04
#define OBOE_PCONFIGL_WIDTHMASK		0xe0
#define OBOE_PCONFIGL_PREAMBLEMASK	0x1f

#define OBOE_PCONFIG_BAUDMASK		0xfc00
#define OBOE_PCONFIG_BAUDSHIFT		10
#define OBOE_PCONFIG_WIDTHMASK		0x04e0
#define OBOE_PCONFIG_WIDTHSHIFT		5
#define OBOE_PCONFIG_PREAMBLEMASK	0x001f
#define OBOE_PCONFIG_PREAMBLESHIFT	0

#define OBOE_MAXLENL			OBOE_REG(0x1a)
#define OBOE_MAXLENH			OBOE_REG(0x1b)

#define OBOE_RXCOUNTH			OBOE_REG(0x1c) /*Reset on recipt */
#define OBOE_RXCOUNTL			OBOE_REG(0x1d) /*of whole packet */

/* The PCI ID of the OBOE chip */
#ifndef PCI_DEVICE_ID_FIR701
#define PCI_DEVICE_ID_FIR701 	0x0701
#endif

#ifndef PCI_DEVICE_ID_FIRD01
#define PCI_DEVICE_ID_FIRD01 	0x0d01
#endif

struct OboeSlot
{
  __u16 len;                    /*Tweleve bits of packet length */
  __u8 unused;
  __u8 control;                 /*Slot control/status see below */
  __u32 address;                /*Slot buffer address */
}
__attribute__ ((packed));

#define OBOE_NTASKS OBOE_TXRING_OFFSET_IN_SLOTS

struct OboeRing
{
  struct OboeSlot rx[OBOE_NTASKS];
  struct OboeSlot tx[OBOE_NTASKS];
};

#define OBOE_RING_LEN (sizeof(struct OboeRing))


#define OBOE_CTL_TX_HW_OWNS	0x80 /*W/R This slot owned by the hardware */
#define OBOE_CTL_TX_DISTX_CRC	0x40 /*W Disable CRC generation for [FM]IR */
#define OBOE_CTL_TX_BAD_CRC     0x20 /*W Generate bad CRC */
#define OBOE_CTL_TX_SIP		0x10   /*W Generate an SIP after xmittion */
#define OBOE_CTL_TX_MKUNDER	0x08 /*W Generate an underrun error */
#define OBOE_CTL_TX_RTCENTX	0x04 /*W Enable receiver and generate TXdone */
     /*  After this slot is processed        */
#define OBOE_CTL_TX_UNDER	0x01  /*R Set by hardware to indicate underrun */


#define OBOE_CTL_RX_HW_OWNS	0x80 /*W/R This slot owned by hardware */
#define OBOE_CTL_RX_PHYERR	0x40 /*R Decoder error on receiption */
#define OBOE_CTL_RX_CRCERR	0x20 /*R CRC error only set for [FM]IR */
#define OBOE_CTL_RX_LENGTH	0x10 /*R Packet > max Rx length  */
#define OBOE_CTL_RX_OVER	0x08   /*R set to indicate an overflow */
#define OBOE_CTL_RX_SIRBAD	0x04 /*R SIR had BOF in packet or ABORT sequence */
#define OBOE_CTL_RX_RXEOF	0x02  /*R Finished receiving on this slot */


struct toshoboe_cb
{
  struct net_device *netdev;    /* Yes! we are some kind of netdevice */
  struct net_device_stats stats;
  struct tty_driver ttydev;

  struct irlap_cb *irlap;       /* The link layer we are binded to */

  chipio_t io;                  /* IrDA controller information */
  struct qos_info qos;          /* QoS capabilities for this device */

  __u32 flags;                  /* Interface flags */

  struct pci_dev *pdev;         /*PCI device */
  int base;                     /*IO base */


  int txpending;                /*how many tx's are pending */
  int txs, rxs;                 /*Which slots are we at  */

  int irdad;                    /*Driver under control of netdev end  */
  int async;                    /*Driver under control of async end   */


  int stopped;                  /*Stopped by some or other APM stuff */

  int filter;                   /*In SIR mode do we want to receive
                                   frames or byte ranges */

  void *ringbuf;                /*The ring buffer */
  struct OboeRing *ring;        /*The ring */

  void *tx_bufs[OBOE_RING_MAX_SIZE]; /*The buffers   */
  void *rx_bufs[OBOE_RING_MAX_SIZE];


  int speed;                    /*Current setting of the speed */
  int new_speed;                /*Set to request a speed change */

/* The spinlock protect critical parts of the driver.
 *	Locking is done like this :
 *		spin_lock_irqsave(&self->spinlock, flags);
 *	Releasing the lock :
 *		spin_unlock_irqrestore(&self->spinlock, flags);
 */
  spinlock_t spinlock;		
  /* Used for the probe and diagnostics code */
  int int_rx;
  int int_tx;
  int int_txunder;
  int int_rxover;
  int int_sip;
};


#endif
