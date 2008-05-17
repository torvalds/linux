/*
 *	Wavelan Pcmcia driver
 *
 *		Jean II - HPLB '96
 *
 * Reorganisation and extension of the driver.
 * Original copyright follow. See wavelan_cs.p.h for details.
 *
 * This code is derived from Anthony D. Joseph's code and all the changes here
 * are also under the original copyright below.
 *
 * This code supports version 2.00 of WaveLAN/PCMCIA cards (2.4GHz), and
 * can work on Linux 2.0.36 with support of David Hinds' PCMCIA Card Services
 *
 * Joe Finney (joe@comp.lancs.ac.uk) at Lancaster University in UK added
 * critical code in the routine to initialize the Modem Management Controller.
 *
 * Thanks to Alan Cox and Bruce Janson for their advice.
 *
 *	-- Yunzhou Li (scip4166@nus.sg)
 *
#ifdef WAVELAN_ROAMING	
 * Roaming support added 07/22/98 by Justin Seger (jseger@media.mit.edu)
 * based on patch by Joe Finney from Lancaster University.
#endif
 *
 * Lucent (formerly AT&T GIS, formerly NCR) WaveLAN PCMCIA card: An
 * Ethernet-like radio transceiver controlled by an Intel 82593 coprocessor.
 *
 *   A non-shared memory PCMCIA ethernet driver for linux
 *
 * ISA version modified to support PCMCIA by Anthony Joseph (adj@lcs.mit.edu)
 *
 *
 * Joseph O'Sullivan & John Langford (josullvn@cs.cmu.edu & jcl@cs.cmu.edu)
 *
 * Apr 2 '98  made changes to bring the i82593 control/int handling in line
 *             with offical specs...
 *
 ****************************************************************************
 *   Copyright 1995
 *   Anthony D. Joseph
 *   Massachusetts Institute of Technology
 *
 *   Permission to use, copy, modify, and distribute this program
 *   for any purpose and without fee is hereby granted, provided
 *   that this copyright and permission notice appear on all copies
 *   and supporting documentation, the name of M.I.T. not be used
 *   in advertising or publicity pertaining to distribution of the
 *   program without specific prior permission, and notice be given
 *   in supporting documentation that copying and distribution is
 *   by permission of M.I.T.  M.I.T. makes no representations about
 *   the suitability of this software for any purpose.  It is pro-
 *   vided "as is" without express or implied warranty.         
 ****************************************************************************
 *
 */

/* Do *NOT* add other headers here, you are guaranteed to be wrong - Jean II */
#include "wavelan_cs.p.h"		/* Private header */

#ifdef WAVELAN_ROAMING
static void wl_cell_expiry(unsigned long data);
static void wl_del_wavepoint(wavepoint_history *wavepoint, struct net_local *lp);
static void wv_nwid_filter(unsigned char mode, net_local *lp);
#endif  /*  WAVELAN_ROAMING  */

/************************* MISC SUBROUTINES **************************/
/*
 * Subroutines which won't fit in one of the following category
 * (wavelan modem or i82593)
 */

/******************* MODEM MANAGEMENT SUBROUTINES *******************/
/*
 * Useful subroutines to manage the modem of the wavelan
 */

/*------------------------------------------------------------------*/
/*
 * Read from card's Host Adaptor Status Register.
 */
static inline u_char
hasr_read(u_long	base)
{
  return(inb(HASR(base)));
} /* hasr_read */

/*------------------------------------------------------------------*/
/*
 * Write to card's Host Adapter Command Register.
 */
static inline void
hacr_write(u_long	base,
	   u_char	hacr)
{
  outb(hacr, HACR(base));
} /* hacr_write */

/*------------------------------------------------------------------*/
/*
 * Write to card's Host Adapter Command Register. Include a delay for
 * those times when it is needed.
 */
static void
hacr_write_slow(u_long	base,
		u_char	hacr)
{
  hacr_write(base, hacr);
  /* delay might only be needed sometimes */
  mdelay(1);
} /* hacr_write_slow */

/*------------------------------------------------------------------*/
/*
 * Read the Parameter Storage Area from the WaveLAN card's memory
 */
static void
psa_read(struct net_device *	dev,
	 int		o,	/* offset in PSA */
	 u_char *	b,	/* buffer to fill */
	 int		n)	/* size to read */
{
  net_local *lp = netdev_priv(dev);
  u_char __iomem *ptr = lp->mem + PSA_ADDR + (o << 1);

  while(n-- > 0)
    {
      *b++ = readb(ptr);
      /* Due to a lack of address decode pins, the WaveLAN PCMCIA card
       * only supports reading even memory addresses. That means the
       * increment here MUST be two.
       * Because of that, we can't use memcpy_fromio()...
       */
      ptr += 2;
    }
} /* psa_read */

/*------------------------------------------------------------------*/
/*
 * Write the Paramter Storage Area to the WaveLAN card's memory
 */
static void
psa_write(struct net_device *	dev,
	  int		o,	/* Offset in psa */
	  u_char *	b,	/* Buffer in memory */
	  int		n)	/* Length of buffer */
{
  net_local *lp = netdev_priv(dev);
  u_char __iomem *ptr = lp->mem + PSA_ADDR + (o << 1);
  int		count = 0;
  unsigned int	base = dev->base_addr;
  /* As there seem to have no flag PSA_BUSY as in the ISA model, we are
   * oblige to verify this address to know when the PSA is ready... */
  volatile u_char __iomem *verify = lp->mem + PSA_ADDR +
    (psaoff(0, psa_comp_number) << 1);

  /* Authorize writing to PSA */
  hacr_write(base, HACR_PWR_STAT | HACR_ROM_WEN);

  while(n-- > 0)
    {
      /* write to PSA */
      writeb(*b++, ptr);
      ptr += 2;

      /* I don't have the spec, so I don't know what the correct
       * sequence to write is. This hack seem to work for me... */
      count = 0;
      while((readb(verify) != PSA_COMP_PCMCIA_915) && (count++ < 100))
	mdelay(1);
    }

  /* Put the host interface back in standard state */
  hacr_write(base, HACR_DEFAULT);
} /* psa_write */

#ifdef SET_PSA_CRC
/*------------------------------------------------------------------*/
/*
 * Calculate the PSA CRC
 * Thanks to Valster, Nico <NVALSTER@wcnd.nl.lucent.com> for the code
 * NOTE: By specifying a length including the CRC position the
 * returned value should be zero. (i.e. a correct checksum in the PSA)
 *
 * The Windows drivers don't use the CRC, but the AP and the PtP tool
 * depend on it.
 */
static u_short
psa_crc(unsigned char *	psa,	/* The PSA */
	int		size)	/* Number of short for CRC */
{
  int		byte_cnt;	/* Loop on the PSA */
  u_short	crc_bytes = 0;	/* Data in the PSA */
  int		bit_cnt;	/* Loop on the bits of the short */

  for(byte_cnt = 0; byte_cnt < size; byte_cnt++ )
    {
      crc_bytes ^= psa[byte_cnt];	/* Its an xor */

      for(bit_cnt = 1; bit_cnt < 9; bit_cnt++ )
	{
	  if(crc_bytes & 0x0001)
	    crc_bytes = (crc_bytes >> 1) ^ 0xA001;
	  else
	    crc_bytes >>= 1 ;
        }
    }

  return crc_bytes;
} /* psa_crc */
#endif	/* SET_PSA_CRC */

/*------------------------------------------------------------------*/
/*
 * update the checksum field in the Wavelan's PSA
 */
static void
update_psa_checksum(struct net_device *	dev)
{
#ifdef SET_PSA_CRC
  psa_t		psa;
  u_short	crc;

  /* read the parameter storage area */
  psa_read(dev, 0, (unsigned char *) &psa, sizeof(psa));

  /* update the checksum */
  crc = psa_crc((unsigned char *) &psa,
		sizeof(psa) - sizeof(psa.psa_crc[0]) - sizeof(psa.psa_crc[1])
		- sizeof(psa.psa_crc_status));

  psa.psa_crc[0] = crc & 0xFF;
  psa.psa_crc[1] = (crc & 0xFF00) >> 8;

  /* Write it ! */
  psa_write(dev, (char *)&psa.psa_crc - (char *)&psa,
	    (unsigned char *)&psa.psa_crc, 2);

#ifdef DEBUG_IOCTL_INFO
  printk (KERN_DEBUG "%s: update_psa_checksum(): crc = 0x%02x%02x\n",
          dev->name, psa.psa_crc[0], psa.psa_crc[1]);

  /* Check again (luxury !) */
  crc = psa_crc((unsigned char *) &psa,
		 sizeof(psa) - sizeof(psa.psa_crc_status));

  if(crc != 0)
    printk(KERN_WARNING "%s: update_psa_checksum(): CRC does not agree with PSA data (even after recalculating)\n", dev->name);
#endif /* DEBUG_IOCTL_INFO */
#endif	/* SET_PSA_CRC */
} /* update_psa_checksum */

/*------------------------------------------------------------------*/
/*
 * Write 1 byte to the MMC.
 */
static void
mmc_out(u_long		base,
	u_short		o,
	u_char		d)
{
  int count = 0;

  /* Wait for MMC to go idle */
  while((count++ < 100) && (inb(HASR(base)) & HASR_MMI_BUSY))
    udelay(10);

  outb((u_char)((o << 1) | MMR_MMI_WR), MMR(base));
  outb(d, MMD(base));
}

/*------------------------------------------------------------------*/
/*
 * Routine to write bytes to the Modem Management Controller.
 * We start by the end because it is the way it should be !
 */
static void
mmc_write(u_long	base,
	  u_char	o,
	  u_char *	b,
	  int		n)
{
  o += n;
  b += n;

  while(n-- > 0 )
    mmc_out(base, --o, *(--b));
} /* mmc_write */

/*------------------------------------------------------------------*/
/*
 * Read 1 byte from the MMC.
 * Optimised version for 1 byte, avoid using memory...
 */
static u_char
mmc_in(u_long	base,
       u_short	o)
{
  int count = 0;

  while((count++ < 100) && (inb(HASR(base)) & HASR_MMI_BUSY))
    udelay(10);
  outb(o << 1, MMR(base));		/* Set the read address */

  outb(0, MMD(base));			/* Required dummy write */

  while((count++ < 100) && (inb(HASR(base)) & HASR_MMI_BUSY))
    udelay(10);
  return (u_char) (inb(MMD(base)));	/* Now do the actual read */
}

/*------------------------------------------------------------------*/
/*
 * Routine to read bytes from the Modem Management Controller.
 * The implementation is complicated by a lack of address lines,
 * which prevents decoding of the low-order bit.
 * (code has just been moved in the above function)
 * We start by the end because it is the way it should be !
 */
static void
mmc_read(u_long		base,
	 u_char		o,
	 u_char *	b,
	 int		n)
{
  o += n;
  b += n;

  while(n-- > 0)
    *(--b) = mmc_in(base, --o);
} /* mmc_read */

/*------------------------------------------------------------------*/
/*
 * Get the type of encryption available...
 */
static inline int
mmc_encr(u_long		base)	/* i/o port of the card */
{
  int	temp;

  temp = mmc_in(base, mmroff(0, mmr_des_avail));
  if((temp != MMR_DES_AVAIL_DES) && (temp != MMR_DES_AVAIL_AES))
    return 0;
  else
    return temp;
}

/*------------------------------------------------------------------*/
/*
 * Wait for the frequency EEprom to complete a command...
 */
static void
fee_wait(u_long		base,	/* i/o port of the card */
	 int		delay,	/* Base delay to wait for */
	 int		number)	/* Number of time to wait */
{
  int		count = 0;	/* Wait only a limited time */

  while((count++ < number) &&
	(mmc_in(base, mmroff(0, mmr_fee_status)) & MMR_FEE_STATUS_BUSY))
    udelay(delay);
}

/*------------------------------------------------------------------*/
/*
 * Read bytes from the Frequency EEprom (frequency select cards).
 */
static void
fee_read(u_long		base,	/* i/o port of the card */
	 u_short	o,	/* destination offset */
	 u_short *	b,	/* data buffer */
	 int		n)	/* number of registers */
{
  b += n;		/* Position at the end of the area */

  /* Write the address */
  mmc_out(base, mmwoff(0, mmw_fee_addr), o + n - 1);

  /* Loop on all buffer */
  while(n-- > 0)
    {
      /* Write the read command */
      mmc_out(base, mmwoff(0, mmw_fee_ctrl), MMW_FEE_CTRL_READ);

      /* Wait until EEprom is ready (should be quick !) */
      fee_wait(base, 10, 100);

      /* Read the value */
      *--b = ((mmc_in(base, mmroff(0, mmr_fee_data_h)) << 8) |
	      mmc_in(base, mmroff(0, mmr_fee_data_l)));
    }
}


/*------------------------------------------------------------------*/
/*
 * Write bytes from the Frequency EEprom (frequency select cards).
 * This is a bit complicated, because the frequency eeprom has to
 * be unprotected and the write enabled.
 * Jean II
 */
static void
fee_write(u_long	base,	/* i/o port of the card */
	  u_short	o,	/* destination offset */
	  u_short *	b,	/* data buffer */
	  int		n)	/* number of registers */
{
  b += n;		/* Position at the end of the area */

#ifdef EEPROM_IS_PROTECTED	/* disabled */
#ifdef DOESNT_SEEM_TO_WORK	/* disabled */
  /* Ask to read the protected register */
  mmc_out(base, mmwoff(0, mmw_fee_ctrl), MMW_FEE_CTRL_PRREAD);

  fee_wait(base, 10, 100);

  /* Read the protected register */
  printk("Protected 2 : %02X-%02X\n",
	 mmc_in(base, mmroff(0, mmr_fee_data_h)),
	 mmc_in(base, mmroff(0, mmr_fee_data_l)));
#endif	/* DOESNT_SEEM_TO_WORK */

  /* Enable protected register */
  mmc_out(base, mmwoff(0, mmw_fee_addr), MMW_FEE_ADDR_EN);
  mmc_out(base, mmwoff(0, mmw_fee_ctrl), MMW_FEE_CTRL_PREN);

  fee_wait(base, 10, 100);

  /* Unprotect area */
  mmc_out(base, mmwoff(0, mmw_fee_addr), o + n);
  mmc_out(base, mmwoff(0, mmw_fee_ctrl), MMW_FEE_CTRL_PRWRITE);
#ifdef DOESNT_SEEM_TO_WORK	/* disabled */
  /* Or use : */
  mmc_out(base, mmwoff(0, mmw_fee_ctrl), MMW_FEE_CTRL_PRCLEAR);
#endif	/* DOESNT_SEEM_TO_WORK */

  fee_wait(base, 10, 100);
#endif	/* EEPROM_IS_PROTECTED */

  /* Write enable */
  mmc_out(base, mmwoff(0, mmw_fee_addr), MMW_FEE_ADDR_EN);
  mmc_out(base, mmwoff(0, mmw_fee_ctrl), MMW_FEE_CTRL_WREN);

  fee_wait(base, 10, 100);

  /* Write the EEprom address */
  mmc_out(base, mmwoff(0, mmw_fee_addr), o + n - 1);

  /* Loop on all buffer */
  while(n-- > 0)
    {
      /* Write the value */
      mmc_out(base, mmwoff(0, mmw_fee_data_h), (*--b) >> 8);
      mmc_out(base, mmwoff(0, mmw_fee_data_l), *b & 0xFF);

      /* Write the write command */
      mmc_out(base, mmwoff(0, mmw_fee_ctrl), MMW_FEE_CTRL_WRITE);

      /* Wavelan doc says : wait at least 10 ms for EEBUSY = 0 */
      mdelay(10);
      fee_wait(base, 10, 100);
    }

  /* Write disable */
  mmc_out(base, mmwoff(0, mmw_fee_addr), MMW_FEE_ADDR_DS);
  mmc_out(base, mmwoff(0, mmw_fee_ctrl), MMW_FEE_CTRL_WDS);

  fee_wait(base, 10, 100);

#ifdef EEPROM_IS_PROTECTED	/* disabled */
  /* Reprotect EEprom */
  mmc_out(base, mmwoff(0, mmw_fee_addr), 0x00);
  mmc_out(base, mmwoff(0, mmw_fee_ctrl), MMW_FEE_CTRL_PRWRITE);

  fee_wait(base, 10, 100);
#endif	/* EEPROM_IS_PROTECTED */
}

/******************* WaveLAN Roaming routines... ********************/

#ifdef WAVELAN_ROAMING	/* Conditional compile, see wavelan_cs.h */

static unsigned char WAVELAN_BEACON_ADDRESS[] = {0x09,0x00,0x0e,0x20,0x03,0x00};
  
static void wv_roam_init(struct net_device *dev)
{
  net_local  *lp= netdev_priv(dev);

  /* Do not remove this unless you have a good reason */
  printk(KERN_NOTICE "%s: Warning, you have enabled roaming on"
	 " device %s !\n", dev->name, dev->name);
  printk(KERN_NOTICE "Roaming is currently an experimental unsupported feature"
	 " of the Wavelan driver.\n");
  printk(KERN_NOTICE "It may work, but may also make the driver behave in"
	 " erratic ways or crash.\n");

  lp->wavepoint_table.head=NULL;           /* Initialise WavePoint table */
  lp->wavepoint_table.num_wavepoints=0;
  lp->wavepoint_table.locked=0;
  lp->curr_point=NULL;                        /* No default WavePoint */
  lp->cell_search=0;
  
  lp->cell_timer.data=(long)lp;               /* Start cell expiry timer */
  lp->cell_timer.function=wl_cell_expiry;
  lp->cell_timer.expires=jiffies+CELL_TIMEOUT;
  add_timer(&lp->cell_timer);
  
  wv_nwid_filter(NWID_PROMISC,lp) ;    /* Enter NWID promiscuous mode */
  /* to build up a good WavePoint */
                                           /* table... */
  printk(KERN_DEBUG "WaveLAN: Roaming enabled on device %s\n",dev->name);
}
 
static void wv_roam_cleanup(struct net_device *dev)
{
  wavepoint_history *ptr,*old_ptr;
  net_local *lp= netdev_priv(dev);
  
  printk(KERN_DEBUG "WaveLAN: Roaming Disabled on device %s\n",dev->name);
  
  /* Fixme : maybe we should check that the timer exist before deleting it */
  del_timer(&lp->cell_timer);          /* Remove cell expiry timer       */
  ptr=lp->wavepoint_table.head;        /* Clear device's WavePoint table */
  while(ptr!=NULL)
    {
      old_ptr=ptr;
      ptr=ptr->next;	
      wl_del_wavepoint(old_ptr,lp);	
    }
}

/* Enable/Disable NWID promiscuous mode on a given device */
static void wv_nwid_filter(unsigned char mode, net_local *lp)
{
  mm_t                  m;
  unsigned long         flags;
  
#ifdef WAVELAN_ROAMING_DEBUG
  printk(KERN_DEBUG "WaveLAN: NWID promisc %s, device %s\n",(mode==NWID_PROMISC) ? "on" : "off", lp->dev->name);
#endif
  
  /* Disable interrupts & save flags */
  spin_lock_irqsave(&lp->spinlock, flags);
  
  m.w.mmw_loopt_sel = (mode==NWID_PROMISC) ? MMW_LOOPT_SEL_DIS_NWID : 0x00;
  mmc_write(lp->dev->base_addr, (char *)&m.w.mmw_loopt_sel - (char *)&m, (unsigned char *)&m.w.mmw_loopt_sel, 1);
  
  if(mode==NWID_PROMISC)
    lp->cell_search=1;
  else
    lp->cell_search=0;

  /* ReEnable interrupts & restore flags */
  spin_unlock_irqrestore(&lp->spinlock, flags);
}

/* Find a record in the WavePoint table matching a given NWID */
static wavepoint_history *wl_roam_check(unsigned short nwid, net_local *lp)
{
  wavepoint_history	*ptr=lp->wavepoint_table.head;
  
  while(ptr!=NULL){
    if(ptr->nwid==nwid)
      return ptr;	
    ptr=ptr->next;
  }
  return NULL;
}

