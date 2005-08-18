 /*
  * Copyright (c) 1997-2000 LAN Media Corporation (LMC)
  * All rights reserved.  www.lanmedia.com
  *
  * This code is written by:
  * Andrew Stanley-Jones (asj@cban.com)
  * Rob Braun (bbraun@vix.com),
  * Michael Graff (explorer@vix.com) and
  * Matt Thomas (matt@3am-software.com).
  *
  * With Help By:
  * David Boggs
  * Ron Crane
  * Alan Cox
  *
  * This software may be used and distributed according to the terms
  * of the GNU General Public License version 2, incorporated herein by reference.
  *
  * Driver for the LanMedia LMC5200, LMC5245, LMC1000, LMC1200 cards.
  *
  * To control link specific options lmcctl is required.
  * It can be obtained from ftp.lanmedia.com.
  *
  * Linux driver notes:
  * Linux uses the device struct lmc_private to pass private information
  * arround.
  *
  * The initialization portion of this driver (the lmc_reset() and the
  * lmc_dec_reset() functions, as well as the led controls and the
  * lmc_initcsrs() functions.
  *
  * The watchdog function runs every second and checks to see if
  * we still have link, and that the timing source is what we expected
  * it to be.  If link is lost, the interface is marked down, and
  * we no longer can transmit.
  *
  */

/* $Id: lmc_main.c,v 1.36 2000/04/11 05:25:25 asj Exp $ */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/in.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/inet.h>
#include <linux/bitops.h>

#include <net/syncppp.h>

#include <asm/processor.h>             /* Processor type for cache alignment. */
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/uaccess.h>
//#include <asm/spinlock.h>

#define DRIVER_MAJOR_VERSION     1
#define DRIVER_MINOR_VERSION    34
#define DRIVER_SUB_VERSION       0

#define DRIVER_VERSION  ((DRIVER_MAJOR_VERSION << 8) + DRIVER_MINOR_VERSION)

#include "lmc.h"
#include "lmc_var.h"
#include "lmc_ioctl.h"
#include "lmc_debug.h"
#include "lmc_proto.h"

static int lmc_first_load = 0;

static int LMC_PKT_BUF_SZ = 1542;

static struct pci_device_id lmc_pci_tbl[] = {
	{ PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_TULIP_FAST,
	  PCI_VENDOR_ID_LMC, PCI_ANY_ID },
	{ PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_TULIP_FAST,
	  PCI_ANY_ID, PCI_VENDOR_ID_LMC },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, lmc_pci_tbl);
MODULE_LICENSE("GPL");


static int lmc_start_xmit(struct sk_buff *skb, struct net_device *dev);
static int lmc_start_xmit(struct sk_buff *skb, struct net_device *dev);
static int lmc_rx (struct net_device *dev);
static int lmc_open(struct net_device *dev);
static int lmc_close(struct net_device *dev);
static struct net_device_stats *lmc_get_stats(struct net_device *dev);
static irqreturn_t lmc_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
static void lmc_initcsrs(lmc_softc_t * const sc, lmc_csrptr_t csr_base, size_t csr_size);
static void lmc_softreset(lmc_softc_t * const);
static void lmc_running_reset(struct net_device *dev);
static int lmc_ifdown(struct net_device * const);
static void lmc_watchdog(unsigned long data);
static void lmc_reset(lmc_softc_t * const sc);
static void lmc_dec_reset(lmc_softc_t * const sc);
static void lmc_driver_timeout(struct net_device *dev);

/*
 * linux reserves 16 device specific IOCTLs.  We call them
 * LMCIOC* to control various bits of our world.
 */
