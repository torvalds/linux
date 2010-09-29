
/*
 * drivers/mtd/nand/rk2818_nand.c
 *
 * Copyright (C) 2010 RockChip, Inc.
 * Author: hxy@rock-chips.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
 
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/io.h>

#include <mach/rk2818_nand.h>
#include <mach/rk2818_iomap.h>
#include <mach/iomux.h>

#define PROGRAM_BUSY_COUNT   10000
#define ERASE_BUSY_COUNT	    20000
#define READ_BUSY_COUNT   	    5000
#define RESET_BUSY_COUNT			20000

/* Define delays in microsec for NAND device operations */
#define TROP_US_DELAY   2000


#ifdef CONFIG_DM9000_USE_NAND_CONTROL
static DEFINE_MUTEX(rknand_mutex);
#define RKNAND_LOCK()   do { int panic = in_interrupt() | in_atomic(); if (!panic) mutex_lock(&rknand_mutex); } while (0)
#define RKNAND_UNLOCK() do { int panic = in_interrupt() | in_atomic(); if (!panic) mutex_unlock(&rknand_mutex); } while (0)
#else
#define RKNAND_LOCK()   do {} while (0)
#define RKNAND_UNLOCK() do {} while (0)
#endif

struct rk2818_nand_mtd {
	struct mtd_info		mtd;
	struct nand_chip		nand;
	struct mtd_partition	*parts;
	struct device		*dev;
       const struct rk2818_nand_flash *flash_info;

	struct clk			*clk;
	unsigned long		 	clk_rate;
	void __iomem			*regs;
	int					cs;	   		// support muliple nand chip,record current chip select
	u_char 				accesstime;
#ifdef CONFIG_CPU_FREQ
	struct notifier_block	freq_transition;
#endif

};

/* OOB placement block for use with software ecc generation */
static struct nand_ecclayout nand_sw_eccoob_8 = {
	.eccbytes = 48,
	.eccpos = { 8, 9, 10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
			  32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55},
	.oobfree = {{0,8},{56, 72}}
};

/* OOB placement block for use with hardware ecc generation */
static struct nand_ecclayout nand_hw_eccoob_16 = {
	.eccbytes = 28,
	.eccpos = { 4,  5, 6,  7,  8, 9, 10,11,12,13,14,15,16,17,
			  18,19,20,21,22,23,24,25,26,27,28,29,30,31},
	.oobfree = {{0, 4}}
};

#ifdef CONFIG_MTD_PARTITIONS
static const char *part_probes[] = { "cmdlinepart", NULL };
#endif


static void rk2818_nand_wait_busy(struct mtd_info *mtd, uint32_t timeout)
{
      
      	struct nand_chip *nand_chip = mtd->priv;
	struct rk2818_nand_mtd *master = nand_chip->priv;
	pNANDC pRK28NC=  (pNANDC)(master->regs);
	
	while (timeout > 0)
	{
		timeout--;
		udelay(10);
		if ( pRK28NC->FMCTL& FMC_FRDY) 
			break;
		
	}
	
    return;
}

static void rk2818_nand_wait_bchdone(struct mtd_info *mtd, uint32_t timeout)
{
      
      	struct nand_chip *nand_chip = mtd->priv;
	struct rk2818_nand_mtd *master = nand_chip->priv;
	pNANDC pRK28NC=  (pNANDC)(master->regs);
	
	while (timeout > 0)
	{
		timeout--;
		udelay(1);
		if(pRK28NC->BCHST &(1<<1))
			break;		
	}
	
    return;
}

// only for dma mode 
static void wait_op_done(struct mtd_info *mtd, int max_retries, uint16_t param)
{
       struct nand_chip *nand_chip = mtd->priv;
	struct rk2818_nand_mtd *master = nand_chip->priv;
	pNANDC pRK28NC=  (pNANDC)(master->regs);
	
	while (max_retries-- > 0) {
		udelay(1);
		if (pRK28NC->FLCTL & FL_RDY)
			break;		
	}	      
}

static int rk2818_nand_dev_ready(struct mtd_info *mtd)
{
      	struct nand_chip *nand_chip = mtd->priv;
	struct rk2818_nand_mtd *master = nand_chip->priv;
	pNANDC pRK28NC=  (pNANDC)(master->regs);
	  
      	if(pRK28NC->FMCTL& FMC_FRDY)
	   return 1;
	else
	   return 0;
}