/* Create a new wavepoint table entry */
static wavepoint_history *wl_new_wavepoint(unsigned short nwid, unsigned char seq, net_local* lp)
{
  wavepoint_history *new_wavepoint;

#ifdef WAVELAN_ROAMING_DEBUG	
  printk(KERN_DEBUG "WaveLAN: New Wavepoint, NWID:%.4X\n",nwid);
#endif
  
  if(lp->wavepoint_table.num_wavepoints==MAX_WAVEPOINTS)
    return NULL;
  
  new_wavepoint = kmalloc(sizeof(wavepoint_history),GFP_ATOMIC);
  if(new_wavepoint==NULL)
    return NULL;
  
  new_wavepoint->nwid=nwid;                       /* New WavePoints NWID */
  new_wavepoint->average_fast=0;                    /* Running Averages..*/
  new_wavepoint->average_slow=0;
  new_wavepoint->qualptr=0;                       /* Start of ringbuffer */
  new_wavepoint->last_seq=seq-1;                /* Last sequence no.seen */
  memset(new_wavepoint->sigqual,0,WAVEPOINT_HISTORY);/* Empty ringbuffer */
  
  new_wavepoint->next=lp->wavepoint_table.head;/* Add to wavepoint table */
  new_wavepoint->prev=NULL;
  
  if(lp->wavepoint_table.head!=NULL)
    lp->wavepoint_table.head->prev=new_wavepoint;
  
  lp->wavepoint_table.head=new_wavepoint;
  
  lp->wavepoint_table.num_wavepoints++;     /* no. of visible wavepoints */
  
  return new_wavepoint;
}

/* Remove a wavepoint entry from WavePoint table */
static void wl_del_wavepoint(wavepoint_history *wavepoint, struct net_local *lp)
{
  if(wavepoint==NULL)
    return;
  
  if(lp->curr_point==wavepoint)
    lp->curr_point=NULL;
  
  if(wavepoint->prev!=NULL)
    wavepoint->prev->next=wavepoint->next;
  
  if(wavepoint->next!=NULL)
    wavepoint->next->prev=wavepoint->prev;
  
  if(lp->wavepoint_table.head==wavepoint)
    lp->wavepoint_table.head=wavepoint->next;
  
  lp->wavepoint_table.num_wavepoints--;
  kfree(wavepoint);
}

/* Timer callback function - checks WavePoint table for stale entries */ 
static void wl_cell_expiry(unsigned long data)
{
  net_local *lp=(net_local *)data;
  wavepoint_history *wavepoint=lp->wavepoint_table.head,*old_point;
  
#if WAVELAN_ROAMING_DEBUG > 1
  printk(KERN_DEBUG "WaveLAN: Wavepoint timeout, dev %s\n",lp->dev->name);
#endif
  
  if(lp->wavepoint_table.locked)
    {
#if WAVELAN_ROAMING_DEBUG > 1
      printk(KERN_DEBUG "WaveLAN: Wavepoint table locked...\n");
#endif
      
      lp->cell_timer.expires=jiffies+1; /* If table in use, come back later */
      add_timer(&lp->cell_timer);
      return;
    }
  
  while(wavepoint!=NULL)
    {
      if(time_after(jiffies, wavepoint->last_seen + CELL_TIMEOUT))
	{
#ifdef WAVELAN_ROAMING_DEBUG
	  printk(KERN_DEBUG "WaveLAN: Bye bye %.4X\n",wavepoint->nwid);
#endif
	  
	  old_point=wavepoint;
	  wavepoint=wavepoint->next;
	  wl_del_wavepoint(old_point,lp);
	}
      else
	wavepoint=wavepoint->next;
    }
  lp->cell_timer.expires=jiffies+CELL_TIMEOUT;
  add_timer(&lp->cell_timer);
}

/* Update SNR history of a wavepoint */
static void wl_update_history(wavepoint_history *wavepoint, unsigned char sigqual, unsigned char seq)	
{
  int i=0,num_missed=0,ptr=0;
  int average_fast=0,average_slow=0;
  
  num_missed=(seq-wavepoint->last_seq)%WAVEPOINT_HISTORY;/* Have we missed
							    any beacons? */
  if(num_missed)
    for(i=0;i<num_missed;i++)
      {
	wavepoint->sigqual[wavepoint->qualptr++]=0; /* If so, enter them as 0's */
	wavepoint->qualptr %=WAVEPOINT_HISTORY;    /* in the ringbuffer. */
      }
  wavepoint->last_seen=jiffies;                 /* Add beacon to history */
  wavepoint->last_seq=seq;	
  wavepoint->sigqual[wavepoint->qualptr++]=sigqual;          
  wavepoint->qualptr %=WAVEPOINT_HISTORY;
  ptr=(wavepoint->qualptr-WAVEPOINT_FAST_HISTORY+WAVEPOINT_HISTORY)%WAVEPOINT_HISTORY;
  
  for(i=0;i<WAVEPOINT_FAST_HISTORY;i++)       /* Update running averages */
    {
      average_fast+=wavepoint->sigqual[ptr++];
      ptr %=WAVEPOINT_HISTORY;
    }
  
  average_slow=average_fast;
  for(i=WAVEPOINT_FAST_HISTORY;i<WAVEPOINT_HISTORY;i++)
    {
      average_slow+=wavepoint->sigqual[ptr++];
      ptr %=WAVEPOINT_HISTORY;
    }
  
  wavepoint->average_fast=average_fast/WAVEPOINT_FAST_HISTORY;
  wavepoint->average_slow=average_slow/WAVEPOINT_HISTORY;	
}

/* Perform a handover to a new WavePoint */
static void wv_roam_handover(wavepoint_history *wavepoint, net_local *lp)
{
  unsigned int		base = lp->dev->base_addr;
  mm_t                  m;
  unsigned long         flags;

  if(wavepoint==lp->curr_point)          /* Sanity check... */
    {
      wv_nwid_filter(!NWID_PROMISC,lp);
      return;
    }
  
#ifdef WAVELAN_ROAMING_DEBUG
  printk(KERN_DEBUG "WaveLAN: Doing handover to %.4X, dev %s\n",wavepoint->nwid,lp->dev->name);
#endif
 	
  /* Disable interrupts & save flags */
  spin_lock_irqsave(&lp->spinlock, flags);

  m.w.mmw_netw_id_l = wavepoint->nwid & 0xFF;
  m.w.mmw_netw_id_h = (wavepoint->nwid & 0xFF00) >> 8;
  
  mmc_write(base, (char *)&m.w.mmw_netw_id_l - (char *)&m, (unsigned char *)&m.w.mmw_netw_id_l, 2);
  
  /* ReEnable interrupts & restore flags */
  spin_unlock_irqrestore(&lp->spinlock, flags);

  wv_nwid_filter(!NWID_PROMISC,lp);
  lp->curr_point=wavepoint;
}

/* Called when a WavePoint beacon is received */
static void wl_roam_gather(struct net_device *  dev,
			   u_char *  hdr,   /* Beacon header */
			   u_char *  stats) /* SNR, Signal quality
						      of packet */
{
  wavepoint_beacon *beacon= (wavepoint_beacon *)hdr; /* Rcvd. Beacon */
  unsigned short nwid=ntohs(beacon->nwid);  
  unsigned short sigqual=stats[2] & MMR_SGNL_QUAL;   /* SNR of beacon */
  wavepoint_history *wavepoint=NULL;                /* WavePoint table entry */
  net_local *lp = netdev_priv(dev);              /* Device info */

#ifdef I_NEED_THIS_FEATURE
  /* Some people don't need this, some other may need it */
  nwid=nwid^ntohs(beacon->domain_id);
#endif

#if WAVELAN_ROAMING_DEBUG > 1
  printk(KERN_DEBUG "WaveLAN: beacon, dev %s:\n",dev->name);
  printk(KERN_DEBUG "Domain: %.4X NWID: %.4X SigQual=%d\n",ntohs(beacon->domain_id),nwid,sigqual);
#endif
  
  lp->wavepoint_table.locked=1;                            /* <Mutex> */
  
  wavepoint=wl_roam_check(nwid,lp);            /* Find WavePoint table entry */
  if(wavepoint==NULL)                    /* If no entry, Create a new one... */
    {
      wavepoint=wl_new_wavepoint(nwid,beacon->seq,lp);
      if(wavepoint==NULL)
	goto out;
    }
  if(lp->curr_point==NULL)             /* If this is the only WavePoint, */
    wv_roam_handover(wavepoint, lp);	         /* Jump on it! */
  
  wl_update_history(wavepoint, sigqual, beacon->seq); /* Update SNR history
							 stats. */
  
  if(lp->curr_point->average_slow < SEARCH_THRESH_LOW) /* If our current */
    if(!lp->cell_search)                  /* WavePoint is getting faint, */
      wv_nwid_filter(NWID_PROMISC,lp);    /* start looking for a new one */
  
  if(wavepoint->average_slow > 
     lp->curr_point->average_slow + WAVELAN_ROAMING_DELTA)
    wv_roam_handover(wavepoint, lp);   /* Handover to a better WavePoint */
  
  if(lp->curr_point->average_slow > SEARCH_THRESH_HIGH) /* If our SNR is */
    if(lp->cell_search)  /* getting better, drop out of cell search mode */
      wv_nwid_filter(!NWID_PROMISC,lp);
  
out:
  lp->wavepoint_table.locked=0;                        /* </MUTEX>   :-) */
}

/* Test this MAC frame a WavePoint beacon */
static inline int WAVELAN_BEACON(unsigned char *data)
{
  wavepoint_beacon *beacon= (wavepoint_beacon *)data;
  static const wavepoint_beacon beacon_template={0xaa,0xaa,0x03,0x08,0x00,0x0e,0x20,0x03,0x00};
  
  if(memcmp(beacon,&beacon_template,9)==0)
    return 1;
  else
    return 0;
}
#endif	/* WAVELAN_ROAMING */

/************************ I82593 SUBROUTINES *************************/
/*
 * Useful subroutines to manage the Ethernet controller
 */

/*------------------------------------------------------------------*/
/*
 * Routine to synchronously send a command to the i82593 chip. 
 * Should be called with interrupts disabled.
 * (called by wv_packet_write(), wv_ru_stop(), wv_ru_start(),
 *  wv_82593_config() & wv_diag())
 */
static int
wv_82593_cmd(struct net_device *	dev,
	     char *	str,
	     int	cmd,
	     int	result)
{
  unsigned int	base = dev->base_addr;
  int		status;
  int		wait_completed;
  long		spin;

  /* Spin until the chip finishes executing its current command (if any) */
  spin = 1000;
  do
    {
      /* Time calibration of the loop */
      udelay(10);

      /* Read the interrupt register */
      outb(OP0_NOP | CR0_STATUS_3, LCCR(base));
      status = inb(LCSR(base));
    }
  while(((status & SR3_EXEC_STATE_MASK) != SR3_EXEC_IDLE) && (spin-- > 0));

  /* If the interrupt hasn't be posted */
  if(spin <= 0)
    {
#ifdef DEBUG_INTERRUPT_ERROR
      printk(KERN_INFO "wv_82593_cmd: %s timeout (previous command), status 0x%02x\n",
	     str, status);
#endif
      return(FALSE);
    }

  /* Issue the command to the controller */
  outb(cmd, LCCR(base));

  /* If we don't have to check the result of the command
   * Note : this mean that the irq handler will deal with that */
  if(result == SR0_NO_RESULT)
    return(TRUE);

  /* We are waiting for command completion */
  wait_completed = TRUE;

  /* Busy wait while the LAN controller executes the command. */
  spin = 1000;
  do
    {
      /* Time calibration of the loop */
      udelay(10);

      /* Read the interrupt register */
      outb(CR0_STATUS_0 | OP0_NOP, LCCR(base));
      status = inb(LCSR(base));

      /* Check if there was an interrupt posted */
      if((status & SR0_INTERRUPT))
	{
	  /* Acknowledge the interrupt */
	  outb(CR0_INT_ACK | OP0_NOP, LCCR(base));

	  /* Check if interrupt is a command completion */
	  if(((status & SR0_BOTH_RX_TX) != SR0_BOTH_RX_TX) &&
	     ((status & SR0_BOTH_RX_TX) != 0x0) &&
	     !(status & SR0_RECEPTION))
	    {
	      /* Signal command completion */
	      wait_completed = FALSE;
	    }
	  else
	    {
	      /* Note : Rx interrupts will be handled later, because we can
	       * handle multiple Rx packets at once */
#ifdef DEBUG_INTERRUPT_INFO
	      printk(KERN_INFO "wv_82593_cmd: not our interrupt\n");
#endif
	    }
	}
    }
  while(wait_completed && (spin-- > 0));

  /* If the interrupt hasn't be posted */
  if(wait_completed)
    {
#ifdef DEBUG_INTERRUPT_ERROR
      printk(KERN_INFO "wv_82593_cmd: %s timeout, status 0x%02x\n",
	     str, status);
#endif
      return(FALSE);
    }

  /* Check the return code returned by the card (see above) against
   * the expected return code provided by the caller */
  if((status & SR0_EVENT_MASK) != result)
    {
#ifdef DEBUG_INTERRUPT_ERROR
      printk(KERN_INFO "wv_82593_cmd: %s failed, status = 0x%x\n",
	     str, status);
#endif
      return(FALSE);
    }

  return(TRUE);
} /* wv_82593_cmd */

/*------------------------------------------------------------------*/
/*
 * This routine does a 593 op-code number 7, and obtains the diagnose
 * status for the WaveLAN.
 */
static inline int
wv_diag(struct net_device *	dev)
{
  return(wv_82593_cmd(dev, "wv_diag(): diagnose",
		      OP0_DIAGNOSE, SR0_DIAGNOSE_PASSED));
} /* wv_diag */

/*------------------------------------------------------------------*/
/*
 * Routine to read len bytes from the i82593's ring buffer, starting at
 * chip address addr. The results read from the chip are stored in buf.
 * The return value is the address to use for next the call.
 */
static int
read_ringbuf(struct net_device *	dev,
	     int	addr,
	     char *	buf,
	     int	len)
{
  unsigned int	base = dev->base_addr;
  int		ring_ptr = addr;
  int		chunk_len;
  char *	buf_ptr = buf;

  /* Get all the buffer */
  while(len > 0)
    {
      /* Position the Program I/O Register at the ring buffer pointer */
      outb(ring_ptr & 0xff, PIORL(base));
      outb(((ring_ptr >> 8) & PIORH_MASK), PIORH(base));

      /* First, determine how much we can read without wrapping around the
	 ring buffer */
      if((addr + len) < (RX_BASE + RX_SIZE))
	chunk_len = len;
      else
	chunk_len = RX_BASE + RX_SIZE - addr;
      insb(PIOP(base), buf_ptr, chunk_len);
      buf_ptr += chunk_len;
      len -= chunk_len;
      ring_ptr = (ring_ptr - RX_BASE + chunk_len) % RX_SIZE + RX_BASE;
    }
  return(ring_ptr);
} /* read_ringbuf */

/*------------------------------------------------------------------*/
/*
 * Reconfigure the i82593, or at least ask for it...
 * Because wv_82593_config use the transmission buffer, we must do it
 * when we are sure that there is no transmission, so we do it now
 * or in wavelan_packet_xmit() (I can't find any better place,
 * wavelan_interrupt is not an option...), so you may experience
 * some delay sometime...
 */
static void
wv_82593_reconfig(struct net_device *	dev)
{
  net_local *		lp = netdev_priv(dev);
  struct pcmcia_device *		link = lp->link;
  unsigned long		flags;

  /* Arm the flag, will be cleard in wv_82593_config() */
  lp->reconfig_82593 = TRUE;

  /* Check if we can do it now ! */
  if((link->open) && (netif_running(dev)) && !(netif_queue_stopped(dev)))
    {
      spin_lock_irqsave(&lp->spinlock, flags);	/* Disable interrupts */
      wv_82593_config(dev);
      spin_unlock_irqrestore(&lp->spinlock, flags);	/* Re-enable interrupts */
    }
  else
    {
#ifdef DEBUG_IOCTL_INFO
      printk(KERN_DEBUG
	     "%s: wv_82593_reconfig(): delayed (state = %lX, link = %d)\n",
	     dev->name, dev->state, link->open);
#endif
    }
}

/********************* DEBUG & INFO SUBROUTINES *********************/
/*
 * This routines are used in the code to show debug informations.
 * Most of the time, it dump the content of hardware structures...
 */

#ifdef DEBUG_PSA_SHOW
/*------------------------------------------------------------------*/
/*
 * Print the formatted contents of the Parameter Storage Area.
 */
static void
wv_psa_show(psa_t *	p)
{
  DECLARE_MAC_BUF(mac);
  printk(KERN_DEBUG "##### wavelan psa contents: #####\n");
  printk(KERN_DEBUG "psa_io_base_addr_1: 0x%02X %02X %02X %02X\n",
	 p->psa_io_base_addr_1,
	 p->psa_io_base_addr_2,
	 p->psa_io_base_addr_3,
	 p->psa_io_base_addr_4);
  printk(KERN_DEBUG "psa_rem_boot_addr_1: 0x%02X %02X %02X\n",
	 p->psa_rem_boot_addr_1,
	 p->psa_rem_boot_addr_2,
	 p->psa_rem_boot_addr_3);
  printk(KERN_DEBUG "psa_holi_params: 0x%02x, ", p->psa_holi_params);
  printk("psa_int_req_no: %d\n", p->psa_int_req_no);
#ifdef DEBUG_SHOW_UNUSED
  printk(KERN_DEBUG "psa_unused0[]: %s\n",
	 print_mac(mac, p->psa_unused0));
#endif	/* DEBUG_SHOW_UNUSED */
  printk(KERN_DEBUG "psa_univ_mac_addr[]: %s\n",
	 print_mac(mac, p->psa_univ_mac_addr));
  printk(KERN_DEBUG "psa_local_mac_addr[]: %s\n",
	 print_mac(mac, p->psa_local_mac_addr));
  printk(KERN_DEBUG "psa_univ_local_sel: %d, ", p->psa_univ_local_sel);
  printk("psa_comp_number: %d, ", p->psa_comp_number);
  printk("psa_thr_pre_set: 0x%02x\n", p->psa_thr_pre_set);
  printk(KERN_DEBUG "psa_feature_select/decay_prm: 0x%02x, ",
	 p->psa_feature_select);
  printk("psa_subband/decay_update_prm: %d\n", p->psa_subband);
  printk(KERN_DEBUG "psa_quality_thr: 0x%02x, ", p->psa_quality_thr);
  printk("psa_mod_delay: 0x%02x\n", p->psa_mod_delay);
  printk(KERN_DEBUG "psa_nwid: 0x%02x%02x, ", p->psa_nwid[0], p->psa_nwid[1]);
  printk("psa_nwid_select: %d\n", p->psa_nwid_select);
  printk(KERN_DEBUG "psa_encryption_select: %d, ", p->psa_encryption_select);
  printk("psa_encryption_key[]: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
	 p->psa_encryption_key[0],
	 p->psa_encryption_key[1],
	 p->psa_encryption_key[2],
	 p->psa_encryption_key[3],
	 p->psa_encryption_key[4],
	 p->psa_encryption_key[5],
	 p->psa_encryption_key[6],
	 p->psa_encryption_key[7]);
  printk(KERN_DEBUG "psa_databus_width: %d\n", p->psa_databus_width);
  printk(KERN_DEBUG "psa_call_code/auto_squelch: 0x%02x, ",
	 p->psa_call_code[0]);
  printk("psa_call_code[]: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
	 p->psa_call_code[0],
	 p->psa_call_code[1],
	 p->psa_call_code[2],
	 p->psa_call_code[3],
	 p->psa_call_code[4],
	 p->psa_call_code[5],
	 p->psa_call_code[6],
	 p->psa_call_code[7]);
#ifdef DEBUG_SHOW_UNUSED
  printk(KERN_DEBUG "psa_reserved[]: %02X:%02X\n",
	 p->psa_reserved[0],
	 p->psa_reserved[1]);
#endif	/* DEBUG_SHOW_UNUSED */
  printk(KERN_DEBUG "psa_conf_status: %d, ", p->psa_conf_status);
  printk("psa_crc: 0x%02x%02x, ", p->psa_crc[0], p->psa_crc[1]);
  printk("psa_crc_status: 0x%02x\n", p->psa_crc_status);
} /* wv_psa_show */
#endif	/* DEBUG_PSA_SHOW */

#ifdef DEBUG_MMC_SHOW
/*------------------------------------------------------------------*/
/*
 * Print the formatted status of the Modem Management Controller.
 * This function need to be completed...
 */
