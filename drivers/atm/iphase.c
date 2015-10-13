/******************************************************************************
         iphase.c: Device driver for Interphase ATM PCI adapter cards 
                    Author: Peter Wang  <pwang@iphase.com>            
		   Some fixes: Arnaldo Carvalho de Melo <acme@conectiva.com.br>
                   Interphase Corporation  <www.iphase.com>           
                               Version: 1.0                           
*******************************************************************************
      
      This software may be used and distributed according to the terms
      of the GNU General Public License (GPL), incorporated herein by reference.
      Drivers based on this skeleton fall under the GPL and must retain
      the authorship (implicit copyright) notice.

      This program is distributed in the hope that it will be useful, but
      WITHOUT ANY WARRANTY; without even the implied warranty of
      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
      General Public License for more details.
      
      Modified from an incomplete driver for Interphase 5575 1KVC 1M card which 
      was originally written by Monalisa Agrawal at UNH. Now this driver 
      supports a variety of varients of Interphase ATM PCI (i)Chip adapter 
      card family (See www.iphase.com/products/ClassSheet.cfm?ClassID=ATM) 
      in terms of PHY type, the size of control memory and the size of 
      packet memory. The followings are the change log and history:
     
          Bugfix the Mona's UBR driver.
          Modify the basic memory allocation and dma logic.
          Port the driver to the latest kernel from 2.0.46.
          Complete the ABR logic of the driver, and added the ABR work-
              around for the hardware anormalies.
          Add the CBR support.
	  Add the flow control logic to the driver to allow rate-limit VC.
          Add 4K VC support to the board with 512K control memory.
          Add the support of all the variants of the Interphase ATM PCI 
          (i)Chip adapter cards including x575 (155M OC3 and UTP155), x525
          (25M UTP25) and x531 (DS3 and E3).
          Add SMP support.

      Support and updates available at: ftp://ftp.iphase.com/pub/atm

*******************************************************************************/

#include <linux/module.h>  
#include <linux/kernel.h>  
#include <linux/mm.h>  
#include <linux/pci.h>  
#include <linux/errno.h>  
#include <linux/atm.h>  
#include <linux/atmdev.h>  
#include <linux/sonet.h>  
#include <linux/skbuff.h>  
#include <linux/time.h>  
#include <linux/delay.h>  
#include <linux/uio.h>  
#include <linux/init.h>  
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <asm/io.h>  
#include <linux/atomic.h>
#include <asm/uaccess.h>  
#include <asm/string.h>  
#include <asm/byteorder.h>  
#include <linux/vmalloc.h>
#include <linux/jiffies.h>
#include "iphase.h"		  
#include "suni.h"		  
#define swap_byte_order(x) (((x & 0xff) << 8) | ((x & 0xff00) >> 8))

#define PRIV(dev) ((struct suni_priv *) dev->phy_data)

static unsigned char ia_phy_get(struct atm_dev *dev, unsigned long addr);
static void desc_dbg(IADEV *iadev);

static IADEV *ia_dev[8];
static struct atm_dev *_ia_dev[8];
static int iadev_count;
static void ia_led_timer(unsigned long arg);
static DEFINE_TIMER(ia_timer, ia_led_timer, 0, 0);
static int IA_TX_BUF = DFL_TX_BUFFERS, IA_TX_BUF_SZ = DFL_TX_BUF_SZ;
static int IA_RX_BUF = DFL_RX_BUFFERS, IA_RX_BUF_SZ = DFL_RX_BUF_SZ;
static uint IADebugFlag = /* IF_IADBG_ERR | IF_IADBG_CBR| IF_IADBG_INIT_ADAPTER
            |IF_IADBG_ABR | IF_IADBG_EVENT*/ 0; 

module_param(IA_TX_BUF, int, 0);
module_param(IA_TX_BUF_SZ, int, 0);
module_param(IA_RX_BUF, int, 0);
module_param(IA_RX_BUF_SZ, int, 0);
module_param(IADebugFlag, uint, 0644);

MODULE_LICENSE("GPL");

/**************************** IA_LIB **********************************/

static void ia_init_rtn_q (IARTN_Q *que) 
{ 
   que->next = NULL; 
   que->tail = NULL; 
}

static void ia_enque_head_rtn_q (IARTN_Q *que, IARTN_Q * data) 
{
   data->next = NULL;
   if (que->next == NULL) 
      que->next = que->tail = data;
   else {
      data->next = que->next;
      que->next = data;
   } 
   return;
}

static int ia_enque_rtn_q (IARTN_Q *que, struct desc_tbl_t data) {
   IARTN_Q *entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
   if (!entry) return -1;
   entry->data = data;
   entry->next = NULL;
   if (que->next == NULL) 
      que->next = que->tail = entry;
   else {
      que->tail->next = entry;
      que->tail = que->tail->next;
   }      
   return 1;
}

static IARTN_Q * ia_deque_rtn_q (IARTN_Q *que) {
   IARTN_Q *tmpdata;
   if (que->next == NULL)
      return NULL;
   tmpdata = que->next;
   if ( que->next == que->tail)  
      que->next = que->tail = NULL;
   else 
      que->next = que->next->next;
   return tmpdata;
}

static void ia_hack_tcq(IADEV *dev) {

  u_short 		desc1;
  u_short		tcq_wr;
  struct ia_vcc         *iavcc_r = NULL; 

  tcq_wr = readl(dev->seg_reg+TCQ_WR_PTR) & 0xffff;
  while (dev->host_tcq_wr != tcq_wr) {
     desc1 = *(u_short *)(dev->seg_ram + dev->host_tcq_wr);
     if (!desc1) ;
     else if (!dev->desc_tbl[desc1 -1].timestamp) {
        IF_ABR(printk(" Desc %d is reset at %ld\n", desc1 -1, jiffies);)
        *(u_short *) (dev->seg_ram + dev->host_tcq_wr) = 0;
     }                                 
     else if (dev->desc_tbl[desc1 -1].timestamp) {
        if (!(iavcc_r = dev->desc_tbl[desc1 -1].iavcc)) { 
           printk("IA: Fatal err in get_desc\n");
           continue;
        }
        iavcc_r->vc_desc_cnt--;
        dev->desc_tbl[desc1 -1].timestamp = 0;
        IF_EVENT(printk("ia_hack: return_q skb = 0x%p desc = %d\n",
                                   dev->desc_tbl[desc1 -1].txskb, desc1);)
        if (iavcc_r->pcr < dev->rate_limit) {
           IA_SKB_STATE (dev->desc_tbl[desc1-1].txskb) |= IA_TX_DONE;
           if (ia_enque_rtn_q(&dev->tx_return_q, dev->desc_tbl[desc1 -1]) < 0)
              printk("ia_hack_tcq: No memory available\n");
        } 
        dev->desc_tbl[desc1 -1].iavcc = NULL;
        dev->desc_tbl[desc1 -1].txskb = NULL;
     }
     dev->host_tcq_wr += 2;
     if (dev->host_tcq_wr > dev->ffL.tcq_ed) 
        dev->host_tcq_wr = dev->ffL.tcq_st;
  }
} /* ia_hack_tcq */

static u16 get_desc (IADEV *dev, struct ia_vcc *iavcc) {
  u_short 		desc_num, i;
  struct sk_buff        *skb;
  struct ia_vcc         *iavcc_r = NULL; 
  unsigned long delta;
  static unsigned long timer = 0;
  int ltimeout;

  ia_hack_tcq (dev);
  if((time_after(jiffies,timer+50)) || ((dev->ffL.tcq_rd==dev->host_tcq_wr))) {
     timer = jiffies; 
     i=0;
     while (i < dev->num_tx_desc) {
        if (!dev->desc_tbl[i].timestamp) {
           i++;
           continue;
        }
        ltimeout = dev->desc_tbl[i].iavcc->ltimeout; 
        delta = jiffies - dev->desc_tbl[i].timestamp;
        if (delta >= ltimeout) {
           IF_ABR(printk("RECOVER run!! desc_tbl %d = %d  delta = %ld, time = %ld\n", i,dev->desc_tbl[i].timestamp, delta, jiffies);)
           if (dev->ffL.tcq_rd == dev->ffL.tcq_st) 
              dev->ffL.tcq_rd =  dev->ffL.tcq_ed;
           else 
              dev->ffL.tcq_rd -= 2;
           *(u_short *)(dev->seg_ram + dev->ffL.tcq_rd) = i+1;
           if (!(skb = dev->desc_tbl[i].txskb) || 
                          !(iavcc_r = dev->desc_tbl[i].iavcc))
              printk("Fatal err, desc table vcc or skb is NULL\n");
           else 
              iavcc_r->vc_desc_cnt--;
           dev->desc_tbl[i].timestamp = 0;
           dev->desc_tbl[i].iavcc = NULL;
           dev->desc_tbl[i].txskb = NULL;
        }
        i++;
     } /* while */
  }
  if (dev->ffL.tcq_rd == dev->host_tcq_wr) 
     return 0xFFFF;
    
  /* Get the next available descriptor number from TCQ */
  desc_num = *(u_short *)(dev->seg_ram + dev->ffL.tcq_rd);

  while (!desc_num || (dev->desc_tbl[desc_num -1]).timestamp) {
     dev->ffL.tcq_rd += 2;
     if (dev->ffL.tcq_rd > dev->ffL.tcq_ed) 
	dev->ffL.tcq_rd = dev->ffL.tcq_st;
     if (dev->ffL.tcq_rd == dev->host_tcq_wr) 
        return 0xFFFF; 
     desc_num = *(u_short *)(dev->seg_ram + dev->ffL.tcq_rd);
  }

  /* get system time */
  dev->desc_tbl[desc_num -1].timestamp = jiffies;
  return desc_num;
}

static void clear_lockup (struct atm_vcc *vcc, IADEV *dev) {
  u_char          	foundLockUp;
  vcstatus_t		*vcstatus;
  u_short               *shd_tbl;
  u_short               tempCellSlot, tempFract;
  struct main_vc *abr_vc = (struct main_vc *)dev->MAIN_VC_TABLE_ADDR;
  struct ext_vc *eabr_vc = (struct ext_vc *)dev->EXT_VC_TABLE_ADDR;
  u_int  i;

  if (vcc->qos.txtp.traffic_class == ATM_ABR) {
     vcstatus = (vcstatus_t *) &(dev->testTable[vcc->vci]->vc_status);
     vcstatus->cnt++;
     foundLockUp = 0;
     if( vcstatus->cnt == 0x05 ) {
        abr_vc += vcc->vci;
	eabr_vc += vcc->vci;
	if( eabr_vc->last_desc ) {
	   if( (abr_vc->status & 0x07) == ABR_STATE /* 0x2 */ ) {
              /* Wait for 10 Micro sec */
              udelay(10);
	      if ((eabr_vc->last_desc)&&((abr_vc->status & 0x07)==ABR_STATE))
		 foundLockUp = 1;
           }
	   else {
	      tempCellSlot = abr_vc->last_cell_slot;
              tempFract    = abr_vc->fraction;
              if((tempCellSlot == dev->testTable[vcc->vci]->lastTime)
                         && (tempFract == dev->testTable[vcc->vci]->fract))
	         foundLockUp = 1; 		    
              dev->testTable[vcc->vci]->lastTime = tempCellSlot;   
              dev->testTable[vcc->vci]->fract = tempFract; 
	   } 	    
        } /* last descriptor */	 	   
        vcstatus->cnt = 0;     	
     } /* vcstatus->cnt */
	
     if (foundLockUp) {
        IF_ABR(printk("LOCK UP found\n");) 
	writew(0xFFFD, dev->seg_reg+MODE_REG_0);
        /* Wait for 10 Micro sec */
        udelay(10); 
        abr_vc->status &= 0xFFF8;
        abr_vc->status |= 0x0001;  /* state is idle */
	shd_tbl = (u_short *)dev->ABR_SCHED_TABLE_ADDR;                
	for( i = 0; ((i < dev->num_vc) && (shd_tbl[i])); i++ );
	if (i < dev->num_vc)
           shd_tbl[i] = vcc->vci;
        else
           IF_ERR(printk("ABR Seg. may not continue on VC %x\n",vcc->vci);)
        writew(T_ONLINE, dev->seg_reg+MODE_REG_0);
        writew(~(TRANSMIT_DONE|TCQ_NOT_EMPTY), dev->seg_reg+SEG_MASK_REG);
        writew(TRANSMIT_DONE, dev->seg_reg+SEG_INTR_STATUS_REG);       
	vcstatus->cnt = 0;
     } /* foundLockUp */

  } /* if an ABR VC */


}
 
/*
** Conversion of 24-bit cellrate (cells/sec) to 16-bit floating point format.
**
**  +----+----+------------------+-------------------------------+
**  |  R | NZ |  5-bit exponent  |        9-bit mantissa         |
**  +----+----+------------------+-------------------------------+
** 
**    R = reserved (written as 0)
**    NZ = 0 if 0 cells/sec; 1 otherwise
**
**    if NZ = 1, rate = 1.mmmmmmmmm x 2^(eeeee) cells/sec
*/
static u16
cellrate_to_float(u32 cr)
{

#define	NZ 		0x4000
#define	M_BITS		9		/* Number of bits in mantissa */
#define	E_BITS		5		/* Number of bits in exponent */
#define	M_MASK		0x1ff		
#define	E_MASK		0x1f
  u16   flot;
  u32	tmp = cr & 0x00ffffff;
  int 	i   = 0;
  if (cr == 0)
     return 0;
  while (tmp != 1) {
     tmp >>= 1;
     i++;
  }
  if (i == M_BITS)
     flot = NZ | (i << M_BITS) | (cr & M_MASK);
  else if (i < M_BITS)
     flot = NZ | (i << M_BITS) | ((cr << (M_BITS - i)) & M_MASK);
  else
     flot = NZ | (i << M_BITS) | ((cr >> (i - M_BITS)) & M_MASK);
  return flot;
}

#if 0
/*
** Conversion of 16-bit floating point format to 24-bit cellrate (cells/sec).
*/
static u32
float_to_cellrate(u16 rate)
{
  u32   exp, mantissa, cps;
  if ((rate & NZ) == 0)
     return 0;
  exp = (rate >> M_BITS) & E_MASK;
  mantissa = rate & M_MASK;
  if (exp == 0)
     return 1;
  cps = (1 << M_BITS) | mantissa;
  if (exp == M_BITS)
     cps = cps;
  else if (exp > M_BITS)
     cps <<= (exp - M_BITS);
  else
     cps >>= (M_BITS - exp);
  return cps;
}
#endif 

static void init_abr_vc (IADEV *dev, srv_cls_param_t *srv_p) {
  srv_p->class_type = ATM_ABR;
  srv_p->pcr        = dev->LineRate;
  srv_p->mcr        = 0;
  srv_p->icr        = 0x055cb7;
  srv_p->tbe        = 0xffffff;
  srv_p->frtt       = 0x3a;
  srv_p->rif        = 0xf;
  srv_p->rdf        = 0xb;
  srv_p->nrm        = 0x4;
  srv_p->trm        = 0x7;
  srv_p->cdf        = 0x3;
  srv_p->adtf       = 50;
}