/*
*  设置片选
*/
static void rk2818_nand_select_chip(struct mtd_info *mtd, int chip)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct rk2818_nand_mtd *master = nand_chip->priv;
	pNANDC pRK28NC=  (pNANDC)(master->regs);

       	   
	if( chip<0 )
	     pRK28NC->FMCTL &=0xffffff00;   // release chip select
	else
	  {
	       master->cs = chip;
		pRK28NC->FMCTL &=0xffffff00;
		pRK28NC ->FMCTL |= 0x1<<chip;  // select chip
	  }
	
}

/*
 *   读一个字节数据
*/
static u_char rk2818_nand_read_byte(struct mtd_info *mtd)
{
     	struct nand_chip *nand_chip = mtd->priv;
	struct rk2818_nand_mtd *master = nand_chip->priv;
	pNANDC pRK28NC=  (pNANDC)(master->regs);
	
      	u_char ret = 0; 
	  
      	ret = (u_char)(pRK28NC ->chip[master->cs].data);

      	return ret;
}

/*
 *   读一个word 长度数据
*/
static u16 rk2818_nand_read_word(struct mtd_info *mtd)
{
     	struct nand_chip *nand_chip = mtd->priv;
	struct rk2818_nand_mtd *master = nand_chip->priv;
	pNANDC pRK28NC=  (pNANDC)(master->regs);

	
      	u_char tmp1 = 0,tmp2=0;
      	u16 ret=0;
	
      	tmp1 = (u_char)(pRK28NC ->chip[master->cs].data);
      	tmp2 = (u_char)(pRK28NC ->chip[master->cs].data);

      	ret =   (tmp2 <<8)|tmp1;
	
      	return ret;
}

static void rk2818_nand_read_buf(struct mtd_info *mtd, u_char* const buf, int len)
{
       struct nand_chip *nand_chip = mtd->priv;
	struct rk2818_nand_mtd *master = nand_chip->priv;
	pNANDC pRK28NC=  (pNANDC)(master->regs);
	uint32_t  i, chipnr;
	 
       RKNAND_LOCK();

       chipnr = master->cs ;
	   
	rk2818_nand_select_chip(mtd,chipnr);
	
	
	
	if ( len < mtd->writesize )   // read oob
	{
	 	pRK28NC ->BCHCTL = BCH_RST;
	       pRK28NC ->FLCTL = (0<<4)|FL_COR_EN|(0x1<<5)|FL_BYPASS|FL_START ; 	
		wait_op_done(mtd,TROP_US_DELAY,0);
		rk2818_nand_wait_bchdone(mtd,TROP_US_DELAY) ;  
		memcpy(buf,(u_char *)(pRK28NC->spare),4);  //  only use nandc sram0
	}
	else
       {
           pRK28NC->FLCTL |= FL_BYPASS;  // dma mode           
	    for(i=0;i<mtd->writesize/0x400;i++)
		{
		       pRK28NC ->BCHCTL = BCH_RST;
	       	pRK28NC ->FLCTL = (0<<4)|FL_COR_EN|(0x1<<5)|FL_BYPASS|FL_START ; 	
			wait_op_done(mtd,TROP_US_DELAY,0);
			rk2818_nand_wait_bchdone(mtd,TROP_US_DELAY) ;
              	memcpy(buf+i*0x400,(u_char *)(pRK28NC->buf),0x400);  //  only use nandc sram0
		}
	}

	
	
	rk2818_nand_select_chip(mtd,-1);

	RKNAND_UNLOCK();

	
	return;	

}

static void rk2818_nand_write_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
       struct nand_chip *nand_chip = mtd->priv;
	struct rk2818_nand_mtd *master = nand_chip->priv;
	pNANDC pRK28NC=  (pNANDC)(master->regs);
     
	uint32_t  i = 0, chipnr;
	   
    	 
   	 RKNAND_LOCK();

	 chipnr = master->cs ;

	rk2818_nand_select_chip(mtd,chipnr);
	
         pRK28NC->FLCTL |= FL_BYPASS;  // dma mode
         
          
	  for(i=0;i<mtd->writesize/0x400;i++)
	    {
	       memcpy((u_char *)(pRK28NC->buf),buf+i*0x400,0x400);  //  only use nandc sram0	
		pRK28NC ->BCHCTL =BCH_WR|BCH_RST;		
		pRK28NC ->FLCTL = (0<<4)|FL_COR_EN|0x1<<5|FL_RDN|FL_BYPASS|FL_START;
		wait_op_done(mtd,TROP_US_DELAY,0);	
	    }
	

	  
       rk2818_nand_select_chip(mtd,-1);
	  
	  RKNAND_UNLOCK();


}