int lmc_ioctl (struct net_device *dev, struct ifreq *ifr, int cmd) /*fold00*/
{
    lmc_softc_t *sc;
    lmc_ctl_t ctl;
    int ret;
    u_int16_t regVal;
    unsigned long flags;

    struct sppp *sp;

    ret = -EOPNOTSUPP;

    sc = dev->priv;

    lmc_trace(dev, "lmc_ioctl in");

    /*
     * Most functions mess with the structure
     * Disable interrupts while we do the polling
     */
    spin_lock_irqsave(&sc->lmc_lock, flags);

    switch (cmd) {
        /*
         * Return current driver state.  Since we keep this up
         * To date internally, just copy this out to the user.
         */
    case LMCIOCGINFO: /*fold01*/
        if (copy_to_user(ifr->ifr_data, &sc->ictl, sizeof (lmc_ctl_t)))
            return -EFAULT;
        ret = 0;
        break;

    case LMCIOCSINFO: /*fold01*/
        sp = &((struct ppp_device *) dev)->sppp;
        if (!capable(CAP_NET_ADMIN)) {
            ret = -EPERM;
            break;
        }

        if(dev->flags & IFF_UP){
            ret = -EBUSY;
            break;
        }

        if (copy_from_user(&ctl, ifr->ifr_data, sizeof (lmc_ctl_t)))
            return -EFAULT;

        sc->lmc_media->set_status (sc, &ctl);

        if(ctl.crc_length != sc->ictl.crc_length) {
            sc->lmc_media->set_crc_length(sc, ctl.crc_length);
	    if (sc->ictl.crc_length == LMC_CTL_CRC_LENGTH_16)
		sc->TxDescriptControlInit |=  LMC_TDES_ADD_CRC_DISABLE;
	    else
		sc->TxDescriptControlInit &= ~LMC_TDES_ADD_CRC_DISABLE;
        }

        if (ctl.keepalive_onoff == LMC_CTL_OFF)
            sp->pp_flags &= ~PP_KEEPALIVE;	/* Turn off */
        else
            sp->pp_flags |= PP_KEEPALIVE;	/* Turn on */

        ret = 0;
        break;

    case LMCIOCIFTYPE: /*fold01*/
        {
            u_int16_t	old_type = sc->if_type;
            u_int16_t	new_type;

	    if (!capable(CAP_NET_ADMIN)) {
		ret = -EPERM;
		break;
	    }

	    if (copy_from_user(&new_type, ifr->ifr_data, sizeof(u_int16_t)))
                return -EFAULT;

            
	    if (new_type == old_type)
	    {
		ret = 0 ;
		break;				/* no change */
            }
            
            lmc_proto_close(sc);
            lmc_proto_detach(sc);

            sc->if_type = new_type;
//            lmc_proto_init(sc);
            lmc_proto_attach(sc);
            lmc_proto_open(sc);

	    ret = 0 ;
	    break ;
	}

    case LMCIOCGETXINFO: /*fold01*/
        sc->lmc_xinfo.Magic0 = 0xBEEFCAFE;

        sc->lmc_xinfo.PciCardType = sc->lmc_cardtype;
        sc->lmc_xinfo.PciSlotNumber = 0;
        sc->lmc_xinfo.DriverMajorVersion = DRIVER_MAJOR_VERSION;
        sc->lmc_xinfo.DriverMinorVersion = DRIVER_MINOR_VERSION;
        sc->lmc_xinfo.DriverSubVersion = DRIVER_SUB_VERSION;
        sc->lmc_xinfo.XilinxRevisionNumber =
            lmc_mii_readreg (sc, 0, 3) & 0xf;
        sc->lmc_xinfo.MaxFrameSize = LMC_PKT_BUF_SZ;
        sc->lmc_xinfo.link_status = sc->lmc_media->get_link_status (sc);
        sc->lmc_xinfo.mii_reg16 = lmc_mii_readreg (sc, 0, 16);

        sc->lmc_xinfo.Magic1 = 0xDEADBEEF;

        if (copy_to_user(ifr->ifr_data, &sc->lmc_xinfo,
                         sizeof (struct lmc_xinfo)))
            return -EFAULT;
        ret = 0;

        break;

    case LMCIOCGETLMCSTATS: /*fold01*/
        if (sc->lmc_cardtype == LMC_CARDTYPE_T1){
            lmc_mii_writereg (sc, 0, 17, T1FRAMER_FERR_LSB);
            sc->stats.framingBitErrorCount +=
                lmc_mii_readreg (sc, 0, 18) & 0xff;
            lmc_mii_writereg (sc, 0, 17, T1FRAMER_FERR_MSB);
            sc->stats.framingBitErrorCount +=
                (lmc_mii_readreg (sc, 0, 18) & 0xff) << 8;
            lmc_mii_writereg (sc, 0, 17, T1FRAMER_LCV_LSB);
            sc->stats.lineCodeViolationCount +=
                lmc_mii_readreg (sc, 0, 18) & 0xff;
            lmc_mii_writereg (sc, 0, 17, T1FRAMER_LCV_MSB);
            sc->stats.lineCodeViolationCount +=
                (lmc_mii_readreg (sc, 0, 18) & 0xff) << 8;
            lmc_mii_writereg (sc, 0, 17, T1FRAMER_AERR);
            regVal = lmc_mii_readreg (sc, 0, 18) & 0xff;

            sc->stats.lossOfFrameCount +=
                (regVal & T1FRAMER_LOF_MASK) >> 4;
            sc->stats.changeOfFrameAlignmentCount +=
                (regVal & T1FRAMER_COFA_MASK) >> 2;
            sc->stats.severelyErroredFrameCount +=
                regVal & T1FRAMER_SEF_MASK;
        }

        if (copy_to_user(ifr->ifr_data, &sc->stats,
                         sizeof (struct lmc_statistics)))
            return -EFAULT;

        ret = 0;
        break;

    case LMCIOCCLEARLMCSTATS: /*fold01*/
        if (!capable(CAP_NET_ADMIN)){
            ret = -EPERM;
            break;
        }

        memset (&sc->stats, 0, sizeof (struct lmc_statistics));
        sc->stats.check = STATCHECK;
        sc->stats.version_size = (DRIVER_VERSION << 16) +
            sizeof (struct lmc_statistics);
        sc->stats.lmc_cardtype = sc->lmc_cardtype;
        ret = 0;
        break;

    case LMCIOCSETCIRCUIT: /*fold01*/
        if (!capable(CAP_NET_ADMIN)){
            ret = -EPERM;
            break;
        }

        if(dev->flags & IFF_UP){
            ret = -EBUSY;
            break;
        }

        if (copy_from_user(&ctl, ifr->ifr_data, sizeof (lmc_ctl_t)))
            return -EFAULT;
        sc->lmc_media->set_circuit_type(sc, ctl.circuit_type);
        sc->ictl.circuit_type = ctl.circuit_type;
        ret = 0;

        break;

    case LMCIOCRESET: /*fold01*/
        if (!capable(CAP_NET_ADMIN)){
            ret = -EPERM;
            break;
        }

        /* Reset driver and bring back to current state */
        printk (" REG16 before reset +%04x\n", lmc_mii_readreg (sc, 0, 16));
        lmc_running_reset (dev);
        printk (" REG16 after reset +%04x\n", lmc_mii_readreg (sc, 0, 16));

        LMC_EVENT_LOG(LMC_EVENT_FORCEDRESET, LMC_CSR_READ (sc, csr_status), lmc_mii_readreg (sc, 0, 16));

        ret = 0;
        break;

#ifdef DEBUG
    case LMCIOCDUMPEVENTLOG:
        if (copy_to_user(ifr->ifr_data, &lmcEventLogIndex, sizeof (u32)))
            return -EFAULT;
        if (copy_to_user(ifr->ifr_data + sizeof (u32), lmcEventLogBuf, sizeof (lmcEventLogBuf)))
            return -EFAULT;

        ret = 0;
        break;
#endif /* end ifdef _DBG_EVENTLOG */
    case LMCIOCT1CONTROL: /*fold01*/
        if (sc->lmc_cardtype != LMC_CARDTYPE_T1){
            ret = -EOPNOTSUPP;
            break;
        }
        break;
    case LMCIOCXILINX: /*fold01*/
        {
            struct lmc_xilinx_control xc; /*fold02*/

            if (!capable(CAP_NET_ADMIN)){
                ret = -EPERM;
                break;
            }

            /*
             * Stop the xwitter whlie we restart the hardware
             */
            netif_stop_queue(dev);

            if (copy_from_user(&xc, ifr->ifr_data, sizeof (struct lmc_xilinx_control)))
                return -EFAULT;
            switch(xc.command){
            case lmc_xilinx_reset: /*fold02*/
                {
                    u16 mii;
                    mii = lmc_mii_readreg (sc, 0, 16);

                    /*
                     * Make all of them 0 and make input
                     */
                    lmc_gpio_mkinput(sc, 0xff);

                    /*
                     * make the reset output
                     */
                    lmc_gpio_mkoutput(sc, LMC_GEP_RESET);

                    /*
                     * RESET low to force configuration.  This also forces
                     * the transmitter clock to be internal, but we expect to reset
                     * that later anyway.
                     */

                    sc->lmc_gpio &= ~LMC_GEP_RESET;
                    LMC_CSR_WRITE(sc, csr_gp, sc->lmc_gpio);


                    /*
                     * hold for more than 10 microseconds
                     */
                    udelay(50);

                    sc->lmc_gpio |= LMC_GEP_RESET;
                    LMC_CSR_WRITE(sc, csr_gp, sc->lmc_gpio);


                    /*
                     * stop driving Xilinx-related signals
                     */
                    lmc_gpio_mkinput(sc, 0xff);

                    /* Reset the frammer hardware */
                    sc->lmc_media->set_link_status (sc, 1);
                    sc->lmc_media->set_status (sc, NULL);
//                    lmc_softreset(sc);

                    {
                        int i;
                        for(i = 0; i < 5; i++){
                            lmc_led_on(sc, LMC_DS3_LED0);
                            mdelay(100);
                            lmc_led_off(sc, LMC_DS3_LED0);
                            lmc_led_on(sc, LMC_DS3_LED1);
                            mdelay(100);
                            lmc_led_off(sc, LMC_DS3_LED1);
                            lmc_led_on(sc, LMC_DS3_LED3);
                            mdelay(100);
                            lmc_led_off(sc, LMC_DS3_LED3);
                            lmc_led_on(sc, LMC_DS3_LED2);
                            mdelay(100);
                            lmc_led_off(sc, LMC_DS3_LED2);
                        }
                    }
                    
                    

                    ret = 0x0;

                }

                break;
            case lmc_xilinx_load_prom: /*fold02*/
                {
                    u16 mii;
                    int timeout = 500000;
                    mii = lmc_mii_readreg (sc, 0, 16);

                    /*
                     * Make all of them 0 and make input
                     */
                    lmc_gpio_mkinput(sc, 0xff);

                    /*
                     * make the reset output
                     */
                    lmc_gpio_mkoutput(sc,  LMC_GEP_DP | LMC_GEP_RESET);

                    /*
                     * RESET low to force configuration.  This also forces
                     * the transmitter clock to be internal, but we expect to reset
                     * that later anyway.
                     */

                    sc->lmc_gpio &= ~(LMC_GEP_RESET | LMC_GEP_DP);
                    LMC_CSR_WRITE(sc, csr_gp, sc->lmc_gpio);


                    /*
                     * hold for more than 10 microseconds
                     */
                    udelay(50);

                    sc->lmc_gpio |= LMC_GEP_DP | LMC_GEP_RESET;
                    LMC_CSR_WRITE(sc, csr_gp, sc->lmc_gpio);

                    /*
                     * busy wait for the chip to reset
                     */
                    while( (LMC_CSR_READ(sc, csr_gp) & LMC_GEP_INIT) == 0 &&
                           (timeout-- > 0))
                        ;


                    /*
                     * stop driving Xilinx-related signals
                     */
                    lmc_gpio_mkinput(sc, 0xff);

                    ret = 0x0;
                    

                    break;

                }

            case lmc_xilinx_load: /*fold02*/
                {
                    char *data;
                    int pos;
                    int timeout = 500000;

                    if(xc.data == 0x0){
                            ret = -EINVAL;
                            break;
                    }

                    data = kmalloc(xc.len, GFP_KERNEL);
                    if(data == 0x0){
                            printk(KERN_WARNING "%s: Failed to allocate memory for copy\n", dev->name);
                            ret = -ENOMEM;
                            break;
                    }
                    
                    if(copy_from_user(data, xc.data, xc.len))
                    {
                    	kfree(data);
                    	ret = -ENOMEM;
                    	break;
                    }

                    printk("%s: Starting load of data Len: %d at 0x%p == 0x%p\n", dev->name, xc.len, xc.data, data);

                    lmc_gpio_mkinput(sc, 0xff);

                    /*
                     * Clear the Xilinx and start prgramming from the DEC
                     */

                    /*
                     * Set ouput as:
                     * Reset: 0 (active)
                     * DP:    0 (active)
                     * Mode:  1
                     *
                     */
                    sc->lmc_gpio = 0x00;
                    sc->lmc_gpio &= ~LMC_GEP_DP;
                    sc->lmc_gpio &= ~LMC_GEP_RESET;
                    sc->lmc_gpio |=  LMC_GEP_MODE;
                    LMC_CSR_WRITE(sc, csr_gp, sc->lmc_gpio);

                    lmc_gpio_mkoutput(sc, LMC_GEP_MODE | LMC_GEP_DP | LMC_GEP_RESET);

                    /*
                     * Wait at least 10 us 20 to be safe
                     */
                    udelay(50);

                    /*
                     * Clear reset and activate programming lines
                     * Reset: Input
                     * DP:    Input
                     * Clock: Output
                     * Data:  Output
                     * Mode:  Output
                     */
                    lmc_gpio_mkinput(sc, LMC_GEP_DP | LMC_GEP_RESET);

                    /*
                     * Set LOAD, DATA, Clock to 1
                     */
                    sc->lmc_gpio = 0x00;
                    sc->lmc_gpio |= LMC_GEP_MODE;
                    sc->lmc_gpio |= LMC_GEP_DATA;
                    sc->lmc_gpio |= LMC_GEP_CLK;
                    LMC_CSR_WRITE(sc, csr_gp, sc->lmc_gpio);
                    
                    lmc_gpio_mkoutput(sc, LMC_GEP_DATA | LMC_GEP_CLK | LMC_GEP_MODE );

                    /*
                     * busy wait for the chip to reset
                     */
                    while( (LMC_CSR_READ(sc, csr_gp) & LMC_GEP_INIT) == 0 &&
                           (timeout-- > 0))
                        ;

                    printk(KERN_DEBUG "%s: Waited %d for the Xilinx to clear it's memory\n", dev->name, 500000-timeout);

                    for(pos = 0; pos < xc.len; pos++){
                        switch(data[pos]){
                        case 0:
                            sc->lmc_gpio &= ~LMC_GEP_DATA; /* Data is 0 */
                            break;
                        case 1:
                            sc->lmc_gpio |= LMC_GEP_DATA; /* Data is 1 */
                            break;
                        default:
                            printk(KERN_WARNING "%s Bad data in xilinx programming data at %d, got %d wanted 0 or 1\n", dev->name, pos, data[pos]);
                            sc->lmc_gpio |= LMC_GEP_DATA; /* Assume it's 1 */
                        }
                        sc->lmc_gpio &= ~LMC_GEP_CLK; /* Clock to zero */
                        sc->lmc_gpio |= LMC_GEP_MODE;
                        LMC_CSR_WRITE(sc, csr_gp, sc->lmc_gpio);
                        udelay(1);
                        
                        sc->lmc_gpio |= LMC_GEP_CLK; /* Put the clack back to one */
                        sc->lmc_gpio |= LMC_GEP_MODE;
                        LMC_CSR_WRITE(sc, csr_gp, sc->lmc_gpio);
                        udelay(1);
                    }
                    if((LMC_CSR_READ(sc, csr_gp) & LMC_GEP_INIT) == 0){
                        printk(KERN_WARNING "%s: Reprogramming FAILED. Needs to be reprogrammed. (corrupted data)\n", dev->name);
                    }
                    else if((LMC_CSR_READ(sc, csr_gp) & LMC_GEP_DP) == 0){
                        printk(KERN_WARNING "%s: Reprogramming FAILED. Needs to be reprogrammed. (done)\n", dev->name);
                    }
                    else {
                        printk(KERN_DEBUG "%s: Done reprogramming Xilinx, %d bits, good luck!\n", dev->name, pos);
                    }

                    lmc_gpio_mkinput(sc, 0xff);
                    
                    sc->lmc_miireg16 |= LMC_MII16_FIFO_RESET;
                    lmc_mii_writereg(sc, 0, 16, sc->lmc_miireg16);

                    sc->lmc_miireg16 &= ~LMC_MII16_FIFO_RESET;
                    lmc_mii_writereg(sc, 0, 16, sc->lmc_miireg16);

                    kfree(data);
                    
                    ret = 0;
                    
                    break;
                }
            default: /*fold02*/
                ret = -EBADE;
                break;
            }

            netif_wake_queue(dev);
            sc->lmc_txfull = 0;

        }
        break;
    default: /*fold01*/
        /* If we don't know what to do, give the protocol a shot. */
        ret = lmc_proto_ioctl (sc, ifr, cmd);
        break;
    }

    spin_unlock_irqrestore(&sc->lmc_lock, flags); /*fold01*/

    lmc_trace(dev, "lmc_ioctl out");

    return ret;
}


