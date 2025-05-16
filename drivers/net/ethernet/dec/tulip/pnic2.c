/*
	drivers/net/ethernet/dec/tulip/pnic2.c

	Copyright 2000,2001  The Linux Kernel Team
	Written/copyright 1994-2001 by Donald Becker.
        Modified to hep support PNIC_II by Kevin B. Hendricks

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

        Please submit bugs to http://bugzilla.kernel.org/ .
*/


/* Understanding the PNIC_II - everything is this file is based
 * on the PNIC_II_PDF datasheet which is sorely lacking in detail
 *
 * As I understand things, here are the registers and bits that
 * explain the masks and constants used in this file that are
 * either different from the 21142/3 or important for basic operation.
 *
 *
 * CSR 6  (mask = 0xfe3bd1fd of bits not to change)
 * -----
 * Bit 24    - SCR
 * Bit 23    - PCS
 * Bit 22    - TTM (Trasmit Threshold Mode)
 * Bit 18    - Port Select
 * Bit 13    - Start - 1, Stop - 0 Transmissions
 * Bit 11:10 - Loop Back Operation Mode
 * Bit 9     - Full Duplex mode (Advertise 10BaseT-FD is CSR14<7> is set)
 * Bit 1     - Start - 1, Stop - 0 Receive
 *
 *
 * CSR 14  (mask = 0xfff0ee39 of bits not to change)
 * ------
 * Bit 19    - PAUSE-Pause
 * Bit 18    - Advertise T4
 * Bit 17    - Advertise 100baseTx-FD
 * Bit 16    - Advertise 100baseTx-HD
 * Bit 12    - LTE - Link Test Enable
 * Bit 7     - ANE - Auto Negotiate Enable
 * Bit 6     - HDE - Advertise 10baseT-HD
 * Bit 2     - Reset to Power down - kept as 1 for normal operation
 * Bit 1     -  Loop Back enable for 10baseT MCC
 *
 *
 * CSR 12
 * ------
 * Bit 25    - Partner can do T4
 * Bit 24    - Partner can do 100baseTx-FD
 * Bit 23    - Partner can do 100baseTx-HD
 * Bit 22    - Partner can do 10baseT-FD
 * Bit 21    - Partner can do 10baseT-HD
 * Bit 15    - LPN is 1 if all above bits are valid other wise 0
 * Bit 14:12 - autonegotiation state (write 001 to start autonegotiate)
 * Bit 3     - Autopolarity state
 * Bit 2     - LS10B - link state of 10baseT 0 - good, 1 - failed
 * Bit 1     - LS100B - link state of 100baseT 0 - good, 1 - failed
 *
 *
 * Data Port Selection Info
 *-------------------------
 *
 * CSR14<7>   CSR6<18>    CSR6<22>    CSR6<23>    CSR6<24>   MODE/PORT
 *   1           0           0 (X)       0 (X)       1        NWAY
 *   0           0           1           0 (X)       0        10baseT
 *   0           1           0           1           1 (X)    100baseT
 *
 *
 */



#include "tulip.h"
#include <linux/delay.h>


void pnic2_timer(struct timer_list *t)
{
	struct tulip_private *tp = from_timer(tp, t, timer);
	struct net_device *dev = tp->dev;
	void __iomem *ioaddr = tp->base_addr;
	int next_tick = 60*HZ;

	if (tulip_debug > 3)
		dev_info(&dev->dev, "PNIC2 negotiation status %08x\n",
			 ioread32(ioaddr + CSR12));

	if (next_tick) {
		mod_timer(&tp->timer, RUN_AT(next_tick));
	}
}