static void rk2818_nand_cmdfunc(struct mtd_info *mtd, unsigned command,int column, int page_addr)
{
       struct nand_chip *nand_chip = mtd->priv;
	struct rk2818_nand_mtd *master = nand_chip->priv;
	pNANDC pRK28NC=  (pNANDC)(master->regs);

       uint32_t timeout = 1000;
	char status,ret;

	
	switch (command) {

       case NAND_CMD_READID:
	   	pRK28NC ->chip[master->cs].cmd = command;
		pRK28NC ->chip[master->cs].addr = 0x0;
		while (timeout>0)
		{
                 timeout --;
		   udelay(1);  
	          if(pRK28NC->FLCTL&FL_INTCLR)
			 break;
		  
		}
		
		rk2818_nand_wait_busy(mtd,READ_BUSY_COUNT);
	   	break;
		
       case NAND_CMD_READ0:
              pRK28NC ->chip[master->cs].cmd = command;
	       if ( column>= 0 )
	         {
                   pRK28NC ->chip[master->cs].addr = column & 0xff;	
                   if( mtd->writesize > 512) 
		         pRK28NC ->chip[master->cs].addr = (column >> 8) & 0xff;
	         }
		if ( page_addr>=0 )
		   {
			pRK28NC ->chip[master->cs].addr = page_addr & 0xff;
			pRK28NC ->chip[master->cs].addr = (page_addr >> 8) & 0xFF;
			pRK28NC ->chip[master->cs].addr = (page_addr >> 16) & 0xff;
		   }
		if( mtd->writesize > 512)
		    pRK28NC ->chip[0].cmd = NAND_CMD_READSTART;

		rk2818_nand_wait_busy(mtd,READ_BUSY_COUNT);
		
	   	break;
		
	case NAND_CMD_READ1:
              pRK28NC ->chip[master->cs].cmd = command;
		break;
		
       case NAND_CMD_READOOB:
		pRK28NC ->BCHCTL = 0x0;		
		if( mtd->writesize > 512 )
			command = NAND_CMD_READ0;  // 全部读，包括读oob
    		
		pRK28NC ->chip[master->cs].cmd = command;  

              if ( mtd->writesize >512 )
               {
			if ( column>= 0 )
		         {
	                   pRK28NC ->chip[master->cs].addr = column  & 0xff;	
			     pRK28NC ->chip[master->cs].addr = ( column   >> 8) & 0xff;
		         }
			if ( page_addr>=0 )
			   {
				pRK28NC ->chip[master->cs].addr = page_addr & 0xff;
				pRK28NC ->chip[master->cs].addr = (page_addr >> 8) & 0xFF;
				pRK28NC ->chip[master->cs].addr = (page_addr >> 16) & 0xff;
			   }
		    	pRK28NC ->chip[master->cs].cmd = NAND_CMD_READSTART;
              }
		else
		{
		   pRK28NC ->chip[master->cs].addr = column;
		}
			
		rk2818_nand_wait_busy(mtd,READ_BUSY_COUNT);
		
	 
	   	break;	
		
	case NAND_CMD_PAGEPROG:
		pRK28NC ->FMCTL |= FMC_WP;  //解除写保护
		pRK28NC ->chip[master->cs].cmd = command;
		rk2818_nand_wait_busy(mtd,PROGRAM_BUSY_COUNT);
		
		pRK28NC ->chip[master->cs].cmd  = NAND_CMD_STATUS;
		status = pRK28NC ->chip[master->cs].data;
		
		if(status&0x1)
			ret = -1;
		else
			ret =0;
		
		break;
		
	case NAND_CMD_ERASE1:
		pRK28NC ->FMCTL |= FMC_WP;  //解除写保护
		pRK28NC ->BCHCTL = 0x0;
		pRK28NC ->chip[master->cs].cmd  = command;
		if ( page_addr>=0 )
		   {
			pRK28NC ->chip[master->cs].addr = page_addr & 0xff;
			pRK28NC ->chip[master->cs].addr = (page_addr>>8)&0xff;
			pRK28NC ->chip[master->cs].addr = (page_addr>>16)&0xff;
		   }  
		break;
		
	case NAND_CMD_ERASE2:
		pRK28NC ->FMCTL |= FMC_WP;  //解除写保护
		pRK28NC ->chip[master->cs].cmd  = command;	       
		rk2818_nand_wait_busy(mtd,ERASE_BUSY_COUNT);
		pRK28NC ->chip[master->cs].cmd  = NAND_CMD_STATUS;
		status = pRK28NC ->chip[master->cs].data;
		
		if(status&0x1)
			ret = -1;
		else
			ret =0;
		
		break;
		
	case NAND_CMD_SEQIN:
		pRK28NC ->FMCTL |= FMC_WP;  //解除写保护
		pRK28NC ->chip[master->cs].cmd  = command;
	       udelay(1);
		if ( column>= 0 )
		  {
                   pRK28NC ->chip[master->cs].addr = column;
		     if( mtd->writesize > 512) 
		       pRK28NC ->chip[master->cs].addr = (column >> 8) & 0xff;
		  }
		if( page_addr>=0 )
		  {
			pRK28NC ->chip[master->cs].addr = page_addr & 0xff;
			pRK28NC ->chip[master->cs].addr = (page_addr>>8)&0xff;
			pRK28NC ->chip[master->cs].addr = (page_addr>>16)&0xff;
		 }
		
		break;
		
	case NAND_CMD_STATUS:
		pRK28NC ->BCHCTL = 0x0;
		pRK28NC ->chip[master->cs].cmd = command;
		while (timeout>0)
		{
                 timeout --;
		   udelay(1);  
	          if(pRK28NC->FLCTL&FL_INTCLR)
			 break;
		  
		}
		break;

	case NAND_CMD_RESET:
		pRK28NC ->chip[master->cs].cmd = command;
		while (timeout>0)
		{
                 timeout --;
		   udelay(1);  
	          if(pRK28NC->FLCTL&FL_INTCLR)
			 break;
		  
		}
		rk2818_nand_wait_busy(mtd,RESET_BUSY_COUNT);
		break;

	/* This applies to read commands */
	default:
	       pRK28NC ->chip[master->cs].cmd = command;
		break;
	}

	udelay (1);
   
}