/* the watchdog process that cruises around */
static void lmc_watchdog (unsigned long data) /*fold00*/
{
    struct net_device *dev = (struct net_device *) data;
    lmc_softc_t *sc;
    int link_status;
    u_int32_t ticks;
    unsigned long flags;

    sc = dev->priv;

    lmc_trace(dev, "lmc_watchdog in");

    spin_lock_irqsave(&sc->lmc_lock, flags);

    if(sc->check != 0xBEAFCAFE){
        printk("LMC: Corrupt net_device stuct, breaking out\n");
	spin_unlock_irqrestore(&sc->lmc_lock, flags);
        return;
    }


    /* Make sure the tx jabber and rx watchdog are off,
     * and the transmit and receive processes are running.
     */

    LMC_CSR_WRITE (sc, csr_15, 0x00000011);
    sc->lmc_cmdmode |= TULIP_CMD_TXRUN | TULIP_CMD_RXRUN;
    LMC_CSR_WRITE (sc, csr_command, sc->lmc_cmdmode);

    if (sc->lmc_ok == 0)
        goto kick_timer;

    LMC_EVENT_LOG(LMC_EVENT_WATCHDOG, LMC_CSR_READ (sc, csr_status), lmc_mii_readreg (sc, 0, 16));

    /* --- begin time out check -----------------------------------
     * check for a transmit interrupt timeout
     * Has the packet xmt vs xmt serviced threshold been exceeded */
    if (sc->lmc_taint_tx == sc->lastlmc_taint_tx &&
        sc->stats.tx_packets > sc->lasttx_packets &&
        sc->tx_TimeoutInd == 0)
    {

        /* wait for the watchdog to come around again */
        sc->tx_TimeoutInd = 1;
    }
    else if (sc->lmc_taint_tx == sc->lastlmc_taint_tx &&
             sc->stats.tx_packets > sc->lasttx_packets &&
             sc->tx_TimeoutInd)
    {

        LMC_EVENT_LOG(LMC_EVENT_XMTINTTMO, LMC_CSR_READ (sc, csr_status), 0);

        sc->tx_TimeoutDisplay = 1;
        sc->stats.tx_TimeoutCnt++;

        /* DEC chip is stuck, hit it with a RESET!!!! */
        lmc_running_reset (dev);


        /* look at receive & transmit process state to make sure they are running */
        LMC_EVENT_LOG(LMC_EVENT_RESET1, LMC_CSR_READ (sc, csr_status), 0);

        /* look at: DSR - 02  for Reg 16
         *                  CTS - 08
         *                  DCD - 10
         *                  RI  - 20
         * for Reg 17
         */
        LMC_EVENT_LOG(LMC_EVENT_RESET2, lmc_mii_readreg (sc, 0, 16), lmc_mii_readreg (sc, 0, 17));

        /* reset the transmit timeout detection flag */
        sc->tx_TimeoutInd = 0;
        sc->lastlmc_taint_tx = sc->lmc_taint_tx;
        sc->lasttx_packets = sc->stats.tx_packets;
    }
    else
    {
        sc->tx_TimeoutInd = 0;
        sc->lastlmc_taint_tx = sc->lmc_taint_tx;
        sc->lasttx_packets = sc->stats.tx_packets;
    }

    /* --- end time out check ----------------------------------- */


    link_status = sc->lmc_media->get_link_status (sc);

    /*
     * hardware level link lost, but the interface is marked as up.
     * Mark it as down.
     */
    if ((link_status == 0) && (sc->last_link_status != 0)) {
        printk(KERN_WARNING "%s: hardware/physical link down\n", dev->name);
        sc->last_link_status = 0;
        /* lmc_reset (sc); Why reset??? The link can go down ok */

        /* Inform the world that link has been lost */
	netif_carrier_off(dev);
    }

    /*
     * hardware link is up, but the interface is marked as down.
     * Bring it back up again.
     */
     if (link_status != 0 && sc->last_link_status == 0) {
         printk(KERN_WARNING "%s: hardware/physical link up\n", dev->name);
         sc->last_link_status = 1;
         /* lmc_reset (sc); Again why reset??? */

         /* Inform the world that link protocol is back up. */
	 netif_carrier_on(dev);

         /* Now we have to tell the syncppp that we had an outage
          * and that it should deal.  Calling sppp_reopen here
          * should do the trick, but we may have to call sppp_close
          * when the link goes down, and call sppp_open here.
          * Subject to more testing.
          * --bbraun
          */

         lmc_proto_reopen(sc);

     }

    /* Call media specific watchdog functions */
    sc->lmc_media->watchdog(sc);

    /*
     * Poke the transmitter to make sure it
     * never stops, even if we run out of mem
     */
    LMC_CSR_WRITE(sc, csr_rxpoll, 0);

    /*
     * Check for code that failed
     * and try and fix it as appropriate
     */
    if(sc->failed_ring == 1){
        /*
         * Failed to setup the recv/xmit rin
         * Try again
         */
        sc->failed_ring = 0;
        lmc_softreset(sc);
    }
    if(sc->failed_recv_alloc == 1){
        /*
         * We failed to alloc mem in the
         * interrupt handler, go through the rings
         * and rebuild them
         */
        sc->failed_recv_alloc = 0;
        lmc_softreset(sc);
    }


    /*
     * remember the timer value
     */
kick_timer:

    ticks = LMC_CSR_READ (sc, csr_gp_timer);
    LMC_CSR_WRITE (sc, csr_gp_timer, 0xffffffffUL);
    sc->ictl.ticks = 0x0000ffff - (ticks & 0x0000ffff);

    /*
     * restart this timer.
     */
    sc->timer.expires = jiffies + (HZ);
    add_timer (&sc->timer);

    spin_unlock_irqrestore(&sc->lmc_lock, flags);

    lmc_trace(dev, "lmc_watchdog out");

}

static void lmc_setup(struct net_device * const dev) /*fold00*/
{
    lmc_trace(dev, "lmc_setup in");

    dev->type = ARPHRD_HDLC;
    dev->hard_start_xmit = lmc_start_xmit;
    dev->open = lmc_open;
    dev->stop = lmc_close;
    dev->get_stats = lmc_get_stats;
    dev->do_ioctl = lmc_ioctl;
    dev->tx_timeout = lmc_driver_timeout;
    dev->watchdog_timeo = (HZ); /* 1 second */
    
    lmc_trace(dev, "lmc_setup out");
}