static int
ia_open_abr_vc(IADEV *dev, srv_cls_param_t *srv_p, 
                                                struct atm_vcc *vcc, u8 flag)
{
  f_vc_abr_entry  *f_abr_vc;
  r_vc_abr_entry  *r_abr_vc;
  u32		icr;
  u8		trm, nrm, crm;
  u16		adtf, air, *ptr16;	
  f_abr_vc =(f_vc_abr_entry *)dev->MAIN_VC_TABLE_ADDR;
  f_abr_vc += vcc->vci;       
  switch (flag) {
     case 1: /* FFRED initialization */
#if 0  /* sanity check */
       if (srv_p->pcr == 0)
          return INVALID_PCR;
       if (srv_p->pcr > dev->LineRate)
          srv_p->pcr = dev->LineRate;
       if ((srv_p->mcr + dev->sum_mcr) > dev->LineRate)
	  return MCR_UNAVAILABLE;
       if (srv_p->mcr > srv_p->pcr)
	  return INVALID_MCR;
       if (!(srv_p->icr))
	  srv_p->icr = srv_p->pcr;
       if ((srv_p->icr < srv_p->mcr) || (srv_p->icr > srv_p->pcr))
	  return INVALID_ICR;
       if ((srv_p->tbe < MIN_TBE) || (srv_p->tbe > MAX_TBE))
	  return INVALID_TBE;
       if ((srv_p->frtt < MIN_FRTT) || (srv_p->frtt > MAX_FRTT))
	  return INVALID_FRTT;
       if (srv_p->nrm > MAX_NRM)
	  return INVALID_NRM;
       if (srv_p->trm > MAX_TRM)
	  return INVALID_TRM;
       if (srv_p->adtf > MAX_ADTF)
          return INVALID_ADTF;
       else if (srv_p->adtf == 0)
	  srv_p->adtf = 1;
       if (srv_p->cdf > MAX_CDF)
	  return INVALID_CDF;
       if (srv_p->rif > MAX_RIF)
	  return INVALID_RIF;
       if (srv_p->rdf > MAX_RDF)
	  return INVALID_RDF;
#endif
       memset ((caddr_t)f_abr_vc, 0, sizeof(*f_abr_vc));
       f_abr_vc->f_vc_type = ABR;
       nrm = 2 << srv_p->nrm;     /* (2 ** (srv_p->nrm +1)) */
			          /* i.e 2**n = 2 << (n-1) */
       f_abr_vc->f_nrm = nrm << 8 | nrm;
       trm = 100000/(2 << (16 - srv_p->trm));
       if ( trm == 0) trm = 1;
       f_abr_vc->f_nrmexp =(((srv_p->nrm +1) & 0x0f) << 12)|(MRM << 8) | trm;
       crm = srv_p->tbe / nrm;
       if (crm == 0) crm = 1;
       f_abr_vc->f_crm = crm & 0xff;
       f_abr_vc->f_pcr = cellrate_to_float(srv_p->pcr);
       icr = min( srv_p->icr, (srv_p->tbe > srv_p->frtt) ?
				((srv_p->tbe/srv_p->frtt)*1000000) :
				(1000000/(srv_p->frtt/srv_p->tbe)));
       f_abr_vc->f_icr = cellrate_to_float(icr);
       adtf = (10000 * srv_p->adtf)/8192;
       if (adtf == 0) adtf = 1; 
       f_abr_vc->f_cdf = ((7 - srv_p->cdf) << 12 | adtf) & 0xfff;
       f_abr_vc->f_mcr = cellrate_to_float(srv_p->mcr);
       f_abr_vc->f_acr = f_abr_vc->f_icr;
       f_abr_vc->f_status = 0x0042;
       break;
    case 0: /* RFRED initialization */	
       ptr16 = (u_short *)(dev->reass_ram + REASS_TABLE*dev->memSize); 
       *(ptr16 + vcc->vci) = NO_AAL5_PKT | REASS_ABR;
       r_abr_vc = (r_vc_abr_entry*)(dev->reass_ram+ABR_VC_TABLE*dev->memSize);
       r_abr_vc += vcc->vci;
       r_abr_vc->r_status_rdf = (15 - srv_p->rdf) & 0x000f;
       air = srv_p->pcr << (15 - srv_p->rif);
       if (air == 0) air = 1;
       r_abr_vc->r_air = cellrate_to_float(air);
       dev->testTable[vcc->vci]->vc_status = VC_ACTIVE | VC_ABR;
       dev->sum_mcr	   += srv_p->mcr;
       dev->n_abr++;
       break;
    default:
       break;
  }
  return	0;
}
static int ia_cbr_setup (IADEV *dev, struct atm_vcc *vcc) {
   u32 rateLow=0, rateHigh, rate;
   int entries;
   struct ia_vcc *ia_vcc;

   int   idealSlot =0, testSlot, toBeAssigned, inc;
   u32   spacing;
   u16  *SchedTbl, *TstSchedTbl;
   u16  cbrVC, vcIndex;
   u32   fracSlot    = 0;
   u32   sp_mod      = 0;
   u32   sp_mod2     = 0;

   /* IpAdjustTrafficParams */
   if (vcc->qos.txtp.max_pcr <= 0) {
      IF_ERR(printk("PCR for CBR not defined\n");)
      return -1;
   }
   rate = vcc->qos.txtp.max_pcr;
   entries = rate / dev->Granularity;
   IF_CBR(printk("CBR: CBR entries=0x%x for rate=0x%x & Gran=0x%x\n",
                                entries, rate, dev->Granularity);)
   if (entries < 1)
      IF_CBR(printk("CBR: Bandwidth smaller than granularity of CBR table\n");) 
   rateLow  =  entries * dev->Granularity;
   rateHigh = (entries + 1) * dev->Granularity;
   if (3*(rate - rateLow) > (rateHigh - rate))
      entries++;
   if (entries > dev->CbrRemEntries) {
      IF_CBR(printk("CBR: Not enough bandwidth to support this PCR.\n");)
      IF_CBR(printk("Entries = 0x%x, CbrRemEntries = 0x%x.\n",
                                       entries, dev->CbrRemEntries);)
      return -EBUSY;
   }   

   ia_vcc = INPH_IA_VCC(vcc);
   ia_vcc->NumCbrEntry = entries; 
   dev->sum_mcr += entries * dev->Granularity; 
   /* IaFFrednInsertCbrSched */
   // Starting at an arbitrary location, place the entries into the table
   // as smoothly as possible
   cbrVC   = 0;
   spacing = dev->CbrTotEntries / entries;
   sp_mod  = dev->CbrTotEntries % entries; // get modulo
   toBeAssigned = entries;
   fracSlot = 0;
   vcIndex  = vcc->vci;
   IF_CBR(printk("Vci=0x%x,Spacing=0x%x,Sp_mod=0x%x\n",vcIndex,spacing,sp_mod);)
   while (toBeAssigned)
   {
      // If this is the first time, start the table loading for this connection
      // as close to entryPoint as possible.
      if (toBeAssigned == entries)
      {
         idealSlot = dev->CbrEntryPt;
         dev->CbrEntryPt += 2;    // Adding 2 helps to prevent clumping
         if (dev->CbrEntryPt >= dev->CbrTotEntries) 
            dev->CbrEntryPt -= dev->CbrTotEntries;// Wrap if necessary
      } else {
         idealSlot += (u32)(spacing + fracSlot); // Point to the next location
         // in the table that would be  smoothest
         fracSlot = ((sp_mod + sp_mod2) / entries);  // get new integer part
         sp_mod2  = ((sp_mod + sp_mod2) % entries);  // calc new fractional part
      }
      if (idealSlot >= (int)dev->CbrTotEntries) 
         idealSlot -= dev->CbrTotEntries;  
      // Continuously check around this ideal value until a null
      // location is encountered.
      SchedTbl = (u16*)(dev->seg_ram+CBR_SCHED_TABLE*dev->memSize); 
      inc = 0;
      testSlot = idealSlot;
      TstSchedTbl = (u16*)(SchedTbl+testSlot);  //set index and read in value
      IF_CBR(printk("CBR Testslot 0x%x AT Location 0x%p, NumToAssign=%d\n",
                                testSlot, TstSchedTbl,toBeAssigned);)
      memcpy((caddr_t)&cbrVC,(caddr_t)TstSchedTbl,sizeof(cbrVC));
      while (cbrVC)  // If another VC at this location, we have to keep looking
      {
          inc++;
          testSlot = idealSlot - inc;
          if (testSlot < 0) { // Wrap if necessary
             testSlot += dev->CbrTotEntries;
             IF_CBR(printk("Testslot Wrap. STable Start=0x%p,Testslot=%d\n",
                                                       SchedTbl,testSlot);)
          }
          TstSchedTbl = (u16 *)(SchedTbl + testSlot);  // set table index
          memcpy((caddr_t)&cbrVC,(caddr_t)TstSchedTbl,sizeof(cbrVC)); 
          if (!cbrVC)
             break;
          testSlot = idealSlot + inc;
          if (testSlot >= (int)dev->CbrTotEntries) { // Wrap if necessary
             testSlot -= dev->CbrTotEntries;
             IF_CBR(printk("TotCbrEntries=%d",dev->CbrTotEntries);)
             IF_CBR(printk(" Testslot=0x%x ToBeAssgned=%d\n", 
                                            testSlot, toBeAssigned);)
          } 
          // set table index and read in value
          TstSchedTbl = (u16*)(SchedTbl + testSlot);
          IF_CBR(printk("Reading CBR Tbl from 0x%p, CbrVal=0x%x Iteration %d\n",
                          TstSchedTbl,cbrVC,inc);)
          memcpy((caddr_t)&cbrVC,(caddr_t)TstSchedTbl,sizeof(cbrVC));
       } /* while */
       // Move this VCI number into this location of the CBR Sched table.
       memcpy((caddr_t)TstSchedTbl, (caddr_t)&vcIndex, sizeof(*TstSchedTbl));
       dev->CbrRemEntries--;
       toBeAssigned--;
   } /* while */ 

   /* IaFFrednCbrEnable */
   dev->NumEnabledCBR++;
   if (dev->NumEnabledCBR == 1) {
       writew((CBR_EN | UBR_EN | ABR_EN | (0x23 << 2)), dev->seg_reg+STPARMS);
       IF_CBR(printk("CBR is enabled\n");)
   }
   return 0;
}
static void ia_cbrVc_close (struct atm_vcc *vcc) {
   IADEV *iadev;
   u16 *SchedTbl, NullVci = 0;
   u32 i, NumFound;

   iadev = INPH_IA_DEV(vcc->dev);
   iadev->NumEnabledCBR--;
   SchedTbl = (u16*)(iadev->seg_ram+CBR_SCHED_TABLE*iadev->memSize);
   if (iadev->NumEnabledCBR == 0) {
      writew((UBR_EN | ABR_EN | (0x23 << 2)), iadev->seg_reg+STPARMS);
      IF_CBR (printk("CBR support disabled\n");)
   }
   NumFound = 0;
   for (i=0; i < iadev->CbrTotEntries; i++)
   {
      if (*SchedTbl == vcc->vci) {
         iadev->CbrRemEntries++;
         *SchedTbl = NullVci;
         IF_CBR(NumFound++;)
      }
      SchedTbl++;   
   } 
   IF_CBR(printk("Exit ia_cbrVc_close, NumRemoved=%d\n",NumFound);)
}

static int ia_avail_descs(IADEV *iadev) {
   int tmp = 0;
   ia_hack_tcq(iadev);
   if (iadev->host_tcq_wr >= iadev->ffL.tcq_rd)
      tmp = (iadev->host_tcq_wr - iadev->ffL.tcq_rd) / 2;
   else
      tmp = (iadev->ffL.tcq_ed - iadev->ffL.tcq_rd + 2 + iadev->host_tcq_wr -
                   iadev->ffL.tcq_st) / 2;
   return tmp;
}    

static int ia_pkt_tx (struct atm_vcc *vcc, struct sk_buff *skb);

static int ia_que_tx (IADEV *iadev) { 
   struct sk_buff *skb;
   int num_desc;
   struct atm_vcc *vcc;
   num_desc = ia_avail_descs(iadev);

   while (num_desc && (skb = skb_dequeue(&iadev->tx_backlog))) {
      if (!(vcc = ATM_SKB(skb)->vcc)) {
         dev_kfree_skb_any(skb);
         printk("ia_que_tx: Null vcc\n");
         break;
      }
      if (!test_bit(ATM_VF_READY,&vcc->flags)) {
         dev_kfree_skb_any(skb);
         printk("Free the SKB on closed vci %d \n", vcc->vci);
         break;
      }
      if (ia_pkt_tx (vcc, skb)) {
         skb_queue_head(&iadev->tx_backlog, skb);
      }
      num_desc--;
   }
   return 0;
}

static void ia_tx_poll (IADEV *iadev) {
   struct atm_vcc *vcc = NULL;
   struct sk_buff *skb = NULL, *skb1 = NULL;
   struct ia_vcc *iavcc;
   IARTN_Q *  rtne;

   ia_hack_tcq(iadev);
   while ( (rtne = ia_deque_rtn_q(&iadev->tx_return_q))) {
       skb = rtne->data.txskb;
       if (!skb) {
           printk("ia_tx_poll: skb is null\n");
           goto out;
       }
       vcc = ATM_SKB(skb)->vcc;
       if (!vcc) {
           printk("ia_tx_poll: vcc is null\n");
           dev_kfree_skb_any(skb);
	   goto out;
       }

       iavcc = INPH_IA_VCC(vcc);
       if (!iavcc) {
           printk("ia_tx_poll: iavcc is null\n");
           dev_kfree_skb_any(skb);
	   goto out;
       }

       skb1 = skb_dequeue(&iavcc->txing_skb);
       while (skb1 && (skb1 != skb)) {
          if (!(IA_SKB_STATE(skb1) & IA_TX_DONE)) {
             printk("IA_tx_intr: Vci %d lost pkt!!!\n", vcc->vci);
          }
          IF_ERR(printk("Release the SKB not match\n");)
          if ((vcc->pop) && (skb1->len != 0))
          {
             vcc->pop(vcc, skb1);
             IF_EVENT(printk("Tansmit Done - skb 0x%lx return\n",
                                                          (long)skb1);)
          }
          else 
             dev_kfree_skb_any(skb1);
          skb1 = skb_dequeue(&iavcc->txing_skb);
       }                                                        
       if (!skb1) {
          IF_EVENT(printk("IA: Vci %d - skb not found requed\n",vcc->vci);)
          ia_enque_head_rtn_q (&iadev->tx_return_q, rtne);
          break;
       }
       if ((vcc->pop) && (skb->len != 0))
       {
          vcc->pop(vcc, skb);
          IF_EVENT(printk("Tx Done - skb 0x%lx return\n",(long)skb);)
       }
       else 
          dev_kfree_skb_any(skb);
       kfree(rtne);
    }
    ia_que_tx(iadev);
out:
    return;
}
#if 0
static void ia_eeprom_put (IADEV *iadev, u32 addr, u_short val)
{
        u32	t;
	int	i;
	/*
	 * Issue a command to enable writes to the NOVRAM
	 */
	NVRAM_CMD (EXTEND + EWEN);
	NVRAM_CLR_CE;
	/*
	 * issue the write command
	 */
	NVRAM_CMD(IAWRITE + addr);
	/* 
	 * Send the data, starting with D15, then D14, and so on for 16 bits
	 */
	for (i=15; i>=0; i--) {
		NVRAM_CLKOUT (val & 0x8000);
		val <<= 1;
	}
	NVRAM_CLR_CE;
	CFG_OR(NVCE);
	t = readl(iadev->reg+IPHASE5575_EEPROM_ACCESS); 
	while (!(t & NVDO))
		t = readl(iadev->reg+IPHASE5575_EEPROM_ACCESS); 

	NVRAM_CLR_CE;
	/*
	 * disable writes again
	 */
	NVRAM_CMD(EXTEND + EWDS)
	NVRAM_CLR_CE;
	CFG_AND(~NVDI);
}
#endif

static u16 ia_eeprom_get (IADEV *iadev, u32 addr)
{
	u_short	val;
        u32	t;
	int	i;
	/*
	 * Read the first bit that was clocked with the falling edge of the
	 * the last command data clock
	 */
	NVRAM_CMD(IAREAD + addr);
	/*
	 * Now read the rest of the bits, the next bit read is D14, then D13,
	 * and so on.
	 */
	val = 0;
	for (i=15; i>=0; i--) {
		NVRAM_CLKIN(t);
		val |= (t << i);
	}
	NVRAM_CLR_CE;
	CFG_AND(~NVDI);
	return val;
}