int rk2818_nand_calculate_ecc(struct mtd_info *mtd,const uint8_t *dat,uint8_t *ecc_code)
{
     struct nand_chip *nand_chip = mtd->priv;
     struct rk2818_nand_mtd *master = nand_chip->priv;
     pNANDC pRK28NC=  (pNANDC)(master->regs);
 
     int eccdata[7],i;
	 
	for(i=0;i<7;i++) 
	 {
	    eccdata[i] = pRK28NC->spare[i+1];

		   
	    ecc_code[i*4] = eccdata[i]& 0xff;
	    ecc_code[i*4+1] = (eccdata[i]>> 8)& 0xff;
	    ecc_code[i*4+2] = (eccdata[i]>>16)& 0xff;
	    ecc_code[i*4+3] = (eccdata[i]>>24)& 0xff;
		  
	  }		
	
     return 0;
}

 void rk2818_nand_hwctl_ecc(struct mtd_info *mtd, int mode)
 {
       struct nand_chip *nand_chip = mtd->priv;
     	struct rk2818_nand_mtd *master = nand_chip->priv;
     	pNANDC pRK28NC=  (pNANDC)(master->regs);

	pRK28NC->BCHCTL = 1;  // reset bch and enable hw ecc
		
       return;
 }
 
 int rk2818_nand_correct_data(struct mtd_info *mtd, uint8_t *dat, uint8_t *read_ecc,uint8_t *calc_ecc)
 {
       struct nand_chip *nand_chip = mtd->priv;
     	struct rk2818_nand_mtd *master = nand_chip->priv;
     	pNANDC pRK28NC=  (pNANDC)(master->regs);		

	// hw correct data
       if( pRK28NC->BCHST & (1<<2) )
	 {
		DEBUG(MTD_DEBUG_LEVEL0,
		      "rk2818 nand :hw ecc uncorrectable error\n");
		return -1;
	}
	
       return 0;
 }
 
 int rk2818_nand_read_page(struct mtd_info *mtd,struct nand_chip *chip,uint8_t *buf, int page)
 {
       struct nand_chip *nand_chip = mtd->priv;
     	struct rk2818_nand_mtd *master = nand_chip->priv;
     	pNANDC pRK28NC=  (pNANDC)(master->regs);
	  
	int i,chipnr;

	   
	RKNAND_LOCK();

	chipnr = master->cs ;
	
	rk2818_nand_select_chip(mtd,chipnr);
 
 
	rk2818_nand_wait_busy(mtd,READ_BUSY_COUNT);
	   
       pRK28NC->FLCTL |= FL_BYPASS;  // dma mode
	  
	for(i=0;i<mtd->writesize/0x400;i++)
	{
	       pRK28NC ->BCHCTL = BCH_RST;
	       pRK28NC ->FLCTL = (0<<4)|FL_COR_EN|(0x1<<5)|FL_BYPASS|FL_START ; 	
		wait_op_done(mtd,TROP_US_DELAY,0);   
		rk2818_nand_wait_bchdone(mtd,TROP_US_DELAY) ;
          
              memcpy(buf+i*0x400,(u_char *)(pRK28NC->buf),0x400);  //  only use nandc sram0
	}
	
		
	rk2818_nand_select_chip(mtd,-1);

	RKNAND_UNLOCK();
	
    return 0;
	
 }