void pnic2_start_nway(struct net_device *dev)
{
	struct tulip_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->base_addr;
        int csr14;
        int csr12;

        /* set up what to advertise during the negotiation */

        /* load in csr14  and mask off bits not to touch
         * comment at top of file explains mask value
         */
	csr14 = (ioread32(ioaddr + CSR14) & 0xfff0ee39);

        /* bit 17 - advetise 100baseTx-FD */
        if (tp->sym_advertise & 0x0100) csr14 |= 0x00020000;

        /* bit 16 - advertise 100baseTx-HD */
        if (tp->sym_advertise & 0x0080) csr14 |= 0x00010000;

        /* bit 6 - advertise 10baseT-HD */
        if (tp->sym_advertise & 0x0020) csr14 |= 0x00000040;

        /* Now set bit 12 Link Test Enable, Bit 7 Autonegotiation Enable
         * and bit 0 Don't PowerDown 10baseT
         */
        csr14 |= 0x00001184;

	if (tulip_debug > 1)
		netdev_dbg(dev, "Restarting PNIC2 autonegotiation, csr14=%08x\n",
			   csr14);

        /* tell pnic2_lnk_change we are doing an nway negotiation */
	dev->if_port = 0;
	tp->nway = tp->mediasense = 1;
	tp->nwayset = tp->lpar = 0;

        /* now we have to set up csr6 for NWAY state */

	tp->csr6 = ioread32(ioaddr + CSR6);
	if (tulip_debug > 1)
		netdev_dbg(dev, "On Entry to Nway, csr6=%08x\n", tp->csr6);

        /* mask off any bits not to touch
         * comment at top of file explains mask value
         */
	tp->csr6 = tp->csr6 & 0xfe3bd1fd;

        /* don't forget that bit 9 is also used for advertising */
        /* advertise 10baseT-FD for the negotiation (bit 9) */
        if (tp->sym_advertise & 0x0040) tp->csr6 |= 0x00000200;

        /* set bit 24 for nway negotiation mode ...
         * see Data Port Selection comment at top of file
         * and "Stop" - reset both Transmit (bit 13) and Receive (bit 1)
         */
        tp->csr6 |= 0x01000000;
	iowrite32(csr14, ioaddr + CSR14);
	iowrite32(tp->csr6, ioaddr + CSR6);
        udelay(100);

        /* all set up so now force the negotiation to begin */

        /* read in current values and mask off all but the
	 * Autonegotiation bits 14:12.  Writing a 001 to those bits
         * should start the autonegotiation
         */
        csr12 = (ioread32(ioaddr + CSR12) & 0xffff8fff);
        csr12 |= 0x1000;
	iowrite32(csr12, ioaddr + CSR12);
}