static void ia_hw_type(IADEV *iadev) {
   u_short memType = ia_eeprom_get(iadev, 25);   
   iadev->memType = memType;
   if ((memType & MEM_SIZE_MASK) == MEM_SIZE_1M) {
      iadev->num_tx_desc = IA_TX_BUF;
      iadev->tx_buf_sz = IA_TX_BUF_SZ;
      iadev->num_rx_desc = IA_RX_BUF;
      iadev->rx_buf_sz = IA_RX_BUF_SZ; 
   } else if ((memType & MEM_SIZE_MASK) == MEM_SIZE_512K) {
      if (IA_TX_BUF == DFL_TX_BUFFERS)
        iadev->num_tx_desc = IA_TX_BUF / 2;
      else 
        iadev->num_tx_desc = IA_TX_BUF;
      iadev->tx_buf_sz = IA_TX_BUF_SZ;
      if (IA_RX_BUF == DFL_RX_BUFFERS)
        iadev->num_rx_desc = IA_RX_BUF / 2;
      else
        iadev->num_rx_desc = IA_RX_BUF;
      iadev->rx_buf_sz = IA_RX_BUF_SZ;
   }
   else {
      if (IA_TX_BUF == DFL_TX_BUFFERS) 
        iadev->num_tx_desc = IA_TX_BUF / 8;
      else
        iadev->num_tx_desc = IA_TX_BUF;
      iadev->tx_buf_sz = IA_TX_BUF_SZ;
      if (IA_RX_BUF == DFL_RX_BUFFERS)
        iadev->num_rx_desc = IA_RX_BUF / 8;
      else
        iadev->num_rx_desc = IA_RX_BUF;
      iadev->rx_buf_sz = IA_RX_BUF_SZ; 
   } 
   iadev->rx_pkt_ram = TX_PACKET_RAM + (iadev->num_tx_desc * iadev->tx_buf_sz); 
   IF_INIT(printk("BUF: tx=%d,sz=%d rx=%d sz= %d rx_pkt_ram=%d\n",
         iadev->num_tx_desc, iadev->tx_buf_sz, iadev->num_rx_desc,
         iadev->rx_buf_sz, iadev->rx_pkt_ram);)

#if 0
   if ((memType & FE_MASK) == FE_SINGLE_MODE) {
      iadev->phy_type = PHY_OC3C_S;
   else if ((memType & FE_MASK) == FE_UTP_OPTION)
      iadev->phy_type = PHY_UTP155;
   else
     iadev->phy_type = PHY_OC3C_M;
#endif
   
   iadev->phy_type = memType & FE_MASK;
   IF_INIT(printk("memType = 0x%x iadev->phy_type = 0x%x\n", 
                                         memType,iadev->phy_type);)
   if (iadev->phy_type == FE_25MBIT_PHY) 
      iadev->LineRate = (u32)(((25600000/8)*26)/(27*53));
   else if (iadev->phy_type == FE_DS3_PHY)
      iadev->LineRate = (u32)(((44736000/8)*26)/(27*53));
   else if (iadev->phy_type == FE_E3_PHY) 
      iadev->LineRate = (u32)(((34368000/8)*26)/(27*53));
   else
       iadev->LineRate = (u32)(ATM_OC3_PCR);
   IF_INIT(printk("iadev->LineRate = %d \n", iadev->LineRate);)

}

static u32 ia_phy_read32(struct iadev_priv *ia, unsigned int reg)
{
	return readl(ia->phy + (reg >> 2));
}

static void ia_phy_write32(struct iadev_priv *ia, unsigned int reg, u32 val)
{
	writel(val, ia->phy + (reg >> 2));
}

static void ia_frontend_intr(struct iadev_priv *iadev)
{
	u32 status;

	if (iadev->phy_type & FE_25MBIT_PHY) {
		status = ia_phy_read32(iadev, MB25_INTR_STATUS);
		iadev->carrier_detect = (status & MB25_IS_GSB) ? 1 : 0;
	} else if (iadev->phy_type & FE_DS3_PHY) {
		ia_phy_read32(iadev, SUNI_DS3_FRM_INTR_STAT);
		status = ia_phy_read32(iadev, SUNI_DS3_FRM_STAT);
		iadev->carrier_detect = (status & SUNI_DS3_LOSV) ? 0 : 1;
	} else if (iadev->phy_type & FE_E3_PHY) {
		ia_phy_read32(iadev, SUNI_E3_FRM_MAINT_INTR_IND);
		status = ia_phy_read32(iadev, SUNI_E3_FRM_FRAM_INTR_IND_STAT);
		iadev->carrier_detect = (status & SUNI_E3_LOS) ? 0 : 1;
	} else {
		status = ia_phy_read32(iadev, SUNI_RSOP_STATUS);
		iadev->carrier_detect = (status & SUNI_LOSV) ? 0 : 1;
	}

	printk(KERN_INFO "IA: SUNI carrier %s\n",
		iadev->carrier_detect ? "detected" : "lost signal");
}

static void ia_mb25_init(struct iadev_priv *iadev)
{
#if 0
   mb25->mb25_master_ctrl = MB25_MC_DRIC | MB25_MC_DREC | MB25_MC_ENABLED;
#endif
	ia_phy_write32(iadev, MB25_MASTER_CTRL, MB25_MC_DRIC | MB25_MC_DREC);
	ia_phy_write32(iadev, MB25_DIAG_CONTROL, 0);

	iadev->carrier_detect =
		(ia_phy_read32(iadev, MB25_INTR_STATUS) & MB25_IS_GSB) ? 1 : 0;
}

struct ia_reg {
	u16 reg;
	u16 val;
};

static void ia_phy_write(struct iadev_priv *iadev,
			 const struct ia_reg *regs, int len)
{
	while (len--) {
		ia_phy_write32(iadev, regs->reg, regs->val);
		regs++;
	}
}

static void ia_suni_pm7345_init_ds3(struct iadev_priv *iadev)
{
	static const struct ia_reg suni_ds3_init [] = {
		{ SUNI_DS3_FRM_INTR_ENBL,	0x17 },
		{ SUNI_DS3_FRM_CFG,		0x01 },
		{ SUNI_DS3_TRAN_CFG,		0x01 },
		{ SUNI_CONFIG,			0 },
		{ SUNI_SPLR_CFG,		0 },
		{ SUNI_SPLT_CFG,		0 }
	};
	u32 status;

	status = ia_phy_read32(iadev, SUNI_DS3_FRM_STAT);
	iadev->carrier_detect = (status & SUNI_DS3_LOSV) ? 0 : 1;

	ia_phy_write(iadev, suni_ds3_init, ARRAY_SIZE(suni_ds3_init));
}

static void ia_suni_pm7345_init_e3(struct iadev_priv *iadev)
{
	static const struct ia_reg suni_e3_init [] = {
		{ SUNI_E3_FRM_FRAM_OPTIONS,		0x04 },
		{ SUNI_E3_FRM_MAINT_OPTIONS,		0x20 },
		{ SUNI_E3_FRM_FRAM_INTR_ENBL,		0x1d },
		{ SUNI_E3_FRM_MAINT_INTR_ENBL,		0x30 },
		{ SUNI_E3_TRAN_STAT_DIAG_OPTIONS,	0 },
		{ SUNI_E3_TRAN_FRAM_OPTIONS,		0x01 },
		{ SUNI_CONFIG,				SUNI_PM7345_E3ENBL },
		{ SUNI_SPLR_CFG,			0x41 },
		{ SUNI_SPLT_CFG,			0x41 }
	};
	u32 status;

	status = ia_phy_read32(iadev, SUNI_E3_FRM_FRAM_INTR_IND_STAT);
	iadev->carrier_detect = (status & SUNI_E3_LOS) ? 0 : 1;
	ia_phy_write(iadev, suni_e3_init, ARRAY_SIZE(suni_e3_init));
}

static void ia_suni_pm7345_init(struct iadev_priv *iadev)
{
	static const struct ia_reg suni_init [] = {
		/* Enable RSOP loss of signal interrupt. */
		{ SUNI_INTR_ENBL,		0x28 },
		/* Clear error counters. */
		{ SUNI_ID_RESET,		0 },
		/* Clear "PMCTST" in master test register. */
		{ SUNI_MASTER_TEST,		0 },

		{ SUNI_RXCP_CTRL,		0x2c },
		{ SUNI_RXCP_FCTRL,		0x81 },

		{ SUNI_RXCP_IDLE_PAT_H1,	0 },
		{ SUNI_RXCP_IDLE_PAT_H2,	0 },
		{ SUNI_RXCP_IDLE_PAT_H3,	0 },
		{ SUNI_RXCP_IDLE_PAT_H4,	0x01 },

		{ SUNI_RXCP_IDLE_MASK_H1,	0xff },
		{ SUNI_RXCP_IDLE_MASK_H2,	0xff },
		{ SUNI_RXCP_IDLE_MASK_H3,	0xff },
		{ SUNI_RXCP_IDLE_MASK_H4,	0xfe },

		{ SUNI_RXCP_CELL_PAT_H1,	0 },
		{ SUNI_RXCP_CELL_PAT_H2,	0 },
		{ SUNI_RXCP_CELL_PAT_H3,	0 },
		{ SUNI_RXCP_CELL_PAT_H4,	0x01 },

		{ SUNI_RXCP_CELL_MASK_H1,	0xff },
		{ SUNI_RXCP_CELL_MASK_H2,	0xff },
		{ SUNI_RXCP_CELL_MASK_H3,	0xff },
		{ SUNI_RXCP_CELL_MASK_H4,	0xff },

		{ SUNI_TXCP_CTRL,		0xa4 },
		{ SUNI_TXCP_INTR_EN_STS,	0x10 },
		{ SUNI_TXCP_IDLE_PAT_H5,	0x55 }
	};

	if (iadev->phy_type & FE_DS3_PHY)
		ia_suni_pm7345_init_ds3(iadev);
	else
		ia_suni_pm7345_init_e3(iadev);

	ia_phy_write(iadev, suni_init, ARRAY_SIZE(suni_init));

	ia_phy_write32(iadev, SUNI_CONFIG, ia_phy_read32(iadev, SUNI_CONFIG) &
		~(SUNI_PM7345_LLB | SUNI_PM7345_CLB |
		  SUNI_PM7345_DLB | SUNI_PM7345_PLB));
#ifdef __SNMP__
   suni_pm7345->suni_rxcp_intr_en_sts |= SUNI_OOCDE;
#endif /* __SNMP__ */
   return;
}


/***************************** IA_LIB END *****************************/
    
#ifdef CONFIG_ATM_IA_DEBUG
static int tcnter = 0;
static void xdump( u_char*  cp, int  length, char*  prefix )
{
    int col, count;
    u_char prntBuf[120];
    u_char*  pBuf = prntBuf;
    count = 0;
    while(count < length){
        pBuf += sprintf( pBuf, "%s", prefix );
        for(col = 0;count + col < length && col < 16; col++){
            if (col != 0 && (col % 4) == 0)
                pBuf += sprintf( pBuf, " " );
            pBuf += sprintf( pBuf, "%02X ", cp[count + col] );
        }
        while(col++ < 16){      /* pad end of buffer with blanks */
            if ((col % 4) == 0)
                sprintf( pBuf, " " );
            pBuf += sprintf( pBuf, "   " );
        }
        pBuf += sprintf( pBuf, "  " );
        for(col = 0;count + col < length && col < 16; col++){
            if (isprint((int)cp[count + col]))
                pBuf += sprintf( pBuf, "%c", cp[count + col] );
            else
                pBuf += sprintf( pBuf, "." );
                }
        printk("%s\n", prntBuf);
        count += col;
        pBuf = prntBuf;
    }

}  /* close xdump(... */
#endif /* CONFIG_ATM_IA_DEBUG */

  
static struct atm_dev *ia_boards = NULL;  
  
#define ACTUAL_RAM_BASE \
	RAM_BASE*((iadev->mem)/(128 * 1024))  
#define ACTUAL_SEG_RAM_BASE \
	IPHASE5575_FRAG_CONTROL_RAM_BASE*((iadev->mem)/(128 * 1024))  
#define ACTUAL_REASS_RAM_BASE \
	IPHASE5575_REASS_CONTROL_RAM_BASE*((iadev->mem)/(128 * 1024))  
  
  
/*-- some utilities and memory allocation stuff will come here -------------*/  
  
static void desc_dbg(IADEV *iadev) {

  u_short tcq_wr_ptr, tcq_st_ptr, tcq_ed_ptr;
  u32 i;
  void __iomem *tmp;
  // regval = readl((u32)ia_cmds->maddr);
  tcq_wr_ptr =  readw(iadev->seg_reg+TCQ_WR_PTR);
  printk("B_tcq_wr = 0x%x desc = %d last desc = %d\n",
                     tcq_wr_ptr, readw(iadev->seg_ram+tcq_wr_ptr),
                     readw(iadev->seg_ram+tcq_wr_ptr-2));
  printk(" host_tcq_wr = 0x%x  host_tcq_rd = 0x%x \n",  iadev->host_tcq_wr, 
                   iadev->ffL.tcq_rd);
  tcq_st_ptr =  readw(iadev->seg_reg+TCQ_ST_ADR);
  tcq_ed_ptr =  readw(iadev->seg_reg+TCQ_ED_ADR);
  printk("tcq_st_ptr = 0x%x    tcq_ed_ptr = 0x%x \n", tcq_st_ptr, tcq_ed_ptr);
  i = 0;
  while (tcq_st_ptr != tcq_ed_ptr) {
      tmp = iadev->seg_ram+tcq_st_ptr;
      printk("TCQ slot %d desc = %d  Addr = %p\n", i++, readw(tmp), tmp);
      tcq_st_ptr += 2;
  }
  for(i=0; i <iadev->num_tx_desc; i++)
      printk("Desc_tbl[%d] = %d \n", i, iadev->desc_tbl[i].timestamp);
} 
  
  
/*----------------------------- Receiving side stuff --------------------------*/  
 
static void rx_excp_rcvd(struct atm_dev *dev)  
{  
#if 0 /* closing the receiving size will cause too many excp int */  
  IADEV *iadev;  
  u_short state;  
  u_short excpq_rd_ptr;  
  //u_short *ptr;  
  int vci, error = 1;  
  iadev = INPH_IA_DEV(dev);  
  state = readl(iadev->reass_reg + STATE_REG) & 0xffff;  
  while((state & EXCPQ_EMPTY) != EXCPQ_EMPTY)  
  { printk("state = %x \n", state); 
        excpq_rd_ptr = readw(iadev->reass_reg + EXCP_Q_RD_PTR) & 0xffff;  
 printk("state = %x excpq_rd_ptr = %x \n", state, excpq_rd_ptr); 
        if (excpq_rd_ptr == *(u16*)(iadev->reass_reg + EXCP_Q_WR_PTR))
            IF_ERR(printk("excpq_rd_ptr is wrong!!!\n");)
        // TODO: update exception stat
	vci = readw(iadev->reass_ram+excpq_rd_ptr);  
	error = readw(iadev->reass_ram+excpq_rd_ptr+2) & 0x0007;  
        // pwang_test
	excpq_rd_ptr += 4;  
	if (excpq_rd_ptr > (readw(iadev->reass_reg + EXCP_Q_ED_ADR)& 0xffff))  
 	    excpq_rd_ptr = readw(iadev->reass_reg + EXCP_Q_ST_ADR)& 0xffff;
	writew( excpq_rd_ptr, iadev->reass_reg + EXCP_Q_RD_PTR);  
        state = readl(iadev->reass_reg + STATE_REG) & 0xffff;  
  }  
#endif
}  
  
static void free_desc(struct atm_dev *dev, int desc)  
{  
	IADEV *iadev;  
	iadev = INPH_IA_DEV(dev);  
        writew(desc, iadev->reass_ram+iadev->rfL.fdq_wr); 
	iadev->rfL.fdq_wr +=2;
	if (iadev->rfL.fdq_wr > iadev->rfL.fdq_ed)
		iadev->rfL.fdq_wr =  iadev->rfL.fdq_st;  
	writew(iadev->rfL.fdq_wr, iadev->reass_reg+FREEQ_WR_PTR);  
}  
  
  
static int rx_pkt(struct atm_dev *dev)  
{  
	IADEV *iadev;  
	struct atm_vcc *vcc;  
	unsigned short status;  
	struct rx_buf_desc __iomem *buf_desc_ptr;  
	int desc;   
	struct dle* wr_ptr;  
	int len;  
	struct sk_buff *skb;  
	u_int buf_addr, dma_addr;  

	iadev = INPH_IA_DEV(dev);  
	if (iadev->rfL.pcq_rd == (readw(iadev->reass_reg+PCQ_WR_PTR)&0xffff)) 
	{  
   	    printk(KERN_ERR DEV_LABEL "(itf %d) Receive queue empty\n", dev->number);  
	    return -EINVAL;  
	}  
	/* mask 1st 3 bits to get the actual descno. */  
	desc = readw(iadev->reass_ram+iadev->rfL.pcq_rd) & 0x1fff;  
        IF_RX(printk("reass_ram = %p iadev->rfL.pcq_rd = 0x%x desc = %d\n", 
                                    iadev->reass_ram, iadev->rfL.pcq_rd, desc);
              printk(" pcq_wr_ptr = 0x%x\n",
                               readw(iadev->reass_reg+PCQ_WR_PTR)&0xffff);)
	/* update the read pointer  - maybe we shud do this in the end*/  
	if ( iadev->rfL.pcq_rd== iadev->rfL.pcq_ed) 
		iadev->rfL.pcq_rd = iadev->rfL.pcq_st;  
	else  
		iadev->rfL.pcq_rd += 2;
	writew(iadev->rfL.pcq_rd, iadev->reass_reg+PCQ_RD_PTR);  
  
	/* get the buffer desc entry.  
		update stuff. - doesn't seem to be any update necessary  
	*/  
	buf_desc_ptr = iadev->RX_DESC_BASE_ADDR;
	/* make the ptr point to the corresponding buffer desc entry */  
	buf_desc_ptr += desc;	  
        if (!desc || (desc > iadev->num_rx_desc) || 
                      ((buf_desc_ptr->vc_index & 0xffff) > iadev->num_vc)) { 
            free_desc(dev, desc);
            IF_ERR(printk("IA: bad descriptor desc = %d \n", desc);)
            return -1;
        }
	vcc = iadev->rx_open[buf_desc_ptr->vc_index & 0xffff];  
	if (!vcc)  
	{      
                free_desc(dev, desc); 
		printk("IA: null vcc, drop PDU\n");  
		return -1;  
	}  
	  
  
	/* might want to check the status bits for errors */  
	status = (u_short) (buf_desc_ptr->desc_mode);  
	if (status & (RX_CER | RX_PTE | RX_OFL))  
	{  
                atomic_inc(&vcc->stats->rx_err);
		IF_ERR(printk("IA: bad packet, dropping it");)  
                if (status & RX_CER) { 
                    IF_ERR(printk(" cause: packet CRC error\n");)
                }
                else if (status & RX_PTE) {
                    IF_ERR(printk(" cause: packet time out\n");)
                }
                else {
                    IF_ERR(printk(" cause: buffer overflow\n");)
                }
		goto out_free_desc;
	}  
  
	/*  
		build DLE.	  
	*/  
  
	buf_addr = (buf_desc_ptr->buf_start_hi << 16) | buf_desc_ptr->buf_start_lo;  
	dma_addr = (buf_desc_ptr->dma_start_hi << 16) | buf_desc_ptr->dma_start_lo;  
	len = dma_addr - buf_addr;  
        if (len > iadev->rx_buf_sz) {
           printk("Over %d bytes sdu received, dropped!!!\n", iadev->rx_buf_sz);
           atomic_inc(&vcc->stats->rx_err);
	   goto out_free_desc;
        }
		  
        if (!(skb = atm_alloc_charge(vcc, len, GFP_ATOMIC))) {
           if (vcc->vci < 32)
              printk("Drop control packets\n");
	      goto out_free_desc;
        }
	skb_put(skb,len);  
        // pwang_test
        ATM_SKB(skb)->vcc = vcc;
        ATM_DESC(skb) = desc;        
	skb_queue_tail(&iadev->rx_dma_q, skb);  

	/* Build the DLE structure */  
	wr_ptr = iadev->rx_dle_q.write;  
	wr_ptr->sys_pkt_addr = dma_map_single(&iadev->pci->dev, skb->data,
					      len, DMA_FROM_DEVICE);
	wr_ptr->local_pkt_addr = buf_addr;  
	wr_ptr->bytes = len;	/* We don't know this do we ?? */  
	wr_ptr->mode = DMA_INT_ENABLE;  
  
	/* shud take care of wrap around here too. */  
        if(++wr_ptr == iadev->rx_dle_q.end)
             wr_ptr = iadev->rx_dle_q.start;
	iadev->rx_dle_q.write = wr_ptr;  
	udelay(1);  
	/* Increment transaction counter */  
	writel(1, iadev->dma+IPHASE5575_RX_COUNTER);   
out:	return 0;  
out_free_desc:
        free_desc(dev, desc);
        goto out;
}  
  
static void rx_intr(struct atm_dev *dev)  
{  
  IADEV *iadev;  
  u_short status;  
  u_short state, i;  
  
  iadev = INPH_IA_DEV(dev);  
  status = readl(iadev->reass_reg+REASS_INTR_STATUS_REG) & 0xffff;  
  IF_EVENT(printk("rx_intr: status = 0x%x\n", status);)
  if (status & RX_PKT_RCVD)  
  {  
	/* do something */  
	/* Basically recvd an interrupt for receiving a packet.  
	A descriptor would have been written to the packet complete   
	queue. Get all the descriptors and set up dma to move the   
	packets till the packet complete queue is empty..  
	*/  
	state = readl(iadev->reass_reg + STATE_REG) & 0xffff;  
        IF_EVENT(printk("Rx intr status: RX_PKT_RCVD %08x\n", status);) 
	while(!(state & PCQ_EMPTY))  
	{  
             rx_pkt(dev);  
	     state = readl(iadev->reass_reg + STATE_REG) & 0xffff;  
	}  
        iadev->rxing = 1;
  }  
  if (status & RX_FREEQ_EMPT)  
  {   
     if (iadev->rxing) {
        iadev->rx_tmp_cnt = iadev->rx_pkt_cnt;
        iadev->rx_tmp_jif = jiffies; 
        iadev->rxing = 0;
     } 
     else if ((time_after(jiffies, iadev->rx_tmp_jif + 50)) &&
               ((iadev->rx_pkt_cnt - iadev->rx_tmp_cnt) == 0)) {
        for (i = 1; i <= iadev->num_rx_desc; i++)
               free_desc(dev, i);
printk("Test logic RUN!!!!\n");
        writew( ~(RX_FREEQ_EMPT|RX_EXCP_RCVD),iadev->reass_reg+REASS_MASK_REG);
        iadev->rxing = 1;
     }
     IF_EVENT(printk("Rx intr status: RX_FREEQ_EMPT %08x\n", status);)  
  }  

  if (status & RX_EXCP_RCVD)  
  {  
	/* probably need to handle the exception queue also. */  
	IF_EVENT(printk("Rx intr status: RX_EXCP_RCVD %08x\n", status);)  
	rx_excp_rcvd(dev);  
  }  


  if (status & RX_RAW_RCVD)  
  {  
	/* need to handle the raw incoming cells. This deepnds on   
	whether we have programmed to receive the raw cells or not.  
	Else ignore. */  
	IF_EVENT(printk("Rx intr status:  RX_RAW_RCVD %08x\n", status);)  
  }  
}  
  
  
static void rx_dle_intr(struct atm_dev *dev)  
{  
  IADEV *iadev;  
  struct atm_vcc *vcc;   
  struct sk_buff *skb;  
  int desc;  
  u_short state;   
  struct dle *dle, *cur_dle;  
  u_int dle_lp;  
  int len;
  iadev = INPH_IA_DEV(dev);  
 
  /* free all the dles done, that is just update our own dle read pointer   
	- do we really need to do this. Think not. */  
  /* DMA is done, just get all the recevie buffers from the rx dma queue  
	and push them up to the higher layer protocol. Also free the desc  
	associated with the buffer. */  
  dle = iadev->rx_dle_q.read;  
  dle_lp = readl(iadev->dma+IPHASE5575_RX_LIST_ADDR) & (sizeof(struct dle)*DLE_ENTRIES - 1);  
  cur_dle = (struct dle*)(iadev->rx_dle_q.start + (dle_lp >> 4));  
  while(dle != cur_dle)  
  {  
      /* free the DMAed skb */  
      skb = skb_dequeue(&iadev->rx_dma_q);  
      if (!skb)  
         goto INCR_DLE;
      desc = ATM_DESC(skb);
      free_desc(dev, desc);  
               
      if (!(len = skb->len))
      {  
          printk("rx_dle_intr: skb len 0\n");  
	  dev_kfree_skb_any(skb);  
      }  
      else  
      {  
          struct cpcs_trailer *trailer;
          u_short length;
          struct ia_vcc *ia_vcc;

	  dma_unmap_single(&iadev->pci->dev, iadev->rx_dle_q.write->sys_pkt_addr,
			   len, DMA_FROM_DEVICE);
          /* no VCC related housekeeping done as yet. lets see */  
          vcc = ATM_SKB(skb)->vcc;
	  if (!vcc) {
	      printk("IA: null vcc\n");  
              dev_kfree_skb_any(skb);
              goto INCR_DLE;
          }
          ia_vcc = INPH_IA_VCC(vcc);
          if (ia_vcc == NULL)
          {
             atomic_inc(&vcc->stats->rx_err);
             atm_return(vcc, skb->truesize);
             dev_kfree_skb_any(skb);
             goto INCR_DLE;
           }
          // get real pkt length  pwang_test
          trailer = (struct cpcs_trailer*)((u_char *)skb->data +
                                 skb->len - sizeof(*trailer));
	  length = swap_byte_order(trailer->length);
          if ((length > iadev->rx_buf_sz) || (length > 
                              (skb->len - sizeof(struct cpcs_trailer))))
          {
             atomic_inc(&vcc->stats->rx_err);
             IF_ERR(printk("rx_dle_intr: Bad  AAL5 trailer %d (skb len %d)", 
                                                            length, skb->len);)
             atm_return(vcc, skb->truesize);
             dev_kfree_skb_any(skb);
             goto INCR_DLE;
          }
          skb_trim(skb, length);
          
	  /* Display the packet */  
	  IF_RXPKT(printk("\nDmad Recvd data: len = %d \n", skb->len);  
          xdump(skb->data, skb->len, "RX: ");
          printk("\n");)

	  IF_RX(printk("rx_dle_intr: skb push");)  
	  vcc->push(vcc,skb);  
	  atomic_inc(&vcc->stats->rx);
          iadev->rx_pkt_cnt++;
      }  
INCR_DLE:
      if (++dle == iadev->rx_dle_q.end)  
    	  dle = iadev->rx_dle_q.start;  
  }  
  iadev->rx_dle_q.read = dle;  
  
  /* if the interrupts are masked because there were no free desc available,  
		unmask them now. */ 
  if (!iadev->rxing) {
     state = readl(iadev->reass_reg + STATE_REG) & 0xffff;
     if (!(state & FREEQ_EMPTY)) {
        state = readl(iadev->reass_reg + REASS_MASK_REG) & 0xffff;
        writel(state & ~(RX_FREEQ_EMPT |/* RX_EXCP_RCVD |*/ RX_PKT_RCVD),
                                      iadev->reass_reg+REASS_MASK_REG);
        iadev->rxing++; 
     }
  }
}  
  
  
static int open_rx(struct atm_vcc *vcc)  
{  
	IADEV *iadev;  
	u_short __iomem *vc_table;  
	u_short __iomem *reass_ptr;  
	IF_EVENT(printk("iadev: open_rx %d.%d\n", vcc->vpi, vcc->vci);)

	if (vcc->qos.rxtp.traffic_class == ATM_NONE) return 0;    
	iadev = INPH_IA_DEV(vcc->dev);  
        if (vcc->qos.rxtp.traffic_class == ATM_ABR) {  
           if (iadev->phy_type & FE_25MBIT_PHY) {
               printk("IA:  ABR not support\n");
               return -EINVAL; 
           }
        }
	/* Make only this VCI in the vc table valid and let all   
		others be invalid entries */  
	vc_table = iadev->reass_ram+RX_VC_TABLE*iadev->memSize;
	vc_table += vcc->vci;
	/* mask the last 6 bits and OR it with 3 for 1K VCs */  

        *vc_table = vcc->vci << 6;
	/* Also keep a list of open rx vcs so that we can attach them with  
		incoming PDUs later. */  
	if ((vcc->qos.rxtp.traffic_class == ATM_ABR) || 
                                (vcc->qos.txtp.traffic_class == ATM_ABR))  
	{  
                srv_cls_param_t srv_p;
                init_abr_vc(iadev, &srv_p);
                ia_open_abr_vc(iadev, &srv_p, vcc, 0);
	} 
       	else {  /* for UBR  later may need to add CBR logic */
        	reass_ptr = iadev->reass_ram+REASS_TABLE*iadev->memSize;
           	reass_ptr += vcc->vci;
           	*reass_ptr = NO_AAL5_PKT;
       	}
	
	if (iadev->rx_open[vcc->vci])  
		printk(KERN_CRIT DEV_LABEL "(itf %d): VCI %d already open\n",  
			vcc->dev->number, vcc->vci);  
	iadev->rx_open[vcc->vci] = vcc;  
	return 0;  
}  
  
static int rx_init(struct atm_dev *dev)  
{  
	IADEV *iadev;  
	struct rx_buf_desc __iomem *buf_desc_ptr;  
	unsigned long rx_pkt_start = 0;  
	void *dle_addr;  
	struct abr_vc_table  *abr_vc_table; 
	u16 *vc_table;  
	u16 *reass_table;  
	int i,j, vcsize_sel;  
	u_short freeq_st_adr;  
	u_short *freeq_start;  
  
	iadev = INPH_IA_DEV(dev);  
  //    spin_lock_init(&iadev->rx_lock); 
  
	/* Allocate 4k bytes - more aligned than needed (4k boundary) */
	dle_addr = dma_alloc_coherent(&iadev->pci->dev, DLE_TOTAL_SIZE,
				      &iadev->rx_dle_dma, GFP_KERNEL);
	if (!dle_addr)  {  
		printk(KERN_ERR DEV_LABEL "can't allocate DLEs\n");
		goto err_out;
	}
	iadev->rx_dle_q.start = (struct dle *)dle_addr;
	iadev->rx_dle_q.read = iadev->rx_dle_q.start;  
	iadev->rx_dle_q.write = iadev->rx_dle_q.start;  
	iadev->rx_dle_q.end = (struct dle*)((unsigned long)dle_addr+sizeof(struct dle)*DLE_ENTRIES);
	/* the end of the dle q points to the entry after the last  
	DLE that can be used. */  
  
	/* write the upper 20 bits of the start address to rx list address register */  
	/* We know this is 32bit bus addressed so the following is safe */
	writel(iadev->rx_dle_dma & 0xfffff000,
	       iadev->dma + IPHASE5575_RX_LIST_ADDR);  
	IF_INIT(printk("Tx Dle list addr: 0x%p value: 0x%0x\n",
                      iadev->dma+IPHASE5575_TX_LIST_ADDR,
                      readl(iadev->dma + IPHASE5575_TX_LIST_ADDR));
	printk("Rx Dle list addr: 0x%p value: 0x%0x\n",
                      iadev->dma+IPHASE5575_RX_LIST_ADDR,
                      readl(iadev->dma + IPHASE5575_RX_LIST_ADDR));)
  
	writew(0xffff, iadev->reass_reg+REASS_MASK_REG);  
	writew(0, iadev->reass_reg+MODE_REG);  
	writew(RESET_REASS, iadev->reass_reg+REASS_COMMAND_REG);  
  
	/* Receive side control memory map  
	   -------------------------------  
  
		Buffer descr	0x0000 (736 - 23K)  
		VP Table	0x5c00 (256 - 512)  
		Except q	0x5e00 (128 - 512)  
		Free buffer q	0x6000 (1K - 2K)  
		Packet comp q	0x6800 (1K - 2K)  
		Reass Table	0x7000 (1K - 2K)  
		VC Table	0x7800 (1K - 2K)  
		ABR VC Table	0x8000 (1K - 32K)  
	*/  
	  
	/* Base address for Buffer Descriptor Table */  
	writew(RX_DESC_BASE >> 16, iadev->reass_reg+REASS_DESC_BASE);  
	/* Set the buffer size register */  
	writew(iadev->rx_buf_sz, iadev->reass_reg+BUF_SIZE);  
  
	/* Initialize each entry in the Buffer Descriptor Table */  
        iadev->RX_DESC_BASE_ADDR = iadev->reass_ram+RX_DESC_BASE*iadev->memSize;
	buf_desc_ptr = iadev->RX_DESC_BASE_ADDR;
	memset_io(buf_desc_ptr, 0, sizeof(*buf_desc_ptr));
	buf_desc_ptr++;  
	rx_pkt_start = iadev->rx_pkt_ram;  
	for(i=1; i<=iadev->num_rx_desc; i++)  
	{  
		memset_io(buf_desc_ptr, 0, sizeof(*buf_desc_ptr));  
		buf_desc_ptr->buf_start_hi = rx_pkt_start >> 16;  
		buf_desc_ptr->buf_start_lo = rx_pkt_start & 0x0000ffff;  
		buf_desc_ptr++;		  
		rx_pkt_start += iadev->rx_buf_sz;  
	}  
	IF_INIT(printk("Rx Buffer desc ptr: 0x%p\n", buf_desc_ptr);)
        i = FREE_BUF_DESC_Q*iadev->memSize; 
	writew(i >> 16,  iadev->reass_reg+REASS_QUEUE_BASE); 
        writew(i, iadev->reass_reg+FREEQ_ST_ADR);
        writew(i+iadev->num_rx_desc*sizeof(u_short), 
                                         iadev->reass_reg+FREEQ_ED_ADR);
        writew(i, iadev->reass_reg+FREEQ_RD_PTR);
        writew(i+iadev->num_rx_desc*sizeof(u_short), 
                                        iadev->reass_reg+FREEQ_WR_PTR);    
	/* Fill the FREEQ with all the free descriptors. */  
	freeq_st_adr = readw(iadev->reass_reg+FREEQ_ST_ADR);  
	freeq_start = (u_short *)(iadev->reass_ram+freeq_st_adr);  
	for(i=1; i<=iadev->num_rx_desc; i++)  
	{  
		*freeq_start = (u_short)i;  
		freeq_start++;  
	}  
	IF_INIT(printk("freeq_start: 0x%p\n", freeq_start);)
        /* Packet Complete Queue */
        i = (PKT_COMP_Q * iadev->memSize) & 0xffff;
        writew(i, iadev->reass_reg+PCQ_ST_ADR);
        writew(i+iadev->num_vc*sizeof(u_short), iadev->reass_reg+PCQ_ED_ADR);
        writew(i, iadev->reass_reg+PCQ_RD_PTR);
        writew(i, iadev->reass_reg+PCQ_WR_PTR);

        /* Exception Queue */
        i = (EXCEPTION_Q * iadev->memSize) & 0xffff;
        writew(i, iadev->reass_reg+EXCP_Q_ST_ADR);
        writew(i + NUM_RX_EXCP * sizeof(RX_ERROR_Q), 
                                             iadev->reass_reg+EXCP_Q_ED_ADR);
        writew(i, iadev->reass_reg+EXCP_Q_RD_PTR);
        writew(i, iadev->reass_reg+EXCP_Q_WR_PTR); 
 
    	/* Load local copy of FREEQ and PCQ ptrs */
        iadev->rfL.fdq_st = readw(iadev->reass_reg+FREEQ_ST_ADR) & 0xffff;
       	iadev->rfL.fdq_ed = readw(iadev->reass_reg+FREEQ_ED_ADR) & 0xffff ;
	iadev->rfL.fdq_rd = readw(iadev->reass_reg+FREEQ_RD_PTR) & 0xffff;
	iadev->rfL.fdq_wr = readw(iadev->reass_reg+FREEQ_WR_PTR) & 0xffff;
        iadev->rfL.pcq_st = readw(iadev->reass_reg+PCQ_ST_ADR) & 0xffff;
	iadev->rfL.pcq_ed = readw(iadev->reass_reg+PCQ_ED_ADR) & 0xffff;
	iadev->rfL.pcq_rd = readw(iadev->reass_reg+PCQ_RD_PTR) & 0xffff;
	iadev->rfL.pcq_wr = readw(iadev->reass_reg+PCQ_WR_PTR) & 0xffff;
	
        IF_INIT(printk("INIT:pcq_st:0x%x pcq_ed:0x%x pcq_rd:0x%x pcq_wr:0x%x", 
              iadev->rfL.pcq_st, iadev->rfL.pcq_ed, iadev->rfL.pcq_rd, 
              iadev->rfL.pcq_wr);)		  
	/* just for check - no VP TBL */  
	/* VP Table */  
	/* writew(0x0b80, iadev->reass_reg+VP_LKUP_BASE); */  
	/* initialize VP Table for invalid VPIs  
		- I guess we can write all 1s or 0x000f in the entire memory  
		  space or something similar.  
	*/  
  
	/* This seems to work and looks right to me too !!! */  
        i =  REASS_TABLE * iadev->memSize;
	writew((i >> 3), iadev->reass_reg+REASS_TABLE_BASE);   
 	/* initialize Reassembly table to I don't know what ???? */  
	reass_table = (u16 *)(iadev->reass_ram+i);  
        j = REASS_TABLE_SZ * iadev->memSize;
	for(i=0; i < j; i++)  
		*reass_table++ = NO_AAL5_PKT;  
       i = 8*1024;
       vcsize_sel =  0;
       while (i != iadev->num_vc) {
          i /= 2;
          vcsize_sel++;
       }
       i = RX_VC_TABLE * iadev->memSize;
       writew(((i>>3) & 0xfff8) | vcsize_sel, iadev->reass_reg+VC_LKUP_BASE);
       vc_table = (u16 *)(iadev->reass_ram+RX_VC_TABLE*iadev->memSize);  
        j = RX_VC_TABLE_SZ * iadev->memSize;
	for(i = 0; i < j; i++)  
	{  
		/* shift the reassembly pointer by 3 + lower 3 bits of   
		vc_lkup_base register (=3 for 1K VCs) and the last byte   
		is those low 3 bits.   
		Shall program this later.  
		*/  
		*vc_table = (i << 6) | 15;	/* for invalid VCI */  
		vc_table++;  
	}  
        /* ABR VC table */
        i =  ABR_VC_TABLE * iadev->memSize;
        writew(i >> 3, iadev->reass_reg+ABR_LKUP_BASE);
                   
        i = ABR_VC_TABLE * iadev->memSize;
	abr_vc_table = (struct abr_vc_table *)(iadev->reass_ram+i);  
        j = REASS_TABLE_SZ * iadev->memSize;
        memset ((char*)abr_vc_table, 0, j * sizeof(*abr_vc_table));
    	for(i = 0; i < j; i++) {   		
		abr_vc_table->rdf = 0x0003;
             	abr_vc_table->air = 0x5eb1;
	       	abr_vc_table++;   	
        }  

	/* Initialize other registers */  
  
	/* VP Filter Register set for VC Reassembly only */  
	writew(0xff00, iadev->reass_reg+VP_FILTER);  
        writew(0, iadev->reass_reg+XTRA_RM_OFFSET);
	writew(0x1,  iadev->reass_reg+PROTOCOL_ID);

	/* Packet Timeout Count  related Registers : 
	   Set packet timeout to occur in about 3 seconds
	   Set Packet Aging Interval count register to overflow in about 4 us
 	*/  
        writew(0xF6F8, iadev->reass_reg+PKT_TM_CNT );

        i = (j >> 6) & 0xFF;
        j += 2 * (j - 1);
        i |= ((j << 2) & 0xFF00);
        writew(i, iadev->reass_reg+TMOUT_RANGE);

        /* initiate the desc_tble */
        for(i=0; i<iadev->num_tx_desc;i++)
            iadev->desc_tbl[i].timestamp = 0;

	/* to clear the interrupt status register - read it */  
	readw(iadev->reass_reg+REASS_INTR_STATUS_REG);   
  
	/* Mask Register - clear it */  
	writew(~(RX_FREEQ_EMPT|RX_PKT_RCVD), iadev->reass_reg+REASS_MASK_REG);  
  
	skb_queue_head_init(&iadev->rx_dma_q);  
	iadev->rx_free_desc_qhead = NULL;   

	iadev->rx_open = kzalloc(4 * iadev->num_vc, GFP_KERNEL);
	if (!iadev->rx_open) {
		printk(KERN_ERR DEV_LABEL "itf %d couldn't get free page\n",
		dev->number);  
		goto err_free_dle;
	}  

        iadev->rxing = 1;
        iadev->rx_pkt_cnt = 0;
	/* Mode Register */  
	writew(R_ONLINE, iadev->reass_reg+MODE_REG);  
	return 0;  

err_free_dle:
	dma_free_coherent(&iadev->pci->dev, DLE_TOTAL_SIZE, iadev->rx_dle_q.start,
			  iadev->rx_dle_dma);
err_out:
	return -ENOMEM;
}  
  

/*  
	The memory map suggested in appendix A and the coding for it.   
	Keeping it around just in case we change our mind later.  
  
		Buffer descr	0x0000 (128 - 4K)  
		UBR sched	0x1000 (1K - 4K)  
		UBR Wait q	0x2000 (1K - 4K)  
		Commn queues	0x3000 Packet Ready, Trasmit comp(0x3100)  
					(128 - 256) each  
		extended VC	0x4000 (1K - 8K)  
		ABR sched	0x6000	and ABR wait queue (1K - 2K) each  
		CBR sched	0x7000 (as needed)  
		VC table	0x8000 (1K - 32K)  
*/  
  
static void tx_intr(struct atm_dev *dev)  
{  
	IADEV *iadev;  
	unsigned short status;  
        unsigned long flags;

	iadev = INPH_IA_DEV(dev);  
  
	status = readl(iadev->seg_reg+SEG_INTR_STATUS_REG);  
        if (status & TRANSMIT_DONE){

           IF_EVENT(printk("Tansmit Done Intr logic run\n");)
           spin_lock_irqsave(&iadev->tx_lock, flags);
           ia_tx_poll(iadev);
           spin_unlock_irqrestore(&iadev->tx_lock, flags);
           writew(TRANSMIT_DONE, iadev->seg_reg+SEG_INTR_STATUS_REG);
           if (iadev->close_pending)  
               wake_up(&iadev->close_wait);
        }     	  
	if (status & TCQ_NOT_EMPTY)  
	{  
	    IF_EVENT(printk("TCQ_NOT_EMPTY int received\n");)  
	}  
}  
  
static void tx_dle_intr(struct atm_dev *dev)
{
        IADEV *iadev;
        struct dle *dle, *cur_dle; 
        struct sk_buff *skb;
        struct atm_vcc *vcc;
        struct ia_vcc  *iavcc;
        u_int dle_lp;
        unsigned long flags;

        iadev = INPH_IA_DEV(dev);
        spin_lock_irqsave(&iadev->tx_lock, flags);   
        dle = iadev->tx_dle_q.read;
        dle_lp = readl(iadev->dma+IPHASE5575_TX_LIST_ADDR) & 
                                        (sizeof(struct dle)*DLE_ENTRIES - 1);
        cur_dle = (struct dle*)(iadev->tx_dle_q.start + (dle_lp >> 4));
        while (dle != cur_dle)
        {
            /* free the DMAed skb */ 
            skb = skb_dequeue(&iadev->tx_dma_q); 
            if (!skb) break;

	    /* Revenge of the 2 dle (skb + trailer) used in ia_pkt_tx() */
	    if (!((dle - iadev->tx_dle_q.start)%(2*sizeof(struct dle)))) {
		dma_unmap_single(&iadev->pci->dev, dle->sys_pkt_addr, skb->len,
				 DMA_TO_DEVICE);
	    }
            vcc = ATM_SKB(skb)->vcc;
            if (!vcc) {
                  printk("tx_dle_intr: vcc is null\n");
		  spin_unlock_irqrestore(&iadev->tx_lock, flags);
                  dev_kfree_skb_any(skb);

                  return;
            }
            iavcc = INPH_IA_VCC(vcc);
            if (!iavcc) {
                  printk("tx_dle_intr: iavcc is null\n");
		  spin_unlock_irqrestore(&iadev->tx_lock, flags);
                  dev_kfree_skb_any(skb);
                  return;
            }
            if (vcc->qos.txtp.pcr >= iadev->rate_limit) {
               if ((vcc->pop) && (skb->len != 0))
               {     
                 vcc->pop(vcc, skb);
               } 
               else {
                 dev_kfree_skb_any(skb);
               }
            }
            else { /* Hold the rate-limited skb for flow control */
               IA_SKB_STATE(skb) |= IA_DLED;
               skb_queue_tail(&iavcc->txing_skb, skb);
            }
            IF_EVENT(printk("tx_dle_intr: enque skb = 0x%p \n", skb);)
            if (++dle == iadev->tx_dle_q.end)
                 dle = iadev->tx_dle_q.start;
        }
        iadev->tx_dle_q.read = dle;
        spin_unlock_irqrestore(&iadev->tx_lock, flags);
}
  
static int open_tx(struct atm_vcc *vcc)  
{  
	struct ia_vcc *ia_vcc;  
	IADEV *iadev;  
	struct main_vc *vc;  
	struct ext_vc *evc;  
        int ret;
	IF_EVENT(printk("iadev: open_tx entered vcc->vci = %d\n", vcc->vci);)  
	if (vcc->qos.txtp.traffic_class == ATM_NONE) return 0;  
	iadev = INPH_IA_DEV(vcc->dev);  
        
        if (iadev->phy_type & FE_25MBIT_PHY) {
           if (vcc->qos.txtp.traffic_class == ATM_ABR) {
               printk("IA:  ABR not support\n");
               return -EINVAL; 
           }
	  if (vcc->qos.txtp.traffic_class == ATM_CBR) {
               printk("IA:  CBR not support\n");
               return -EINVAL; 
          }
        }
        ia_vcc =  INPH_IA_VCC(vcc);
        memset((caddr_t)ia_vcc, 0, sizeof(*ia_vcc));
        if (vcc->qos.txtp.max_sdu > 
                         (iadev->tx_buf_sz - sizeof(struct cpcs_trailer))){
           printk("IA:  SDU size over (%d) the configured SDU size %d\n",
		  vcc->qos.txtp.max_sdu,iadev->tx_buf_sz);
	   vcc->dev_data = NULL;
           kfree(ia_vcc);
           return -EINVAL; 
        }
	ia_vcc->vc_desc_cnt = 0;
        ia_vcc->txing = 1;

        /* find pcr */
        if (vcc->qos.txtp.max_pcr == ATM_MAX_PCR) 
           vcc->qos.txtp.pcr = iadev->LineRate;
        else if ((vcc->qos.txtp.max_pcr == 0)&&( vcc->qos.txtp.pcr <= 0))
           vcc->qos.txtp.pcr = iadev->LineRate;
        else if ((vcc->qos.txtp.max_pcr > vcc->qos.txtp.pcr) && (vcc->qos.txtp.max_pcr> 0)) 
           vcc->qos.txtp.pcr = vcc->qos.txtp.max_pcr;
        if (vcc->qos.txtp.pcr > iadev->LineRate)
             vcc->qos.txtp.pcr = iadev->LineRate;
        ia_vcc->pcr = vcc->qos.txtp.pcr;

        if (ia_vcc->pcr > (iadev->LineRate / 6) ) ia_vcc->ltimeout = HZ / 10;
        else if (ia_vcc->pcr > (iadev->LineRate / 130)) ia_vcc->ltimeout = HZ;
        else if (ia_vcc->pcr <= 170) ia_vcc->ltimeout = 16 * HZ;
        else ia_vcc->ltimeout = 2700 * HZ  / ia_vcc->pcr;
        if (ia_vcc->pcr < iadev->rate_limit)
           skb_queue_head_init (&ia_vcc->txing_skb);
        if (ia_vcc->pcr < iadev->rate_limit) {
	   struct sock *sk = sk_atm(vcc);

	   if (vcc->qos.txtp.max_sdu != 0) {
               if (ia_vcc->pcr > 60000)
                  sk->sk_sndbuf = vcc->qos.txtp.max_sdu * 5;
               else if (ia_vcc->pcr > 2000)
                  sk->sk_sndbuf = vcc->qos.txtp.max_sdu * 4;
               else
                 sk->sk_sndbuf = vcc->qos.txtp.max_sdu * 3;
           }
           else
             sk->sk_sndbuf = 24576;
        }
           
	vc = (struct main_vc *)iadev->MAIN_VC_TABLE_ADDR;  
	evc = (struct ext_vc *)iadev->EXT_VC_TABLE_ADDR;  
	vc += vcc->vci;  
	evc += vcc->vci;  
	memset((caddr_t)vc, 0, sizeof(*vc));  
	memset((caddr_t)evc, 0, sizeof(*evc));  
	  
	/* store the most significant 4 bits of vci as the last 4 bits   
		of first part of atm header.  
	   store the last 12 bits of vci as first 12 bits of the second  
		part of the atm header.  
	*/  
	evc->atm_hdr1 = (vcc->vci >> 12) & 0x000f;  
	evc->atm_hdr2 = (vcc->vci & 0x0fff) << 4;  
 
	/* check the following for different traffic classes */  
	if (vcc->qos.txtp.traffic_class == ATM_UBR)  
	{  
		vc->type = UBR;  
                vc->status = CRC_APPEND;
		vc->acr = cellrate_to_float(iadev->LineRate);  
                if (vcc->qos.txtp.pcr > 0) 
                   vc->acr = cellrate_to_float(vcc->qos.txtp.pcr);  
                IF_UBR(printk("UBR: txtp.pcr = 0x%x f_rate = 0x%x\n", 
                                             vcc->qos.txtp.max_pcr,vc->acr);)
	}  
	else if (vcc->qos.txtp.traffic_class == ATM_ABR)  
	{       srv_cls_param_t srv_p;
		IF_ABR(printk("Tx ABR VCC\n");)  
                init_abr_vc(iadev, &srv_p);
                if (vcc->qos.txtp.pcr > 0) 
                   srv_p.pcr = vcc->qos.txtp.pcr;
                if (vcc->qos.txtp.min_pcr > 0) {
                   int tmpsum = iadev->sum_mcr+iadev->sum_cbr+vcc->qos.txtp.min_pcr;
                   if (tmpsum > iadev->LineRate)
                       return -EBUSY;
                   srv_p.mcr = vcc->qos.txtp.min_pcr;
                   iadev->sum_mcr += vcc->qos.txtp.min_pcr;
                } 
                else srv_p.mcr = 0;
                if (vcc->qos.txtp.icr)
                   srv_p.icr = vcc->qos.txtp.icr;
                if (vcc->qos.txtp.tbe)
                   srv_p.tbe = vcc->qos.txtp.tbe;
                if (vcc->qos.txtp.frtt)
                   srv_p.frtt = vcc->qos.txtp.frtt;
                if (vcc->qos.txtp.rif)
                   srv_p.rif = vcc->qos.txtp.rif;
                if (vcc->qos.txtp.rdf)
                   srv_p.rdf = vcc->qos.txtp.rdf;
                if (vcc->qos.txtp.nrm_pres)
                   srv_p.nrm = vcc->qos.txtp.nrm;
                if (vcc->qos.txtp.trm_pres)
                   srv_p.trm = vcc->qos.txtp.trm;
                if (vcc->qos.txtp.adtf_pres)
                   srv_p.adtf = vcc->qos.txtp.adtf;
                if (vcc->qos.txtp.cdf_pres)
                   srv_p.cdf = vcc->qos.txtp.cdf;    
                if (srv_p.icr > srv_p.pcr)
                   srv_p.icr = srv_p.pcr;    
                IF_ABR(printk("ABR:vcc->qos.txtp.max_pcr = %d  mcr = %d\n", 
                                                      srv_p.pcr, srv_p.mcr);)
		ia_open_abr_vc(iadev, &srv_p, vcc, 1);
	} else if (vcc->qos.txtp.traffic_class == ATM_CBR) {
                if (iadev->phy_type & FE_25MBIT_PHY) {
                    printk("IA:  CBR not support\n");
                    return -EINVAL; 
                }
                if (vcc->qos.txtp.max_pcr > iadev->LineRate) {
                   IF_CBR(printk("PCR is not available\n");)
                   return -1;
                }
                vc->type = CBR;
                vc->status = CRC_APPEND;
                if ((ret = ia_cbr_setup (iadev, vcc)) < 0) {     
                    return ret;
                }
       } 
	else  
           printk("iadev:  Non UBR, ABR and CBR traffic not supportedn"); 
        
        iadev->testTable[vcc->vci]->vc_status |= VC_ACTIVE;
	IF_EVENT(printk("ia open_tx returning \n");)  
	return 0;  
}  
  
  
static int tx_init(struct atm_dev *dev)  
{  
	IADEV *iadev;  
	struct tx_buf_desc *buf_desc_ptr;
	unsigned int tx_pkt_start;  
	void *dle_addr;  
	int i;  
	u_short tcq_st_adr;  
	u_short *tcq_start;  
	u_short prq_st_adr;  
	u_short *prq_start;  
	struct main_vc *vc;  
	struct ext_vc *evc;   
        u_short tmp16;
        u32 vcsize_sel;
 
	iadev = INPH_IA_DEV(dev);  
        spin_lock_init(&iadev->tx_lock);
 
	IF_INIT(printk("Tx MASK REG: 0x%0x\n", 
                                readw(iadev->seg_reg+SEG_MASK_REG));)  

	/* Allocate 4k (boundary aligned) bytes */
	dle_addr = dma_alloc_coherent(&iadev->pci->dev, DLE_TOTAL_SIZE,
				      &iadev->tx_dle_dma, GFP_KERNEL);
	if (!dle_addr)  {
		printk(KERN_ERR DEV_LABEL "can't allocate DLEs\n");
		goto err_out;
	}
	iadev->tx_dle_q.start = (struct dle*)dle_addr;  
	iadev->tx_dle_q.read = iadev->tx_dle_q.start;  
	iadev->tx_dle_q.write = iadev->tx_dle_q.start;  
	iadev->tx_dle_q.end = (struct dle*)((unsigned long)dle_addr+sizeof(struct dle)*DLE_ENTRIES);

	/* write the upper 20 bits of the start address to tx list address register */  
	writel(iadev->tx_dle_dma & 0xfffff000,
	       iadev->dma + IPHASE5575_TX_LIST_ADDR);  
	writew(0xffff, iadev->seg_reg+SEG_MASK_REG);  
	writew(0, iadev->seg_reg+MODE_REG_0);  
	writew(RESET_SEG, iadev->seg_reg+SEG_COMMAND_REG);  
        iadev->MAIN_VC_TABLE_ADDR = iadev->seg_ram+MAIN_VC_TABLE*iadev->memSize;
        iadev->EXT_VC_TABLE_ADDR = iadev->seg_ram+EXT_VC_TABLE*iadev->memSize;
        iadev->ABR_SCHED_TABLE_ADDR=iadev->seg_ram+ABR_SCHED_TABLE*iadev->memSize;
  
	/*  
	   Transmit side control memory map  
	   --------------------------------    
	 Buffer descr 	0x0000 (128 - 4K)  
	 Commn queues	0x1000	Transmit comp, Packet ready(0x1400)   
					(512 - 1K) each  
					TCQ - 4K, PRQ - 5K  
	 CBR Table 	0x1800 (as needed) - 6K  
	 UBR Table	0x3000 (1K - 4K) - 12K  
	 UBR Wait queue	0x4000 (1K - 4K) - 16K  
	 ABR sched	0x5000	and ABR wait queue (1K - 2K) each  
				ABR Tbl - 20K, ABR Wq - 22K   
	 extended VC	0x6000 (1K - 8K) - 24K  
	 VC Table	0x8000 (1K - 32K) - 32K  
	  
	Between 0x2000 (8K) and 0x3000 (12K) there is 4K space left for VBR Tbl  
	and Wait q, which can be allotted later.  
	*/  
     
	/* Buffer Descriptor Table Base address */  
	writew(TX_DESC_BASE, iadev->seg_reg+SEG_DESC_BASE);  
  
	/* initialize each entry in the buffer descriptor table */  
	buf_desc_ptr =(struct tx_buf_desc *)(iadev->seg_ram+TX_DESC_BASE);  
	memset((caddr_t)buf_desc_ptr, 0, sizeof(*buf_desc_ptr));  
	buf_desc_ptr++;  
	tx_pkt_start = TX_PACKET_RAM;  
	for(i=1; i<=iadev->num_tx_desc; i++)  
	{  
		memset((caddr_t)buf_desc_ptr, 0, sizeof(*buf_desc_ptr));  
		buf_desc_ptr->desc_mode = AAL5;  
		buf_desc_ptr->buf_start_hi = tx_pkt_start >> 16;  
		buf_desc_ptr->buf_start_lo = tx_pkt_start & 0x0000ffff;  
		buf_desc_ptr++;		  
		tx_pkt_start += iadev->tx_buf_sz;  
	}  
        iadev->tx_buf = kmalloc(iadev->num_tx_desc*sizeof(struct cpcs_trailer_desc), GFP_KERNEL);
        if (!iadev->tx_buf) {
            printk(KERN_ERR DEV_LABEL " couldn't get mem\n");
	    goto err_free_dle;
        }
       	for (i= 0; i< iadev->num_tx_desc; i++)
       	{
	    struct cpcs_trailer *cpcs;
 
       	    cpcs = kmalloc(sizeof(*cpcs), GFP_KERNEL|GFP_DMA);
            if(!cpcs) {                
		printk(KERN_ERR DEV_LABEL " couldn't get freepage\n"); 
		goto err_free_tx_bufs;
            }
	    iadev->tx_buf[i].cpcs = cpcs;
	    iadev->tx_buf[i].dma_addr = dma_map_single(&iadev->pci->dev,
						       cpcs,
						       sizeof(*cpcs),
						       DMA_TO_DEVICE);
        }
        iadev->desc_tbl = kmalloc(iadev->num_tx_desc *
                                   sizeof(struct desc_tbl_t), GFP_KERNEL);
	if (!iadev->desc_tbl) {
		printk(KERN_ERR DEV_LABEL " couldn't get mem\n");
		goto err_free_all_tx_bufs;
	}
  
	/* Communication Queues base address */  
        i = TX_COMP_Q * iadev->memSize;
	writew(i >> 16, iadev->seg_reg+SEG_QUEUE_BASE);  
  
	/* Transmit Complete Queue */  
	writew(i, iadev->seg_reg+TCQ_ST_ADR);  
	writew(i, iadev->seg_reg+TCQ_RD_PTR);  
	writew(i+iadev->num_tx_desc*sizeof(u_short),iadev->seg_reg+TCQ_WR_PTR); 
	iadev->host_tcq_wr = i + iadev->num_tx_desc*sizeof(u_short);
        writew(i+2 * iadev->num_tx_desc * sizeof(u_short), 
                                              iadev->seg_reg+TCQ_ED_ADR); 
	/* Fill the TCQ with all the free descriptors. */  
	tcq_st_adr = readw(iadev->seg_reg+TCQ_ST_ADR);  
	tcq_start = (u_short *)(iadev->seg_ram+tcq_st_adr);  
	for(i=1; i<=iadev->num_tx_desc; i++)  
	{  
		*tcq_start = (u_short)i;  
		tcq_start++;  
	}  
  
	/* Packet Ready Queue */  
        i = PKT_RDY_Q * iadev->memSize; 
	writew(i, iadev->seg_reg+PRQ_ST_ADR);  
	writew(i+2 * iadev->num_tx_desc * sizeof(u_short), 
                                              iadev->seg_reg+PRQ_ED_ADR);
	writew(i, iadev->seg_reg+PRQ_RD_PTR);  
	writew(i, iadev->seg_reg+PRQ_WR_PTR);  
	 
        /* Load local copy of PRQ and TCQ ptrs */
        iadev->ffL.prq_st = readw(iadev->seg_reg+PRQ_ST_ADR) & 0xffff;
	iadev->ffL.prq_ed = readw(iadev->seg_reg+PRQ_ED_ADR) & 0xffff;
 	iadev->ffL.prq_wr = readw(iadev->seg_reg+PRQ_WR_PTR) & 0xffff;

	iadev->ffL.tcq_st = readw(iadev->seg_reg+TCQ_ST_ADR) & 0xffff;
	iadev->ffL.tcq_ed = readw(iadev->seg_reg+TCQ_ED_ADR) & 0xffff;
	iadev->ffL.tcq_rd = readw(iadev->seg_reg+TCQ_RD_PTR) & 0xffff;

	/* Just for safety initializing the queue to have desc 1 always */  
	/* Fill the PRQ with all the free descriptors. */  
	prq_st_adr = readw(iadev->seg_reg+PRQ_ST_ADR);  
	prq_start = (u_short *)(iadev->seg_ram+prq_st_adr);  
	for(i=1; i<=iadev->num_tx_desc; i++)  
	{  
		*prq_start = (u_short)0;	/* desc 1 in all entries */  
		prq_start++;  
	}  
	/* CBR Table */  
        IF_INIT(printk("Start CBR Init\n");)
#if 1  /* for 1K VC board, CBR_PTR_BASE is 0 */
        writew(0,iadev->seg_reg+CBR_PTR_BASE);
#else /* Charlie's logic is wrong ? */
        tmp16 = (iadev->seg_ram+CBR_SCHED_TABLE*iadev->memSize)>>17;
        IF_INIT(printk("cbr_ptr_base = 0x%x ", tmp16);)
        writew(tmp16,iadev->seg_reg+CBR_PTR_BASE);
#endif

        IF_INIT(printk("value in register = 0x%x\n",
                                   readw(iadev->seg_reg+CBR_PTR_BASE));)
        tmp16 = (CBR_SCHED_TABLE*iadev->memSize) >> 1;
        writew(tmp16, iadev->seg_reg+CBR_TAB_BEG);
        IF_INIT(printk("cbr_tab_beg = 0x%x in reg = 0x%x \n", tmp16,
                                        readw(iadev->seg_reg+CBR_TAB_BEG));)
        writew(tmp16, iadev->seg_reg+CBR_TAB_END+1); // CBR_PTR;
        tmp16 = (CBR_SCHED_TABLE*iadev->memSize + iadev->num_vc*6 - 2) >> 1;
        writew(tmp16, iadev->seg_reg+CBR_TAB_END);
        IF_INIT(printk("iadev->seg_reg = 0x%p CBR_PTR_BASE = 0x%x\n",
               iadev->seg_reg, readw(iadev->seg_reg+CBR_PTR_BASE));)
        IF_INIT(printk("CBR_TAB_BEG = 0x%x, CBR_TAB_END = 0x%x, CBR_PTR = 0x%x\n",
          readw(iadev->seg_reg+CBR_TAB_BEG), readw(iadev->seg_reg+CBR_TAB_END),
          readw(iadev->seg_reg+CBR_TAB_END+1));)

        /* Initialize the CBR Schedualing Table */
        memset_io(iadev->seg_ram+CBR_SCHED_TABLE*iadev->memSize, 
                                                          0, iadev->num_vc*6); 
        iadev->CbrRemEntries = iadev->CbrTotEntries = iadev->num_vc*3;
        iadev->CbrEntryPt = 0;
        iadev->Granularity = MAX_ATM_155 / iadev->CbrTotEntries;
        iadev->NumEnabledCBR = 0;

	/* UBR scheduling Table and wait queue */  
	/* initialize all bytes of UBR scheduler table and wait queue to 0   
		- SCHEDSZ is 1K (# of entries).  
		- UBR Table size is 4K  
		- UBR wait queue is 4K  
	   since the table and wait queues are contiguous, all the bytes   
	   can be initialized by one memeset.
	*/  
        
        vcsize_sel = 0;
        i = 8*1024;
        while (i != iadev->num_vc) {
          i /= 2;
          vcsize_sel++;
        }
 
        i = MAIN_VC_TABLE * iadev->memSize;
        writew(vcsize_sel | ((i >> 8) & 0xfff8),iadev->seg_reg+VCT_BASE);
        i =  EXT_VC_TABLE * iadev->memSize;
        writew((i >> 8) & 0xfffe, iadev->seg_reg+VCTE_BASE);
        i = UBR_SCHED_TABLE * iadev->memSize;
        writew((i & 0xffff) >> 11,  iadev->seg_reg+UBR_SBPTR_BASE);
        i = UBR_WAIT_Q * iadev->memSize; 
        writew((i >> 7) & 0xffff,  iadev->seg_reg+UBRWQ_BASE);
 	memset((caddr_t)(iadev->seg_ram+UBR_SCHED_TABLE*iadev->memSize),
                                                       0, iadev->num_vc*8);
	/* ABR scheduling Table(0x5000-0x57ff) and wait queue(0x5800-0x5fff)*/  
	/* initialize all bytes of ABR scheduler table and wait queue to 0   
		- SCHEDSZ is 1K (# of entries).  
		- ABR Table size is 2K  
		- ABR wait queue is 2K  
	   since the table and wait queues are contiguous, all the bytes   
	   can be initialized by one memeset.
	*/  
        i = ABR_SCHED_TABLE * iadev->memSize;
        writew((i >> 11) & 0xffff, iadev->seg_reg+ABR_SBPTR_BASE);
        i = ABR_WAIT_Q * iadev->memSize;
        writew((i >> 7) & 0xffff, iadev->seg_reg+ABRWQ_BASE);
 
        i = ABR_SCHED_TABLE*iadev->memSize;
	memset((caddr_t)(iadev->seg_ram+i),  0, iadev->num_vc*4);
	vc = (struct main_vc *)iadev->MAIN_VC_TABLE_ADDR;  
	evc = (struct ext_vc *)iadev->EXT_VC_TABLE_ADDR;  
        iadev->testTable = kmalloc(sizeof(long)*iadev->num_vc, GFP_KERNEL); 
        if (!iadev->testTable) {
           printk("Get freepage  failed\n");
	   goto err_free_desc_tbl;
        }
	for(i=0; i<iadev->num_vc; i++)  
	{  
		memset((caddr_t)vc, 0, sizeof(*vc));  
		memset((caddr_t)evc, 0, sizeof(*evc));  
                iadev->testTable[i] = kmalloc(sizeof(struct testTable_t),
						GFP_KERNEL);
		if (!iadev->testTable[i])
			goto err_free_test_tables;
              	iadev->testTable[i]->lastTime = 0;
 		iadev->testTable[i]->fract = 0;
                iadev->testTable[i]->vc_status = VC_UBR;
		vc++;  
		evc++;  
	}  
  
	/* Other Initialization */  
	  
	/* Max Rate Register */  
        if (iadev->phy_type & FE_25MBIT_PHY) {
	   writew(RATE25, iadev->seg_reg+MAXRATE);  
	   writew((UBR_EN | (0x23 << 2)), iadev->seg_reg+STPARMS);  
        }
        else {
	   writew(cellrate_to_float(iadev->LineRate),iadev->seg_reg+MAXRATE);
	   writew((UBR_EN | ABR_EN | (0x23 << 2)), iadev->seg_reg+STPARMS);  
        }
	/* Set Idle Header Reigisters to be sure */  
	writew(0, iadev->seg_reg+IDLEHEADHI);  
	writew(0, iadev->seg_reg+IDLEHEADLO);  
  
	/* Program ABR UBR Priority Register  as  PRI_ABR_UBR_EQUAL */
        writew(0xaa00, iadev->seg_reg+ABRUBR_ARB); 

        iadev->close_pending = 0;
        init_waitqueue_head(&iadev->close_wait);
        init_waitqueue_head(&iadev->timeout_wait);
	skb_queue_head_init(&iadev->tx_dma_q);  
	ia_init_rtn_q(&iadev->tx_return_q);  

	/* RM Cell Protocol ID and Message Type */  
	writew(RM_TYPE_4_0, iadev->seg_reg+RM_TYPE);  
        skb_queue_head_init (&iadev->tx_backlog);
  
	/* Mode Register 1 */  
	writew(MODE_REG_1_VAL, iadev->seg_reg+MODE_REG_1);  
  
	/* Mode Register 0 */  
	writew(T_ONLINE, iadev->seg_reg+MODE_REG_0);  
  
	/* Interrupt Status Register - read to clear */  
	readw(iadev->seg_reg+SEG_INTR_STATUS_REG);  
  
	/* Interrupt Mask Reg- don't mask TCQ_NOT_EMPTY interrupt generation */  
        writew(~(TRANSMIT_DONE | TCQ_NOT_EMPTY), iadev->seg_reg+SEG_MASK_REG);
        writew(TRANSMIT_DONE, iadev->seg_reg+SEG_INTR_STATUS_REG);  
        iadev->tx_pkt_cnt = 0;
        iadev->rate_limit = iadev->LineRate / 3;
  
	return 0;

err_free_test_tables:
	while (--i >= 0)
		kfree(iadev->testTable[i]);
	kfree(iadev->testTable);
err_free_desc_tbl:
	kfree(iadev->desc_tbl);
err_free_all_tx_bufs:
	i = iadev->num_tx_desc;
err_free_tx_bufs:
	while (--i >= 0) {
		struct cpcs_trailer_desc *desc = iadev->tx_buf + i;

		dma_unmap_single(&iadev->pci->dev, desc->dma_addr,
				 sizeof(*desc->cpcs), DMA_TO_DEVICE);
		kfree(desc->cpcs);
	}
	kfree(iadev->tx_buf);
err_free_dle:
	dma_free_coherent(&iadev->pci->dev, DLE_TOTAL_SIZE, iadev->tx_dle_q.start,
			  iadev->tx_dle_dma);
err_out:
	return -ENOMEM;
}   
   
static irqreturn_t ia_int(int irq, void *dev_id)  
{  
   struct atm_dev *dev;  
   IADEV *iadev;  
   unsigned int status;  
   int handled = 0;

   dev = dev_id;  
   iadev = INPH_IA_DEV(dev);  
   while( (status = readl(iadev->reg+IPHASE5575_BUS_STATUS_REG) & 0x7f))  
   { 
	handled = 1;
        IF_EVENT(printk("ia_int: status = 0x%x\n", status);) 
	if (status & STAT_REASSINT)  
	{  
	   /* do something */  
	   IF_EVENT(printk("REASSINT Bus status reg: %08x\n", status);) 
	   rx_intr(dev);  
	}  
	if (status & STAT_DLERINT)  
	{  
	   /* Clear this bit by writing a 1 to it. */  
	   writel(STAT_DLERINT, iadev->reg + IPHASE5575_BUS_STATUS_REG);
	   rx_dle_intr(dev);  
	}  
	if (status & STAT_SEGINT)  
	{  
	   /* do something */ 
           IF_EVENT(printk("IA: tx_intr \n");) 
	   tx_intr(dev);  
	}  
	if (status & STAT_DLETINT)  
	{  
	   writel(STAT_DLETINT, iadev->reg + IPHASE5575_BUS_STATUS_REG);
	   tx_dle_intr(dev);  
	}  
	if (status & (STAT_FEINT | STAT_ERRINT | STAT_MARKINT))  
	{  
           if (status & STAT_FEINT) 
               ia_frontend_intr(iadev);
	}  
   }
   return IRQ_RETVAL(handled);
}  
	  
	  
	  
/*----------------------------- entries --------------------------------*/  
static int get_esi(struct atm_dev *dev)  
{  
	IADEV *iadev;  
	int i;  
	u32 mac1;  
	u16 mac2;  
	  
	iadev = INPH_IA_DEV(dev);  
	mac1 = cpu_to_be32(le32_to_cpu(readl(  
				iadev->reg+IPHASE5575_MAC1)));  
	mac2 = cpu_to_be16(le16_to_cpu(readl(iadev->reg+IPHASE5575_MAC2)));  
	IF_INIT(printk("ESI: 0x%08x%04x\n", mac1, mac2);)  
	for (i=0; i<MAC1_LEN; i++)  
		dev->esi[i] = mac1 >>(8*(MAC1_LEN-1-i));  
	  
	for (i=0; i<MAC2_LEN; i++)  
		dev->esi[i+MAC1_LEN] = mac2 >>(8*(MAC2_LEN - 1 -i));  
	return 0;  
}  
	  
static int reset_sar(struct atm_dev *dev)  
{  
	IADEV *iadev;  
	int i, error = 1;  
	unsigned int pci[64];  
	  
	iadev = INPH_IA_DEV(dev);  
	for(i=0; i<64; i++)  
	  if ((error = pci_read_config_dword(iadev->pci,  
				i*4, &pci[i])) != PCIBIOS_SUCCESSFUL)  
  	      return error;  
	writel(0, iadev->reg+IPHASE5575_EXT_RESET);  
	for(i=0; i<64; i++)  
	  if ((error = pci_write_config_dword(iadev->pci,  
					i*4, pci[i])) != PCIBIOS_SUCCESSFUL)  
	    return error;  
	udelay(5);  
	return 0;  
}  
	  
	  
static int ia_init(struct atm_dev *dev)
{  
	IADEV *iadev;  
	unsigned long real_base;
	void __iomem *base;
	unsigned short command;  
	int error, i; 
	  
	/* The device has been identified and registered. Now we read   
	   necessary configuration info like memory base address,   
	   interrupt number etc */  
	  
	IF_INIT(printk(">ia_init\n");)  
	dev->ci_range.vpi_bits = 0;  
	dev->ci_range.vci_bits = NR_VCI_LD;  

	iadev = INPH_IA_DEV(dev);  
	real_base = pci_resource_start (iadev->pci, 0);
	iadev->irq = iadev->pci->irq;
		  
	error = pci_read_config_word(iadev->pci, PCI_COMMAND, &command);
	if (error) {
		printk(KERN_ERR DEV_LABEL "(itf %d): init error 0x%x\n",  
				dev->number,error);  
		return -EINVAL;  
	}  
	IF_INIT(printk(DEV_LABEL "(itf %d): rev.%d,realbase=0x%lx,irq=%d\n",  
			dev->number, iadev->pci->revision, real_base, iadev->irq);)
	  
	/* find mapping size of board */  
	  
	iadev->pci_map_size = pci_resource_len(iadev->pci, 0);

        if (iadev->pci_map_size == 0x100000){
          iadev->num_vc = 4096;
	  dev->ci_range.vci_bits = NR_VCI_4K_LD;  
          iadev->memSize = 4;
        }
        else if (iadev->pci_map_size == 0x40000) {
          iadev->num_vc = 1024;
          iadev->memSize = 1;
        }
        else {
           printk("Unknown pci_map_size = 0x%x\n", iadev->pci_map_size);
           return -EINVAL;
        }
	IF_INIT(printk (DEV_LABEL "map size: %i\n", iadev->pci_map_size);)  
	  
	/* enable bus mastering */
	pci_set_master(iadev->pci);

	/*  
	 * Delay at least 1us before doing any mem accesses (how 'bout 10?)  
	 */  
	udelay(10);  
	  
	/* mapping the physical address to a virtual address in address space */  
	base = ioremap(real_base,iadev->pci_map_size);  /* ioremap is not resolved ??? */  
	  
	if (!base)  
	{  
		printk(DEV_LABEL " (itf %d): can't set up page mapping\n",  
			    dev->number);  
		return -ENOMEM;
	}  
	IF_INIT(printk(DEV_LABEL " (itf %d): rev.%d,base=%p,irq=%d\n",  
			dev->number, iadev->pci->revision, base, iadev->irq);)
	  
	/* filling the iphase dev structure */  
	iadev->mem = iadev->pci_map_size /2;  
	iadev->real_base = real_base;  
	iadev->base = base;  
		  
	/* Bus Interface Control Registers */  
	iadev->reg = base + REG_BASE;
	/* Segmentation Control Registers */  
	iadev->seg_reg = base + SEG_BASE;
	/* Reassembly Control Registers */  
	iadev->reass_reg = base + REASS_BASE;  
	/* Front end/ DMA control registers */  
	iadev->phy = base + PHY_BASE;  
	iadev->dma = base + PHY_BASE;  
	/* RAM - Segmentation RAm and Reassembly RAM */  
	iadev->ram = base + ACTUAL_RAM_BASE;  
	iadev->seg_ram = base + ACTUAL_SEG_RAM_BASE;  
	iadev->reass_ram = base + ACTUAL_REASS_RAM_BASE;  
  
	/* lets print out the above */  
	IF_INIT(printk("Base addrs: %p %p %p \n %p %p %p %p\n", 
          iadev->reg,iadev->seg_reg,iadev->reass_reg, 
          iadev->phy, iadev->ram, iadev->seg_ram, 
          iadev->reass_ram);) 
	  
	/* lets try reading the MAC address */  
	error = get_esi(dev);  
	if (error) {
	  iounmap(iadev->base);
	  return error;  
	}
        printk("IA: ");
	for (i=0; i < ESI_LEN; i++)  
                printk("%s%02X",i ? "-" : "",dev->esi[i]);  
        printk("\n");  
  
        /* reset SAR */  
        if (reset_sar(dev)) {
	   iounmap(iadev->base);
           printk("IA: reset SAR fail, please try again\n");
           return 1;
        }
	return 0;  
}  

static void ia_update_stats(IADEV *iadev) {
    if (!iadev->carrier_detect)
        return;
    iadev->rx_cell_cnt += readw(iadev->reass_reg+CELL_CTR0)&0xffff;
    iadev->rx_cell_cnt += (readw(iadev->reass_reg+CELL_CTR1) & 0xffff) << 16;
    iadev->drop_rxpkt +=  readw(iadev->reass_reg + DRP_PKT_CNTR ) & 0xffff;
    iadev->drop_rxcell += readw(iadev->reass_reg + ERR_CNTR) & 0xffff;
    iadev->tx_cell_cnt += readw(iadev->seg_reg + CELL_CTR_LO_AUTO)&0xffff;
    iadev->tx_cell_cnt += (readw(iadev->seg_reg+CELL_CTR_HIGH_AUTO)&0xffff)<<16;
    return;
}
  
static void ia_led_timer(unsigned long arg) {
 	unsigned long flags;
  	static u_char blinking[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        u_char i;
        static u32 ctrl_reg; 
        for (i = 0; i < iadev_count; i++) {
           if (ia_dev[i]) {
	      ctrl_reg = readl(ia_dev[i]->reg+IPHASE5575_BUS_CONTROL_REG);
	      if (blinking[i] == 0) {
		 blinking[i]++;
                 ctrl_reg &= (~CTRL_LED);
                 writel(ctrl_reg, ia_dev[i]->reg+IPHASE5575_BUS_CONTROL_REG);
                 ia_update_stats(ia_dev[i]);
              }
              else {
		 blinking[i] = 0;
		 ctrl_reg |= CTRL_LED;
                 writel(ctrl_reg, ia_dev[i]->reg+IPHASE5575_BUS_CONTROL_REG);
                 spin_lock_irqsave(&ia_dev[i]->tx_lock, flags);
                 if (ia_dev[i]->close_pending)  
                    wake_up(&ia_dev[i]->close_wait);
                 ia_tx_poll(ia_dev[i]);
                 spin_unlock_irqrestore(&ia_dev[i]->tx_lock, flags);
              }
           }
        }
	mod_timer(&ia_timer, jiffies + HZ / 4);
 	return;
}

static void ia_phy_put(struct atm_dev *dev, unsigned char value,   
	unsigned long addr)  
{  
	writel(value, INPH_IA_DEV(dev)->phy+addr);  
}  
  
static unsigned char ia_phy_get(struct atm_dev *dev, unsigned long addr)  
{  
	return readl(INPH_IA_DEV(dev)->phy+addr);  
}  

static void ia_free_tx(IADEV *iadev)
{
	int i;

	kfree(iadev->desc_tbl);
	for (i = 0; i < iadev->num_vc; i++)
		kfree(iadev->testTable[i]);
	kfree(iadev->testTable);
	for (i = 0; i < iadev->num_tx_desc; i++) {
		struct cpcs_trailer_desc *desc = iadev->tx_buf + i;

		dma_unmap_single(&iadev->pci->dev, desc->dma_addr,
				 sizeof(*desc->cpcs), DMA_TO_DEVICE);
		kfree(desc->cpcs);
	}
	kfree(iadev->tx_buf);
	dma_free_coherent(&iadev->pci->dev, DLE_TOTAL_SIZE, iadev->tx_dle_q.start,
			  iadev->tx_dle_dma);
}

static void ia_free_rx(IADEV *iadev)
{
	kfree(iadev->rx_open);
	dma_free_coherent(&iadev->pci->dev, DLE_TOTAL_SIZE, iadev->rx_dle_q.start,
			  iadev->rx_dle_dma);
}

static int ia_start(struct atm_dev *dev)
{  
	IADEV *iadev;  
	int error;  
	unsigned char phy;  
	u32 ctrl_reg;  
	IF_EVENT(printk(">ia_start\n");)  
	iadev = INPH_IA_DEV(dev);  
        if (request_irq(iadev->irq, &ia_int, IRQF_SHARED, DEV_LABEL, dev)) {
                printk(KERN_ERR DEV_LABEL "(itf %d): IRQ%d is already in use\n",  
                    dev->number, iadev->irq);  
		error = -EAGAIN;
		goto err_out;
        }  
        /* @@@ should release IRQ on error */  
	/* enabling memory + master */  
        if ((error = pci_write_config_word(iadev->pci,   
				PCI_COMMAND,   
				PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER )))   
	{  
                printk(KERN_ERR DEV_LABEL "(itf %d): can't enable memory+"  
                    "master (0x%x)\n",dev->number, error);  
		error = -EIO;  
		goto err_free_irq;
        }  
	udelay(10);  
  
	/* Maybe we should reset the front end, initialize Bus Interface Control   
		Registers and see. */  
  
	IF_INIT(printk("Bus ctrl reg: %08x\n", 
                            readl(iadev->reg+IPHASE5575_BUS_CONTROL_REG));)  
	ctrl_reg = readl(iadev->reg+IPHASE5575_BUS_CONTROL_REG);  
	ctrl_reg = (ctrl_reg & (CTRL_LED | CTRL_FE_RST))  
			| CTRL_B8  
			| CTRL_B16  
			| CTRL_B32  
			| CTRL_B48  
			| CTRL_B64  
			| CTRL_B128  
			| CTRL_ERRMASK  
			| CTRL_DLETMASK		/* shud be removed l8r */  
			| CTRL_DLERMASK  
			| CTRL_SEGMASK  
			| CTRL_REASSMASK 	  
			| CTRL_FEMASK  
			| CTRL_CSPREEMPT;  
  
       writel(ctrl_reg, iadev->reg+IPHASE5575_BUS_CONTROL_REG);   
  
	IF_INIT(printk("Bus ctrl reg after initializing: %08x\n", 
                           readl(iadev->reg+IPHASE5575_BUS_CONTROL_REG));  
	   printk("Bus status reg after init: %08x\n", 
                            readl(iadev->reg+IPHASE5575_BUS_STATUS_REG));)  
    
        ia_hw_type(iadev); 
	error = tx_init(dev);  
	if (error)
		goto err_free_irq;
	error = rx_init(dev);  
	if (error)
		goto err_free_tx;
  
	ctrl_reg = readl(iadev->reg+IPHASE5575_BUS_CONTROL_REG);  
       	writel(ctrl_reg | CTRL_FE_RST, iadev->reg+IPHASE5575_BUS_CONTROL_REG);   
	IF_INIT(printk("Bus ctrl reg after initializing: %08x\n", 
                               readl(iadev->reg+IPHASE5575_BUS_CONTROL_REG));)  
        phy = 0; /* resolve compiler complaint */
        IF_INIT ( 
	if ((phy=ia_phy_get(dev,0)) == 0x30)  
		printk("IA: pm5346,rev.%d\n",phy&0x0f);  
	else  
		printk("IA: utopia,rev.%0x\n",phy);) 

	if (iadev->phy_type &  FE_25MBIT_PHY)
           ia_mb25_init(iadev);
	else if (iadev->phy_type & (FE_DS3_PHY | FE_E3_PHY))
           ia_suni_pm7345_init(iadev);
	else {
		error = suni_init(dev);
		if (error)
			goto err_free_rx;
		if (dev->phy->start) {
			error = dev->phy->start(dev);
			if (error)
				goto err_free_rx;
		}
		/* Get iadev->carrier_detect status */
		ia_frontend_intr(iadev);
	}
	return 0;

err_free_rx:
	ia_free_rx(iadev);
err_free_tx:
	ia_free_tx(iadev);
err_free_irq:
	free_irq(iadev->irq, dev);  
err_out:
	return error;
}  
  
static void ia_close(struct atm_vcc *vcc)  
{
	DEFINE_WAIT(wait);
        u16 *vc_table;
        IADEV *iadev;
        struct ia_vcc *ia_vcc;
        struct sk_buff *skb = NULL;
        struct sk_buff_head tmp_tx_backlog, tmp_vcc_backlog;
        unsigned long closetime, flags;

        iadev = INPH_IA_DEV(vcc->dev);
        ia_vcc = INPH_IA_VCC(vcc);
	if (!ia_vcc) return;  

        IF_EVENT(printk("ia_close: ia_vcc->vc_desc_cnt = %d  vci = %d\n", 
                                              ia_vcc->vc_desc_cnt,vcc->vci);)
	clear_bit(ATM_VF_READY,&vcc->flags);
        skb_queue_head_init (&tmp_tx_backlog);
        skb_queue_head_init (&tmp_vcc_backlog); 
        if (vcc->qos.txtp.traffic_class != ATM_NONE) {
           iadev->close_pending++;
	   prepare_to_wait(&iadev->timeout_wait, &wait, TASK_UNINTERRUPTIBLE);
	   schedule_timeout(msecs_to_jiffies(500));
	   finish_wait(&iadev->timeout_wait, &wait);
           spin_lock_irqsave(&iadev->tx_lock, flags); 
           while((skb = skb_dequeue(&iadev->tx_backlog))) {
              if (ATM_SKB(skb)->vcc == vcc){ 
                 if (vcc->pop) vcc->pop(vcc, skb);
                 else dev_kfree_skb_any(skb);
              }
              else 
                 skb_queue_tail(&tmp_tx_backlog, skb);
           } 
           while((skb = skb_dequeue(&tmp_tx_backlog))) 
             skb_queue_tail(&iadev->tx_backlog, skb);
           IF_EVENT(printk("IA TX Done decs_cnt = %d\n", ia_vcc->vc_desc_cnt);) 
           closetime = 300000 / ia_vcc->pcr;
           if (closetime == 0)
              closetime = 1;
           spin_unlock_irqrestore(&iadev->tx_lock, flags);
           wait_event_timeout(iadev->close_wait, (ia_vcc->vc_desc_cnt <= 0), closetime);
           spin_lock_irqsave(&iadev->tx_lock, flags);
           iadev->close_pending--;
           iadev->testTable[vcc->vci]->lastTime = 0;
           iadev->testTable[vcc->vci]->fract = 0; 
           iadev->testTable[vcc->vci]->vc_status = VC_UBR; 
           if (vcc->qos.txtp.traffic_class == ATM_ABR) {
              if (vcc->qos.txtp.min_pcr > 0)
                 iadev->sum_mcr -= vcc->qos.txtp.min_pcr;
           }
           if (vcc->qos.txtp.traffic_class == ATM_CBR) {
              ia_vcc = INPH_IA_VCC(vcc); 
              iadev->sum_mcr -= ia_vcc->NumCbrEntry*iadev->Granularity;
              ia_cbrVc_close (vcc);
           }
           spin_unlock_irqrestore(&iadev->tx_lock, flags);
        }
        
        if (vcc->qos.rxtp.traffic_class != ATM_NONE) {   
           // reset reass table
           vc_table = (u16 *)(iadev->reass_ram+REASS_TABLE*iadev->memSize);
           vc_table += vcc->vci; 
           *vc_table = NO_AAL5_PKT;
           // reset vc table
           vc_table = (u16 *)(iadev->reass_ram+RX_VC_TABLE*iadev->memSize);
           vc_table += vcc->vci;
           *vc_table = (vcc->vci << 6) | 15;
           if (vcc->qos.rxtp.traffic_class == ATM_ABR) {
              struct abr_vc_table __iomem *abr_vc_table = 
                                (iadev->reass_ram+ABR_VC_TABLE*iadev->memSize);
              abr_vc_table +=  vcc->vci;
              abr_vc_table->rdf = 0x0003;
              abr_vc_table->air = 0x5eb1;
           }                                 
           // Drain the packets
           rx_dle_intr(vcc->dev); 
           iadev->rx_open[vcc->vci] = NULL;
        }
	kfree(INPH_IA_VCC(vcc));  
        ia_vcc = NULL;
        vcc->dev_data = NULL;
        clear_bit(ATM_VF_ADDR,&vcc->flags);
        return;        
}  
  
static int ia_open(struct atm_vcc *vcc)
{  
	struct ia_vcc *ia_vcc;  
	int error;  
	if (!test_bit(ATM_VF_PARTIAL,&vcc->flags))  
	{  
		IF_EVENT(printk("ia: not partially allocated resources\n");)  
		vcc->dev_data = NULL;
	}  
	if (vcc->vci != ATM_VPI_UNSPEC && vcc->vpi != ATM_VCI_UNSPEC)  
	{  
		IF_EVENT(printk("iphase open: unspec part\n");)  
		set_bit(ATM_VF_ADDR,&vcc->flags);
	}  
	if (vcc->qos.aal != ATM_AAL5)  
		return -EINVAL;  
	IF_EVENT(printk(DEV_LABEL "(itf %d): open %d.%d\n", 
                                 vcc->dev->number, vcc->vpi, vcc->vci);)  
  
	/* Device dependent initialization */  
	ia_vcc = kmalloc(sizeof(*ia_vcc), GFP_KERNEL);  
	if (!ia_vcc) return -ENOMEM;  
	vcc->dev_data = ia_vcc;
  
	if ((error = open_rx(vcc)))  
	{  
		IF_EVENT(printk("iadev: error in open_rx, closing\n");)  
		ia_close(vcc);  
		return error;  
	}  
  
	if ((error = open_tx(vcc)))  
	{  
		IF_EVENT(printk("iadev: error in open_tx, closing\n");)  
		ia_close(vcc);  
		return error;  
	}  
  
	set_bit(ATM_VF_READY,&vcc->flags);

#if 0
        {
           static u8 first = 1; 
           if (first) {
              ia_timer.expires = jiffies + 3*HZ;
              add_timer(&ia_timer);
              first = 0;
           }           
        }
#endif
	IF_EVENT(printk("ia open returning\n");)  
	return 0;  
}  
  
static int ia_change_qos(struct atm_vcc *vcc, struct atm_qos *qos, int flags)  
{  
	IF_EVENT(printk(">ia_change_qos\n");)  
	return 0;  
}  
  
static int ia_ioctl(struct atm_dev *dev, unsigned int cmd, void __user *arg)  
{  
   IA_CMDBUF ia_cmds;
   IADEV *iadev;
   int i, board;
   u16 __user *tmps;
   IF_EVENT(printk(">ia_ioctl\n");)  
   if (cmd != IA_CMD) {
      if (!dev->phy->ioctl) return -EINVAL;
      return dev->phy->ioctl(dev,cmd,arg);
   }
   if (copy_from_user(&ia_cmds, arg, sizeof ia_cmds)) return -EFAULT; 
   board = ia_cmds.status;
   if ((board < 0) || (board > iadev_count))
         board = 0;    
   iadev = ia_dev[board];
   switch (ia_cmds.cmd) {
   case MEMDUMP:
   {
	switch (ia_cmds.sub_cmd) {
       	  case MEMDUMP_DEV:     
	     if (!capable(CAP_NET_ADMIN)) return -EPERM;
	     if (copy_to_user(ia_cmds.buf, iadev, sizeof(IADEV)))
                return -EFAULT;
             ia_cmds.status = 0;
             break;
          case MEMDUMP_SEGREG:
	     if (!capable(CAP_NET_ADMIN)) return -EPERM;
             tmps = (u16 __user *)ia_cmds.buf;
             for(i=0; i<0x80; i+=2, tmps++)
                if(put_user((u16)(readl(iadev->seg_reg+i) & 0xffff), tmps)) return -EFAULT;
             ia_cmds.status = 0;
             ia_cmds.len = 0x80;
             break;
          case MEMDUMP_REASSREG:
	     if (!capable(CAP_NET_ADMIN)) return -EPERM;
             tmps = (u16 __user *)ia_cmds.buf;
             for(i=0; i<0x80; i+=2, tmps++)
                if(put_user((u16)(readl(iadev->reass_reg+i) & 0xffff), tmps)) return -EFAULT;
             ia_cmds.status = 0;
             ia_cmds.len = 0x80;
             break;
          case MEMDUMP_FFL:
          {  
             ia_regs_t       *regs_local;
             ffredn_t        *ffL;
             rfredn_t        *rfL;
                     
	     if (!capable(CAP_NET_ADMIN)) return -EPERM;
	     regs_local = kmalloc(sizeof(*regs_local), GFP_KERNEL);
	     if (!regs_local) return -ENOMEM;
	     ffL = &regs_local->ffredn;
	     rfL = &regs_local->rfredn;
             /* Copy real rfred registers into the local copy */
 	     for (i=0; i<(sizeof (rfredn_t))/4; i++)
                ((u_int *)rfL)[i] = readl(iadev->reass_reg + i) & 0xffff;
             	/* Copy real ffred registers into the local copy */
	     for (i=0; i<(sizeof (ffredn_t))/4; i++)
                ((u_int *)ffL)[i] = readl(iadev->seg_reg + i) & 0xffff;

             if (copy_to_user(ia_cmds.buf, regs_local,sizeof(ia_regs_t))) {
                kfree(regs_local);
                return -EFAULT;
             }
             kfree(regs_local);
             printk("Board %d registers dumped\n", board);
             ia_cmds.status = 0;                  
	 }	
    	     break;        
         case READ_REG:
         {  
	     if (!capable(CAP_NET_ADMIN)) return -EPERM;
             desc_dbg(iadev); 
             ia_cmds.status = 0; 
         }
             break;
         case 0x6:
         {  
             ia_cmds.status = 0; 
             printk("skb = 0x%lx\n", (long)skb_peek(&iadev->tx_backlog));
             printk("rtn_q: 0x%lx\n",(long)ia_deque_rtn_q(&iadev->tx_return_q));
         }
             break;
         case 0x8:
         {
             struct k_sonet_stats *stats;
             stats = &PRIV(_ia_dev[board])->sonet_stats;
             printk("section_bip: %d\n", atomic_read(&stats->section_bip));
             printk("line_bip   : %d\n", atomic_read(&stats->line_bip));
             printk("path_bip   : %d\n", atomic_read(&stats->path_bip));
             printk("line_febe  : %d\n", atomic_read(&stats->line_febe));
             printk("path_febe  : %d\n", atomic_read(&stats->path_febe));
             printk("corr_hcs   : %d\n", atomic_read(&stats->corr_hcs));
             printk("uncorr_hcs : %d\n", atomic_read(&stats->uncorr_hcs));
             printk("tx_cells   : %d\n", atomic_read(&stats->tx_cells));
             printk("rx_cells   : %d\n", atomic_read(&stats->rx_cells));
         }
            ia_cmds.status = 0;
            break;
         case 0x9:
	    if (!capable(CAP_NET_ADMIN)) return -EPERM;
            for (i = 1; i <= iadev->num_rx_desc; i++)
               free_desc(_ia_dev[board], i);
            writew( ~(RX_FREEQ_EMPT | RX_EXCP_RCVD), 
                                            iadev->reass_reg+REASS_MASK_REG);
            iadev->rxing = 1;
            
            ia_cmds.status = 0;
            break;

         case 0xb:
	    if (!capable(CAP_NET_ADMIN)) return -EPERM;
            ia_frontend_intr(iadev);
            break;
         case 0xa:
	    if (!capable(CAP_NET_ADMIN)) return -EPERM;
         {  
             ia_cmds.status = 0; 
             IADebugFlag = ia_cmds.maddr;
             printk("New debug option loaded\n");
         }
             break;
         default:
             ia_cmds.status = 0;
             break;
      }	
   }
      break;
   default:
      break;

   }	
   return 0;  
}  
  
static int ia_getsockopt(struct atm_vcc *vcc, int level, int optname,   
	void __user *optval, int optlen)  
{  
	IF_EVENT(printk(">ia_getsockopt\n");)  
	return -EINVAL;  
}  
  
static int ia_setsockopt(struct atm_vcc *vcc, int level, int optname,   
	void __user *optval, unsigned int optlen)  
{  
	IF_EVENT(printk(">ia_setsockopt\n");)  
	return -EINVAL;  
}  
  
static int ia_pkt_tx (struct atm_vcc *vcc, struct sk_buff *skb) {
        IADEV *iadev;
        struct dle *wr_ptr;
        struct tx_buf_desc __iomem *buf_desc_ptr;
        int desc;
        int comp_code;
        int total_len;
        struct cpcs_trailer *trailer;
        struct ia_vcc *iavcc;

        iadev = INPH_IA_DEV(vcc->dev);  
        iavcc = INPH_IA_VCC(vcc);
        if (!iavcc->txing) {
           printk("discard packet on closed VC\n");
           if (vcc->pop)
		vcc->pop(vcc, skb);
           else
		dev_kfree_skb_any(skb);
	   return 0;
        }

        if (skb->len > iadev->tx_buf_sz - 8) {
           printk("Transmit size over tx buffer size\n");
           if (vcc->pop)
                 vcc->pop(vcc, skb);
           else
                 dev_kfree_skb_any(skb);
          return 0;
        }
        if ((unsigned long)skb->data & 3) {
           printk("Misaligned SKB\n");
           if (vcc->pop)
                 vcc->pop(vcc, skb);
           else
                 dev_kfree_skb_any(skb);
           return 0;
        }       
	/* Get a descriptor number from our free descriptor queue  
	   We get the descr number from the TCQ now, since I am using  
	   the TCQ as a free buffer queue. Initially TCQ will be   
	   initialized with all the descriptors and is hence, full.  
	*/
	desc = get_desc (iadev, iavcc);
	if (desc == 0xffff) 
	    return 1;
	comp_code = desc >> 13;  
	desc &= 0x1fff;  
  
	if ((desc == 0) || (desc > iadev->num_tx_desc))  
	{  
		IF_ERR(printk(DEV_LABEL "invalid desc for send: %d\n", desc);) 
                atomic_inc(&vcc->stats->tx);
		if (vcc->pop)   
		    vcc->pop(vcc, skb);   
		else  
		    dev_kfree_skb_any(skb);
		return 0;   /* return SUCCESS */
	}  
  
	if (comp_code)  
	{  
	    IF_ERR(printk(DEV_LABEL "send desc:%d completion code %d error\n", 
                                                            desc, comp_code);)  
	}  
       
        /* remember the desc and vcc mapping */
        iavcc->vc_desc_cnt++;
        iadev->desc_tbl[desc-1].iavcc = iavcc;
        iadev->desc_tbl[desc-1].txskb = skb;
        IA_SKB_STATE(skb) = 0;

        iadev->ffL.tcq_rd += 2;
        if (iadev->ffL.tcq_rd > iadev->ffL.tcq_ed)
	  	iadev->ffL.tcq_rd  = iadev->ffL.tcq_st;
	writew(iadev->ffL.tcq_rd, iadev->seg_reg+TCQ_RD_PTR);
  
	/* Put the descriptor number in the packet ready queue  
		and put the updated write pointer in the DLE field   
	*/   
	*(u16*)(iadev->seg_ram+iadev->ffL.prq_wr) = desc; 

 	iadev->ffL.prq_wr += 2;
        if (iadev->ffL.prq_wr > iadev->ffL.prq_ed)
                iadev->ffL.prq_wr = iadev->ffL.prq_st;
	  
	/* Figure out the exact length of the packet and padding required to 
           make it  aligned on a 48 byte boundary.  */
	total_len = skb->len + sizeof(struct cpcs_trailer);  
	total_len = ((total_len + 47) / 48) * 48;
	IF_TX(printk("ia packet len:%d padding:%d\n", total_len, total_len - skb->len);)  
 
	/* Put the packet in a tx buffer */   
	trailer = iadev->tx_buf[desc-1].cpcs;
        IF_TX(printk("Sent: skb = 0x%p skb->data: 0x%p len: %d, desc: %d\n",
                  skb, skb->data, skb->len, desc);)
	trailer->control = 0; 
        /*big endian*/ 
	trailer->length = ((skb->len & 0xff) << 8) | ((skb->len & 0xff00) >> 8);
	trailer->crc32 = 0;	/* not needed - dummy bytes */  

	/* Display the packet */  
	IF_TXPKT(printk("Sent data: len = %d MsgNum = %d\n", 
                                                        skb->len, tcnter++);  
        xdump(skb->data, skb->len, "TX: ");
        printk("\n");)

	/* Build the buffer descriptor */  
	buf_desc_ptr = iadev->seg_ram+TX_DESC_BASE;
	buf_desc_ptr += desc;	/* points to the corresponding entry */  
	buf_desc_ptr->desc_mode = AAL5 | EOM_EN | APP_CRC32 | CMPL_INT;   
	/* Huh ? p.115 of users guide describes this as a read-only register */
        writew(TRANSMIT_DONE, iadev->seg_reg+SEG_INTR_STATUS_REG);
	buf_desc_ptr->vc_index = vcc->vci;
	buf_desc_ptr->bytes = total_len;  

        if (vcc->qos.txtp.traffic_class == ATM_ABR)  
	   clear_lockup (vcc, iadev);

	/* Build the DLE structure */  
	wr_ptr = iadev->tx_dle_q.write;  
	memset((caddr_t)wr_ptr, 0, sizeof(*wr_ptr));  
	wr_ptr->sys_pkt_addr = dma_map_single(&iadev->pci->dev, skb->data,
					      skb->len, DMA_TO_DEVICE);
	wr_ptr->local_pkt_addr = (buf_desc_ptr->buf_start_hi << 16) | 
                                                  buf_desc_ptr->buf_start_lo;  
	/* wr_ptr->bytes = swap_byte_order(total_len); didn't seem to affect?? */
	wr_ptr->bytes = skb->len;  

        /* hw bug - DLEs of 0x2d, 0x2e, 0x2f cause DMA lockup */
        if ((wr_ptr->bytes >> 2) == 0xb)
           wr_ptr->bytes = 0x30;

	wr_ptr->mode = TX_DLE_PSI; 
	wr_ptr->prq_wr_ptr_data = 0;
  
	/* end is not to be used for the DLE q */  
	if (++wr_ptr == iadev->tx_dle_q.end)  
		wr_ptr = iadev->tx_dle_q.start;  
        
        /* Build trailer dle */
        wr_ptr->sys_pkt_addr = iadev->tx_buf[desc-1].dma_addr;
        wr_ptr->local_pkt_addr = ((buf_desc_ptr->buf_start_hi << 16) | 
          buf_desc_ptr->buf_start_lo) + total_len - sizeof(struct cpcs_trailer);

        wr_ptr->bytes = sizeof(struct cpcs_trailer);
        wr_ptr->mode = DMA_INT_ENABLE; 
        wr_ptr->prq_wr_ptr_data = iadev->ffL.prq_wr;
        
        /* end is not to be used for the DLE q */
        if (++wr_ptr == iadev->tx_dle_q.end)  
                wr_ptr = iadev->tx_dle_q.start;

	iadev->tx_dle_q.write = wr_ptr;  
        ATM_DESC(skb) = vcc->vci;
        skb_queue_tail(&iadev->tx_dma_q, skb);

        atomic_inc(&vcc->stats->tx);
        iadev->tx_pkt_cnt++;
	/* Increment transaction counter */  
	writel(2, iadev->dma+IPHASE5575_TX_COUNTER);  
        
#if 0        
        /* add flow control logic */ 
        if (atomic_read(&vcc->stats->tx) % 20 == 0) {
          if (iavcc->vc_desc_cnt > 10) {
             vcc->tx_quota =  vcc->tx_quota * 3 / 4;
            printk("Tx1:  vcc->tx_quota = %d \n", (u32)vcc->tx_quota );
              iavcc->flow_inc = -1;
              iavcc->saved_tx_quota = vcc->tx_quota;
           } else if ((iavcc->flow_inc < 0) && (iavcc->vc_desc_cnt < 3)) {
             // vcc->tx_quota = 3 * iavcc->saved_tx_quota / 4;
             printk("Tx2:  vcc->tx_quota = %d \n", (u32)vcc->tx_quota ); 
              iavcc->flow_inc = 0;
           }
        }
#endif
	IF_TX(printk("ia send done\n");)  
	return 0;  
}  

static int ia_send(struct atm_vcc *vcc, struct sk_buff *skb)
{
        IADEV *iadev; 
        unsigned long flags;

        iadev = INPH_IA_DEV(vcc->dev);
        if ((!skb)||(skb->len>(iadev->tx_buf_sz-sizeof(struct cpcs_trailer))))
        {
            if (!skb)
                printk(KERN_CRIT "null skb in ia_send\n");
            else dev_kfree_skb_any(skb);
            return -EINVAL;
        }                         
        spin_lock_irqsave(&iadev->tx_lock, flags); 
        if (!test_bit(ATM_VF_READY,&vcc->flags)){ 
            dev_kfree_skb_any(skb);
            spin_unlock_irqrestore(&iadev->tx_lock, flags);
            return -EINVAL; 
        }
        ATM_SKB(skb)->vcc = vcc;
 
        if (skb_peek(&iadev->tx_backlog)) {
           skb_queue_tail(&iadev->tx_backlog, skb);
        }
        else {
           if (ia_pkt_tx (vcc, skb)) {
              skb_queue_tail(&iadev->tx_backlog, skb);
           }
        }
        spin_unlock_irqrestore(&iadev->tx_lock, flags);
        return 0;

}

static int ia_proc_read(struct atm_dev *dev,loff_t *pos,char *page)
{ 
  int   left = *pos, n;   
  char  *tmpPtr;
  IADEV *iadev = INPH_IA_DEV(dev);
  if(!left--) {
     if (iadev->phy_type == FE_25MBIT_PHY) {
       n = sprintf(page, "  Board Type         :  Iphase5525-1KVC-128K\n");
       return n;
     }
     if (iadev->phy_type == FE_DS3_PHY)
        n = sprintf(page, "  Board Type         :  Iphase-ATM-DS3");
     else if (iadev->phy_type == FE_E3_PHY)
        n = sprintf(page, "  Board Type         :  Iphase-ATM-E3");
     else if (iadev->phy_type == FE_UTP_OPTION)
         n = sprintf(page, "  Board Type         :  Iphase-ATM-UTP155"); 
     else
        n = sprintf(page, "  Board Type         :  Iphase-ATM-OC3");
     tmpPtr = page + n;
     if (iadev->pci_map_size == 0x40000)
        n += sprintf(tmpPtr, "-1KVC-");
     else
        n += sprintf(tmpPtr, "-4KVC-");  
     tmpPtr = page + n; 
     if ((iadev->memType & MEM_SIZE_MASK) == MEM_SIZE_1M)
        n += sprintf(tmpPtr, "1M  \n");
     else if ((iadev->memType & MEM_SIZE_MASK) == MEM_SIZE_512K)
        n += sprintf(tmpPtr, "512K\n");
     else
       n += sprintf(tmpPtr, "128K\n");
     return n;
  }
  if (!left) {
     return  sprintf(page, "  Number of Tx Buffer:  %u\n"
                           "  Size of Tx Buffer  :  %u\n"
                           "  Number of Rx Buffer:  %u\n"
                           "  Size of Rx Buffer  :  %u\n"
                           "  Packets Receiverd  :  %u\n"
                           "  Packets Transmitted:  %u\n"
                           "  Cells Received     :  %u\n"
                           "  Cells Transmitted  :  %u\n"
                           "  Board Dropped Cells:  %u\n"
                           "  Board Dropped Pkts :  %u\n",
                           iadev->num_tx_desc,  iadev->tx_buf_sz,
                           iadev->num_rx_desc,  iadev->rx_buf_sz,
                           iadev->rx_pkt_cnt,   iadev->tx_pkt_cnt,
                           iadev->rx_cell_cnt, iadev->tx_cell_cnt,
                           iadev->drop_rxcell, iadev->drop_rxpkt);                        
  }
  return 0;
}
  
static const struct atmdev_ops ops = {  
	.open		= ia_open,  
	.close		= ia_close,  
	.ioctl		= ia_ioctl,  
	.getsockopt	= ia_getsockopt,  
	.setsockopt	= ia_setsockopt,  
	.send		= ia_send,  
	.phy_put	= ia_phy_put,  
	.phy_get	= ia_phy_get,  
	.change_qos	= ia_change_qos,  
	.proc_read	= ia_proc_read,
	.owner		= THIS_MODULE,
};  
	  
static int ia_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{  
	struct atm_dev *dev;  
	IADEV *iadev;  
	int ret;

	iadev = kzalloc(sizeof(*iadev), GFP_KERNEL);
	if (!iadev) {
		ret = -ENOMEM;
		goto err_out;
	}

	iadev->pci = pdev;

	IF_INIT(printk("ia detected at bus:%d dev: %d function:%d\n",
		pdev->bus->number, PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));)
	if (pci_enable_device(pdev)) {
		ret = -ENODEV;
		goto err_out_free_iadev;
	}
	dev = atm_dev_register(DEV_LABEL, &pdev->dev, &ops, -1, NULL);
	if (!dev) {
		ret = -ENOMEM;
		goto err_out_disable_dev;
	}
	dev->dev_data = iadev;
	IF_INIT(printk(DEV_LABEL "registered at (itf :%d)\n", dev->number);)
	IF_INIT(printk("dev_id = 0x%p iadev->LineRate = %d \n", dev,
		iadev->LineRate);)

	pci_set_drvdata(pdev, dev);

	ia_dev[iadev_count] = iadev;
	_ia_dev[iadev_count] = dev;
	iadev_count++;
	if (ia_init(dev) || ia_start(dev)) {  
		IF_INIT(printk("IA register failed!\n");)
		iadev_count--;
		ia_dev[iadev_count] = NULL;
		_ia_dev[iadev_count] = NULL;
		ret = -EINVAL;
		goto err_out_deregister_dev;
	}
	IF_EVENT(printk("iadev_count = %d\n", iadev_count);)

	iadev->next_board = ia_boards;  
	ia_boards = dev;  

	return 0;

err_out_deregister_dev:
	atm_dev_deregister(dev);  
err_out_disable_dev:
	pci_disable_device(pdev);
err_out_free_iadev:
	kfree(iadev);
err_out:
	return ret;
}

static void ia_remove_one(struct pci_dev *pdev)
{
	struct atm_dev *dev = pci_get_drvdata(pdev);
	IADEV *iadev = INPH_IA_DEV(dev);

	/* Disable phy interrupts */
	ia_phy_put(dev, ia_phy_get(dev, SUNI_RSOP_CIE) & ~(SUNI_RSOP_CIE_LOSE),
				   SUNI_RSOP_CIE);
	udelay(1);

	if (dev->phy && dev->phy->stop)
		dev->phy->stop(dev);

	/* De-register device */  
      	free_irq(iadev->irq, dev);
	iadev_count--;
	ia_dev[iadev_count] = NULL;
	_ia_dev[iadev_count] = NULL;
	IF_EVENT(printk("deregistering iav at (itf:%d)\n", dev->number);)
	atm_dev_deregister(dev);

      	iounmap(iadev->base);  
	pci_disable_device(pdev);

	ia_free_rx(iadev);
	ia_free_tx(iadev);

      	kfree(iadev);
}

static struct pci_device_id ia_pci_tbl[] = {
	{ PCI_VENDOR_ID_IPHASE, 0x0008, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_IPHASE, 0x0009, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0,}
};
MODULE_DEVICE_TABLE(pci, ia_pci_tbl);

static struct pci_driver ia_driver = {
	.name =         DEV_LABEL,
	.id_table =     ia_pci_tbl,
	.probe =        ia_init_one,
	.remove =       ia_remove_one,
};

static int __init ia_module_init(void)
{
	int ret;

	ret = pci_register_driver(&ia_driver);
	if (ret >= 0) {
		ia_timer.expires = jiffies + 3*HZ;
		add_timer(&ia_timer); 
	} else
		printk(KERN_ERR DEV_LABEL ": no adapter found\n");  
	return ret;
}

static void __exit ia_module_exit(void)
{
	pci_unregister_driver(&ia_driver);

        del_timer(&ia_timer);
}

module_init(ia_module_init);
module_exit(ia_module_exit);