void  rk2818_nand_write_page(struct mtd_info *mtd,struct nand_chip *chip,const uint8_t *buf)
 {
       struct nand_chip *nand_chip = mtd->priv;
	struct rk2818_nand_mtd *master = nand_chip->priv;
	pNANDC pRK28NC=  (pNANDC)(master->regs);
       uint32_t  i = 0, chipnr;
	   
	RKNAND_LOCK();

       chipnr = master->cs ;
	   
	rk2818_nand_select_chip(mtd,chipnr);
	
	pRK28NC->FLCTL |= FL_BYPASS;  // dma mode

   		
	  for(i=0;i<mtd->writesize/0x400;i++)
	   {
	       memcpy((u_char *)(pRK28NC->buf),(buf+i*0x400),0x400);  //  only use nandc sram0		
	       if(i==0)
		   memcpy((u_char *)(pRK28NC->spare),(u_char *)(chip->oob_poi + chip->ops.ooboffs),4);  
		   	
		pRK28NC ->BCHCTL = BCH_WR|BCH_RST;		
		pRK28NC ->FLCTL = (0<<4)|FL_COR_EN|(0x1<<5)|FL_RDN|FL_BYPASS|FL_START;
		wait_op_done(mtd,TROP_US_DELAY,0);	
	   }

         pRK28NC ->chip[0].cmd = NAND_CMD_PAGEPROG;
	 

	  
	  rk2818_nand_wait_busy(mtd,PROGRAM_BUSY_COUNT);
	  
	 rk2818_nand_select_chip(mtd,-1);

     RKNAND_UNLOCK();
	
    return;
	  
 }

int rk2818_nand_read_oob(struct mtd_info *mtd, struct nand_chip *chip, int page, int sndcmd)
{	
       struct nand_chip *nand_chip = mtd->priv;
     	struct rk2818_nand_mtd *master = nand_chip->priv;
     	pNANDC pRK28NC=  (pNANDC)(master->regs);
       int i,chipnr;

	  
	if (sndcmd) {
		chip->cmdfunc(mtd, NAND_CMD_READOOB, 0, page);
		sndcmd = 0;
	}

    	RKNAND_LOCK();

	chipnr = master->cs ;

	rk2818_nand_select_chip(mtd,chipnr);

	rk2818_nand_wait_busy(mtd,READ_BUSY_COUNT);

	
       pRK28NC->FLCTL |= FL_BYPASS;  // dma mode

	

	
	for(i=0;i<mtd->writesize/0x400;i++)
	{
	       pRK28NC ->BCHCTL = BCH_RST;
	       pRK28NC ->FLCTL = (0<<4)|FL_COR_EN|(0x1<<5)|FL_BYPASS|FL_START ; 	
		wait_op_done(mtd,TROP_US_DELAY,0);   
		rk2818_nand_wait_bchdone(mtd,TROP_US_DELAY) ;          
              if(i==0)
                 memcpy((u_char *)(chip->oob_poi+ chip->ops.ooboffs),(u_char *)(pRK28NC->spare),4); 
	}

	   
 	 rk2818_nand_select_chip(mtd,-1);

	 RKNAND_UNLOCK();


	return sndcmd;
}