static int __devinit lmc_init_one(struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
    struct net_device *dev;
    lmc_softc_t *sc;
    u16 subdevice;
    u_int16_t AdapModelNum;
    int err = -ENOMEM;
    static int cards_found;
#ifndef GCOM
    /* We name by type not by vendor */
    static const char lmcname[] = "hdlc%d";
#else
    /* 
     * GCOM uses LMC vendor name so that clients can know which card
     * to attach to.
     */
    static const char lmcname[] = "lmc%d";
#endif


    /*
     * Allocate our own device structure
     */
    dev = alloc_netdev(sizeof(lmc_softc_t), lmcname, lmc_setup);
    if (!dev) {
        printk (KERN_ERR "lmc:alloc_netdev for device failed\n");
	goto out1;
    }
 
    lmc_trace(dev, "lmc_init_one in");

    err = pci_enable_device(pdev);
    if (err) {
	    printk(KERN_ERR "lmc: pci enable failed:%d\n", err);
	    goto out2;
    }
    
    if (pci_request_regions(pdev, "lmc")) {
	    printk(KERN_ERR "lmc: pci_request_region failed\n");
	    err = -EIO;
	    goto out3;
    }

    pci_set_drvdata(pdev, dev);

    if(lmc_first_load == 0){
        printk(KERN_INFO "Lan Media Corporation WAN Driver Version %d.%d.%d\n",
	       DRIVER_MAJOR_VERSION, DRIVER_MINOR_VERSION,DRIVER_SUB_VERSION);
        lmc_first_load = 1;
    }
    
    sc = dev->priv;
    sc->lmc_device = dev;
    sc->name = dev->name;

    /* Initialize the sppp layer */
    /* An ioctl can cause a subsequent detach for raw frame interface */
    sc->if_type = LMC_PPP;
    sc->check = 0xBEAFCAFE;
    dev->base_addr = pci_resource_start(pdev, 0);
    dev->irq = pdev->irq;

    SET_MODULE_OWNER(dev);
    SET_NETDEV_DEV(dev, &pdev->dev);

    /*
     * This will get the protocol layer ready and do any 1 time init's
     * Must have a valid sc and dev structure
     */
    lmc_proto_init(sc);

    lmc_proto_attach(sc);

    /*
     * Why were we changing this???
     dev->tx_queue_len = 100;
     */

    /* Init the spin lock so can call it latter */

    spin_lock_init(&sc->lmc_lock);
    pci_set_master(pdev);

    printk ("%s: detected at %lx, irq %d\n", dev->name,
	    dev->base_addr, dev->irq);

    if (register_netdev (dev) != 0) {
        printk (KERN_ERR "%s: register_netdev failed.\n", dev->name);
	goto out4;
    }

    sc->lmc_cardtype = LMC_CARDTYPE_UNKNOWN;
    sc->lmc_timing = LMC_CTL_CLOCK_SOURCE_EXT;

    /*
     *
     * Check either the subvendor or the subdevice, some systems reverse
     * the setting in the bois, seems to be version and arch dependent?
     * Fix the error, exchange the two values 
     */
    if ((subdevice = pdev->subsystem_device) == PCI_VENDOR_ID_LMC)
	    subdevice = pdev->subsystem_vendor;

    switch (subdevice) {
    case PCI_DEVICE_ID_LMC_HSSI:
        printk ("%s: LMC HSSI\n", dev->name);
        sc->lmc_cardtype = LMC_CARDTYPE_HSSI;
        sc->lmc_media = &lmc_hssi_media;
        break;
    case PCI_DEVICE_ID_LMC_DS3:
        printk ("%s: LMC DS3\n", dev->name);
        sc->lmc_cardtype = LMC_CARDTYPE_DS3;
        sc->lmc_media = &lmc_ds3_media;
        break;
    case PCI_DEVICE_ID_LMC_SSI:
        printk ("%s: LMC SSI\n", dev->name);
        sc->lmc_cardtype = LMC_CARDTYPE_SSI;
        sc->lmc_media = &lmc_ssi_media;
        break;
    case PCI_DEVICE_ID_LMC_T1:
        printk ("%s: LMC T1\n", dev->name);
        sc->lmc_cardtype = LMC_CARDTYPE_T1;
        sc->lmc_media = &lmc_t1_media;
        break;
    default:
        printk (KERN_WARNING "%s: LMC UNKOWN CARD!\n", dev->name);
        break;
    }

    lmc_initcsrs (sc, dev->base_addr, 8);

    lmc_gpio_mkinput (sc, 0xff);
    sc->lmc_gpio = 0;		/* drive no signals yet */

    sc->lmc_media->defaults (sc);

    sc->lmc_media->set_link_status (sc, LMC_LINK_UP);

    /* verify that the PCI Sub System ID matches the Adapter Model number
     * from the MII register
     */
    AdapModelNum = (lmc_mii_readreg (sc, 0, 3) & 0x3f0) >> 4;

    if ((AdapModelNum == LMC_ADAP_T1
         && subdevice == PCI_DEVICE_ID_LMC_T1) ||	/* detect LMC1200 */
        (AdapModelNum == LMC_ADAP_SSI
         && subdevice == PCI_DEVICE_ID_LMC_SSI) ||	/* detect LMC1000 */
        (AdapModelNum == LMC_ADAP_DS3
         && subdevice == PCI_DEVICE_ID_LMC_DS3) ||	/* detect LMC5245 */
        (AdapModelNum == LMC_ADAP_HSSI
         && subdevice == PCI_DEVICE_ID_LMC_HSSI))
    {				/* detect LMC5200 */

    }
    else {
        printk ("%s: Model number (%d) miscompare for PCI Subsystem ID = 0x%04x\n",
                dev->name, AdapModelNum, subdevice);
//        return (NULL);
    }
    /*
     * reset clock
     */
    LMC_CSR_WRITE (sc, csr_gp_timer, 0xFFFFFFFFUL);

    sc->board_idx = cards_found++;
    sc->stats.check = STATCHECK;
    sc->stats.version_size = (DRIVER_VERSION << 16) +
        sizeof (struct lmc_statistics);
    sc->stats.lmc_cardtype = sc->lmc_cardtype;

    sc->lmc_ok = 0;
    sc->last_link_status = 0;

    lmc_trace(dev, "lmc_init_one out");
    return 0;

 out4:
    lmc_proto_detach(sc);
 out3:
    if (pdev) {
	    pci_release_regions(pdev);
	    pci_set_drvdata(pdev, NULL);
    }
 out2:
    free_netdev(dev);
 out1:
    return err;
}

/*
 * Called from pci when removing module.
 */
static void __devexit lmc_remove_one (struct pci_dev *pdev)
{
    struct net_device *dev = pci_get_drvdata(pdev);
    
    if (dev) {
	    lmc_softc_t *sc = dev->priv;
	    
	    printk("%s: removing...\n", dev->name);
	    lmc_proto_detach(sc);
	    unregister_netdev(dev);
	    free_netdev(dev);
	    pci_release_regions(pdev);
	    pci_disable_device(pdev);
	    pci_set_drvdata(pdev, NULL);
    }
}

/* After this is called, packets can be sent.
 * Does not initialize the addresses
 */
static int lmc_open (struct net_device *dev) /*fold00*/
{
    lmc_softc_t *sc = dev->priv;

    lmc_trace(dev, "lmc_open in");

    lmc_led_on(sc, LMC_DS3_LED0);

    lmc_dec_reset (sc);
    lmc_reset (sc);

    LMC_EVENT_LOG(LMC_EVENT_RESET1, LMC_CSR_READ (sc, csr_status), 0);
    LMC_EVENT_LOG(LMC_EVENT_RESET2,
                  lmc_mii_readreg (sc, 0, 16),
                  lmc_mii_readreg (sc, 0, 17));


    if (sc->lmc_ok){
        lmc_trace(dev, "lmc_open lmc_ok out");
        return (0);
    }

    lmc_softreset (sc);

    /* Since we have to use PCI bus, this should work on x86,alpha,ppc */
    if (request_irq (dev->irq, &lmc_interrupt, SA_SHIRQ, dev->name, dev)){
        printk(KERN_WARNING "%s: could not get irq: %d\n", dev->name, dev->irq);
        lmc_trace(dev, "lmc_open irq failed out");
        return -EAGAIN;
    }
    sc->got_irq = 1;

    /* Assert Terminal Active */
    sc->lmc_miireg16 |= LMC_MII16_LED_ALL;
    sc->lmc_media->set_link_status (sc, LMC_LINK_UP);

    /*
     * reset to last state.
     */
    sc->lmc_media->set_status (sc, NULL);

    /* setup default bits to be used in tulip_desc_t transmit descriptor
     * -baz */
    sc->TxDescriptControlInit = (
                                 LMC_TDES_INTERRUPT_ON_COMPLETION
                                 | LMC_TDES_FIRST_SEGMENT
                                 | LMC_TDES_LAST_SEGMENT
                                 | LMC_TDES_SECOND_ADDR_CHAINED
                                 | LMC_TDES_DISABLE_PADDING
                                );

    if (sc->ictl.crc_length == LMC_CTL_CRC_LENGTH_16) {
        /* disable 32 bit CRC generated by ASIC */
        sc->TxDescriptControlInit |= LMC_TDES_ADD_CRC_DISABLE;
    }
    sc->lmc_media->set_crc_length(sc, sc->ictl.crc_length);
    /* Acknoledge the Terminal Active and light LEDs */

    /* dev->flags |= IFF_UP; */

    lmc_proto_open(sc);

    dev->do_ioctl = lmc_ioctl;


    netif_start_queue(dev);
    
    sc->stats.tx_tbusy0++ ;

    /*
     * select what interrupts we want to get
     */
    sc->lmc_intrmask = 0;
    /* Should be using the default interrupt mask defined in the .h file. */
    sc->lmc_intrmask |= (TULIP_STS_NORMALINTR
                         | TULIP_STS_RXINTR
                         | TULIP_STS_TXINTR
                         | TULIP_STS_ABNRMLINTR
                         | TULIP_STS_SYSERROR
                         | TULIP_STS_TXSTOPPED
                         | TULIP_STS_TXUNDERFLOW
                         | TULIP_STS_RXSTOPPED
		         | TULIP_STS_RXNOBUF
                        );
    LMC_CSR_WRITE (sc, csr_intr, sc->lmc_intrmask);

    sc->lmc_cmdmode |= TULIP_CMD_TXRUN;
    sc->lmc_cmdmode |= TULIP_CMD_RXRUN;
    LMC_CSR_WRITE (sc, csr_command, sc->lmc_cmdmode);

    sc->lmc_ok = 1; /* Run watchdog */

    /*
     * Set the if up now - pfb
     */

    sc->last_link_status = 1;

    /*
     * Setup a timer for the watchdog on probe, and start it running.
     * Since lmc_ok == 0, it will be a NOP for now.
     */
    init_timer (&sc->timer);
    sc->timer.expires = jiffies + HZ;
    sc->timer.data = (unsigned long) dev;
    sc->timer.function = &lmc_watchdog;
    add_timer (&sc->timer);

    lmc_trace(dev, "lmc_open out");

    return (0);
}