void pnic2_lnk_change(struct net_device *dev, int csr5)
{
	struct tulip_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->base_addr;
        int csr14;

        /* read the staus register to find out what is up */
	int csr12 = ioread32(ioaddr + CSR12);

	if (tulip_debug > 1)
		dev_info(&dev->dev,
			 "PNIC2 link status interrupt %08x,  CSR5 %x, %08x\n",
			 csr12, csr5, ioread32(ioaddr + CSR14));

	/* If NWay finished and we have a negotiated partner capability.
         * check bits 14:12 for bit pattern 101 - all is good
         */
	if (tp->nway  &&  !tp->nwayset) {

	        /* we did an auto negotiation */

                if ((csr12 & 0x7000) == 0x5000) {

	               /* negotiation ended successfully */

	               /* get the link partners reply and mask out all but
                        * bits 24-21 which show the partners capabilities
                        * and match those to what we advertised
                        *
                        * then begin to interpret the results of the negotiation.
                        * Always go in this order : (we are ignoring T4 for now)
                        *     100baseTx-FD, 100baseTx-HD, 10baseT-FD, 10baseT-HD
                        */

		        int negotiated = ((csr12 >> 16) & 0x01E0) & tp->sym_advertise;
		        tp->lpar = (csr12 >> 16);
		        tp->nwayset = 1;

                        if (negotiated & 0x0100)        dev->if_port = 5;
		        else if (negotiated & 0x0080)	dev->if_port = 3;
		        else if (negotiated & 0x0040)	dev->if_port = 4;
			else if (negotiated & 0x0020)	dev->if_port = 0;
			else {
			     if (tulip_debug > 1)
				     dev_info(&dev->dev,
					      "funny autonegotiate result csr12 %08x advertising %04x\n",
					      csr12, tp->sym_advertise);
			     tp->nwayset = 0;
			     /* so check  if 100baseTx link state is okay */
			     if ((csr12 & 2) == 0  &&  (tp->sym_advertise & 0x0180))
			       dev->if_port = 3;
			}

			/* now record the duplex that was negotiated */
			tp->full_duplex = 0;
			if ((dev->if_port == 4) || (dev->if_port == 5))
			       tp->full_duplex = 1;

			if (tulip_debug > 1) {
			       if (tp->nwayset)
				       dev_info(&dev->dev,
						"Switching to %s based on link negotiation %04x & %04x = %04x\n",
						medianame[dev->if_port],
						tp->sym_advertise, tp->lpar,
						negotiated);
			}

                        /* remember to turn off bit 7 - autonegotiate
                         * enable so we can properly end nway mode and
                         * set duplex (ie. use csr6<9> again)
                         */
	                csr14 = (ioread32(ioaddr + CSR14) & 0xffffff7f);
                        iowrite32(csr14,ioaddr + CSR14);


                        /* now set the data port and operating mode
			 * (see the Data Port Selection comments at
			 * the top of the file
			 */

			/* get current csr6 and mask off bits not to touch */
			/* see comment at top of file */

			tp->csr6 = (ioread32(ioaddr + CSR6) & 0xfe3bd1fd);

			/* so if using if_port 3 or 5 then select the 100baseT
			 * port else select the 10baseT port.
			 * See the Data Port Selection table at the top
			 * of the file which was taken from the PNIC_II.PDF
			 * datasheet
			 */
			if (dev->if_port & 1) tp->csr6 |= 0x01840000;
			else tp->csr6 |= 0x00400000;

			/* now set the full duplex bit appropriately */
			if (tp->full_duplex) tp->csr6 |= 0x00000200;

			iowrite32(1, ioaddr + CSR13);

			if (tulip_debug > 2)
				netdev_dbg(dev, "Setting CSR6 %08x/%x CSR12 %08x\n",
					   tp->csr6,
					   ioread32(ioaddr + CSR6),
					   ioread32(ioaddr + CSR12));

			/* now the following actually writes out the
			 * new csr6 values
			 */
			tulip_start_rxtx(tp);

                        return;

	        } else {
	                dev_info(&dev->dev,
				 "Autonegotiation failed, using %s, link beat status %04x\n",
				 medianame[dev->if_port], csr12);

                        /* remember to turn off bit 7 - autonegotiate
                         * enable so we don't forget
                         */
	                csr14 = (ioread32(ioaddr + CSR14) & 0xffffff7f);
                        iowrite32(csr14,ioaddr + CSR14);

                        /* what should we do when autonegotiate fails?
                         * should we try again or default to baseline
                         * case.  I just don't know.
                         *
                         * for now default to some baseline case
                         */

	                 dev->if_port = 0;
                         tp->nway = 0;
                         tp->nwayset = 1;

                         /* set to 10baseTx-HD - see Data Port Selection
                          * comment given at the top of the file
                          */
	                 tp->csr6 = (ioread32(ioaddr + CSR6) & 0xfe3bd1fd);
                         tp->csr6 |= 0x00400000;

	                 tulip_restart_rxtx(tp);

                         return;

		}
	}

	if ((tp->nwayset  &&  (csr5 & 0x08000000) &&
	     (dev->if_port == 3  ||  dev->if_port == 5) &&
	     (csr12 & 2) == 2) || (tp->nway && (csr5 & (TPLnkFail)))) {

		/* Link blew? Maybe restart NWay. */

		if (tulip_debug > 2)
			netdev_dbg(dev, "Ugh! Link blew?\n");

		timer_delete_sync(&tp->timer);
		pnic2_start_nway(dev);
		tp->timer.expires = RUN_AT(3*HZ);
		add_timer(&tp->timer);

                return;
	}


        if (dev->if_port == 3  ||  dev->if_port == 5) {

	        /* we are at 100mb and a potential link change occurred */

		if (tulip_debug > 1)
			dev_info(&dev->dev, "PNIC2 %s link beat %s\n",
				 medianame[dev->if_port],
				 (csr12 & 2) ? "failed" : "good");

                /* check 100 link beat */

                tp->nway = 0;
                tp->nwayset = 1;

                /* if failed then try doing an nway to get in sync */
		if ((csr12 & 2)  &&  ! tp->medialock) {
			timer_delete_sync(&tp->timer);
			pnic2_start_nway(dev);
			tp->timer.expires = RUN_AT(3*HZ);
			add_timer(&tp->timer);
                }

                return;
        }

	if (dev->if_port == 0  ||  dev->if_port == 4) {

	        /* we are at 10mb and a potential link change occurred */

		if (tulip_debug > 1)
			dev_info(&dev->dev, "PNIC2 %s link beat %s\n",
				 medianame[dev->if_port],
				 (csr12 & 4) ? "failed" : "good");


                tp->nway = 0;
                tp->nwayset = 1;

                /* if failed, try doing an nway to get in sync */
		if ((csr12 & 4)  &&  ! tp->medialock) {
			timer_delete_sync(&tp->timer);
			pnic2_start_nway(dev);
			tp->timer.expires = RUN_AT(3*HZ);
			add_timer(&tp->timer);
                }

                return;
        }


	if (tulip_debug > 1)
		dev_info(&dev->dev, "PNIC2 Link Change Default?\n");

        /* if all else fails default to trying 10baseT-HD */
	dev->if_port = 0;

        /* make sure autonegotiate enable is off */
	csr14 = (ioread32(ioaddr + CSR14) & 0xffffff7f);
        iowrite32(csr14,ioaddr + CSR14);

        /* set to 10baseTx-HD - see Data Port Selection
         * comment given at the top of the file
         */
	tp->csr6 = (ioread32(ioaddr + CSR6) & 0xfe3bd1fd);
        tp->csr6 |= 0x00400000;

	tulip_restart_rxtx(tp);
}