int	rk2818_nand_read_page_raw(struct mtd_info *mtd, struct nand_chip *chip, uint8_t *buf, int page)
{
       struct nand_chip *nand_chip = mtd->priv;
     	struct rk2818_nand_mtd *master = nand_chip->priv;
     	pNANDC pRK28NC=  (pNANDC)(master->regs);

	int i,chipnr;

	
    RKNAND_LOCK();

	chipnr = master->cs ;
	
	rk2818_nand_select_chip(mtd,chipnr);

	rk2818_nand_wait_busy(mtd,READ_BUSY_COUNT);
	   
       pRK28NC->FLCTL |= FL_BYPASS;  // dma mode

 	
	   
	for(i=0;i<mtd->writesize/0x400;i++)
	{
	       pRK28NC ->BCHCTL = BCH_RST;
	       pRK28NC ->FLCTL = (0<<4)|FL_COR_EN|(0x1<<5)|FL_BYPASS|FL_START ; 	
		wait_op_done(mtd,TROP_US_DELAY,0);   
		rk2818_nand_wait_bchdone(mtd,TROP_US_DELAY) ;          
              memcpy(buf+i*0x400,(u_char *)(pRK28NC->buf),0x400);  //  only use nandc sram0
              if(i==0)
                 memcpy((u_char *)(chip->oob_poi+ chip->ops.ooboffs),(u_char *)(pRK28NC->spare),4); 
	}

	 
	 rk2818_nand_select_chip(mtd,-1);
	 RKNAND_UNLOCK();

	 
    return 0;
}

static int rk2818_nand_setrate(struct rk2818_nand_mtd *info)
{
	pNANDC pRK28NC=  (pNANDC)(info->regs);
	
	unsigned long clkrate = clk_get_rate(info->clk);

       u_char accesstime,rwpw,csrw,rwcs;

	unsigned int ns=0,timingcfg;

       unsigned long flags; 

       //scan nand flash access time
       if ( info->accesstime ==0x00 )
              accesstime=50;
       else if ( info->accesstime==0x80)
        	accesstime=25;
    	else if ( info->accesstime==0x08)
        	accesstime=20;
    	else
        	accesstime=60;   //60ns

       info->clk_rate = clkrate;
	clkrate /= 1000000;	/* turn clock into MHz for ease of use */
	
       if(clkrate>0 && clkrate<200)
	   ns= 1000/clkrate; // ns
	 else
	   return -1;
	   	
	timingcfg = (accesstime + ns -1)/ns;

	timingcfg = (timingcfg>=3) ? (timingcfg-2) : timingcfg;           //csrw+1, rwcs+1

	rwpw = timingcfg-timingcfg/4;
	csrw = timingcfg/4;
	rwcs = (timingcfg/4 >=1)?(timingcfg/4):1;

	RKNAND_LOCK();

	pRK28NC ->FMWAIT |=  (rwcs<<FMW_RWCS_OFFSET)|(rwpw<<FMW_RWPW_OFFSET)|(csrw<<FMW_CSRW_OFFSET);

	RKNAND_UNLOCK();


	return 0;
}

/* cpufreq driver support */

#ifdef CONFIG_CPU_FREQ

static int rk2818_nand_cpufreq_transition(struct notifier_block *nb, unsigned long val, void *data)
{
	struct rk2818_nand_mtd *info;
	unsigned long newclk;

	info = container_of(nb, struct rk2818_nand_mtd, freq_transition);
	newclk = clk_get_rate(info->clk);

	if (val == CPUFREQ_POSTCHANGE && newclk != info->clk_rate) 
	 {
		rk2818_nand_setrate(info);
	}

	return 0;
}

static inline int rk2818_nand_cpufreq_register(struct rk2818_nand_mtd *info)
{
	info->freq_transition.notifier_call = rk2818_nand_cpufreq_transition;

	return cpufreq_register_notifier(&info->freq_transition, CPUFREQ_TRANSITION_NOTIFIER);
}

static inline void rk2818_nand_cpufreq_deregister(struct rk2818_nand_mtd *info)
{
	cpufreq_unregister_notifier(&info->freq_transition, CPUFREQ_TRANSITION_NOTIFIER);
}

#else
static inline int rk2818_nand_cpufreq_register(struct rk2818_nand_mtd *info)
{
	return 0;
}

static inline void rk2818_nand_cpufreq_deregister(struct rk2818_nand_mtd *info)
{
}
#endif