/* Total reset to compensate for the AdTran DSU doing bad things
 *  under heavy load
 */

static void lmc_running_reset (struct net_device *dev) /*fold00*/
{

    lmc_softc_t *sc = (lmc_softc_t *) dev->priv;

    lmc_trace(dev, "lmc_runnig_reset in");

    /* stop interrupts */
    /* Clear the interrupt mask */
    LMC_CSR_WRITE (sc, csr_intr, 0x00000000);

    lmc_dec_reset (sc);
    lmc_reset (sc);
    lmc_softreset (sc);
    /* sc->lmc_miireg16 |= LMC_MII16_LED_ALL; */
    sc->lmc_media->set_link_status (sc, 1);
    sc->lmc_media->set_status (sc, NULL);

    netif_wake_queue(dev);

    sc->lmc_txfull = 0;
    sc->stats.tx_tbusy0++ ;

    sc->lmc_intrmask = TULIP_DEFAULT_INTR_MASK;
    LMC_CSR_WRITE (sc, csr_intr, sc->lmc_intrmask);

    sc->lmc_cmdmode |= (TULIP_CMD_TXRUN | TULIP_CMD_RXRUN);
    LMC_CSR_WRITE (sc, csr_command, sc->lmc_cmdmode);

    lmc_trace(dev, "lmc_runnin_reset_out");
}


/* This is what is called when you ifconfig down a device.
 * This disables the timer for the watchdog and keepalives,
 * and disables the irq for dev.
 */
static int lmc_close (struct net_device *dev) /*fold00*/
{
    /* not calling release_region() as we should */
    lmc_softc_t *sc;

    lmc_trace(dev, "lmc_close in");
    
    sc = dev->priv;
    sc->lmc_ok = 0;
    sc->lmc_media->set_link_status (sc, 0);
    del_timer (&sc->timer);
    lmc_proto_close(sc);
    lmc_ifdown (dev);

    lmc_trace(dev, "lmc_close out");
    
    return 0;
}

/* Ends the transfer of packets */
/* When the interface goes down, this is called */
static int lmc_ifdown (struct net_device *dev) /*fold00*/
{
    lmc_softc_t *sc = dev->priv;
    u32 csr6;
    int i;

    lmc_trace(dev, "lmc_ifdown in");
    
    /* Don't let anything else go on right now */
    //    dev->start = 0;
    netif_stop_queue(dev);
    sc->stats.tx_tbusy1++ ;

    /* stop interrupts */
    /* Clear the interrupt mask */
    LMC_CSR_WRITE (sc, csr_intr, 0x00000000);

    /* Stop Tx and Rx on the chip */
    csr6 = LMC_CSR_READ (sc, csr_command);
    csr6 &= ~LMC_DEC_ST;		/* Turn off the Transmission bit */
    csr6 &= ~LMC_DEC_SR;		/* Turn off the Receive bit */
    LMC_CSR_WRITE (sc, csr_command, csr6);

    sc->stats.rx_missed_errors +=
        LMC_CSR_READ (sc, csr_missed_frames) & 0xffff;

    /* release the interrupt */
    if(sc->got_irq == 1){
        free_irq (dev->irq, dev);
        sc->got_irq = 0;
    }

    /* free skbuffs in the Rx queue */
    for (i = 0; i < LMC_RXDESCS; i++)
    {
        struct sk_buff *skb = sc->lmc_rxq[i];
        sc->lmc_rxq[i] = NULL;
        sc->lmc_rxring[i].status = 0;
        sc->lmc_rxring[i].length = 0;
        sc->lmc_rxring[i].buffer1 = 0xDEADBEEF;
        if (skb != NULL)
            dev_kfree_skb(skb);
        sc->lmc_rxq[i] = NULL;
    }

    for (i = 0; i < LMC_TXDESCS; i++)
    {
        if (sc->lmc_txq[i] != NULL)
            dev_kfree_skb(sc->lmc_txq[i]);
        sc->lmc_txq[i] = NULL;
    }

    lmc_led_off (sc, LMC_MII16_LED_ALL);

    netif_wake_queue(dev);
    sc->stats.tx_tbusy0++ ;

    lmc_trace(dev, "lmc_ifdown out");

    return 0;
}

/* Interrupt handling routine.  This will take an incoming packet, or clean
 * up after a trasmit.
 */
static irqreturn_t lmc_interrupt (int irq, void *dev_instance, struct pt_regs *regs) /*fold00*/
{
    struct net_device *dev = (struct net_device *) dev_instance;
    lmc_softc_t *sc;
    u32 csr;
    int i;
    s32 stat;
    unsigned int badtx;
    u32 firstcsr;
    int max_work = LMC_RXDESCS;
    int handled = 0;

    lmc_trace(dev, "lmc_interrupt in");

    sc = dev->priv;
    
    spin_lock(&sc->lmc_lock);

    /*
     * Read the csr to find what interrupts we have (if any)
     */
    csr = LMC_CSR_READ (sc, csr_status);

    /*
     * Make sure this is our interrupt
     */
    if ( ! (csr & sc->lmc_intrmask)) {
        goto lmc_int_fail_out;
    }

    firstcsr = csr;

    /* always go through this loop at least once */
    while (csr & sc->lmc_intrmask) {
	handled = 1;

        /*
         * Clear interrupt bits, we handle all case below
         */
        LMC_CSR_WRITE (sc, csr_status, csr);

        /*
         * One of
         *  - Transmit process timed out CSR5<1>
         *  - Transmit jabber timeout    CSR5<3>
         *  - Transmit underflow         CSR5<5>
         *  - Transmit Receiver buffer unavailable CSR5<7>
         *  - Receive process stopped    CSR5<8>
         *  - Receive watchdog timeout   CSR5<9>
         *  - Early transmit interrupt   CSR5<10>
         *
         * Is this really right? Should we do a running reset for jabber?
         * (being a WAN card and all)
         */
        if (csr & TULIP_STS_ABNRMLINTR){
            lmc_running_reset (dev);
            break;
        }
        
        if (csr & TULIP_STS_RXINTR){
            lmc_trace(dev, "rx interrupt");
            lmc_rx (dev);
            
        }
        if (csr & (TULIP_STS_TXINTR | TULIP_STS_TXNOBUF | TULIP_STS_TXSTOPPED)) {

	    int		n_compl = 0 ;
            /* reset the transmit timeout detection flag -baz */
            sc->stats.tx_NoCompleteCnt = 0;

            badtx = sc->lmc_taint_tx;
            i = badtx % LMC_TXDESCS;

            while ((badtx < sc->lmc_next_tx)) {
                stat = sc->lmc_txring[i].status;

                LMC_EVENT_LOG (LMC_EVENT_XMTINT, stat,
						 sc->lmc_txring[i].length);
                /*
                 * If bit 31 is 1 the tulip owns it break out of the loop
                 */
                if (stat & 0x80000000)
                    break;

		n_compl++ ;		/* i.e., have an empty slot in ring */
                /*
                 * If we have no skbuff or have cleared it
                 * Already continue to the next buffer
                 */
                if (sc->lmc_txq[i] == NULL)
                    continue;

                /*
                 * Check the total error summary to look for any errors
                 */
                if (stat & 0x8000) {
                    sc->stats.tx_errors++;
                    if (stat & 0x4104)
                        sc->stats.tx_aborted_errors++;
                    if (stat & 0x0C00)
                        sc->stats.tx_carrier_errors++;
                    if (stat & 0x0200)
                        sc->stats.tx_window_errors++;
                    if (stat & 0x0002)
                        sc->stats.tx_fifo_errors++;
                }
                else {
                    
                    sc->stats.tx_bytes += sc->lmc_txring[i].length & 0x7ff;
                    
                    sc->stats.tx_packets++;
                }
                
                //                dev_kfree_skb(sc->lmc_txq[i]);
                dev_kfree_skb_irq(sc->lmc_txq[i]);
                sc->lmc_txq[i] = NULL;

                badtx++;
                i = badtx % LMC_TXDESCS;
            }

            if (sc->lmc_next_tx - badtx > LMC_TXDESCS)
            {
                printk ("%s: out of sync pointer\n", dev->name);
                badtx += LMC_TXDESCS;
            }
            LMC_EVENT_LOG(LMC_EVENT_TBUSY0, n_compl, 0);
            sc->lmc_txfull = 0;
            netif_wake_queue(dev);
            sc->stats.tx_tbusy0++ ;


#ifdef DEBUG
            sc->stats.dirtyTx = badtx;
            sc->stats.lmc_next_tx = sc->lmc_next_tx;
            sc->stats.lmc_txfull = sc->lmc_txfull;
#endif
            sc->lmc_taint_tx = badtx;

            /*
             * Why was there a break here???
             */
        }			/* end handle transmit interrupt */

        if (csr & TULIP_STS_SYSERROR) {
            u32 error;
            printk (KERN_WARNING "%s: system bus error csr: %#8.8x\n", dev->name, csr);
            error = csr>>23 & 0x7;
            switch(error){
            case 0x000:
                printk(KERN_WARNING "%s: Parity Fault (bad)\n", dev->name);
                break;
            case 0x001:
                printk(KERN_WARNING "%s: Master Abort (naughty)\n", dev->name);
                break;
            case 0x010:
                printk(KERN_WARNING "%s: Target Abort (not so naughty)\n", dev->name);
                break;
            default:
                printk(KERN_WARNING "%s: This bus error code was supposed to be reserved!\n", dev->name);
            }
            lmc_dec_reset (sc);
            lmc_reset (sc);
            LMC_EVENT_LOG(LMC_EVENT_RESET1, LMC_CSR_READ (sc, csr_status), 0);
            LMC_EVENT_LOG(LMC_EVENT_RESET2,
                          lmc_mii_readreg (sc, 0, 16),
                          lmc_mii_readreg (sc, 0, 17));

        }

        
        if(max_work-- <= 0)
            break;
        
        /*
         * Get current csr status to make sure
         * we've cleared all interrupts
         */
        csr = LMC_CSR_READ (sc, csr_status);
    }				/* end interrupt loop */
    LMC_EVENT_LOG(LMC_EVENT_INT, firstcsr, csr);

lmc_int_fail_out:

    spin_unlock(&sc->lmc_lock);

    lmc_trace(dev, "lmc_interrupt out");
    return IRQ_RETVAL(handled);
}