static void
wv_mmc_show(struct net_device *	dev)
{
  unsigned int	base = dev->base_addr;
  net_local *	lp = netdev_priv(dev);
  mmr_t		m;

  /* Basic check */
  if(hasr_read(base) & HASR_NO_CLK)
    {
      printk(KERN_WARNING "%s: wv_mmc_show: modem not connected\n",
	     dev->name);
      return;
    }

  spin_lock_irqsave(&lp->spinlock, flags);

  /* Read the mmc */
  mmc_out(base, mmwoff(0, mmw_freeze), 1);
  mmc_read(base, 0, (u_char *)&m, sizeof(m));
  mmc_out(base, mmwoff(0, mmw_freeze), 0);

  /* Don't forget to update statistics */
  lp->wstats.discard.nwid += (m.mmr_wrong_nwid_h << 8) | m.mmr_wrong_nwid_l;

  spin_unlock_irqrestore(&lp->spinlock, flags);

  printk(KERN_DEBUG "##### wavelan modem status registers: #####\n");
#ifdef DEBUG_SHOW_UNUSED
  printk(KERN_DEBUG "mmc_unused0[]: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
	 m.mmr_unused0[0],
	 m.mmr_unused0[1],
	 m.mmr_unused0[2],
	 m.mmr_unused0[3],
	 m.mmr_unused0[4],
	 m.mmr_unused0[5],
	 m.mmr_unused0[6],
	 m.mmr_unused0[7]);
#endif	/* DEBUG_SHOW_UNUSED */
  printk(KERN_DEBUG "Encryption algorithm: %02X - Status: %02X\n",
	 m.mmr_des_avail, m.mmr_des_status);
#ifdef DEBUG_SHOW_UNUSED
  printk(KERN_DEBUG "mmc_unused1[]: %02X:%02X:%02X:%02X:%02X\n",
	 m.mmr_unused1[0],
	 m.mmr_unused1[1],
	 m.mmr_unused1[2],
	 m.mmr_unused1[3],
	 m.mmr_unused1[4]);
#endif	/* DEBUG_SHOW_UNUSED */
  printk(KERN_DEBUG "dce_status: 0x%x [%s%s%s%s]\n",
	 m.mmr_dce_status,
	 (m.mmr_dce_status & MMR_DCE_STATUS_RX_BUSY) ? "energy detected,":"",
	 (m.mmr_dce_status & MMR_DCE_STATUS_LOOPT_IND) ?
	 "loop test indicated," : "",
	 (m.mmr_dce_status & MMR_DCE_STATUS_TX_BUSY) ? "transmitter on," : "",
	 (m.mmr_dce_status & MMR_DCE_STATUS_JBR_EXPIRED) ?
	 "jabber timer expired," : "");
  printk(KERN_DEBUG "Dsp ID: %02X\n",
	 m.mmr_dsp_id);
#ifdef DEBUG_SHOW_UNUSED
  printk(KERN_DEBUG "mmc_unused2[]: %02X:%02X\n",
	 m.mmr_unused2[0],
	 m.mmr_unused2[1]);
#endif	/* DEBUG_SHOW_UNUSED */
  printk(KERN_DEBUG "# correct_nwid: %d, # wrong_nwid: %d\n",
	 (m.mmr_correct_nwid_h << 8) | m.mmr_correct_nwid_l,
	 (m.mmr_wrong_nwid_h << 8) | m.mmr_wrong_nwid_l);
  printk(KERN_DEBUG "thr_pre_set: 0x%x [current signal %s]\n",
	 m.mmr_thr_pre_set & MMR_THR_PRE_SET,
	 (m.mmr_thr_pre_set & MMR_THR_PRE_SET_CUR) ? "above" : "below");
  printk(KERN_DEBUG "signal_lvl: %d [%s], ",
	 m.mmr_signal_lvl & MMR_SIGNAL_LVL,
	 (m.mmr_signal_lvl & MMR_SIGNAL_LVL_VALID) ? "new msg" : "no new msg");
  printk("silence_lvl: %d [%s], ", m.mmr_silence_lvl & MMR_SILENCE_LVL,
	 (m.mmr_silence_lvl & MMR_SILENCE_LVL_VALID) ? "update done" : "no new update");
  printk("sgnl_qual: 0x%x [%s]\n", m.mmr_sgnl_qual & MMR_SGNL_QUAL,
	 (m.mmr_sgnl_qual & MMR_SGNL_QUAL_ANT) ? "Antenna 1" : "Antenna 0");
#ifdef DEBUG_SHOW_UNUSED
  printk(KERN_DEBUG "netw_id_l: %x\n", m.mmr_netw_id_l);
#endif	/* DEBUG_SHOW_UNUSED */
} /* wv_mmc_show */
#endif	/* DEBUG_MMC_SHOW */

#ifdef DEBUG_I82593_SHOW
/*------------------------------------------------------------------*/
/*
 * Print the formatted status of the i82593's receive unit.
 */
static void
wv_ru_show(struct net_device *	dev)
{
  net_local *lp = netdev_priv(dev);

  printk(KERN_DEBUG "##### wavelan i82593 receiver status: #####\n");
  printk(KERN_DEBUG "ru: rfp %d stop %d", lp->rfp, lp->stop);
  /*
   * Not implemented yet...
   */
  printk("\n");
} /* wv_ru_show */
#endif	/* DEBUG_I82593_SHOW */

#ifdef DEBUG_DEVICE_SHOW
/*------------------------------------------------------------------*/
/*
 * Print the formatted status of the WaveLAN PCMCIA device driver.
 */
static void
wv_dev_show(struct net_device *	dev)
{
  printk(KERN_DEBUG "dev:");
  printk(" state=%lX,", dev->state);
  printk(" trans_start=%ld,", dev->trans_start);
  printk(" flags=0x%x,", dev->flags);
  printk("\n");
} /* wv_dev_show */

/*------------------------------------------------------------------*/
/*
 * Print the formatted status of the WaveLAN PCMCIA device driver's
 * private information.
 */
static void
wv_local_show(struct net_device *	dev)
{
  net_local *lp = netdev_priv(dev);

  printk(KERN_DEBUG "local:");
  /*
   * Not implemented yet...
   */
  printk("\n");
} /* wv_local_show */
#endif	/* DEBUG_DEVICE_SHOW */

#if defined(DEBUG_RX_INFO) || defined(DEBUG_TX_INFO)
/*------------------------------------------------------------------*/
/*
 * Dump packet header (and content if necessary) on the screen
 */
static void
wv_packet_info(u_char *		p,		/* Packet to dump */
	       int		length,		/* Length of the packet */
	       char *		msg1,		/* Name of the device */
	       char *		msg2)		/* Name of the function */
{
  int		i;
  int		maxi;
  DECLARE_MAC_BUF(mac);

  printk(KERN_DEBUG "%s: %s(): dest %s, length %d\n",
	 msg1, msg2, print_mac(mac, p), length);
  printk(KERN_DEBUG "%s: %s(): src %s, type 0x%02X%02X\n",
	 msg1, msg2, print_mac(mac, &p[6]), p[12], p[13]);

#ifdef DEBUG_PACKET_DUMP

  printk(KERN_DEBUG "data=\"");

  if((maxi = length) > DEBUG_PACKET_DUMP)
    maxi = DEBUG_PACKET_DUMP;
  for(i = 14; i < maxi; i++)
    if(p[i] >= ' ' && p[i] <= '~')
      printk(" %c", p[i]);
    else
      printk("%02X", p[i]);
  if(maxi < length)
    printk("..");
  printk("\"\n");
  printk(KERN_DEBUG "\n");
#endif	/* DEBUG_PACKET_DUMP */
}
#endif	/* defined(DEBUG_RX_INFO) || defined(DEBUG_TX_INFO) */

/*------------------------------------------------------------------*/
/*
 * This is the information which is displayed by the driver at startup
 * There  is a lot of flag to configure it at your will...
 */
static void
wv_init_info(struct net_device *	dev)
{
  unsigned int	base = dev->base_addr;
  psa_t		psa;
  DECLARE_MAC_BUF(mac);

  /* Read the parameter storage area */
  psa_read(dev, 0, (unsigned char *) &psa, sizeof(psa));

#ifdef DEBUG_PSA_SHOW
  wv_psa_show(&psa);
#endif
#ifdef DEBUG_MMC_SHOW
  wv_mmc_show(dev);
#endif
#ifdef DEBUG_I82593_SHOW
  wv_ru_show(dev);
#endif

#ifdef DEBUG_BASIC_SHOW
  /* Now, let's go for the basic stuff */
  printk(KERN_NOTICE "%s: WaveLAN: port %#x, irq %d, "
	 "hw_addr %s",
	 dev->name, base, dev->irq,
	 print_mac(mac, dev->dev_addr));

  /* Print current network id */
  if(psa.psa_nwid_select)
    printk(", nwid 0x%02X-%02X", psa.psa_nwid[0], psa.psa_nwid[1]);
  else
    printk(", nwid off");

  /* If 2.00 card */
  if(!(mmc_in(base, mmroff(0, mmr_fee_status)) &
       (MMR_FEE_STATUS_DWLD | MMR_FEE_STATUS_BUSY)))
    {
      unsigned short	freq;

      /* Ask the EEprom to read the frequency from the first area */
      fee_read(base, 0x00 /* 1st area - frequency... */,
	       &freq, 1);

      /* Print frequency */
      printk(", 2.00, %ld", (freq >> 6) + 2400L);

      /* Hack !!! */
      if(freq & 0x20)
	printk(".5");
    }
  else
    {
      printk(", PCMCIA, ");
      switch (psa.psa_subband)
	{
	case PSA_SUBBAND_915:
	  printk("915");
	  break;
	case PSA_SUBBAND_2425:
	  printk("2425");
	  break;
	case PSA_SUBBAND_2460:
	  printk("2460");
	  break;
	case PSA_SUBBAND_2484:
	  printk("2484");
	  break;
	case PSA_SUBBAND_2430_5:
	  printk("2430.5");
	  break;
	default:
	  printk("unknown");
	}
    }

  printk(" MHz\n");
#endif	/* DEBUG_BASIC_SHOW */

#ifdef DEBUG_VERSION_SHOW
  /* Print version information */
  printk(KERN_NOTICE "%s", version);
#endif
} /* wv_init_info */

/********************* IOCTL, STATS & RECONFIG *********************/
/*
 * We found here routines that are called by Linux on differents
 * occasions after the configuration and not for transmitting data
 * These may be called when the user use ifconfig, /proc/net/dev
 * or wireless extensions
 */

/*------------------------------------------------------------------*/
/*
 * Get the current ethernet statistics. This may be called with the
 * card open or closed.
 * Used when the user read /proc/net/dev
 */
static en_stats	*
wavelan_get_stats(struct net_device *	dev)
{
#ifdef DEBUG_IOCTL_TRACE
  printk(KERN_DEBUG "%s: <>wavelan_get_stats()\n", dev->name);
#endif

  return(&((net_local *)netdev_priv(dev))->stats);
}

/*------------------------------------------------------------------*/
/*
 * Set or clear the multicast filter for this adaptor.
 * num_addrs == -1	Promiscuous mode, receive all packets
 * num_addrs == 0	Normal mode, clear multicast list
 * num_addrs > 0	Multicast mode, receive normal and MC packets,
 *			and do best-effort filtering.
 */

static void
wavelan_set_multicast_list(struct net_device *	dev)
{
  net_local *	lp = netdev_priv(dev);

#ifdef DEBUG_IOCTL_TRACE
  printk(KERN_DEBUG "%s: ->wavelan_set_multicast_list()\n", dev->name);
#endif

#ifdef DEBUG_IOCTL_INFO
  printk(KERN_DEBUG "%s: wavelan_set_multicast_list(): setting Rx mode %02X to %d addresses.\n",
	 dev->name, dev->flags, dev->mc_count);
#endif

  if(dev->flags & IFF_PROMISC)
    {
      /*
       * Enable promiscuous mode: receive all packets.
       */
      if(!lp->promiscuous)
	{
	  lp->promiscuous = 1;
	  lp->allmulticast = 0;
	  lp->mc_count = 0;

	  wv_82593_reconfig(dev);

	  /* Tell the kernel that we are doing a really bad job... */
	  dev->flags |= IFF_PROMISC;
	}
    }
  else
    /* If all multicast addresses
     * or too much multicast addresses for the hardware filter */
    if((dev->flags & IFF_ALLMULTI) ||
       (dev->mc_count > I82593_MAX_MULTICAST_ADDRESSES))
      {
	/*
	 * Disable promiscuous mode, but active the all multicast mode
	 */
	if(!lp->allmulticast)
	  {
	    lp->promiscuous = 0;
	    lp->allmulticast = 1;
	    lp->mc_count = 0;

	    wv_82593_reconfig(dev);

	    /* Tell the kernel that we are doing a really bad job... */
	    dev->flags |= IFF_ALLMULTI;
	  }
      }
    else
      /* If there is some multicast addresses to send */
      if(dev->mc_list != (struct dev_mc_list *) NULL)
	{
	  /*
	   * Disable promiscuous mode, but receive all packets
	   * in multicast list
	   */
#ifdef MULTICAST_AVOID
	  if(lp->promiscuous || lp->allmulticast ||
	     (dev->mc_count != lp->mc_count))
#endif
	    {
	      lp->promiscuous = 0;
	      lp->allmulticast = 0;
	      lp->mc_count = dev->mc_count;

	      wv_82593_reconfig(dev);
	    }
	}
      else
	{
	  /*
	   * Switch to normal mode: disable promiscuous mode and 
	   * clear the multicast list.
	   */
	  if(lp->promiscuous || lp->mc_count == 0)
	    {
	      lp->promiscuous = 0;
	      lp->allmulticast = 0;
	      lp->mc_count = 0;

	      wv_82593_reconfig(dev);
	    }
	}
#ifdef DEBUG_IOCTL_TRACE
  printk(KERN_DEBUG "%s: <-wavelan_set_multicast_list()\n", dev->name);
#endif
}

/*------------------------------------------------------------------*/
/*
 * This function doesn't exist...
 * (Note : it was a nice way to test the reconfigure stuff...)
 */
#ifdef SET_MAC_ADDRESS
static int
wavelan_set_mac_address(struct net_device *	dev,
			void *		addr)
{
  struct sockaddr *	mac = addr;

  /* Copy the address */
  memcpy(dev->dev_addr, mac->sa_data, WAVELAN_ADDR_SIZE);

  /* Reconfig the beast */
  wv_82593_reconfig(dev);

  return 0;
}
#endif	/* SET_MAC_ADDRESS */


/*------------------------------------------------------------------*/
/*
 * Frequency setting (for hardware able of it)
 * It's a bit complicated and you don't really want to look into it...
 */
static int
wv_set_frequency(u_long		base,	/* i/o port of the card */
		 iw_freq *	frequency)
{
  const int	BAND_NUM = 10;	/* Number of bands */
  long		freq = 0L;	/* offset to 2.4 GHz in .5 MHz */
#ifdef DEBUG_IOCTL_INFO
  int		i;
#endif

  /* Setting by frequency */
  /* Theoritically, you may set any frequency between
   * the two limits with a 0.5 MHz precision. In practice,
   * I don't want you to have trouble with local
   * regulations... */
  if((frequency->e == 1) &&
     (frequency->m >= (int) 2.412e8) && (frequency->m <= (int) 2.487e8))
    {
      freq = ((frequency->m / 10000) - 24000L) / 5;
    }

  /* Setting by channel (same as wfreqsel) */
  /* Warning : each channel is 22MHz wide, so some of the channels
   * will interfere... */
  if((frequency->e == 0) &&
     (frequency->m >= 0) && (frequency->m < BAND_NUM))
    {
      /* Get frequency offset. */
      freq = channel_bands[frequency->m] >> 1;
    }

  /* Verify if the frequency is allowed */
  if(freq != 0L)
    {
      u_short	table[10];	/* Authorized frequency table */

      /* Read the frequency table */
      fee_read(base, 0x71 /* frequency table */,
	       table, 10);

#ifdef DEBUG_IOCTL_INFO
      printk(KERN_DEBUG "Frequency table :");
      for(i = 0; i < 10; i++)
	{
	  printk(" %04X",
		 table[i]);
	}
      printk("\n");
#endif

      /* Look in the table if the frequency is allowed */
      if(!(table[9 - ((freq - 24) / 16)] &
	   (1 << ((freq - 24) % 16))))
	return -EINVAL;		/* not allowed */
    }
  else
    return -EINVAL;

  /* If we get a usable frequency */
  if(freq != 0L)
    {
      unsigned short	area[16];
      unsigned short	dac[2];
      unsigned short	area_verify[16];
      unsigned short	dac_verify[2];
      /* Corresponding gain (in the power adjust value table)
       * see AT&T Wavelan Data Manual, REF 407-024689/E, page 3-8
       * & WCIN062D.DOC, page 6.2.9 */
      unsigned short	power_limit[] = { 40, 80, 120, 160, 0 };
      int		power_band = 0;		/* Selected band */
      unsigned short	power_adjust;		/* Correct value */

      /* Search for the gain */
      power_band = 0;
      while((freq > power_limit[power_band]) &&
	    (power_limit[++power_band] != 0))
	;

      /* Read the first area */
      fee_read(base, 0x00,
	       area, 16);

      /* Read the DAC */
      fee_read(base, 0x60,
	       dac, 2);

      /* Read the new power adjust value */
      fee_read(base, 0x6B - (power_band >> 1),
	       &power_adjust, 1);
      if(power_band & 0x1)
	power_adjust >>= 8;
      else
	power_adjust &= 0xFF;

#ifdef DEBUG_IOCTL_INFO
      printk(KERN_DEBUG "Wavelan EEprom Area 1 :");
      for(i = 0; i < 16; i++)
	{
	  printk(" %04X",
		 area[i]);
	}
      printk("\n");

      printk(KERN_DEBUG "Wavelan EEprom DAC : %04X %04X\n",
	     dac[0], dac[1]);
#endif

      /* Frequency offset (for info only...) */
      area[0] = ((freq << 5) & 0xFFE0) | (area[0] & 0x1F);

      /* Receiver Principle main divider coefficient */
      area[3] = (freq >> 1) + 2400L - 352L;
      area[2] = ((freq & 0x1) << 4) | (area[2] & 0xFFEF);

      /* Transmitter Main divider coefficient */
      area[13] = (freq >> 1) + 2400L;
      area[12] = ((freq & 0x1) << 4) | (area[2] & 0xFFEF);

      /* Others part of the area are flags, bit streams or unused... */

      /* Set the value in the DAC */
      dac[1] = ((power_adjust >> 1) & 0x7F) | (dac[1] & 0xFF80);
      dac[0] = ((power_adjust & 0x1) << 4) | (dac[0] & 0xFFEF);

      /* Write the first area */
      fee_write(base, 0x00,
		area, 16);

      /* Write the DAC */
      fee_write(base, 0x60,
		dac, 2);

      /* We now should verify here that the EEprom writing was ok */

      /* ReRead the first area */
      fee_read(base, 0x00,
	       area_verify, 16);

      /* ReRead the DAC */
      fee_read(base, 0x60,
	       dac_verify, 2);

      /* Compare */
      if(memcmp(area, area_verify, 16 * 2) ||
	 memcmp(dac, dac_verify, 2 * 2))
	{
#ifdef DEBUG_IOCTL_ERROR
	  printk(KERN_INFO "Wavelan: wv_set_frequency : unable to write new frequency to EEprom (?)\n");
#endif
	  return -EOPNOTSUPP;
	}

      /* We must download the frequency parameters to the
       * synthetisers (from the EEprom - area 1)
       * Note : as the EEprom is auto decremented, we set the end
       * if the area... */
      mmc_out(base, mmwoff(0, mmw_fee_addr), 0x0F);
      mmc_out(base, mmwoff(0, mmw_fee_ctrl),
	      MMW_FEE_CTRL_READ | MMW_FEE_CTRL_DWLD);

      /* Wait until the download is finished */
      fee_wait(base, 100, 100);

      /* We must now download the power adjust value (gain) to
       * the synthetisers (from the EEprom - area 7 - DAC) */
      mmc_out(base, mmwoff(0, mmw_fee_addr), 0x61);
      mmc_out(base, mmwoff(0, mmw_fee_ctrl),
	      MMW_FEE_CTRL_READ | MMW_FEE_CTRL_DWLD);

      /* Wait until the download is finished */
      fee_wait(base, 100, 100);

#ifdef DEBUG_IOCTL_INFO
      /* Verification of what we have done... */

      printk(KERN_DEBUG "Wavelan EEprom Area 1 :");
      for(i = 0; i < 16; i++)
	{
	  printk(" %04X",
		 area_verify[i]);
	}
      printk("\n");

      printk(KERN_DEBUG "Wavelan EEprom DAC : %04X %04X\n",
	     dac_verify[0], dac_verify[1]);
#endif

      return 0;
    }
  else
    return -EINVAL;		/* Bah, never get there... */
}