static int rk2818_nand_probe(struct platform_device *pdev)
{
       struct nand_chip *this;
	struct mtd_info *mtd;
	struct rk2818_nand_platform_data *pdata = pdev->dev.platform_data;
	struct rk2818_nand_mtd *master;
	struct resource *res;
	int err = 0;
	pNANDC pRK28NC;
	u_char  maf_id,dev_id,ext_id3,ext_id4;
    struct nand_chip *chip;
    
#ifdef CONFIG_MTD_PARTITIONS
	struct mtd_partition *partitions = NULL;
	int num_partitions = 0;
#endif

      /* Allocate memory for MTD device structure and private data */
	master = kzalloc(sizeof(struct rk2818_nand_mtd), GFP_KERNEL);
	if (!master)
		return -ENOMEM;

	 master->dev = &pdev->dev;
	/* structures must be linked */
	this = &master->nand;
	mtd = &master->mtd;
	mtd->priv = this;
	mtd->owner = THIS_MODULE;
       mtd->name = dev_name(&pdev->dev);
	   
	/* 50 us command delay time */
	this->chip_delay = 5;

	this->priv = master;
	this->dev_ready = rk2818_nand_dev_ready;
	this->cmdfunc = rk2818_nand_cmdfunc;
	this->select_chip = rk2818_nand_select_chip;
	this->read_byte = rk2818_nand_read_byte;
	this->read_word = rk2818_nand_read_word;
	this->write_buf = rk2818_nand_write_buf;
	this->read_buf = rk2818_nand_read_buf;
	this->options |= NAND_USE_FLASH_BBT;    // open bbt options
	
	   
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		err = -ENODEV;
		goto outres;
	}

	master->regs = ioremap(res->start, res->end - res->start + 1);
	if (!master->regs) {
		err = -EIO;
		goto outres;
	}

	if (pdata->hw_ecc) {
		this->ecc.calculate = rk2818_nand_calculate_ecc;
		this->ecc.hwctl = rk2818_nand_hwctl_ecc;
		this->ecc.correct = rk2818_nand_correct_data;
		this->ecc.mode = NAND_ECC_HW;
		this->ecc.read_page = rk2818_nand_read_page;
		this->ecc.write_page = rk2818_nand_write_page;
		this->ecc.read_oob = rk2818_nand_read_oob;
		this->ecc.read_page_raw = rk2818_nand_read_page_raw;
		this->ecc.size = 1024;
		this->ecc.bytes = 28;
		this->ecc.layout = &nand_hw_eccoob_16;	
	} else {
		this->ecc.size = 256;
		this->ecc.bytes = 3;
		this->ecc.layout = &nand_sw_eccoob_8;
		this->ecc.mode = NAND_ECC_SOFT;		
	}



	master->clk = clk_get(NULL, "nandc");

	clk_enable(master->clk);
	
       pRK28NC =  (pNANDC)(master->regs);
       pRK28NC ->FMCTL = FMC_WP|FMC_FRDY;
       pRK28NC ->FMWAIT |=  (1<<FMW_RWCS_OFFSET)|(4<<FMW_RWPW_OFFSET)|(1<<FMW_CSRW_OFFSET);
	pRK28NC ->BCHCTL = 0x1;

       this->select_chip(mtd, 0);
	this->cmdfunc(mtd, NAND_CMD_READID, 0x00, -1);
	maf_id = this->read_byte(mtd);
	dev_id = this->read_byte(mtd);
       ext_id3 = this->read_byte(mtd);
	ext_id4 = this->read_byte(mtd);
	
       master->accesstime = ext_id4&0x88;
	
	rk2818_nand_setrate(master);
	
	/* Reset NAND */
	this->cmdfunc(mtd, NAND_CMD_RESET, -1, -1);
	/* NAND bus width determines access funtions used by upper layer */
	if (pdata->width == 2) {
		this->options |= NAND_BUSWIDTH_16;
		this->ecc.layout = &nand_hw_eccoob_16;
	}
      // iomux flash  cs1~cs7
    if (pdata && pdata->io_init) {
        pdata->io_init();
    }  
   
	/* Scan to find existence of the device */
	   if (nand_scan(mtd, 8)) {     // rk2818 nandc support max 8 cs
	 
		DEBUG(MTD_DEBUG_LEVEL0,
		      "RK2818 NAND: Unable to find any NAND device.\n");
		err = -ENXIO;
		goto outscan;
	}

	//根据片选情况恢复IO MUX原始值
#if 0
    chip = mtd->priv;
    switch(chip->numchips)
    {
        case 1:
            rk2818_mux_api_mode_resume(GPIOA5_FLASHCS1_SEL_NAME);
        case 2:
            rk2818_mux_api_mode_resume(GPIOA6_FLASHCS2_SEL_NAME);
        case 3:
            rk2818_mux_api_mode_resume(GPIOA7_FLASHCS3_SEL_NAME);
        case 4:
            rk2818_mux_api_mode_resume(GPIOE_SPI1_FLASH_SEL1_NAME);
        case 5:
        case 6:
            rk2818_mux_api_mode_resume(GPIOE_SPI1_FLASH_SEL_NAME);
        case 7:
        case 8:
            break;
        default:
            DEBUG(MTD_DEBUG_LEVEL0, "RK2818 NAND: numchips error!!!\n");
    }