static int lmc_start_xmit (struct sk_buff *skb, struct net_device *dev) /*fold00*/
{
    lmc_softc_t *sc;
    u32 flag;
    int entry;
    int ret = 0;
    unsigned long flags;

    lmc_trace(dev, "lmc_start_xmit in");

    sc = dev->priv;

    spin_lock_irqsave(&sc->lmc_lock, flags);

    /* normal path, tbusy known to be zero */

    entry = sc->lmc_next_tx % LMC_TXDESCS;

    sc->lmc_txq[entry] = skb;
    sc->lmc_txring[entry].buffer1 = virt_to_bus (skb->data);

    LMC_CONSOLE_LOG("xmit", skb->data, skb->len);

#ifndef GCOM
    /* If the queue is less than half full, don't interrupt */
    if (sc->lmc_next_tx - sc->lmc_taint_tx < LMC_TXDESCS / 2)
    {
        /* Do not interrupt on completion of this packet */
        flag = 0x60000000;
        netif_wake_queue(dev);
    }
    else if (sc->lmc_next_tx - sc->lmc_taint_tx == LMC_TXDESCS / 2)
    {
        /* This generates an interrupt on completion of this packet */
        flag = 0xe0000000;
        netif_wake_queue(dev);
    }
    else if (sc->lmc_next_tx - sc->lmc_taint_tx < LMC_TXDESCS - 1)
    {
        /* Do not interrupt on completion of this packet */
        flag = 0x60000000;
        netif_wake_queue(dev);
    }
    else
    {
        /* This generates an interrupt on completion of this packet */
        flag = 0xe0000000;
        sc->lmc_txfull = 1;
        netif_stop_queue(dev);
    }
#else
    flag = LMC_TDES_INTERRUPT_ON_COMPLETION;

    if (sc->lmc_next_tx - sc->lmc_taint_tx >= LMC_TXDESCS - 1)
    {				/* ring full, go busy */
        sc->lmc_txfull = 1;
        netif_stop_queue(dev);
        sc->stats.tx_tbusy1++ ;
        LMC_EVENT_LOG(LMC_EVENT_TBUSY1, entry, 0);
    }
#endif


    if (entry == LMC_TXDESCS - 1)	/* last descriptor in ring */
	flag |= LMC_TDES_END_OF_RING;	/* flag as such for Tulip */

    /* don't pad small packets either */
    flag = sc->lmc_txring[entry].length = (skb->len) | flag |
						sc->TxDescriptControlInit;

    /* set the transmit timeout flag to be checked in
     * the watchdog timer handler. -baz
     */

    sc->stats.tx_NoCompleteCnt++;
    sc->lmc_next_tx++;

    /* give ownership to the chip */
    LMC_EVENT_LOG(LMC_EVENT_XMT, flag, entry);
    sc->lmc_txring[entry].status = 0x80000000;

    /* send now! */
    LMC_CSR_WRITE (sc, csr_txpoll, 0);

    dev->trans_start = jiffies;

    spin_unlock_irqrestore(&sc->lmc_lock, flags);

    lmc_trace(dev, "lmc_start_xmit_out");
    return ret;
}


static int lmc_rx (struct net_device *dev) /*fold00*/
{
    lmc_softc_t *sc;
    int i;
    int rx_work_limit = LMC_RXDESCS;
    unsigned int next_rx;
    int rxIntLoopCnt;		/* debug -baz */
    int localLengthErrCnt = 0;
    long stat;
    struct sk_buff *skb, *nsb;
    u16 len;

    lmc_trace(dev, "lmc_rx in");

    sc = dev->priv;

    lmc_led_on(sc, LMC_DS3_LED3);

    rxIntLoopCnt = 0;		/* debug -baz */

    i = sc->lmc_next_rx % LMC_RXDESCS;
    next_rx = sc->lmc_next_rx;

    while (((stat = sc->lmc_rxring[i].status) & LMC_RDES_OWN_BIT) != DESC_OWNED_BY_DC21X4)
    {
        rxIntLoopCnt++;		/* debug -baz */
        len = ((stat & LMC_RDES_FRAME_LENGTH) >> RDES_FRAME_LENGTH_BIT_NUMBER);
        if ((stat & 0x0300) != 0x0300) {  /* Check first segment and last segment */
            if ((stat & 0x0000ffff) != 0x7fff) {
                /* Oversized frame */
                sc->stats.rx_length_errors++;
                goto skip_packet;
            }
        }

        if(stat & 0x00000008){ /* Catch a dribbling bit error */
            sc->stats.rx_errors++;
            sc->stats.rx_frame_errors++;
            goto skip_packet;
        }


        if(stat & 0x00000004){ /* Catch a CRC error by the Xilinx */
            sc->stats.rx_errors++;
            sc->stats.rx_crc_errors++;
            goto skip_packet;
        }


        if (len > LMC_PKT_BUF_SZ){
            sc->stats.rx_length_errors++;
            localLengthErrCnt++;
            goto skip_packet;
        }

        if (len < sc->lmc_crcSize + 2) {
            sc->stats.rx_length_errors++;
            sc->stats.rx_SmallPktCnt++;
            localLengthErrCnt++;
            goto skip_packet;
        }

        if(stat & 0x00004000){
            printk(KERN_WARNING "%s: Receiver descriptor error, receiver out of sync?\n", dev->name);
        }

        len -= sc->lmc_crcSize;

        skb = sc->lmc_rxq[i];

        /*
         * We ran out of memory at some point
         * just allocate an skb buff and continue.
         */
        
        if(skb == 0x0){
            nsb = dev_alloc_skb (LMC_PKT_BUF_SZ + 2);
            if (nsb) {
                sc->lmc_rxq[i] = nsb;
                nsb->dev = dev;
                sc->lmc_rxring[i].buffer1 = virt_to_bus (nsb->tail);
            }
            sc->failed_recv_alloc = 1;
            goto skip_packet;
        }
        
        dev->last_rx = jiffies;
        sc->stats.rx_packets++;
        sc->stats.rx_bytes += len;

        LMC_CONSOLE_LOG("recv", skb->data, len);

        /*
         * I'm not sure of the sanity of this
         * Packets could be arriving at a constant
         * 44.210mbits/sec and we're going to copy
         * them into a new buffer??
         */
        
        if(len > (LMC_MTU - (LMC_MTU>>2))){ /* len > LMC_MTU * 0.75 */
            /*
             * If it's a large packet don't copy it just hand it up
             */
        give_it_anyways:

            sc->lmc_rxq[i] = NULL;
            sc->lmc_rxring[i].buffer1 = 0x0;

            skb_put (skb, len);
            skb->protocol = lmc_proto_type(sc, skb);
            skb->protocol = htons(ETH_P_WAN_PPP);
            skb->mac.raw = skb->data;
//            skb->nh.raw = skb->data;
            skb->dev = dev;
            lmc_proto_netif(sc, skb);

            /*
             * This skb will be destroyed by the upper layers, make a new one
             */
            nsb = dev_alloc_skb (LMC_PKT_BUF_SZ + 2);
            if (nsb) {
                sc->lmc_rxq[i] = nsb;
                nsb->dev = dev;
                sc->lmc_rxring[i].buffer1 = virt_to_bus (nsb->tail);
                /* Transferred to 21140 below */
            }
            else {
                /*
                 * We've run out of memory, stop trying to allocate
                 * memory and exit the interrupt handler
                 *
                 * The chip may run out of receivers and stop
                 * in which care we'll try to allocate the buffer
                 * again.  (once a second)
                 */
                sc->stats.rx_BuffAllocErr++;
                LMC_EVENT_LOG(LMC_EVENT_RCVINT, stat, len);
                sc->failed_recv_alloc = 1;
                goto skip_out_of_mem;
            }
        }
        else {
            nsb = dev_alloc_skb(len);
            if(!nsb) {
                goto give_it_anyways;
            }
            memcpy(skb_put(nsb, len), skb->data, len);
            
            nsb->protocol = lmc_proto_type(sc, skb);
            nsb->mac.raw = nsb->data;
//            nsb->nh.raw = nsb->data;
            nsb->dev = dev;
            lmc_proto_netif(sc, nsb);
        }

    skip_packet:
        LMC_EVENT_LOG(LMC_EVENT_RCVINT, stat, len);
        sc->lmc_rxring[i].status = DESC_OWNED_BY_DC21X4;

        sc->lmc_next_rx++;
        i = sc->lmc_next_rx % LMC_RXDESCS;
        rx_work_limit--;
        if (rx_work_limit < 0)
            break;
    }

    /* detect condition for LMC1000 where DSU cable attaches and fills
     * descriptors with bogus packets
     *
    if (localLengthErrCnt > LMC_RXDESCS - 3) {
        sc->stats.rx_BadPktSurgeCnt++;
        LMC_EVENT_LOG(LMC_EVENT_BADPKTSURGE,
                      localLengthErrCnt,
                      sc->stats.rx_BadPktSurgeCnt);
    } */

    /* save max count of receive descriptors serviced */
    if (rxIntLoopCnt > sc->stats.rxIntLoopCnt) {
        sc->stats.rxIntLoopCnt = rxIntLoopCnt;	/* debug -baz */
    }

#ifdef DEBUG
    if (rxIntLoopCnt == 0)
    {
        for (i = 0; i < LMC_RXDESCS; i++)
        {
            if ((sc->lmc_rxring[i].status & LMC_RDES_OWN_BIT)
                != DESC_OWNED_BY_DC21X4)
            {
                rxIntLoopCnt++;
            }
        }
        LMC_EVENT_LOG(LMC_EVENT_RCVEND, rxIntLoopCnt, 0);
    }
#endif


    lmc_led_off(sc, LMC_DS3_LED3);

skip_out_of_mem:

    lmc_trace(dev, "lmc_rx out");

    return 0;
}