/*------------------------------------------------------------------*/
/*
 * Give the list of available frequencies
 */
static int
wv_frequency_list(u_long	base,	/* i/o port of the card */
		  iw_freq *	list,	/* List of frequency to fill */
		  int		max)	/* Maximum number of frequencies */
{
  u_short	table[10];	/* Authorized frequency table */
  long		freq = 0L;	/* offset to 2.4 GHz in .5 MHz + 12 MHz */
  int		i;		/* index in the table */
  const int	BAND_NUM = 10;	/* Number of bands */
  int		c = 0;		/* Channel number */

  /* Read the frequency table */
  fee_read(base, 0x71 /* frequency table */,
	   table, 10);

  /* Look all frequencies */
  i = 0;
  for(freq = 0; freq < 150; freq++)
    /* Look in the table if the frequency is allowed */
    if(table[9 - (freq / 16)] & (1 << (freq % 16)))
      {
	/* Compute approximate channel number */
	while((((channel_bands[c] >> 1) - 24) < freq) &&
	      (c < BAND_NUM))
	  c++;
	list[i].i = c;	/* Set the list index */

	/* put in the list */
	list[i].m = (((freq + 24) * 5) + 24000L) * 10000;
	list[i++].e = 1;

	/* Check number */
	if(i >= max)
	  return(i);
      }

  return(i);
}

#ifdef IW_WIRELESS_SPY
/*------------------------------------------------------------------*/
/*
 * Gather wireless spy statistics : for each packet, compare the source
 * address with out list, and if match, get the stats...
 * Sorry, but this function really need wireless extensions...
 */
static inline void
wl_spy_gather(struct net_device *	dev,
	      u_char *	mac,		/* MAC address */
	      u_char *	stats)		/* Statistics to gather */
{
  struct iw_quality wstats;

  wstats.qual = stats[2] & MMR_SGNL_QUAL;
  wstats.level = stats[0] & MMR_SIGNAL_LVL;
  wstats.noise = stats[1] & MMR_SILENCE_LVL;
  wstats.updated = 0x7;

  /* Update spy records */
  wireless_spy_update(dev, mac, &wstats);
}
#endif	/* IW_WIRELESS_SPY */

#ifdef HISTOGRAM
/*------------------------------------------------------------------*/
/*
 * This function calculate an histogram on the signal level.
 * As the noise is quite constant, it's like doing it on the SNR.
 * We have defined a set of interval (lp->his_range), and each time
 * the level goes in that interval, we increment the count (lp->his_sum).
 * With this histogram you may detect if one wavelan is really weak,
 * or you may also calculate the mean and standard deviation of the level...
 */
static inline void
wl_his_gather(struct net_device *	dev,
	      u_char *	stats)		/* Statistics to gather */
{
  net_local *	lp = netdev_priv(dev);
  u_char	level = stats[0] & MMR_SIGNAL_LVL;
  int		i;

  /* Find the correct interval */
  i = 0;
  while((i < (lp->his_number - 1)) && (level >= lp->his_range[i++]))
    ;

  /* Increment interval counter */
  (lp->his_sum[i])++;
}
#endif	/* HISTOGRAM */

static void wl_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	strncpy(info->driver, "wavelan_cs", sizeof(info->driver)-1);
}

static const struct ethtool_ops ops = {
	.get_drvinfo = wl_get_drvinfo
};

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get protocol name
 */