#endif    
#if 0
      // rk281x dma mode bch must  (1k data + 32 oob) bytes align , so cheat system writesize =1024,oobsize=32
	mtd->writesize = 1024;
	mtd->oobsize = 32;
#endif

#ifdef CONFIG_MTD_PARTITIONS
        num_partitions = parse_mtd_partitions(mtd, part_probes, &partitions, 0);
	if (num_partitions > 0) {
		printk(KERN_INFO "Using commandline partition definition\n");
              add_mtd_partitions(mtd, partitions, num_partitions);
                if(partitions)
		 kfree(partitions);
	} else if (pdata->nr_parts) {
		printk(KERN_INFO "Using board partition definition\n");
		add_mtd_partitions(mtd, pdata->parts, pdata->nr_parts);
	} else
#endif
	{
		printk(KERN_INFO "no partition info available, registering whole flash at once\n");
		add_mtd_device(mtd);
	}

	platform_set_drvdata(pdev, master);

	err =rk2818_nand_cpufreq_register(master);
	if (err < 0) {
		printk(KERN_ERR"rk2818 nand failed to init cpufreq support\n");
		goto outscan;
	}

	return 0;
	
outres:
outscan:
	iounmap(master->regs);
	kfree(master);

	return err;
	
}

static int rk2818_nand_remove(struct platform_device *pdev)
{
	struct rk2818_nand_mtd *master = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

       if(master == NULL)
	  	return 0;

	rk2818_nand_cpufreq_deregister(master);
	

	nand_release(&master->mtd);

	if(master->regs!=NULL){
		iounmap(master->regs);
	      	master->regs = NULL;
	}

	if (master->clk != NULL && !IS_ERR(master->clk)) {
		clk_disable(master->clk);
		clk_put(master->clk);
	}
	
	kfree(master);

	return 0;
}

#ifdef CONFIG_PM
static int rk2818_nand_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct mtd_info *info = platform_get_drvdata(pdev);
	int ret = 0;

	DEBUG(MTD_DEBUG_LEVEL0, "RK2818_NAND : NAND suspend\n");
	if (info)
		ret = info->suspend(info);
	return ret;
}

static int rk2818_nand_resume(struct platform_device *pdev)
{
	struct mtd_info *info = platform_get_drvdata(pdev);
	int ret = 0;

	DEBUG(MTD_DEBUG_LEVEL0, "RK2818_NAND : NAND resume\n");
	/* Enable the NFC clock */

	if (info)
		info->resume(info);

	return ret;
}
#else
#define rk2818_nand_suspend   NULL
#define rk2818_nand_resume    NULL
#endif	/* CONFIG_PM */


static struct platform_driver rk2818_nand_driver = {
	.driver = {
		   .name = "rk2818-nand",
		   },
       .probe    = rk2818_nand_probe,
	.remove = rk2818_nand_remove,
	.suspend = rk2818_nand_suspend,
	.resume = rk2818_nand_resume,
};

static int __init rk2818_nand_init(void)
{
	/* Register the device driver structure. */
	printk("rk2818_nand_init\n");
	return platform_driver_register(&rk2818_nand_driver);;
}

static void __exit rk2818_nand_exit(void)
{
	/* Unregister the device structure */
	platform_driver_unregister(&rk2818_nand_driver);
}

#ifdef CONFIG_DM9000_USE_NAND_CONTROL
// nandc dma cs mutex for dm9000 interface
void rk2818_nand_status_mutex_lock(void)
{
     pNANDC pRK28NC=  (pNANDC)RK2818_NANDC_BASE;
     mutex_lock(&rknand_mutex);
     pRK28NC->FMCTL &=0xffffff00;   // release chip select

}

int rk2818_nand_status_mutex_trylock(void)
{
     pNANDC pRK28NC=  (pNANDC)RK2818_NANDC_BASE;
     if( mutex_trylock(&rknand_mutex))
     	{
	 	pRK28NC->FMCTL &=0xffffff00;   // release chip select
	 	return 1;      // ready 
     	}
      else
	  	return 0;     // busy
}

void rk2818_nand_status_mutex_unlock(void)
{
     mutex_unlock(&rknand_mutex);
     return;
}
#endif

module_init(rk2818_nand_init);
module_exit(rk2818_nand_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("hxy <hxy@rock-chips.com>");
MODULE_DESCRIPTION("MTD NAND driver for rk2818 device");