static struct net_device_stats *lmc_get_stats (struct net_device *dev) /*fold00*/
{
    lmc_softc_t *sc = dev->priv;
    unsigned long flags;

    lmc_trace(dev, "lmc_get_stats in");


    spin_lock_irqsave(&sc->lmc_lock, flags);

    sc->stats.rx_missed_errors += LMC_CSR_READ (sc, csr_missed_frames) & 0xffff;

    spin_unlock_irqrestore(&sc->lmc_lock, flags);

    lmc_trace(dev, "lmc_get_stats out");

    return (struct net_device_stats *) &sc->stats;
}

static struct pci_driver lmc_driver = {
	.name		= "lmc",
	.id_table	= lmc_pci_tbl,
	.probe		= lmc_init_one,
	.remove		= __devexit_p(lmc_remove_one),
};

static int __init init_lmc(void)
{
    return pci_module_init(&lmc_driver);
}

static void __exit exit_lmc(void)
{
    pci_unregister_driver(&lmc_driver);
}

module_init(init_lmc);
module_exit(exit_lmc);

unsigned lmc_mii_readreg (lmc_softc_t * const sc, unsigned devaddr, unsigned regno) /*fold00*/
{
    int i;
    int command = (0xf6 << 10) | (devaddr << 5) | regno;
    int retval = 0;

    lmc_trace(sc->lmc_device, "lmc_mii_readreg in");

    LMC_MII_SYNC (sc);

    lmc_trace(sc->lmc_device, "lmc_mii_readreg: done sync");

    for (i = 15; i >= 0; i--)
    {
        int dataval = (command & (1 << i)) ? 0x20000 : 0;

        LMC_CSR_WRITE (sc, csr_9, dataval);
        lmc_delay ();
        /* __SLOW_DOWN_IO; */
        LMC_CSR_WRITE (sc, csr_9, dataval | 0x10000);
        lmc_delay ();
        /* __SLOW_DOWN_IO; */
    }

    lmc_trace(sc->lmc_device, "lmc_mii_readreg: done1");

    for (i = 19; i > 0; i--)
    {
        LMC_CSR_WRITE (sc, csr_9, 0x40000);
        lmc_delay ();
        /* __SLOW_DOWN_IO; */
        retval = (retval << 1) | ((LMC_CSR_READ (sc, csr_9) & 0x80000) ? 1 : 0);
        LMC_CSR_WRITE (sc, csr_9, 0x40000 | 0x10000);
        lmc_delay ();
        /* __SLOW_DOWN_IO; */
    }

    lmc_trace(sc->lmc_device, "lmc_mii_readreg out");

    return (retval >> 1) & 0xffff;
}

void lmc_mii_writereg (lmc_softc_t * const sc, unsigned devaddr, unsigned regno, unsigned data) /*fold00*/
{
    int i = 32;
    int command = (0x5002 << 16) | (devaddr << 23) | (regno << 18) | data;

    lmc_trace(sc->lmc_device, "lmc_mii_writereg in");

    LMC_MII_SYNC (sc);

    i = 31;
    while (i >= 0)
    {
        int datav;

        if (command & (1 << i))
            datav = 0x20000;
        else
            datav = 0x00000;

        LMC_CSR_WRITE (sc, csr_9, datav);
        lmc_delay ();
        /* __SLOW_DOWN_IO; */
        LMC_CSR_WRITE (sc, csr_9, (datav | 0x10000));
        lmc_delay ();
        /* __SLOW_DOWN_IO; */
        i--;
    }

    i = 2;
    while (i > 0)
    {
        LMC_CSR_WRITE (sc, csr_9, 0x40000);
        lmc_delay ();
        /* __SLOW_DOWN_IO; */
        LMC_CSR_WRITE (sc, csr_9, 0x50000);
        lmc_delay ();
        /* __SLOW_DOWN_IO; */
        i--;
    }

    lmc_trace(sc->lmc_device, "lmc_mii_writereg out");
}

static void lmc_softreset (lmc_softc_t * const sc) /*fold00*/
{
    int i;

    lmc_trace(sc->lmc_device, "lmc_softreset in");

    /* Initialize the receive rings and buffers. */
    sc->lmc_txfull = 0;
    sc->lmc_next_rx = 0;
    sc->lmc_next_tx = 0;
    sc->lmc_taint_rx = 0;
    sc->lmc_taint_tx = 0;

    /*
     * Setup each one of the receiver buffers
     * allocate an skbuff for each one, setup the descriptor table
     * and point each buffer at the next one
     */

    for (i = 0; i < LMC_RXDESCS; i++)
    {
        struct sk_buff *skb;

        if (sc->lmc_rxq[i] == NULL)
        {
            skb = dev_alloc_skb (LMC_PKT_BUF_SZ + 2);
            if(skb == NULL){
                printk(KERN_WARNING "%s: Failed to allocate receiver ring, will try again\n", sc->name);
                sc->failed_ring = 1;
                break;
            }
            else{
                sc->lmc_rxq[i] = skb;
            }
        }
        else
        {
            skb = sc->lmc_rxq[i];
        }

        skb->dev = sc->lmc_device;

        /* owned by 21140 */
        sc->lmc_rxring[i].status = 0x80000000;

        /* used to be PKT_BUF_SZ now uses skb since we lose some to head room */
        sc->lmc_rxring[i].length = skb->end - skb->data;

        /* use to be tail which is dumb since you're thinking why write
         * to the end of the packj,et but since there's nothing there tail == data
         */
        sc->lmc_rxring[i].buffer1 = virt_to_bus (skb->data);

        /* This is fair since the structure is static and we have the next address */
        sc->lmc_rxring[i].buffer2 = virt_to_bus (&sc->lmc_rxring[i + 1]);

    }

    /*
     * Sets end of ring
     */
    sc->lmc_rxring[i - 1].length |= 0x02000000; /* Set end of buffers flag */
    sc->lmc_rxring[i - 1].buffer2 = virt_to_bus (&sc->lmc_rxring[0]); /* Point back to the start */
    LMC_CSR_WRITE (sc, csr_rxlist, virt_to_bus (sc->lmc_rxring)); /* write base address */


    /* Initialize the transmit rings and buffers */
    for (i = 0; i < LMC_TXDESCS; i++)
    {
        if (sc->lmc_txq[i] != NULL){		/* have buffer */
            dev_kfree_skb(sc->lmc_txq[i]);	/* free it */
            sc->stats.tx_dropped++;      /* We just dropped a packet */
        }
        sc->lmc_txq[i] = NULL;
        sc->lmc_txring[i].status = 0x00000000;
        sc->lmc_txring[i].buffer2 = virt_to_bus (&sc->lmc_txring[i + 1]);
    }
    sc->lmc_txring[i - 1].buffer2 = virt_to_bus (&sc->lmc_txring[0]);
    LMC_CSR_WRITE (sc, csr_txlist, virt_to_bus (sc->lmc_txring));

    lmc_trace(sc->lmc_device, "lmc_softreset out");
}