static int wavelan_get_name(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu,
			    char *extra)
{
	strcpy(wrqu->name, "WaveLAN");
	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set NWID
 */
static int wavelan_set_nwid(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu,
			    char *extra)
{
	unsigned int base = dev->base_addr;
	net_local *lp = netdev_priv(dev);
	psa_t psa;
	mm_t m;
	unsigned long flags;
	int ret = 0;

	/* Disable interrupts and save flags. */
	spin_lock_irqsave(&lp->spinlock, flags);
	
	/* Set NWID in WaveLAN. */
	if (!wrqu->nwid.disabled) {
		/* Set NWID in psa */
		psa.psa_nwid[0] = (wrqu->nwid.value & 0xFF00) >> 8;
		psa.psa_nwid[1] = wrqu->nwid.value & 0xFF;
		psa.psa_nwid_select = 0x01;
		psa_write(dev,
			  (char *) psa.psa_nwid - (char *) &psa,
			  (unsigned char *) psa.psa_nwid, 3);

		/* Set NWID in mmc. */
		m.w.mmw_netw_id_l = psa.psa_nwid[1];
		m.w.mmw_netw_id_h = psa.psa_nwid[0];
		mmc_write(base,
			  (char *) &m.w.mmw_netw_id_l -
			  (char *) &m,
			  (unsigned char *) &m.w.mmw_netw_id_l, 2);
		mmc_out(base, mmwoff(0, mmw_loopt_sel), 0x00);
	} else {
		/* Disable NWID in the psa. */
		psa.psa_nwid_select = 0x00;
		psa_write(dev,
			  (char *) &psa.psa_nwid_select -
			  (char *) &psa,
			  (unsigned char *) &psa.psa_nwid_select,
			  1);

		/* Disable NWID in the mmc (no filtering). */
		mmc_out(base, mmwoff(0, mmw_loopt_sel),
			MMW_LOOPT_SEL_DIS_NWID);
	}
	/* update the Wavelan checksum */
	update_psa_checksum(dev);

	/* Enable interrupts and restore flags. */
	spin_unlock_irqrestore(&lp->spinlock, flags);

	return ret;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get NWID 
 */
static int wavelan_get_nwid(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu,
			    char *extra)
{
	net_local *lp = netdev_priv(dev);
	psa_t psa;
	unsigned long flags;
	int ret = 0;

	/* Disable interrupts and save flags. */
	spin_lock_irqsave(&lp->spinlock, flags);
	
	/* Read the NWID. */
	psa_read(dev,
		 (char *) psa.psa_nwid - (char *) &psa,
		 (unsigned char *) psa.psa_nwid, 3);
	wrqu->nwid.value = (psa.psa_nwid[0] << 8) + psa.psa_nwid[1];
	wrqu->nwid.disabled = !(psa.psa_nwid_select);
	wrqu->nwid.fixed = 1;	/* Superfluous */

	/* Enable interrupts and restore flags. */
	spin_unlock_irqrestore(&lp->spinlock, flags);

	return ret;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set frequency
 */
static int wavelan_set_freq(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu,
			    char *extra)
{
	unsigned int base = dev->base_addr;
	net_local *lp = netdev_priv(dev);
	unsigned long flags;
	int ret;

	/* Disable interrupts and save flags. */
	spin_lock_irqsave(&lp->spinlock, flags);
	
	/* Attempt to recognise 2.00 cards (2.4 GHz frequency selectable). */
	if (!(mmc_in(base, mmroff(0, mmr_fee_status)) &
	      (MMR_FEE_STATUS_DWLD | MMR_FEE_STATUS_BUSY)))
		ret = wv_set_frequency(base, &(wrqu->freq));
	else
		ret = -EOPNOTSUPP;

	/* Enable interrupts and restore flags. */
	spin_unlock_irqrestore(&lp->spinlock, flags);

	return ret;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get frequency
 */
static int wavelan_get_freq(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu,
			    char *extra)
{
	unsigned int base = dev->base_addr;
	net_local *lp = netdev_priv(dev);
	psa_t psa;
	unsigned long flags;
	int ret = 0;

	/* Disable interrupts and save flags. */
	spin_lock_irqsave(&lp->spinlock, flags);
	
	/* Attempt to recognise 2.00 cards (2.4 GHz frequency selectable).
	 * Does it work for everybody, especially old cards? */
	if (!(mmc_in(base, mmroff(0, mmr_fee_status)) &
	      (MMR_FEE_STATUS_DWLD | MMR_FEE_STATUS_BUSY))) {
		unsigned short freq;

		/* Ask the EEPROM to read the frequency from the first area. */
		fee_read(base, 0x00, &freq, 1);
		wrqu->freq.m = ((freq >> 5) * 5 + 24000L) * 10000;
		wrqu->freq.e = 1;
	} else {
		psa_read(dev,
			 (char *) &psa.psa_subband - (char *) &psa,
			 (unsigned char *) &psa.psa_subband, 1);

		if (psa.psa_subband <= 4) {
			wrqu->freq.m = fixed_bands[psa.psa_subband];
			wrqu->freq.e = (psa.psa_subband != 0);
		} else
			ret = -EOPNOTSUPP;
	}

	/* Enable interrupts and restore flags. */
	spin_unlock_irqrestore(&lp->spinlock, flags);

	return ret;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set level threshold
 */
static int wavelan_set_sens(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu,
			    char *extra)
{
	unsigned int base = dev->base_addr;
	net_local *lp = netdev_priv(dev);
	psa_t psa;
	unsigned long flags;
	int ret = 0;

	/* Disable interrupts and save flags. */
	spin_lock_irqsave(&lp->spinlock, flags);
	
	/* Set the level threshold. */
	/* We should complain loudly if wrqu->sens.fixed = 0, because we
	 * can't set auto mode... */
	psa.psa_thr_pre_set = wrqu->sens.value & 0x3F;
	psa_write(dev,
		  (char *) &psa.psa_thr_pre_set - (char *) &psa,
		  (unsigned char *) &psa.psa_thr_pre_set, 1);
	/* update the Wavelan checksum */
	update_psa_checksum(dev);
	mmc_out(base, mmwoff(0, mmw_thr_pre_set),
		psa.psa_thr_pre_set);

	/* Enable interrupts and restore flags. */
	spin_unlock_irqrestore(&lp->spinlock, flags);

	return ret;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get level threshold
 */
static int wavelan_get_sens(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu,
			    char *extra)
{
	net_local *lp = netdev_priv(dev);
	psa_t psa;
	unsigned long flags;
	int ret = 0;

	/* Disable interrupts and save flags. */
	spin_lock_irqsave(&lp->spinlock, flags);
	
	/* Read the level threshold. */
	psa_read(dev,
		 (char *) &psa.psa_thr_pre_set - (char *) &psa,
		 (unsigned char *) &psa.psa_thr_pre_set, 1);
	wrqu->sens.value = psa.psa_thr_pre_set & 0x3F;
	wrqu->sens.fixed = 1;

	/* Enable interrupts and restore flags. */
	spin_unlock_irqrestore(&lp->spinlock, flags);

	return ret;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set encryption key
 */
static int wavelan_set_encode(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu,
			      char *extra)
{
	unsigned int base = dev->base_addr;
	net_local *lp = netdev_priv(dev);
	unsigned long flags;
	psa_t psa;
	int ret = 0;

	/* Disable interrupts and save flags. */
	spin_lock_irqsave(&lp->spinlock, flags);

	/* Check if capable of encryption */
	if (!mmc_encr(base)) {
		ret = -EOPNOTSUPP;
	}

	/* Check the size of the key */
	if((wrqu->encoding.length != 8) && (wrqu->encoding.length != 0)) {
		ret = -EINVAL;
	}

	if(!ret) {
		/* Basic checking... */
		if (wrqu->encoding.length == 8) {
			/* Copy the key in the driver */
			memcpy(psa.psa_encryption_key, extra,
			       wrqu->encoding.length);
			psa.psa_encryption_select = 1;

			psa_write(dev,
				  (char *) &psa.psa_encryption_select -
				  (char *) &psa,
				  (unsigned char *) &psa.
				  psa_encryption_select, 8 + 1);

			mmc_out(base, mmwoff(0, mmw_encr_enable),
				MMW_ENCR_ENABLE_EN | MMW_ENCR_ENABLE_MODE);
			mmc_write(base, mmwoff(0, mmw_encr_key),
				  (unsigned char *) &psa.
				  psa_encryption_key, 8);
		}

		/* disable encryption */
		if (wrqu->encoding.flags & IW_ENCODE_DISABLED) {
			psa.psa_encryption_select = 0;
			psa_write(dev,
				  (char *) &psa.psa_encryption_select -
				  (char *) &psa,
				  (unsigned char *) &psa.
				  psa_encryption_select, 1);

			mmc_out(base, mmwoff(0, mmw_encr_enable), 0);
		}
		/* update the Wavelan checksum */
		update_psa_checksum(dev);
	}

	/* Enable interrupts and restore flags. */
	spin_unlock_irqrestore(&lp->spinlock, flags);

	return ret;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get encryption key
 */
static int wavelan_get_encode(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu,
			      char *extra)
{
	unsigned int base = dev->base_addr;
	net_local *lp = netdev_priv(dev);
	psa_t psa;
	unsigned long flags;
	int ret = 0;

	/* Disable interrupts and save flags. */
	spin_lock_irqsave(&lp->spinlock, flags);
	
	/* Check if encryption is available */
	if (!mmc_encr(base)) {
		ret = -EOPNOTSUPP;
	} else {
		/* Read the encryption key */
		psa_read(dev,
			 (char *) &psa.psa_encryption_select -
			 (char *) &psa,
			 (unsigned char *) &psa.
			 psa_encryption_select, 1 + 8);

		/* encryption is enabled ? */
		if (psa.psa_encryption_select)
			wrqu->encoding.flags = IW_ENCODE_ENABLED;
		else
			wrqu->encoding.flags = IW_ENCODE_DISABLED;
		wrqu->encoding.flags |= mmc_encr(base);

		/* Copy the key to the user buffer */
		wrqu->encoding.length = 8;
		memcpy(extra, psa.psa_encryption_key, wrqu->encoding.length);
	}

	/* Enable interrupts and restore flags. */
	spin_unlock_irqrestore(&lp->spinlock, flags);

	return ret;
}

#ifdef WAVELAN_ROAMING_EXT
/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set ESSID (domain)
 */
static int wavelan_set_essid(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu,
			     char *extra)
{
	net_local *lp = netdev_priv(dev);
	unsigned long flags;
	int ret = 0;

	/* Disable interrupts and save flags. */
	spin_lock_irqsave(&lp->spinlock, flags);
	
	/* Check if disable */
	if(wrqu->data.flags == 0)
		lp->filter_domains = 0;
	else {
		char	essid[IW_ESSID_MAX_SIZE + 1];
		char *	endp;

		/* Terminate the string */
		memcpy(essid, extra, wrqu->data.length);
		essid[IW_ESSID_MAX_SIZE] = '\0';

#ifdef DEBUG_IOCTL_INFO
		printk(KERN_DEBUG "SetEssid : ``%s''\n", essid);
#endif	/* DEBUG_IOCTL_INFO */

		/* Convert to a number (note : Wavelan specific) */
		lp->domain_id = simple_strtoul(essid, &endp, 16);
		/* Has it worked  ? */
		if(endp > essid)
			lp->filter_domains = 1;
		else {
			lp->filter_domains = 0;
			ret = -EINVAL;
		}
	}

	/* Enable interrupts and restore flags. */
	spin_unlock_irqrestore(&lp->spinlock, flags);

	return ret;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get ESSID (domain)
 */
static int wavelan_get_essid(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu,
			     char *extra)
{
	net_local *lp = netdev_priv(dev);

	/* Is the domain ID active ? */
	wrqu->data.flags = lp->filter_domains;

	/* Copy Domain ID into a string (Wavelan specific) */
	/* Sound crazy, be we can't have a snprintf in the kernel !!! */
	sprintf(extra, "%lX", lp->domain_id);
	extra[IW_ESSID_MAX_SIZE] = '\0';

	/* Set the length */
	wrqu->data.length = strlen(extra);

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set AP address
 */
static int wavelan_set_wap(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu,
			   char *extra)
{
#ifdef DEBUG_IOCTL_INFO
	printk(KERN_DEBUG "Set AP to : %02X:%02X:%02X:%02X:%02X:%02X\n",
	       wrqu->ap_addr.sa_data[0],
	       wrqu->ap_addr.sa_data[1],
	       wrqu->ap_addr.sa_data[2],
	       wrqu->ap_addr.sa_data[3],
	       wrqu->ap_addr.sa_data[4],
	       wrqu->ap_addr.sa_data[5]);
#endif	/* DEBUG_IOCTL_INFO */

	return -EOPNOTSUPP;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get AP address
 */
static int wavelan_get_wap(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu,
			   char *extra)
{
	/* Should get the real McCoy instead of own Ethernet address */
	memcpy(wrqu->ap_addr.sa_data, dev->dev_addr, WAVELAN_ADDR_SIZE);
	wrqu->ap_addr.sa_family = ARPHRD_ETHER;

	return -EOPNOTSUPP;
}
#endif	/* WAVELAN_ROAMING_EXT */

#ifdef WAVELAN_ROAMING
/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set mode
 */
static int wavelan_set_mode(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu,
			    char *extra)
{
	net_local *lp = netdev_priv(dev);
	unsigned long flags;
	int ret = 0;

	/* Disable interrupts and save flags. */
	spin_lock_irqsave(&lp->spinlock, flags);

	/* Check mode */
	switch(wrqu->mode) {
	case IW_MODE_ADHOC:
		if(do_roaming) {
			wv_roam_cleanup(dev);
			do_roaming = 0;
		}
		break;
	case IW_MODE_INFRA:
		if(!do_roaming) {
			wv_roam_init(dev);
			do_roaming = 1;
		}
		break;
	default:
		ret = -EINVAL;
	}

	/* Enable interrupts and restore flags. */
	spin_unlock_irqrestore(&lp->spinlock, flags);

	return ret;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get mode
 */
static int wavelan_get_mode(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu,
			    char *extra)
{
	if(do_roaming)
		wrqu->mode = IW_MODE_INFRA;
	else
		wrqu->mode = IW_MODE_ADHOC;

	return 0;
}
#endif	/* WAVELAN_ROAMING */

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get range info
 */
static int wavelan_get_range(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu,
			     char *extra)
{
	unsigned int base = dev->base_addr;
	net_local *lp = netdev_priv(dev);
	struct iw_range *range = (struct iw_range *) extra;
	unsigned long flags;
	int ret = 0;

	/* Set the length (very important for backward compatibility) */
	wrqu->data.length = sizeof(struct iw_range);

	/* Set all the info we don't care or don't know about to zero */
	memset(range, 0, sizeof(struct iw_range));

	/* Set the Wireless Extension versions */
	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 9;

	/* Set information in the range struct.  */
	range->throughput = 1.4 * 1000 * 1000;	/* don't argue on this ! */
	range->min_nwid = 0x0000;
	range->max_nwid = 0xFFFF;

	range->sensitivity = 0x3F;
	range->max_qual.qual = MMR_SGNL_QUAL;
	range->max_qual.level = MMR_SIGNAL_LVL;
	range->max_qual.noise = MMR_SILENCE_LVL;
	range->avg_qual.qual = MMR_SGNL_QUAL; /* Always max */
	/* Need to get better values for those two */
	range->avg_qual.level = 30;
	range->avg_qual.noise = 8;

	range->num_bitrates = 1;
	range->bitrate[0] = 2000000;	/* 2 Mb/s */

	/* Event capability (kernel + driver) */
	range->event_capa[0] = (IW_EVENT_CAPA_MASK(0x8B02) |
				IW_EVENT_CAPA_MASK(0x8B04) |
				IW_EVENT_CAPA_MASK(0x8B06));
	range->event_capa[1] = IW_EVENT_CAPA_K_1;

	/* Disable interrupts and save flags. */
	spin_lock_irqsave(&lp->spinlock, flags);
	
	/* Attempt to recognise 2.00 cards (2.4 GHz frequency selectable). */
	if (!(mmc_in(base, mmroff(0, mmr_fee_status)) &
	      (MMR_FEE_STATUS_DWLD | MMR_FEE_STATUS_BUSY))) {
		range->num_channels = 10;
		range->num_frequency = wv_frequency_list(base, range->freq,
							IW_MAX_FREQUENCIES);
	} else
		range->num_channels = range->num_frequency = 0;

	/* Encryption supported ? */
	if (mmc_encr(base)) {
		range->encoding_size[0] = 8;	/* DES = 64 bits key */
		range->num_encoding_sizes = 1;
		range->max_encoding_tokens = 1;	/* Only one key possible */
	} else {
		range->num_encoding_sizes = 0;
		range->max_encoding_tokens = 0;
	}

	/* Enable interrupts and restore flags. */
	spin_unlock_irqrestore(&lp->spinlock, flags);

	return ret;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Private Handler : set quality threshold
 */
static int wavelan_set_qthr(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu,
			    char *extra)
{
	unsigned int base = dev->base_addr;
	net_local *lp = netdev_priv(dev);
	psa_t psa;
	unsigned long flags;

	/* Disable interrupts and save flags. */
	spin_lock_irqsave(&lp->spinlock, flags);
	
	psa.psa_quality_thr = *(extra) & 0x0F;
	psa_write(dev,
		  (char *) &psa.psa_quality_thr - (char *) &psa,
		  (unsigned char *) &psa.psa_quality_thr, 1);
	/* update the Wavelan checksum */
	update_psa_checksum(dev);
	mmc_out(base, mmwoff(0, mmw_quality_thr),
		psa.psa_quality_thr);

	/* Enable interrupts and restore flags. */
	spin_unlock_irqrestore(&lp->spinlock, flags);

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Private Handler : get quality threshold
 */
static int wavelan_get_qthr(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu,
			    char *extra)
{
	net_local *lp = netdev_priv(dev);
	psa_t psa;
	unsigned long flags;

	/* Disable interrupts and save flags. */
	spin_lock_irqsave(&lp->spinlock, flags);
	
	psa_read(dev,
		 (char *) &psa.psa_quality_thr - (char *) &psa,
		 (unsigned char *) &psa.psa_quality_thr, 1);
	*(extra) = psa.psa_quality_thr & 0x0F;

	/* Enable interrupts and restore flags. */
	spin_unlock_irqrestore(&lp->spinlock, flags);

	return 0;
}

#ifdef WAVELAN_ROAMING
/*------------------------------------------------------------------*/
/*
 * Wireless Private Handler : set roaming
 */
static int wavelan_set_roam(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu,
			    char *extra)
{
	net_local *lp = netdev_priv(dev);
	unsigned long flags;

	/* Disable interrupts and save flags. */
	spin_lock_irqsave(&lp->spinlock, flags);
	
	/* Note : should check if user == root */
	if(do_roaming && (*extra)==0)
		wv_roam_cleanup(dev);
	else if(do_roaming==0 && (*extra)!=0)
		wv_roam_init(dev);

	do_roaming = (*extra);

	/* Enable interrupts and restore flags. */
	spin_unlock_irqrestore(&lp->spinlock, flags);

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Private Handler : get quality threshold
 */
static int wavelan_get_roam(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu,
			    char *extra)
{
	*(extra) = do_roaming;

	return 0;
}
#endif	/* WAVELAN_ROAMING */

#ifdef HISTOGRAM
/*------------------------------------------------------------------*/
/*
 * Wireless Private Handler : set histogram
 */
static int wavelan_set_histo(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu,
			     char *extra)
{
	net_local *lp = netdev_priv(dev);

	/* Check the number of intervals. */
	if (wrqu->data.length > 16) {
		return(-E2BIG);
	}

	/* Disable histo while we copy the addresses.
	 * As we don't disable interrupts, we need to do this */
	lp->his_number = 0;

	/* Are there ranges to copy? */
	if (wrqu->data.length > 0) {
		/* Copy interval ranges to the driver */
		memcpy(lp->his_range, extra, wrqu->data.length);

		{
		  int i;
		  printk(KERN_DEBUG "Histo :");
		  for(i = 0; i < wrqu->data.length; i++)
		    printk(" %d", lp->his_range[i]);
		  printk("\n");
		}

		/* Reset result structure. */
		memset(lp->his_sum, 0x00, sizeof(long) * 16);
	}

	/* Now we can set the number of ranges */
	lp->his_number = wrqu->data.length;

	return(0);
}

/*------------------------------------------------------------------*/
/*
 * Wireless Private Handler : get histogram
 */
static int wavelan_get_histo(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu,
			     char *extra)
{
	net_local *lp = netdev_priv(dev);

	/* Set the number of intervals. */
	wrqu->data.length = lp->his_number;

	/* Give back the distribution statistics */
	if(lp->his_number > 0)
		memcpy(extra, lp->his_sum, sizeof(long) * lp->his_number);

	return(0);
}
#endif			/* HISTOGRAM */

/*------------------------------------------------------------------*/
/*
 * Structures to export the Wireless Handlers
 */

static const struct iw_priv_args wavelan_private_args[] = {
/*{ cmd,         set_args,                            get_args, name } */
  { SIOCSIPQTHR, IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | 1, 0, "setqualthr" },
  { SIOCGIPQTHR, 0, IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | 1, "getqualthr" },
  { SIOCSIPROAM, IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | 1, 0, "setroam" },
  { SIOCGIPROAM, 0, IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | 1, "getroam" },
  { SIOCSIPHISTO, IW_PRIV_TYPE_BYTE | 16,                    0, "sethisto" },
  { SIOCGIPHISTO, 0,                     IW_PRIV_TYPE_INT | 16, "gethisto" },
};

static const iw_handler		wavelan_handler[] =
{
	NULL,				/* SIOCSIWNAME */
	wavelan_get_name,		/* SIOCGIWNAME */
	wavelan_set_nwid,		/* SIOCSIWNWID */
	wavelan_get_nwid,		/* SIOCGIWNWID */
	wavelan_set_freq,		/* SIOCSIWFREQ */
	wavelan_get_freq,		/* SIOCGIWFREQ */
#ifdef WAVELAN_ROAMING
	wavelan_set_mode,		/* SIOCSIWMODE */
	wavelan_get_mode,		/* SIOCGIWMODE */
#else	/* WAVELAN_ROAMING */
	NULL,				/* SIOCSIWMODE */
	NULL,				/* SIOCGIWMODE */
#endif	/* WAVELAN_ROAMING */
	wavelan_set_sens,		/* SIOCSIWSENS */
	wavelan_get_sens,		/* SIOCGIWSENS */
	NULL,				/* SIOCSIWRANGE */
	wavelan_get_range,		/* SIOCGIWRANGE */
	NULL,				/* SIOCSIWPRIV */
	NULL,				/* SIOCGIWPRIV */
	NULL,				/* SIOCSIWSTATS */
	NULL,				/* SIOCGIWSTATS */
	iw_handler_set_spy,		/* SIOCSIWSPY */
	iw_handler_get_spy,		/* SIOCGIWSPY */
	iw_handler_set_thrspy,		/* SIOCSIWTHRSPY */
	iw_handler_get_thrspy,		/* SIOCGIWTHRSPY */
#ifdef WAVELAN_ROAMING_EXT
	wavelan_set_wap,		/* SIOCSIWAP */
	wavelan_get_wap,		/* SIOCGIWAP */
	NULL,				/* -- hole -- */
	NULL,				/* SIOCGIWAPLIST */
	NULL,				/* -- hole -- */
	NULL,				/* -- hole -- */
	wavelan_set_essid,		/* SIOCSIWESSID */
	wavelan_get_essid,		/* SIOCGIWESSID */
#else	/* WAVELAN_ROAMING_EXT */
	NULL,				/* SIOCSIWAP */
	NULL,				/* SIOCGIWAP */
	NULL,				/* -- hole -- */
	NULL,				/* SIOCGIWAPLIST */
	NULL,				/* -- hole -- */
	NULL,				/* -- hole -- */
	NULL,				/* SIOCSIWESSID */
	NULL,				/* SIOCGIWESSID */
#endif	/* WAVELAN_ROAMING_EXT */
	NULL,				/* SIOCSIWNICKN */
	NULL,				/* SIOCGIWNICKN */
	NULL,				/* -- hole -- */
	NULL,				/* -- hole -- */
	NULL,				/* SIOCSIWRATE */
	NULL,				/* SIOCGIWRATE */
	NULL,				/* SIOCSIWRTS */
	NULL,				/* SIOCGIWRTS */
	NULL,				/* SIOCSIWFRAG */
	NULL,				/* SIOCGIWFRAG */
	NULL,				/* SIOCSIWTXPOW */
	NULL,				/* SIOCGIWTXPOW */
	NULL,				/* SIOCSIWRETRY */
	NULL,				/* SIOCGIWRETRY */
	wavelan_set_encode,		/* SIOCSIWENCODE */
	wavelan_get_encode,		/* SIOCGIWENCODE */
};

static const iw_handler		wavelan_private_handler[] =
{
	wavelan_set_qthr,		/* SIOCIWFIRSTPRIV */
	wavelan_get_qthr,		/* SIOCIWFIRSTPRIV + 1 */
#ifdef WAVELAN_ROAMING
	wavelan_set_roam,		/* SIOCIWFIRSTPRIV + 2 */
	wavelan_get_roam,		/* SIOCIWFIRSTPRIV + 3 */
#else	/* WAVELAN_ROAMING */
	NULL,				/* SIOCIWFIRSTPRIV + 2 */
	NULL,				/* SIOCIWFIRSTPRIV + 3 */
#endif	/* WAVELAN_ROAMING */
#ifdef HISTOGRAM
	wavelan_set_histo,		/* SIOCIWFIRSTPRIV + 4 */
	wavelan_get_histo,		/* SIOCIWFIRSTPRIV + 5 */
#endif	/* HISTOGRAM */
};

static const struct iw_handler_def	wavelan_handler_def =
{
	.num_standard	= ARRAY_SIZE(wavelan_handler),
	.num_private	= ARRAY_SIZE(wavelan_private_handler),
	.num_private_args = ARRAY_SIZE(wavelan_private_args),
	.standard	= wavelan_handler,
	.private	= wavelan_private_handler,
	.private_args	= wavelan_private_args,
	.get_wireless_stats = wavelan_get_wireless_stats,
};

/*------------------------------------------------------------------*/
/*
 * Get wireless statistics
 * Called by /proc/net/wireless...
 */
static iw_stats *
wavelan_get_wireless_stats(struct net_device *	dev)
{
  unsigned int		base = dev->base_addr;
  net_local *		lp = netdev_priv(dev);
  mmr_t			m;
  iw_stats *		wstats;
  unsigned long		flags;

#ifdef DEBUG_IOCTL_TRACE
  printk(KERN_DEBUG "%s: ->wavelan_get_wireless_stats()\n", dev->name);
#endif

  /* Disable interrupts & save flags */
  spin_lock_irqsave(&lp->spinlock, flags);

  wstats = &lp->wstats;

  /* Get data from the mmc */
  mmc_out(base, mmwoff(0, mmw_freeze), 1);

  mmc_read(base, mmroff(0, mmr_dce_status), &m.mmr_dce_status, 1);
  mmc_read(base, mmroff(0, mmr_wrong_nwid_l), &m.mmr_wrong_nwid_l, 2);
  mmc_read(base, mmroff(0, mmr_thr_pre_set), &m.mmr_thr_pre_set, 4);

  mmc_out(base, mmwoff(0, mmw_freeze), 0);

  /* Copy data to wireless stuff */
  wstats->status = m.mmr_dce_status & MMR_DCE_STATUS;
  wstats->qual.qual = m.mmr_sgnl_qual & MMR_SGNL_QUAL;
  wstats->qual.level = m.mmr_signal_lvl & MMR_SIGNAL_LVL;
  wstats->qual.noise = m.mmr_silence_lvl & MMR_SILENCE_LVL;
  wstats->qual.updated = (((m.mmr_signal_lvl & MMR_SIGNAL_LVL_VALID) >> 7) |
			  ((m.mmr_signal_lvl & MMR_SIGNAL_LVL_VALID) >> 6) |
			  ((m.mmr_silence_lvl & MMR_SILENCE_LVL_VALID) >> 5));
  wstats->discard.nwid += (m.mmr_wrong_nwid_h << 8) | m.mmr_wrong_nwid_l;
  wstats->discard.code = 0L;
  wstats->discard.misc = 0L;

  /* ReEnable interrupts & restore flags */
  spin_unlock_irqrestore(&lp->spinlock, flags);

#ifdef DEBUG_IOCTL_TRACE
  printk(KERN_DEBUG "%s: <-wavelan_get_wireless_stats()\n", dev->name);
#endif
  return &lp->wstats;
}

/************************* PACKET RECEPTION *************************/
/*
 * This part deal with receiving the packets.
 * The interrupt handler get an interrupt when a packet has been
 * successfully received and called this part...
 */

/*------------------------------------------------------------------*/
/*
 * Calculate the starting address of the frame pointed to by the receive
 * frame pointer and verify that the frame seem correct
 * (called by wv_packet_rcv())
 */
static int
wv_start_of_frame(struct net_device *	dev,
		  int		rfp,	/* end of frame */
		  int		wrap)	/* start of buffer */
{
  unsigned int	base = dev->base_addr;
  int		rp;
  int		len;

  rp = (rfp - 5 + RX_SIZE) % RX_SIZE;
  outb(rp & 0xff, PIORL(base));
  outb(((rp >> 8) & PIORH_MASK), PIORH(base));
  len = inb(PIOP(base));
  len |= inb(PIOP(base)) << 8;

  /* Sanity checks on size */
  /* Frame too big */
  if(len > MAXDATAZ + 100)
    {
#ifdef DEBUG_RX_ERROR
      printk(KERN_INFO "%s: wv_start_of_frame: Received frame too large, rfp %d len 0x%x\n",
	     dev->name, rfp, len);
#endif
      return(-1);
    }
  
  /* Frame too short */
  if(len < 7)
    {
#ifdef DEBUG_RX_ERROR
      printk(KERN_INFO "%s: wv_start_of_frame: Received null frame, rfp %d len 0x%x\n",
	     dev->name, rfp, len);
#endif
      return(-1);
    }
  
  /* Wrap around buffer */
  if(len > ((wrap - (rfp - len) + RX_SIZE) % RX_SIZE))	/* magic formula ! */
    {
#ifdef DEBUG_RX_ERROR
      printk(KERN_INFO "%s: wv_start_of_frame: wrap around buffer, wrap %d rfp %d len 0x%x\n",
	     dev->name, wrap, rfp, len);
#endif
      return(-1);
    }

  return((rp - len + RX_SIZE) % RX_SIZE);
} /* wv_start_of_frame */

/*------------------------------------------------------------------*/
/*
 * This routine does the actual copy of data (including the ethernet
 * header structure) from the WaveLAN card to an sk_buff chain that
 * will be passed up to the network interface layer. NOTE: We
 * currently don't handle trailer protocols (neither does the rest of
 * the network interface), so if that is needed, it will (at least in
 * part) be added here.  The contents of the receive ring buffer are
 * copied to a message chain that is then passed to the kernel.
 *
 * Note: if any errors occur, the packet is "dropped on the floor"
 * (called by wv_packet_rcv())
 */
static void
wv_packet_read(struct net_device *		dev,
	       int		fd_p,
	       int		sksize)
{
  net_local *		lp = netdev_priv(dev);
  struct sk_buff *	skb;

#ifdef DEBUG_RX_TRACE
  printk(KERN_DEBUG "%s: ->wv_packet_read(0x%X, %d)\n",
	 dev->name, fd_p, sksize);
#endif

  /* Allocate some buffer for the new packet */
  if((skb = dev_alloc_skb(sksize+2)) == (struct sk_buff *) NULL)
    {
#ifdef DEBUG_RX_ERROR
      printk(KERN_INFO "%s: wv_packet_read(): could not alloc_skb(%d, GFP_ATOMIC)\n",
	     dev->name, sksize);
#endif
      lp->stats.rx_dropped++;
      /*
       * Not only do we want to return here, but we also need to drop the
       * packet on the floor to clear the interrupt.
       */
      return;
    }

  skb_reserve(skb, 2);
  fd_p = read_ringbuf(dev, fd_p, (char *) skb_put(skb, sksize), sksize);
  skb->protocol = eth_type_trans(skb, dev);

#ifdef DEBUG_RX_INFO
  wv_packet_info(skb_mac_header(skb), sksize, dev->name, "wv_packet_read");
#endif	/* DEBUG_RX_INFO */
     
  /* Statistics gathering & stuff associated.
   * It seem a bit messy with all the define, but it's really simple... */
  if(
#ifdef IW_WIRELESS_SPY
     (lp->spy_data.spy_number > 0) ||
#endif	/* IW_WIRELESS_SPY */
#ifdef HISTOGRAM
     (lp->his_number > 0) ||
#endif	/* HISTOGRAM */
#ifdef WAVELAN_ROAMING
     (do_roaming) ||
#endif	/* WAVELAN_ROAMING */
     0)
    {
      u_char	stats[3];	/* Signal level, Noise level, Signal quality */

      /* read signal level, silence level and signal quality bytes */
      fd_p = read_ringbuf(dev, (fd_p + 4) % RX_SIZE + RX_BASE,
			  stats, 3);
#ifdef DEBUG_RX_INFO
      printk(KERN_DEBUG "%s: wv_packet_read(): Signal level %d/63, Silence level %d/63, signal quality %d/16\n",
	     dev->name, stats[0] & 0x3F, stats[1] & 0x3F, stats[2] & 0x0F);
#endif

#ifdef WAVELAN_ROAMING
      if(do_roaming)
	if(WAVELAN_BEACON(skb->data))
	  wl_roam_gather(dev, skb->data, stats);
#endif	/* WAVELAN_ROAMING */
	  
#ifdef WIRELESS_SPY
      wl_spy_gather(dev, skb_mac_header(skb) + WAVELAN_ADDR_SIZE, stats);
#endif	/* WIRELESS_SPY */
#ifdef HISTOGRAM
      wl_his_gather(dev, stats);
#endif	/* HISTOGRAM */
    }

  /*
   * Hand the packet to the Network Module
   */
  netif_rx(skb);

  /* Keep stats up to date */
  dev->last_rx = jiffies;
  lp->stats.rx_packets++;
  lp->stats.rx_bytes += sksize;

#ifdef DEBUG_RX_TRACE
  printk(KERN_DEBUG "%s: <-wv_packet_read()\n", dev->name);
#endif
  return;
}

/*------------------------------------------------------------------*/
/*
 * This routine is called by the interrupt handler to initiate a
 * packet transfer from the card to the network interface layer above
 * this driver.  This routine checks if a buffer has been successfully
 * received by the WaveLAN card.  If so, the routine wv_packet_read is
 * called to do the actual transfer of the card's data including the
 * ethernet header into a packet consisting of an sk_buff chain.
 * (called by wavelan_interrupt())
 * Note : the spinlock is already grabbed for us and irq are disabled.
 */
static void
wv_packet_rcv(struct net_device *	dev)
{
  unsigned int	base = dev->base_addr;
  net_local *	lp = netdev_priv(dev);
  int		newrfp;
  int		rp;
  int		len;
  int		f_start;
  int		status;
  int		i593_rfp;
  int		stat_ptr;
  u_char	c[4];

#ifdef DEBUG_RX_TRACE
  printk(KERN_DEBUG "%s: ->wv_packet_rcv()\n", dev->name);
#endif

  /* Get the new receive frame pointer from the i82593 chip */
  outb(CR0_STATUS_2 | OP0_NOP, LCCR(base));
  i593_rfp = inb(LCSR(base));
  i593_rfp |= inb(LCSR(base)) << 8;
  i593_rfp %= RX_SIZE;

  /* Get the new receive frame pointer from the WaveLAN card.
   * It is 3 bytes more than the increment of the i82593 receive
   * frame pointer, for each packet. This is because it includes the
   * 3 roaming bytes added by the mmc.
   */
  newrfp = inb(RPLL(base));
  newrfp |= inb(RPLH(base)) << 8;
  newrfp %= RX_SIZE;

#ifdef DEBUG_RX_INFO
  printk(KERN_DEBUG "%s: wv_packet_rcv(): i593_rfp %d stop %d newrfp %d lp->rfp %d\n",
	 dev->name, i593_rfp, lp->stop, newrfp, lp->rfp);
#endif

#ifdef DEBUG_RX_ERROR
  /* If no new frame pointer... */
  if(lp->overrunning || newrfp == lp->rfp)
    printk(KERN_INFO "%s: wv_packet_rcv(): no new frame: i593_rfp %d stop %d newrfp %d lp->rfp %d\n",
	   dev->name, i593_rfp, lp->stop, newrfp, lp->rfp);
#endif

  /* Read all frames (packets) received */
  while(newrfp != lp->rfp)
    {
      /* A frame is composed of the packet, followed by a status word,
       * the length of the frame (word) and the mmc info (SNR & qual).
       * It's because the length is at the end that we can only scan
       * frames backward. */

      /* Find the first frame by skipping backwards over the frames */
      rp = newrfp;	/* End of last frame */
      while(((f_start = wv_start_of_frame(dev, rp, newrfp)) != lp->rfp) &&
	    (f_start != -1))
	  rp = f_start;

      /* If we had a problem */
      if(f_start == -1)
	{
#ifdef DEBUG_RX_ERROR
	  printk(KERN_INFO "wavelan_cs: cannot find start of frame ");
	  printk(" i593_rfp %d stop %d newrfp %d lp->rfp %d\n",
		 i593_rfp, lp->stop, newrfp, lp->rfp);
#endif
	  lp->rfp = rp;		/* Get to the last usable frame */
	  continue;
	}

      /* f_start point to the beggining of the first frame received
       * and rp to the beggining of the next one */

      /* Read status & length of the frame */
      stat_ptr = (rp - 7 + RX_SIZE) % RX_SIZE;
      stat_ptr = read_ringbuf(dev, stat_ptr, c, 4);
      status = c[0] | (c[1] << 8);
      len = c[2] | (c[3] << 8);

      /* Check status */
      if((status & RX_RCV_OK) != RX_RCV_OK)
	{
	  lp->stats.rx_errors++;
	  if(status & RX_NO_SFD)
	    lp->stats.rx_frame_errors++;
	  if(status & RX_CRC_ERR)
	    lp->stats.rx_crc_errors++;
	  if(status & RX_OVRRUN)
	    lp->stats.rx_over_errors++;

#ifdef DEBUG_RX_FAIL
	  printk(KERN_DEBUG "%s: wv_packet_rcv(): packet not received ok, status = 0x%x\n",
		 dev->name, status);
#endif
	}
      else
	/* Read the packet and transmit to Linux */
	wv_packet_read(dev, f_start, len - 2);

      /* One frame has been processed, skip it */
      lp->rfp = rp;
    }

  /*
   * Update the frame stop register, but set it to less than
   * the full 8K to allow space for 3 bytes of signal strength
   * per packet.
   */
  lp->stop = (i593_rfp + RX_SIZE - ((RX_SIZE / 64) * 3)) % RX_SIZE;
  outb(OP0_SWIT_TO_PORT_1 | CR0_CHNL, LCCR(base));
  outb(CR1_STOP_REG_UPDATE | (lp->stop >> RX_SIZE_SHIFT), LCCR(base));
  outb(OP1_SWIT_TO_PORT_0, LCCR(base));

#ifdef DEBUG_RX_TRACE
  printk(KERN_DEBUG "%s: <-wv_packet_rcv()\n", dev->name);
#endif
}

/*********************** PACKET TRANSMISSION ***********************/
/*
 * This part deal with sending packet through the wavelan
 * We copy the packet to the send buffer and then issue the send
 * command to the i82593. The result of this operation will be
 * checked in wavelan_interrupt()
 */

/*------------------------------------------------------------------*/
/*
 * This routine fills in the appropriate registers and memory
 * locations on the WaveLAN card and starts the card off on
 * the transmit.
 * (called in wavelan_packet_xmit())
 */
static void
wv_packet_write(struct net_device *	dev,
		void *		buf,
		short		length)
{
  net_local *		lp = netdev_priv(dev);
  unsigned int		base = dev->base_addr;
  unsigned long		flags;
  int			clen = length;
  register u_short	xmtdata_base = TX_BASE;

#ifdef DEBUG_TX_TRACE
  printk(KERN_DEBUG "%s: ->wv_packet_write(%d)\n", dev->name, length);
#endif

  spin_lock_irqsave(&lp->spinlock, flags);

  /* Write the length of data buffer followed by the buffer */
  outb(xmtdata_base & 0xff, PIORL(base));
  outb(((xmtdata_base >> 8) & PIORH_MASK) | PIORH_SEL_TX, PIORH(base));
  outb(clen & 0xff, PIOP(base));	/* lsb */
  outb(clen >> 8, PIOP(base));  	/* msb */

  /* Send the data */
  outsb(PIOP(base), buf, clen);

  /* Indicate end of transmit chain */
  outb(OP0_NOP, PIOP(base));
  /* josullvn@cs.cmu.edu: need to send a second NOP for alignment... */
  outb(OP0_NOP, PIOP(base));

  /* Reset the transmit DMA pointer */
  hacr_write_slow(base, HACR_PWR_STAT | HACR_TX_DMA_RESET);
  hacr_write(base, HACR_DEFAULT);
  /* Send the transmit command */
  wv_82593_cmd(dev, "wv_packet_write(): transmit",
	       OP0_TRANSMIT, SR0_NO_RESULT);

  /* Make sure the watchdog will keep quiet for a while */
  dev->trans_start = jiffies;

  /* Keep stats up to date */
  lp->stats.tx_bytes += length;

  spin_unlock_irqrestore(&lp->spinlock, flags);

#ifdef DEBUG_TX_INFO
  wv_packet_info((u_char *) buf, length, dev->name, "wv_packet_write");
#endif	/* DEBUG_TX_INFO */

#ifdef DEBUG_TX_TRACE
  printk(KERN_DEBUG "%s: <-wv_packet_write()\n", dev->name);
#endif
}

/*------------------------------------------------------------------*/
/*
 * This routine is called when we want to send a packet (NET3 callback)
 * In this routine, we check if the harware is ready to accept
 * the packet. We also prevent reentrance. Then, we call the function
 * to send the packet...
 */
static int
wavelan_packet_xmit(struct sk_buff *	skb,
		    struct net_device *		dev)
{
  net_local *		lp = netdev_priv(dev);
  unsigned long		flags;

#ifdef DEBUG_TX_TRACE
  printk(KERN_DEBUG "%s: ->wavelan_packet_xmit(0x%X)\n", dev->name,
	 (unsigned) skb);
#endif

  /*
   * Block a timer-based transmit from overlapping a previous transmit.
   * In other words, prevent reentering this routine.
   */
  netif_stop_queue(dev);

  /* If somebody has asked to reconfigure the controller,
   * we can do it now */
  if(lp->reconfig_82593)
    {
      spin_lock_irqsave(&lp->spinlock, flags);	/* Disable interrupts */
      wv_82593_config(dev);
      spin_unlock_irqrestore(&lp->spinlock, flags);	/* Re-enable interrupts */
      /* Note : the configure procedure was totally synchronous,
       * so the Tx buffer is now free */
    }

#ifdef DEBUG_TX_ERROR
	if (skb->next)
		printk(KERN_INFO "skb has next\n");
#endif

	/* Check if we need some padding */
	/* Note : on wireless the propagation time is in the order of 1us,
	 * and we don't have the Ethernet specific requirement of beeing
	 * able to detect collisions, therefore in theory we don't really
	 * need to pad. Jean II */
	if (skb_padto(skb, ETH_ZLEN))
		return 0;

  wv_packet_write(dev, skb->data, skb->len);

  dev_kfree_skb(skb);

#ifdef DEBUG_TX_TRACE
  printk(KERN_DEBUG "%s: <-wavelan_packet_xmit()\n", dev->name);
#endif
  return(0);
}

/********************** HARDWARE CONFIGURATION **********************/
/*
 * This part do the real job of starting and configuring the hardware.
 */

/*------------------------------------------------------------------*/
/*
 * Routine to initialize the Modem Management Controller.
 * (called by wv_hw_config())
 */
static int
wv_mmc_init(struct net_device *	dev)
{
  unsigned int	base = dev->base_addr;
  psa_t		psa;
  mmw_t		m;
  int		configured;
  int		i;		/* Loop counter */

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: ->wv_mmc_init()\n", dev->name);
#endif

  /* Read the parameter storage area */
  psa_read(dev, 0, (unsigned char *) &psa, sizeof(psa));

  /*
   * Check the first three octets of the MAC addr for the manufacturer's code.
   * Note: If you get the error message below, you've got a
   * non-NCR/AT&T/Lucent PCMCIA cards, see wavelan_cs.h for detail on
   * how to configure your card...
   */
  for (i = 0; i < ARRAY_SIZE(MAC_ADDRESSES); i++)
    if ((psa.psa_univ_mac_addr[0] == MAC_ADDRESSES[i][0]) &&
        (psa.psa_univ_mac_addr[1] == MAC_ADDRESSES[i][1]) &&
        (psa.psa_univ_mac_addr[2] == MAC_ADDRESSES[i][2]))
      break;

  /* If we have not found it... */
  if (i == ARRAY_SIZE(MAC_ADDRESSES))
    {
#ifdef DEBUG_CONFIG_ERRORS
      printk(KERN_WARNING "%s: wv_mmc_init(): Invalid MAC address: %02X:%02X:%02X:...\n",
	     dev->name, psa.psa_univ_mac_addr[0],
	     psa.psa_univ_mac_addr[1], psa.psa_univ_mac_addr[2]);
#endif
      return FALSE;
    }

  /* Get the MAC address */
  memcpy(&dev->dev_addr[0], &psa.psa_univ_mac_addr[0], WAVELAN_ADDR_SIZE);

#ifdef USE_PSA_CONFIG
  configured = psa.psa_conf_status & 1;
#else
  configured = 0;
#endif

  /* Is the PSA is not configured */
  if(!configured)
    {
      /* User will be able to configure NWID after (with iwconfig) */
      psa.psa_nwid[0] = 0;
      psa.psa_nwid[1] = 0;

      /* As NWID is not set : no NWID checking */
      psa.psa_nwid_select = 0;

      /* Disable encryption */
      psa.psa_encryption_select = 0;

      /* Set to standard values
       * 0x04 for AT,
       * 0x01 for MCA,
       * 0x04 for PCMCIA and 2.00 card (AT&T 407-024689/E document)
       */
      if (psa.psa_comp_number & 1)
	psa.psa_thr_pre_set = 0x01;
      else
	psa.psa_thr_pre_set = 0x04;
      psa.psa_quality_thr = 0x03;

      /* It is configured */
      psa.psa_conf_status |= 1;

#ifdef USE_PSA_CONFIG
      /* Write the psa */
      psa_write(dev, (char *)psa.psa_nwid - (char *)&psa,
		(unsigned char *)psa.psa_nwid, 4);
      psa_write(dev, (char *)&psa.psa_thr_pre_set - (char *)&psa,
		(unsigned char *)&psa.psa_thr_pre_set, 1);
      psa_write(dev, (char *)&psa.psa_quality_thr - (char *)&psa,
		(unsigned char *)&psa.psa_quality_thr, 1);
      psa_write(dev, (char *)&psa.psa_conf_status - (char *)&psa,
		(unsigned char *)&psa.psa_conf_status, 1);
      /* update the Wavelan checksum */
      update_psa_checksum(dev);
#endif	/* USE_PSA_CONFIG */
    }

  /* Zero the mmc structure */
  memset(&m, 0x00, sizeof(m));

  /* Copy PSA info to the mmc */
  m.mmw_netw_id_l = psa.psa_nwid[1];
  m.mmw_netw_id_h = psa.psa_nwid[0];
  
  if(psa.psa_nwid_select & 1)
    m.mmw_loopt_sel = 0x00;
  else
    m.mmw_loopt_sel = MMW_LOOPT_SEL_DIS_NWID;

  memcpy(&m.mmw_encr_key, &psa.psa_encryption_key, 
	 sizeof(m.mmw_encr_key));

  if(psa.psa_encryption_select)
    m.mmw_encr_enable = MMW_ENCR_ENABLE_EN | MMW_ENCR_ENABLE_MODE;
  else
    m.mmw_encr_enable = 0;

  m.mmw_thr_pre_set = psa.psa_thr_pre_set & 0x3F;
  m.mmw_quality_thr = psa.psa_quality_thr & 0x0F;

  /*
   * Set default modem control parameters.
   * See NCR document 407-0024326 Rev. A.
   */
  m.mmw_jabber_enable = 0x01;
  m.mmw_anten_sel = MMW_ANTEN_SEL_ALG_EN;
  m.mmw_ifs = 0x20;
  m.mmw_mod_delay = 0x04;
  m.mmw_jam_time = 0x38;

  m.mmw_des_io_invert = 0;
  m.mmw_freeze = 0;
  m.mmw_decay_prm = 0;
  m.mmw_decay_updat_prm = 0;

  /* Write all info to mmc */
  mmc_write(base, 0, (u_char *)&m, sizeof(m));

  /* The following code start the modem of the 2.00 frequency
   * selectable cards at power on. It's not strictly needed for the
   * following boots...
   * The original patch was by Joe Finney for the PCMCIA driver, but
   * I've cleaned it a bit and add documentation.
   * Thanks to Loeke Brederveld from Lucent for the info.
   */

  /* Attempt to recognise 2.00 cards (2.4 GHz frequency selectable)
   * (does it work for everybody ? - especially old cards...) */
  /* Note : WFREQSEL verify that it is able to read from EEprom
   * a sensible frequency (address 0x00) + that MMR_FEE_STATUS_ID
   * is 0xA (Xilinx version) or 0xB (Ariadne version).
   * My test is more crude but do work... */
  if(!(mmc_in(base, mmroff(0, mmr_fee_status)) &
       (MMR_FEE_STATUS_DWLD | MMR_FEE_STATUS_BUSY)))
    {
      /* We must download the frequency parameters to the
       * synthetisers (from the EEprom - area 1)
       * Note : as the EEprom is auto decremented, we set the end
       * if the area... */
      m.mmw_fee_addr = 0x0F;
      m.mmw_fee_ctrl = MMW_FEE_CTRL_READ | MMW_FEE_CTRL_DWLD;
      mmc_write(base, (char *)&m.mmw_fee_ctrl - (char *)&m,
		(unsigned char *)&m.mmw_fee_ctrl, 2);

      /* Wait until the download is finished */
      fee_wait(base, 100, 100);

#ifdef DEBUG_CONFIG_INFO
      /* The frequency was in the last word downloaded... */
      mmc_read(base, (char *)&m.mmw_fee_data_l - (char *)&m,
	       (unsigned char *)&m.mmw_fee_data_l, 2);

      /* Print some info for the user */
      printk(KERN_DEBUG "%s: Wavelan 2.00 recognised (frequency select) : Current frequency = %ld\n",
	     dev->name,
	     ((m.mmw_fee_data_h << 4) |
	      (m.mmw_fee_data_l >> 4)) * 5 / 2 + 24000L);
#endif

      /* We must now download the power adjust value (gain) to
       * the synthetisers (from the EEprom - area 7 - DAC) */
      m.mmw_fee_addr = 0x61;
      m.mmw_fee_ctrl = MMW_FEE_CTRL_READ | MMW_FEE_CTRL_DWLD;
      mmc_write(base, (char *)&m.mmw_fee_ctrl - (char *)&m,
		(unsigned char *)&m.mmw_fee_ctrl, 2);

      /* Wait until the download is finished */
    }	/* if 2.00 card */

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: <-wv_mmc_init()\n", dev->name);
#endif
  return TRUE;
}

/*------------------------------------------------------------------*/
/*
 * Routine to gracefully turn off reception, and wait for any commands
 * to complete.
 * (called in wv_ru_start() and wavelan_close() and wavelan_event())
 */
static int
wv_ru_stop(struct net_device *	dev)
{
  unsigned int	base = dev->base_addr;
  net_local *	lp = netdev_priv(dev);
  unsigned long	flags;
  int		status;
  int		spin;

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: ->wv_ru_stop()\n", dev->name);
#endif

  spin_lock_irqsave(&lp->spinlock, flags);

  /* First, send the LAN controller a stop receive command */
  wv_82593_cmd(dev, "wv_graceful_shutdown(): stop-rcv",
	       OP0_STOP_RCV, SR0_NO_RESULT);

  /* Then, spin until the receive unit goes idle */
  spin = 300;
  do
    {
      udelay(10);
      outb(OP0_NOP | CR0_STATUS_3, LCCR(base));
      status = inb(LCSR(base));
    }
  while(((status & SR3_RCV_STATE_MASK) != SR3_RCV_IDLE) && (spin-- > 0));

  /* Now, spin until the chip finishes executing its current command */
  do
    {
      udelay(10);
      outb(OP0_NOP | CR0_STATUS_3, LCCR(base));
      status = inb(LCSR(base));
    }
  while(((status & SR3_EXEC_STATE_MASK) != SR3_EXEC_IDLE) && (spin-- > 0));

  spin_unlock_irqrestore(&lp->spinlock, flags);

  /* If there was a problem */
  if(spin <= 0)
    {
#ifdef DEBUG_CONFIG_ERRORS
      printk(KERN_INFO "%s: wv_ru_stop(): The chip doesn't want to stop...\n",
	     dev->name);
#endif
      return FALSE;
    }

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: <-wv_ru_stop()\n", dev->name);
#endif
  return TRUE;
} /* wv_ru_stop */

/*------------------------------------------------------------------*/
/*
 * This routine starts the receive unit running.  First, it checks if
 * the card is actually ready. Then the card is instructed to receive
 * packets again.
 * (called in wv_hw_reset() & wavelan_open())
 */
static int
wv_ru_start(struct net_device *	dev)
{
  unsigned int	base = dev->base_addr;
  net_local *	lp = netdev_priv(dev);
  unsigned long	flags;

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: ->wv_ru_start()\n", dev->name);
#endif

  /*
   * We need to start from a quiescent state. To do so, we could check
   * if the card is already running, but instead we just try to shut
   * it down. First, we disable reception (in case it was already enabled).
   */
  if(!wv_ru_stop(dev))
    return FALSE;

  spin_lock_irqsave(&lp->spinlock, flags);

  /* Now we know that no command is being executed. */

  /* Set the receive frame pointer and stop pointer */
  lp->rfp = 0;
  outb(OP0_SWIT_TO_PORT_1 | CR0_CHNL, LCCR(base));

  /* Reset ring management.  This sets the receive frame pointer to 1 */
  outb(OP1_RESET_RING_MNGMT, LCCR(base));

#if 0
  /* XXX the i82593 manual page 6-4 seems to indicate that the stop register
     should be set as below */
  /* outb(CR1_STOP_REG_UPDATE|((RX_SIZE - 0x40)>> RX_SIZE_SHIFT),LCCR(base));*/
#elif 0
  /* but I set it 0 instead */
  lp->stop = 0;
#else
  /* but I set it to 3 bytes per packet less than 8K */
  lp->stop = (0 + RX_SIZE - ((RX_SIZE / 64) * 3)) % RX_SIZE;
#endif
  outb(CR1_STOP_REG_UPDATE | (lp->stop >> RX_SIZE_SHIFT), LCCR(base));
  outb(OP1_INT_ENABLE, LCCR(base));
  outb(OP1_SWIT_TO_PORT_0, LCCR(base));

  /* Reset receive DMA pointer */
  hacr_write_slow(base, HACR_PWR_STAT | HACR_TX_DMA_RESET);
  hacr_write_slow(base, HACR_DEFAULT);

  /* Receive DMA on channel 1 */
  wv_82593_cmd(dev, "wv_ru_start(): rcv-enable",
	       CR0_CHNL | OP0_RCV_ENABLE, SR0_NO_RESULT);

#ifdef DEBUG_I82593_SHOW
  {
    int	status;
    int	opri;
    int	spin = 10000;

    /* spin until the chip starts receiving */
    do
      {
	outb(OP0_NOP | CR0_STATUS_3, LCCR(base));
	status = inb(LCSR(base));
	if(spin-- <= 0)
	  break;
      }
    while(((status & SR3_RCV_STATE_MASK) != SR3_RCV_ACTIVE) &&
	  ((status & SR3_RCV_STATE_MASK) != SR3_RCV_READY));
    printk(KERN_DEBUG "rcv status is 0x%x [i:%d]\n",
	   (status & SR3_RCV_STATE_MASK), i);
  }
#endif

  spin_unlock_irqrestore(&lp->spinlock, flags);

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: <-wv_ru_start()\n", dev->name);
#endif
  return TRUE;
}

/*------------------------------------------------------------------*/
/*
 * This routine does a standard config of the WaveLAN controller (i82593).
 * In the ISA driver, this is integrated in wavelan_hardware_reset()
 * (called by wv_hw_config(), wv_82593_reconfig() & wavelan_packet_xmit())
 */
static int
wv_82593_config(struct net_device *	dev)
{
  unsigned int			base = dev->base_addr;
  net_local *			lp = netdev_priv(dev);
  struct i82593_conf_block	cfblk;
  int				ret = TRUE;

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: ->wv_82593_config()\n", dev->name);
#endif

  /* Create & fill i82593 config block
   *
   * Now conform to Wavelan document WCIN085B
   */
  memset(&cfblk, 0x00, sizeof(struct i82593_conf_block));
  cfblk.d6mod = FALSE;  	/* Run in i82593 advanced mode */
  cfblk.fifo_limit = 5;         /* = 56 B rx and 40 B tx fifo thresholds */
  cfblk.forgnesi = FALSE;       /* 0=82C501, 1=AMD7992B compatibility */
  cfblk.fifo_32 = 1;
  cfblk.throttle_enb = FALSE;
  cfblk.contin = TRUE;          /* enable continuous mode */
  cfblk.cntrxint = FALSE;       /* enable continuous mode receive interrupts */
  cfblk.addr_len = WAVELAN_ADDR_SIZE;
  cfblk.acloc = TRUE;           /* Disable source addr insertion by i82593 */
  cfblk.preamb_len = 0;         /* 2 bytes preamble (SFD) */
  cfblk.loopback = FALSE;
  cfblk.lin_prio = 0;   	/* conform to 802.3 backoff algorithm */
  cfblk.exp_prio = 5;	        /* conform to 802.3 backoff algorithm */
  cfblk.bof_met = 1;	        /* conform to 802.3 backoff algorithm */
  cfblk.ifrm_spc = 0x20 >> 4;	/* 32 bit times interframe spacing */
  cfblk.slottim_low = 0x20 >> 5;	/* 32 bit times slot time */
  cfblk.slottim_hi = 0x0;
  cfblk.max_retr = 15;
  cfblk.prmisc = ((lp->promiscuous) ? TRUE: FALSE);	/* Promiscuous mode */
  cfblk.bc_dis = FALSE;         /* Enable broadcast reception */
  cfblk.crs_1 = TRUE;		/* Transmit without carrier sense */
  cfblk.nocrc_ins = FALSE;	/* i82593 generates CRC */	
  cfblk.crc_1632 = FALSE;	/* 32-bit Autodin-II CRC */
  cfblk.crs_cdt = FALSE;	/* CD not to be interpreted as CS */
  cfblk.cs_filter = 0;  	/* CS is recognized immediately */
  cfblk.crs_src = FALSE;	/* External carrier sense */
  cfblk.cd_filter = 0;  	/* CD is recognized immediately */
  cfblk.min_fr_len = ETH_ZLEN >> 2;     /* Minimum frame length 64 bytes */
  cfblk.lng_typ = FALSE;	/* Length field > 1500 = type field */
  cfblk.lng_fld = TRUE; 	/* Disable 802.3 length field check */
  cfblk.rxcrc_xf = TRUE;	/* Don't transfer CRC to memory */
  cfblk.artx = TRUE;		/* Disable automatic retransmission */
  cfblk.sarec = TRUE;		/* Disable source addr trig of CD */
  cfblk.tx_jabber = TRUE;	/* Disable jabber jam sequence */
  cfblk.hash_1 = FALSE; 	/* Use bits 0-5 in mc address hash */
  cfblk.lbpkpol = TRUE; 	/* Loopback pin active high */
  cfblk.fdx = FALSE;		/* Disable full duplex operation */
  cfblk.dummy_6 = 0x3f; 	/* all ones */
  cfblk.mult_ia = FALSE;	/* No multiple individual addresses */
  cfblk.dis_bof = FALSE;	/* Disable the backoff algorithm ?! */
  cfblk.dummy_1 = TRUE; 	/* set to 1 */
  cfblk.tx_ifs_retrig = 3;	/* Hmm... Disabled */
#ifdef MULTICAST_ALL
  cfblk.mc_all = (lp->allmulticast ? TRUE: FALSE);	/* Allow all multicasts */
#else
  cfblk.mc_all = FALSE;		/* No multicast all mode */
#endif
  cfblk.rcv_mon = 0;		/* Monitor mode disabled */
  cfblk.frag_acpt = TRUE;	/* Do not accept fragments */
  cfblk.tstrttrs = FALSE;	/* No start transmission threshold */
  cfblk.fretx = TRUE;		/* FIFO automatic retransmission */
  cfblk.syncrqs = FALSE; 	/* Synchronous DRQ deassertion... */
  cfblk.sttlen = TRUE;  	/* 6 byte status registers */
  cfblk.rx_eop = TRUE;  	/* Signal EOP on packet reception */
  cfblk.tx_eop = TRUE;  	/* Signal EOP on packet transmission */
  cfblk.rbuf_size = RX_SIZE>>11;	/* Set receive buffer size */
  cfblk.rcvstop = TRUE; 	/* Enable Receive Stop Register */

#ifdef DEBUG_I82593_SHOW
  {
    u_char *c = (u_char *) &cfblk;
    int i;
    printk(KERN_DEBUG "wavelan_cs: config block:");
    for(i = 0; i < sizeof(struct i82593_conf_block); i++,c++)
      {
	if((i % 16) == 0) printk("\n" KERN_DEBUG);
	printk("%02x ", *c);
      }
    printk("\n");
  }
#endif

  /* Copy the config block to the i82593 */
  outb(TX_BASE & 0xff, PIORL(base));
  outb(((TX_BASE >> 8) & PIORH_MASK) | PIORH_SEL_TX, PIORH(base));
  outb(sizeof(struct i82593_conf_block) & 0xff, PIOP(base));    /* lsb */
  outb(sizeof(struct i82593_conf_block) >> 8, PIOP(base));	/* msb */
  outsb(PIOP(base), (char *) &cfblk, sizeof(struct i82593_conf_block));

  /* reset transmit DMA pointer */
  hacr_write_slow(base, HACR_PWR_STAT | HACR_TX_DMA_RESET);
  hacr_write(base, HACR_DEFAULT);
  if(!wv_82593_cmd(dev, "wv_82593_config(): configure",
		   OP0_CONFIGURE, SR0_CONFIGURE_DONE))
    ret = FALSE;

  /* Initialize adapter's ethernet MAC address */
  outb(TX_BASE & 0xff, PIORL(base));
  outb(((TX_BASE >> 8) & PIORH_MASK) | PIORH_SEL_TX, PIORH(base));
  outb(WAVELAN_ADDR_SIZE, PIOP(base));	/* byte count lsb */
  outb(0, PIOP(base));			/* byte count msb */
  outsb(PIOP(base), &dev->dev_addr[0], WAVELAN_ADDR_SIZE);

  /* reset transmit DMA pointer */
  hacr_write_slow(base, HACR_PWR_STAT | HACR_TX_DMA_RESET);
  hacr_write(base, HACR_DEFAULT);
  if(!wv_82593_cmd(dev, "wv_82593_config(): ia-setup",
		   OP0_IA_SETUP, SR0_IA_SETUP_DONE))
    ret = FALSE;

#ifdef WAVELAN_ROAMING
    /* If roaming is enabled, join the "Beacon Request" multicast group... */
    /* But only if it's not in there already! */
  if(do_roaming)
    dev_mc_add(dev,WAVELAN_BEACON_ADDRESS, WAVELAN_ADDR_SIZE, 1);
#endif	/* WAVELAN_ROAMING */

  /* If any multicast address to set */
  if(lp->mc_count)
    {
      struct dev_mc_list *	dmi;
      int			addrs_len = WAVELAN_ADDR_SIZE * lp->mc_count;

#ifdef DEBUG_CONFIG_INFO
      DECLARE_MAC_BUF(mac);
      printk(KERN_DEBUG "%s: wv_hw_config(): set %d multicast addresses:\n",
	     dev->name, lp->mc_count);
      for(dmi=dev->mc_list; dmi; dmi=dmi->next)
	printk(KERN_DEBUG " %s\n",
	       print_mac(mac, dmi->dmi_addr));
#endif

      /* Initialize adapter's ethernet multicast addresses */
      outb(TX_BASE & 0xff, PIORL(base));
      outb(((TX_BASE >> 8) & PIORH_MASK) | PIORH_SEL_TX, PIORH(base));
      outb(addrs_len & 0xff, PIOP(base));	/* byte count lsb */
      outb((addrs_len >> 8), PIOP(base));	/* byte count msb */
      for(dmi=dev->mc_list; dmi; dmi=dmi->next)
	outsb(PIOP(base), dmi->dmi_addr, dmi->dmi_addrlen);

      /* reset transmit DMA pointer */
      hacr_write_slow(base, HACR_PWR_STAT | HACR_TX_DMA_RESET);
      hacr_write(base, HACR_DEFAULT);
      if(!wv_82593_cmd(dev, "wv_82593_config(): mc-setup",
		       OP0_MC_SETUP, SR0_MC_SETUP_DONE))
	ret = FALSE;
      lp->mc_count = dev->mc_count;	/* remember to avoid repeated reset */
    }

  /* Job done, clear the flag */
  lp->reconfig_82593 = FALSE;

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: <-wv_82593_config()\n", dev->name);
#endif
  return(ret);
}

/*------------------------------------------------------------------*/
/*
 * Read the Access Configuration Register, perform a software reset,
 * and then re-enable the card's software.
 *
 * If I understand correctly : reset the pcmcia interface of the
 * wavelan.
 * (called by wv_config())
 */
static int
wv_pcmcia_reset(struct net_device *	dev)
{
  int		i;
  conf_reg_t	reg = { 0, CS_READ, CISREG_COR, 0 };
  struct pcmcia_device *	link = ((net_local *)netdev_priv(dev))->link;

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: ->wv_pcmcia_reset()\n", dev->name);
#endif

  i = pcmcia_access_configuration_register(link, &reg);
  if(i != CS_SUCCESS)
    {
      cs_error(link, AccessConfigurationRegister, i);
      return FALSE;
    }
      
#ifdef DEBUG_CONFIG_INFO
  printk(KERN_DEBUG "%s: wavelan_pcmcia_reset(): Config reg is 0x%x\n",
	 dev->name, (u_int) reg.Value);
#endif

  reg.Action = CS_WRITE;
  reg.Value = reg.Value | COR_SW_RESET;
  i = pcmcia_access_configuration_register(link, &reg);
  if(i != CS_SUCCESS)
    {
      cs_error(link, AccessConfigurationRegister, i);
      return FALSE;
    }
      
  reg.Action = CS_WRITE;
  reg.Value = COR_LEVEL_IRQ | COR_CONFIG;
  i = pcmcia_access_configuration_register(link, &reg);
  if(i != CS_SUCCESS)
    {
      cs_error(link, AccessConfigurationRegister, i);
      return FALSE;
    }

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: <-wv_pcmcia_reset()\n", dev->name);
#endif
  return TRUE;
}

/*------------------------------------------------------------------*/
/*
 * wavelan_hw_config() is called after a CARD_INSERTION event is
 * received, to configure the wavelan hardware.
 * Note that the reception will be enabled in wavelan->open(), so the
 * device is configured but idle...
 * Performs the following actions:
 * 	1. A pcmcia software reset (using wv_pcmcia_reset())
 *	2. A power reset (reset DMA)
 *	3. Reset the LAN controller
 *	4. Initialize the radio modem (using wv_mmc_init)
 *	5. Configure LAN controller (using wv_82593_config)
 *	6. Perform a diagnostic on the LAN controller
 * (called by wavelan_event() & wv_hw_reset())
 */
static int
wv_hw_config(struct net_device *	dev)
{
  net_local *		lp = netdev_priv(dev);
  unsigned int		base = dev->base_addr;
  unsigned long		flags;
  int			ret = FALSE;

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: ->wv_hw_config()\n", dev->name);
#endif

  /* compile-time check the sizes of structures */
  BUILD_BUG_ON(sizeof(psa_t) != PSA_SIZE);
  BUILD_BUG_ON(sizeof(mmw_t) != MMW_SIZE);
  BUILD_BUG_ON(sizeof(mmr_t) != MMR_SIZE);

  /* Reset the pcmcia interface */
  if(wv_pcmcia_reset(dev) == FALSE)
    return FALSE;

  /* Disable interrupts */
  spin_lock_irqsave(&lp->spinlock, flags);

  /* Disguised goto ;-) */
  do
    {
      /* Power UP the module + reset the modem + reset host adapter
       * (in fact, reset DMA channels) */
      hacr_write_slow(base, HACR_RESET);
      hacr_write(base, HACR_DEFAULT);

      /* Check if the module has been powered up... */
      if(hasr_read(base) & HASR_NO_CLK)
	{
#ifdef DEBUG_CONFIG_ERRORS
	  printk(KERN_WARNING "%s: wv_hw_config(): modem not connected or not a wavelan card\n",
		 dev->name);
#endif
	  break;
	}

      /* initialize the modem */
      if(wv_mmc_init(dev) == FALSE)
	{
#ifdef DEBUG_CONFIG_ERRORS
	  printk(KERN_WARNING "%s: wv_hw_config(): Can't configure the modem\n",
		 dev->name);
#endif
	  break;
	}

      /* reset the LAN controller (i82593) */
      outb(OP0_RESET, LCCR(base));
      mdelay(1);	/* A bit crude ! */

      /* Initialize the LAN controller */
      if(wv_82593_config(dev) == FALSE)
	{
#ifdef DEBUG_CONFIG_ERRORS
	  printk(KERN_INFO "%s: wv_hw_config(): i82593 init failed\n",
		 dev->name);
#endif
	  break;
	}

      /* Diagnostic */
      if(wv_diag(dev) == FALSE)
	{
#ifdef DEBUG_CONFIG_ERRORS
	  printk(KERN_INFO "%s: wv_hw_config(): i82593 diagnostic failed\n",
		 dev->name);
#endif
	  break;
	}

      /* 
       * insert code for loopback test here
       */

      /* The device is now configured */
      lp->configured = 1;
      ret = TRUE;
    }
  while(0);

  /* Re-enable interrupts */
  spin_unlock_irqrestore(&lp->spinlock, flags);

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: <-wv_hw_config()\n", dev->name);
#endif
  return(ret);
}

/*------------------------------------------------------------------*/
/*
 * Totally reset the wavelan and restart it.
 * Performs the following actions:
 * 	1. Call wv_hw_config()
 *	2. Start the LAN controller's receive unit
 * (called by wavelan_event(), wavelan_watchdog() and wavelan_open())
 */
static void
wv_hw_reset(struct net_device *	dev)
{
  net_local *	lp = netdev_priv(dev);

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: ->wv_hw_reset()\n", dev->name);
#endif

  lp->nresets++;
  lp->configured = 0;
  
  /* Call wv_hw_config() for most of the reset & init stuff */
  if(wv_hw_config(dev) == FALSE)
    return;

  /* start receive unit */
  wv_ru_start(dev);

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: <-wv_hw_reset()\n", dev->name);
#endif
}

/*------------------------------------------------------------------*/
/*
 * wv_pcmcia_config() is called after a CARD_INSERTION event is
 * received, to configure the PCMCIA socket, and to make the ethernet
 * device available to the system.
 * (called by wavelan_event())
 */
static int
wv_pcmcia_config(struct pcmcia_device *	link)
{
  struct net_device *	dev = (struct net_device *) link->priv;
  int			i;
  win_req_t		req;
  memreq_t		mem;
  net_local *		lp = netdev_priv(dev);


#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "->wv_pcmcia_config(0x%p)\n", link);
#endif

  do
    {
      i = pcmcia_request_io(link, &link->io);
      if(i != CS_SUCCESS)
	{
	  cs_error(link, RequestIO, i);
	  break;
	}

      /*
       * Now allocate an interrupt line.  Note that this does not
       * actually assign a handler to the interrupt.
       */
      i = pcmcia_request_irq(link, &link->irq);
      if(i != CS_SUCCESS)
	{
	  cs_error(link, RequestIRQ, i);
	  break;
	}

      /*
       * This actually configures the PCMCIA socket -- setting up
       * the I/O windows and the interrupt mapping.
       */
      link->conf.ConfigIndex = 1;
      i = pcmcia_request_configuration(link, &link->conf);
      if(i != CS_SUCCESS)
	{
	  cs_error(link, RequestConfiguration, i);
	  break;
	}

      /*
       * Allocate a small memory window.  Note that the struct pcmcia_device
       * structure provides space for one window handle -- if your
       * device needs several windows, you'll need to keep track of
       * the handles in your private data structure, link->priv.
       */
      req.Attributes = WIN_DATA_WIDTH_8|WIN_MEMORY_TYPE_AM|WIN_ENABLE;
      req.Base = req.Size = 0;
      req.AccessSpeed = mem_speed;
      i = pcmcia_request_window(&link, &req, &link->win);
      if(i != CS_SUCCESS)
	{
	  cs_error(link, RequestWindow, i);
	  break;
	}

      lp->mem = ioremap(req.Base, req.Size);
      dev->mem_start = (u_long)lp->mem;
      dev->mem_end = dev->mem_start + req.Size;

      mem.CardOffset = 0; mem.Page = 0;
      i = pcmcia_map_mem_page(link->win, &mem);
      if(i != CS_SUCCESS)
	{
	  cs_error(link, MapMemPage, i);
	  break;
	}

      /* Feed device with this info... */
      dev->irq = link->irq.AssignedIRQ;
      dev->base_addr = link->io.BasePort1;
      netif_start_queue(dev);

#ifdef DEBUG_CONFIG_INFO
      printk(KERN_DEBUG "wv_pcmcia_config: MEMSTART %p IRQ %d IOPORT 0x%x\n",
	     lp->mem, dev->irq, (u_int) dev->base_addr);
#endif

      SET_NETDEV_DEV(dev, &handle_to_dev(link));
      i = register_netdev(dev);
      if(i != 0)
	{
#ifdef DEBUG_CONFIG_ERRORS
	  printk(KERN_INFO "wv_pcmcia_config(): register_netdev() failed\n");
#endif
	  break;
	}
    }
  while(0);		/* Humm... Disguised goto !!! */

  /* If any step failed, release any partially configured state */
  if(i != 0)
    {
      wv_pcmcia_release(link);
      return FALSE;
    }

  strcpy(((net_local *) netdev_priv(dev))->node.dev_name, dev->name);
  link->dev_node = &((net_local *) netdev_priv(dev))->node;

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "<-wv_pcmcia_config()\n");
#endif
  return TRUE;
}

/*------------------------------------------------------------------*/
/*
 * After a card is removed, wv_pcmcia_release() will unregister the net
 * device, and release the PCMCIA configuration.  If the device is
 * still open, this will be postponed until it is closed.
 */
static void
wv_pcmcia_release(struct pcmcia_device *link)
{
	struct net_device *	dev = (struct net_device *) link->priv;
	net_local *		lp = netdev_priv(dev);

#ifdef DEBUG_CONFIG_TRACE
	printk(KERN_DEBUG "%s: -> wv_pcmcia_release(0x%p)\n", dev->name, link);
#endif

	iounmap(lp->mem);
	pcmcia_disable_device(link);

#ifdef DEBUG_CONFIG_TRACE
	printk(KERN_DEBUG "%s: <- wv_pcmcia_release()\n", dev->name);
#endif
}

/************************ INTERRUPT HANDLING ************************/

/*
 * This function is the interrupt handler for the WaveLAN card. This
 * routine will be called whenever: 
 *	1. A packet is received.
 *	2. A packet has successfully been transferred and the unit is
 *	   ready to transmit another packet.
 *	3. A command has completed execution.
 */
static irqreturn_t
wavelan_interrupt(int		irq,
		  void *	dev_id)
{
  struct net_device *	dev = dev_id;
  net_local *	lp;
  unsigned int	base;
  int		status0;
  u_int		tx_status;

#ifdef DEBUG_INTERRUPT_TRACE
  printk(KERN_DEBUG "%s: ->wavelan_interrupt()\n", dev->name);
#endif

  lp = netdev_priv(dev);
  base = dev->base_addr;

#ifdef DEBUG_INTERRUPT_INFO
  /* Check state of our spinlock (it should be cleared) */
  if(spin_is_locked(&lp->spinlock))
    printk(KERN_DEBUG
	   "%s: wavelan_interrupt(): spinlock is already locked !!!\n",
	   dev->name);
#endif

  /* Prevent reentrancy. We need to do that because we may have
   * multiple interrupt handler running concurently.
   * It is safe because interrupts are disabled before aquiring
   * the spinlock. */
  spin_lock(&lp->spinlock);

  /* Treat all pending interrupts */
  while(1)
    {
      /* ---------------- INTERRUPT CHECKING ---------------- */
      /*
       * Look for the interrupt and verify the validity
       */
      outb(CR0_STATUS_0 | OP0_NOP, LCCR(base));
      status0 = inb(LCSR(base));

#ifdef DEBUG_INTERRUPT_INFO
      printk(KERN_DEBUG "status0 0x%x [%s => 0x%x]", status0, 
	     (status0&SR0_INTERRUPT)?"int":"no int",status0&~SR0_INTERRUPT);
      if(status0&SR0_INTERRUPT)
	{
	  printk(" [%s => %d]\n", (status0 & SR0_CHNL) ? "chnl" :
		 ((status0 & SR0_EXECUTION) ? "cmd" :
		  ((status0 & SR0_RECEPTION) ? "recv" : "unknown")),
		 (status0 & SR0_EVENT_MASK));
	}
      else
	printk("\n");
#endif

      /* Return if no actual interrupt from i82593 (normal exit) */
      if(!(status0 & SR0_INTERRUPT))
	break;

      /* If interrupt is both Rx and Tx or none...
       * This code in fact is there to catch the spurious interrupt
       * when you remove the wavelan pcmcia card from the socket */
      if(((status0 & SR0_BOTH_RX_TX) == SR0_BOTH_RX_TX) ||
	 ((status0 & SR0_BOTH_RX_TX) == 0x0))
	{
#ifdef DEBUG_INTERRUPT_INFO
	  printk(KERN_INFO "%s: wv_interrupt(): bogus interrupt (or from dead card) : %X\n",
		 dev->name, status0);
#endif
	  /* Acknowledge the interrupt */
	  outb(CR0_INT_ACK | OP0_NOP, LCCR(base));
	  break;
	}

      /* ----------------- RECEIVING PACKET ----------------- */
      /*
       * When the wavelan signal the reception of a new packet,
       * we call wv_packet_rcv() to copy if from the buffer and
       * send it to NET3
       */
      if(status0 & SR0_RECEPTION)
	{
#ifdef DEBUG_INTERRUPT_INFO
	  printk(KERN_DEBUG "%s: wv_interrupt(): receive\n", dev->name);
#endif

	  if((status0 & SR0_EVENT_MASK) == SR0_STOP_REG_HIT)
	    {
#ifdef DEBUG_INTERRUPT_ERROR
	      printk(KERN_INFO "%s: wv_interrupt(): receive buffer overflow\n",
		     dev->name);
#endif
	      lp->stats.rx_over_errors++;
	      lp->overrunning = 1;
      	    }

	  /* Get the packet */
	  wv_packet_rcv(dev);
	  lp->overrunning = 0;

	  /* Acknowledge the interrupt */
	  outb(CR0_INT_ACK | OP0_NOP, LCCR(base));
	  continue;
    	}

      /* ---------------- COMMAND COMPLETION ---------------- */
      /*
       * Interrupts issued when the i82593 has completed a command.
       * Most likely : transmission done
       */

      /* If a transmission has been done */
      if((status0 & SR0_EVENT_MASK) == SR0_TRANSMIT_DONE ||
	 (status0 & SR0_EVENT_MASK) == SR0_RETRANSMIT_DONE ||
	 (status0 & SR0_EVENT_MASK) == SR0_TRANSMIT_NO_CRC_DONE)
	{
#ifdef DEBUG_TX_ERROR
	  if((status0 & SR0_EVENT_MASK) == SR0_TRANSMIT_NO_CRC_DONE)
	    printk(KERN_INFO "%s: wv_interrupt(): packet transmitted without CRC.\n",
		   dev->name);
#endif

	  /* Get transmission status */
	  tx_status = inb(LCSR(base));
	  tx_status |= (inb(LCSR(base)) << 8);
#ifdef DEBUG_INTERRUPT_INFO
	  printk(KERN_DEBUG "%s: wv_interrupt(): transmission done\n",
		 dev->name);
	  {
	    u_int	rcv_bytes;
	    u_char	status3;
	    rcv_bytes = inb(LCSR(base));
	    rcv_bytes |= (inb(LCSR(base)) << 8);
	    status3 = inb(LCSR(base));
	    printk(KERN_DEBUG "tx_status 0x%02x rcv_bytes 0x%02x status3 0x%x\n",
		   tx_status, rcv_bytes, (u_int) status3);
	  }
#endif
	  /* Check for possible errors */
	  if((tx_status & TX_OK) != TX_OK)
	    {
	      lp->stats.tx_errors++;

	      if(tx_status & TX_FRTL)
		{
#ifdef DEBUG_TX_ERROR
		  printk(KERN_INFO "%s: wv_interrupt(): frame too long\n",
			 dev->name);
#endif
		}
	      if(tx_status & TX_UND_RUN)
		{
#ifdef DEBUG_TX_FAIL
		  printk(KERN_DEBUG "%s: wv_interrupt(): DMA underrun\n",
			 dev->name);
#endif
		  lp->stats.tx_aborted_errors++;
		}
	      if(tx_status & TX_LOST_CTS)
		{
#ifdef DEBUG_TX_FAIL
		  printk(KERN_DEBUG "%s: wv_interrupt(): no CTS\n", dev->name);
#endif
		  lp->stats.tx_carrier_errors++;
		}
	      if(tx_status & TX_LOST_CRS)
		{
#ifdef DEBUG_TX_FAIL
		  printk(KERN_DEBUG "%s: wv_interrupt(): no carrier\n",
			 dev->name);
#endif
		  lp->stats.tx_carrier_errors++;
		}
	      if(tx_status & TX_HRT_BEAT)
		{
#ifdef DEBUG_TX_FAIL
		  printk(KERN_DEBUG "%s: wv_interrupt(): heart beat\n", dev->name);
#endif
		  lp->stats.tx_heartbeat_errors++;
		}
	      if(tx_status & TX_DEFER)
		{
#ifdef DEBUG_TX_FAIL
		  printk(KERN_DEBUG "%s: wv_interrupt(): channel jammed\n",
			 dev->name);
#endif
		}
	      /* Ignore late collisions since they're more likely to happen
	       * here (the WaveLAN design prevents the LAN controller from
	       * receiving while it is transmitting). We take action only when
	       * the maximum retransmit attempts is exceeded.
	       */
	      if(tx_status & TX_COLL)
		{
		  if(tx_status & TX_MAX_COL)
		    {
#ifdef DEBUG_TX_FAIL
		      printk(KERN_DEBUG "%s: wv_interrupt(): channel congestion\n",
			     dev->name);
#endif
		      if(!(tx_status & TX_NCOL_MASK))
			{
			  lp->stats.collisions += 0x10;
			}
		    }
		}
	    }	/* if(!(tx_status & TX_OK)) */

	  lp->stats.collisions += (tx_status & TX_NCOL_MASK);
	  lp->stats.tx_packets++;

	  netif_wake_queue(dev);
	  outb(CR0_INT_ACK | OP0_NOP, LCCR(base));	/* Acknowledge the interrupt */
    	} 
      else	/* if interrupt = transmit done or retransmit done */
	{
#ifdef DEBUG_INTERRUPT_ERROR
	  printk(KERN_INFO "wavelan_cs: unknown interrupt, status0 = %02x\n",
		 status0);
#endif
	  outb(CR0_INT_ACK | OP0_NOP, LCCR(base));	/* Acknowledge the interrupt */
    	}
    }	/* while(1) */

  spin_unlock(&lp->spinlock);

#ifdef DEBUG_INTERRUPT_TRACE
  printk(KERN_DEBUG "%s: <-wavelan_interrupt()\n", dev->name);
#endif

  /* We always return IRQ_HANDLED, because we will receive empty
   * interrupts under normal operations. Anyway, it doesn't matter
   * as we are dealing with an ISA interrupt that can't be shared.
   *
   * Explanation : under heavy receive, the following happens :
   * ->wavelan_interrupt()
   *    (status0 & SR0_INTERRUPT) != 0
   *       ->wv_packet_rcv()
   *    (status0 & SR0_INTERRUPT) != 0
   *       ->wv_packet_rcv()
   *    (status0 & SR0_INTERRUPT) == 0	// i.e. no more event
   * <-wavelan_interrupt()
   * ->wavelan_interrupt()
   *    (status0 & SR0_INTERRUPT) == 0	// i.e. empty interrupt
   * <-wavelan_interrupt()
   * Jean II */
  return IRQ_HANDLED;
} /* wv_interrupt */

/*------------------------------------------------------------------*/
/*
 * Watchdog: when we start a transmission, a timer is set for us in the
 * kernel.  If the transmission completes, this timer is disabled. If
 * the timer expires, we are called and we try to unlock the hardware.
 *
 * Note : This watchdog is move clever than the one in the ISA driver,
 * because it try to abort the current command before reseting
 * everything...
 * On the other hand, it's a bit simpler, because we don't have to
 * deal with the multiple Tx buffers...
 */
static void
wavelan_watchdog(struct net_device *	dev)
{
  net_local *		lp = netdev_priv(dev);
  unsigned int		base = dev->base_addr;
  unsigned long		flags;
  int			aborted = FALSE;

#ifdef DEBUG_INTERRUPT_TRACE
  printk(KERN_DEBUG "%s: ->wavelan_watchdog()\n", dev->name);
#endif

#ifdef DEBUG_INTERRUPT_ERROR
  printk(KERN_INFO "%s: wavelan_watchdog: watchdog timer expired\n",
	 dev->name);
#endif

  spin_lock_irqsave(&lp->spinlock, flags);

  /* Ask to abort the current command */
  outb(OP0_ABORT, LCCR(base));

  /* Wait for the end of the command (a bit hackish) */
  if(wv_82593_cmd(dev, "wavelan_watchdog(): abort",
		  OP0_NOP | CR0_STATUS_3, SR0_EXECUTION_ABORTED))
    aborted = TRUE;

  /* Release spinlock here so that wv_hw_reset() can grab it */
  spin_unlock_irqrestore(&lp->spinlock, flags);

  /* Check if we were successful in aborting it */
  if(!aborted)
    {
      /* It seem that it wasn't enough */
#ifdef DEBUG_INTERRUPT_ERROR
      printk(KERN_INFO "%s: wavelan_watchdog: abort failed, trying reset\n",
	     dev->name);
#endif
      wv_hw_reset(dev);
    }

#ifdef DEBUG_PSA_SHOW
  {
    psa_t		psa;
    psa_read(dev, 0, (unsigned char *) &psa, sizeof(psa));
    wv_psa_show(&psa);
  }
#endif
#ifdef DEBUG_MMC_SHOW
  wv_mmc_show(dev);
#endif
#ifdef DEBUG_I82593_SHOW
  wv_ru_show(dev);
#endif

  /* We are no more waiting for something... */
  netif_wake_queue(dev);

#ifdef DEBUG_INTERRUPT_TRACE
  printk(KERN_DEBUG "%s: <-wavelan_watchdog()\n", dev->name);
#endif
}

/********************* CONFIGURATION CALLBACKS *********************/
/*
 * Here are the functions called by the pcmcia package (cardmgr) and
 * linux networking (NET3) for initialization, configuration and
 * deinstallations of the Wavelan Pcmcia Hardware.
 */

/*------------------------------------------------------------------*/
/*
 * Configure and start up the WaveLAN PCMCIA adaptor.
 * Called by NET3 when it "open" the device.
 */
static int
wavelan_open(struct net_device *	dev)
{
  net_local *	lp = netdev_priv(dev);
  struct pcmcia_device *	link = lp->link;
  unsigned int	base = dev->base_addr;

#ifdef DEBUG_CALLBACK_TRACE
  printk(KERN_DEBUG "%s: ->wavelan_open(dev=0x%x)\n", dev->name,
	 (unsigned int) dev);
#endif

  /* Check if the modem is powered up (wavelan_close() power it down */
  if(hasr_read(base) & HASR_NO_CLK)
    {
      /* Power up (power up time is 250us) */
      hacr_write(base, HACR_DEFAULT);

      /* Check if the module has been powered up... */
      if(hasr_read(base) & HASR_NO_CLK)
	{
#ifdef DEBUG_CONFIG_ERRORS
	  printk(KERN_WARNING "%s: wavelan_open(): modem not connected\n",
		 dev->name);
#endif
	  return FALSE;
	}
    }

  /* Start reception and declare the driver ready */
  if(!lp->configured)
    return FALSE;
  if(!wv_ru_start(dev))
    wv_hw_reset(dev);		/* If problem : reset */
  netif_start_queue(dev);

  /* Mark the device as used */
  link->open++;

#ifdef WAVELAN_ROAMING
  if(do_roaming)
    wv_roam_init(dev);
#endif	/* WAVELAN_ROAMING */

#ifdef DEBUG_CALLBACK_TRACE
  printk(KERN_DEBUG "%s: <-wavelan_open()\n", dev->name);
#endif
  return 0;
}

/*------------------------------------------------------------------*/
/*
 * Shutdown the WaveLAN PCMCIA adaptor.
 * Called by NET3 when it "close" the device.
 */
static int
wavelan_close(struct net_device *	dev)
{
  struct pcmcia_device *	link = ((net_local *)netdev_priv(dev))->link;
  unsigned int	base = dev->base_addr;

#ifdef DEBUG_CALLBACK_TRACE
  printk(KERN_DEBUG "%s: ->wavelan_close(dev=0x%x)\n", dev->name,
	 (unsigned int) dev);
#endif

  /* If the device isn't open, then nothing to do */
  if(!link->open)
    {
#ifdef DEBUG_CONFIG_INFO
      printk(KERN_DEBUG "%s: wavelan_close(): device not open\n", dev->name);
#endif
      return 0;
    }

#ifdef WAVELAN_ROAMING
  /* Cleanup of roaming stuff... */
  if(do_roaming)
    wv_roam_cleanup(dev);
#endif	/* WAVELAN_ROAMING */

  link->open--;

  /* If the card is still present */
  if(netif_running(dev))
    {
      netif_stop_queue(dev);

      /* Stop receiving new messages and wait end of transmission */
      wv_ru_stop(dev);

      /* Power down the module */
      hacr_write(base, HACR_DEFAULT & (~HACR_PWR_STAT));
    }

#ifdef DEBUG_CALLBACK_TRACE
  printk(KERN_DEBUG "%s: <-wavelan_close()\n", dev->name);
#endif
  return 0;
}

/*------------------------------------------------------------------*/
/*
 * wavelan_attach() creates an "instance" of the driver, allocating
 * local data structures for one device (one interface).  The device
 * is registered with Card Services.
 *
 * The dev_link structure is initialized, but we don't actually
 * configure the card at this point -- we wait until we receive a
 * card insertion event.
 */
static int
wavelan_probe(struct pcmcia_device *p_dev)
{
  struct net_device *	dev;		/* Interface generic data */
  net_local *	lp;		/* Interface specific data */
  int ret;

#ifdef DEBUG_CALLBACK_TRACE
  printk(KERN_DEBUG "-> wavelan_attach()\n");
#endif

  /* The io structure describes IO port mapping */
  p_dev->io.NumPorts1 = 8;
  p_dev->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
  p_dev->io.IOAddrLines = 3;

  /* Interrupt setup */
  p_dev->irq.Attributes = IRQ_TYPE_EXCLUSIVE | IRQ_HANDLE_PRESENT;
  p_dev->irq.IRQInfo1 = IRQ_LEVEL_ID;
  p_dev->irq.Handler = wavelan_interrupt;

  /* General socket configuration */
  p_dev->conf.Attributes = CONF_ENABLE_IRQ;
  p_dev->conf.IntType = INT_MEMORY_AND_IO;

  /* Allocate the generic data structure */
  dev = alloc_etherdev(sizeof(net_local));
  if (!dev)
      return -ENOMEM;

  p_dev->priv = p_dev->irq.Instance = dev;

  lp = netdev_priv(dev);

  /* Init specific data */
  lp->configured = 0;
  lp->reconfig_82593 = FALSE;
  lp->nresets = 0;
  /* Multicast stuff */
  lp->promiscuous = 0;
  lp->allmulticast = 0;
  lp->mc_count = 0;

  /* Init spinlock */
  spin_lock_init(&lp->spinlock);

  /* back links */
  lp->dev = dev;

  /* wavelan NET3 callbacks */
  dev->open = &wavelan_open;
  dev->stop = &wavelan_close;
  dev->hard_start_xmit = &wavelan_packet_xmit;
  dev->get_stats = &wavelan_get_stats;
  dev->set_multicast_list = &wavelan_set_multicast_list;
#ifdef SET_MAC_ADDRESS
  dev->set_mac_address = &wavelan_set_mac_address;
#endif	/* SET_MAC_ADDRESS */

  /* Set the watchdog timer */
  dev->tx_timeout	= &wavelan_watchdog;
  dev->watchdog_timeo	= WATCHDOG_JIFFIES;
  SET_ETHTOOL_OPS(dev, &ops);

  dev->wireless_handlers = &wavelan_handler_def;
  lp->wireless_data.spy_data = &lp->spy_data;
  dev->wireless_data = &lp->wireless_data;

  /* Other specific data */
  dev->mtu = WAVELAN_MTU;

  ret = wv_pcmcia_config(p_dev);
  if (ret)
	  return ret;

  ret = wv_hw_config(dev);
  if (ret) {
	  dev->irq = 0;
	  pcmcia_disable_device(p_dev);
	  return ret;
  }

  wv_init_info(dev);

#ifdef DEBUG_CALLBACK_TRACE
  printk(KERN_DEBUG "<- wavelan_attach()\n");
#endif

  return 0;
}

/*------------------------------------------------------------------*/
/*
 * This deletes a driver "instance".  The device is de-registered with
 * Card Services.  If it has been released, all local data structures
 * are freed.  Otherwise, the structures will be freed when the device
 * is released.
 */
static void
wavelan_detach(struct pcmcia_device *link)
{
#ifdef DEBUG_CALLBACK_TRACE
  printk(KERN_DEBUG "-> wavelan_detach(0x%p)\n", link);
#endif

  /* Some others haven't done their job : give them another chance */
  wv_pcmcia_release(link);

  /* Free pieces */
  if(link->priv)
    {
      struct net_device *	dev = (struct net_device *) link->priv;

      /* Remove ourselves from the kernel list of ethernet devices */
      /* Warning : can't be called from interrupt, timer or wavelan_close() */
      if (link->dev_node)
	unregister_netdev(dev);
      link->dev_node = NULL;
      ((net_local *)netdev_priv(dev))->link = NULL;
      ((net_local *)netdev_priv(dev))->dev = NULL;
      free_netdev(dev);
    }

#ifdef DEBUG_CALLBACK_TRACE
  printk(KERN_DEBUG "<- wavelan_detach()\n");
#endif
}

static int wavelan_suspend(struct pcmcia_device *link)
{
	struct net_device *	dev = (struct net_device *) link->priv;

	/* NB: wavelan_close will be called, but too late, so we are
	 * obliged to close nicely the wavelan here. David, could you
	 * close the device before suspending them ? And, by the way,
	 * could you, on resume, add a "route add -net ..." after the
	 * ifconfig up ? Thanks... */

	/* Stop receiving new messages and wait end of transmission */
	wv_ru_stop(dev);

	if (link->open)
		netif_device_detach(dev);

	/* Power down the module */
	hacr_write(dev->base_addr, HACR_DEFAULT & (~HACR_PWR_STAT));

	return 0;
}

static int wavelan_resume(struct pcmcia_device *link)
{
	struct net_device *	dev = (struct net_device *) link->priv;

	if (link->open) {
		wv_hw_reset(dev);
		netif_device_attach(dev);
	}

	return 0;
}


static struct pcmcia_device_id wavelan_ids[] = {
	PCMCIA_DEVICE_PROD_ID12("AT&T","WaveLAN/PCMCIA", 0xe7c5affd, 0x1bc50975),
	PCMCIA_DEVICE_PROD_ID12("Digital", "RoamAbout/DS", 0x9999ab35, 0x00d05e06),
	PCMCIA_DEVICE_PROD_ID12("Lucent Technologies", "WaveLAN/PCMCIA", 0x23eb9949, 0x1bc50975),
	PCMCIA_DEVICE_PROD_ID12("NCR", "WaveLAN/PCMCIA", 0x24358cd4, 0x1bc50975),
	PCMCIA_DEVICE_NULL,
};
MODULE_DEVICE_TABLE(pcmcia, wavelan_ids);

static struct pcmcia_driver wavelan_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "wavelan_cs",
	},
	.probe		= wavelan_probe,
	.remove		= wavelan_detach,
	.id_table       = wavelan_ids,
	.suspend	= wavelan_suspend,
	.resume		= wavelan_resume,
};

static int __init
init_wavelan_cs(void)
{
	return pcmcia_register_driver(&wavelan_driver);
}

static void __exit
exit_wavelan_cs(void)
{
	pcmcia_unregister_driver(&wavelan_driver);
}

module_init(init_wavelan_cs);
module_exit(exit_wavelan_cs);