void lmc_gpio_mkinput(lmc_softc_t * const sc, u_int32_t bits) /*fold00*/
{
    lmc_trace(sc->lmc_device, "lmc_gpio_mkinput in");
    sc->lmc_gpio_io &= ~bits;
    LMC_CSR_WRITE(sc, csr_gp, TULIP_GP_PINSET | (sc->lmc_gpio_io));
    lmc_trace(sc->lmc_device, "lmc_gpio_mkinput out");
}

void lmc_gpio_mkoutput(lmc_softc_t * const sc, u_int32_t bits) /*fold00*/
{
    lmc_trace(sc->lmc_device, "lmc_gpio_mkoutput in");
    sc->lmc_gpio_io |= bits;
    LMC_CSR_WRITE(sc, csr_gp, TULIP_GP_PINSET | (sc->lmc_gpio_io));
    lmc_trace(sc->lmc_device, "lmc_gpio_mkoutput out");
}

void lmc_led_on(lmc_softc_t * const sc, u_int32_t led) /*fold00*/
{
    lmc_trace(sc->lmc_device, "lmc_led_on in");
    if((~sc->lmc_miireg16) & led){ /* Already on! */
        lmc_trace(sc->lmc_device, "lmc_led_on aon out");
        return;
    }
    
    sc->lmc_miireg16 &= ~led;
    lmc_mii_writereg(sc, 0, 16, sc->lmc_miireg16);
    lmc_trace(sc->lmc_device, "lmc_led_on out");
}

void lmc_led_off(lmc_softc_t * const sc, u_int32_t led) /*fold00*/
{
    lmc_trace(sc->lmc_device, "lmc_led_off in");
    if(sc->lmc_miireg16 & led){ /* Already set don't do anything */
        lmc_trace(sc->lmc_device, "lmc_led_off aoff out");
        return;
    }
    
    sc->lmc_miireg16 |= led;
    lmc_mii_writereg(sc, 0, 16, sc->lmc_miireg16);
    lmc_trace(sc->lmc_device, "lmc_led_off out");
}

static void lmc_reset(lmc_softc_t * const sc) /*fold00*/
{
    lmc_trace(sc->lmc_device, "lmc_reset in");
    sc->lmc_miireg16 |= LMC_MII16_FIFO_RESET;
    lmc_mii_writereg(sc, 0, 16, sc->lmc_miireg16);

    sc->lmc_miireg16 &= ~LMC_MII16_FIFO_RESET;
    lmc_mii_writereg(sc, 0, 16, sc->lmc_miireg16);

    /*
     * make some of the GPIO pins be outputs
     */
    lmc_gpio_mkoutput(sc, LMC_GEP_RESET);

    /*
     * RESET low to force state reset.  This also forces
     * the transmitter clock to be internal, but we expect to reset
     * that later anyway.
     */
    sc->lmc_gpio &= ~(LMC_GEP_RESET);
    LMC_CSR_WRITE(sc, csr_gp, sc->lmc_gpio);

    /*
     * hold for more than 10 microseconds
     */
    udelay(50);

    /*
     * stop driving Xilinx-related signals
     */
    lmc_gpio_mkinput(sc, LMC_GEP_RESET);

    /*
     * Call media specific init routine
     */
    sc->lmc_media->init(sc);

    sc->stats.resetCount++;
    lmc_trace(sc->lmc_device, "lmc_reset out");
}

static void lmc_dec_reset(lmc_softc_t * const sc) /*fold00*/
{
    u_int32_t val;
    lmc_trace(sc->lmc_device, "lmc_dec_reset in");

    /*
     * disable all interrupts
     */
    sc->lmc_intrmask = 0;
    LMC_CSR_WRITE(sc, csr_intr, sc->lmc_intrmask);

    /*
     * Reset the chip with a software reset command.
     * Wait 10 microseconds (actually 50 PCI cycles but at
     * 33MHz that comes to two microseconds but wait a
     * bit longer anyways)
     */
    LMC_CSR_WRITE(sc, csr_busmode, TULIP_BUSMODE_SWRESET);
    udelay(25);
#ifdef __sparc__
    sc->lmc_busmode = LMC_CSR_READ(sc, csr_busmode);
    sc->lmc_busmode = 0x00100000;
    sc->lmc_busmode &= ~TULIP_BUSMODE_SWRESET;
    LMC_CSR_WRITE(sc, csr_busmode, sc->lmc_busmode);
#endif
    sc->lmc_cmdmode = LMC_CSR_READ(sc, csr_command);

    /*
     * We want:
     *   no ethernet address in frames we write
     *   disable padding (txdesc, padding disable)
     *   ignore runt frames (rdes0 bit 15)
     *   no receiver watchdog or transmitter jabber timer
     *       (csr15 bit 0,14 == 1)
     *   if using 16-bit CRC, turn off CRC (trans desc, crc disable)
     */

    sc->lmc_cmdmode |= ( TULIP_CMD_PROMISCUOUS
                         | TULIP_CMD_FULLDUPLEX
                         | TULIP_CMD_PASSBADPKT
                         | TULIP_CMD_NOHEARTBEAT
                         | TULIP_CMD_PORTSELECT
                         | TULIP_CMD_RECEIVEALL
                         | TULIP_CMD_MUSTBEONE
                       );
    sc->lmc_cmdmode &= ~( TULIP_CMD_OPERMODE
                          | TULIP_CMD_THRESHOLDCTL
                          | TULIP_CMD_STOREFWD
                          | TULIP_CMD_TXTHRSHLDCTL
                        );

    LMC_CSR_WRITE(sc, csr_command, sc->lmc_cmdmode);

    /*
     * disable receiver watchdog and transmit jabber
     */
    val = LMC_CSR_READ(sc, csr_sia_general);
    val |= (TULIP_WATCHDOG_TXDISABLE | TULIP_WATCHDOG_RXDISABLE);
    LMC_CSR_WRITE(sc, csr_sia_general, val);

    lmc_trace(sc->lmc_device, "lmc_dec_reset out");
}

static void lmc_initcsrs(lmc_softc_t * const sc, lmc_csrptr_t csr_base, /*fold00*/
                         size_t csr_size)
{
    lmc_trace(sc->lmc_device, "lmc_initcsrs in");
    sc->lmc_csrs.csr_busmode	        = csr_base +  0 * csr_size;
    sc->lmc_csrs.csr_txpoll		= csr_base +  1 * csr_size;
    sc->lmc_csrs.csr_rxpoll		= csr_base +  2 * csr_size;
    sc->lmc_csrs.csr_rxlist		= csr_base +  3 * csr_size;
    sc->lmc_csrs.csr_txlist		= csr_base +  4 * csr_size;
    sc->lmc_csrs.csr_status		= csr_base +  5 * csr_size;
    sc->lmc_csrs.csr_command	        = csr_base +  6 * csr_size;
    sc->lmc_csrs.csr_intr		= csr_base +  7 * csr_size;
    sc->lmc_csrs.csr_missed_frames	= csr_base +  8 * csr_size;
    sc->lmc_csrs.csr_9		        = csr_base +  9 * csr_size;
    sc->lmc_csrs.csr_10		        = csr_base + 10 * csr_size;
    sc->lmc_csrs.csr_11		        = csr_base + 11 * csr_size;
    sc->lmc_csrs.csr_12		        = csr_base + 12 * csr_size;
    sc->lmc_csrs.csr_13		        = csr_base + 13 * csr_size;
    sc->lmc_csrs.csr_14		        = csr_base + 14 * csr_size;
    sc->lmc_csrs.csr_15		        = csr_base + 15 * csr_size;
    lmc_trace(sc->lmc_device, "lmc_initcsrs out");
}

static void lmc_driver_timeout(struct net_device *dev) { /*fold00*/
    lmc_softc_t *sc;
    u32 csr6;
    unsigned long flags;

    lmc_trace(dev, "lmc_driver_timeout in");

    sc = dev->priv;

    spin_lock_irqsave(&sc->lmc_lock, flags);

    printk("%s: Xmitter busy|\n", dev->name);

    sc->stats.tx_tbusy_calls++ ;
    if (jiffies - dev->trans_start < TX_TIMEOUT) {
        goto bug_out;
    }

    /*
     * Chip seems to have locked up
     * Reset it
     * This whips out all our decriptor
     * table and starts from scartch
     */

    LMC_EVENT_LOG(LMC_EVENT_XMTPRCTMO,
                  LMC_CSR_READ (sc, csr_status),
                  sc->stats.tx_ProcTimeout);

    lmc_running_reset (dev);

    LMC_EVENT_LOG(LMC_EVENT_RESET1, LMC_CSR_READ (sc, csr_status), 0);
    LMC_EVENT_LOG(LMC_EVENT_RESET2,
                  lmc_mii_readreg (sc, 0, 16),
                  lmc_mii_readreg (sc, 0, 17));

    /* restart the tx processes */
    csr6 = LMC_CSR_READ (sc, csr_command);
    LMC_CSR_WRITE (sc, csr_command, csr6 | 0x0002);
    LMC_CSR_WRITE (sc, csr_command, csr6 | 0x2002);

    /* immediate transmit */
    LMC_CSR_WRITE (sc, csr_txpoll, 0);

    sc->stats.tx_errors++;
    sc->stats.tx_ProcTimeout++;	/* -baz */

    dev->trans_start = jiffies;

bug_out:

    spin_unlock_irqrestore(&sc->lmc_lock, flags);

    lmc_trace(dev, "lmc_driver_timout out");


}
