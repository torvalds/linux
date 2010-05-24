
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
#include <linux/err.h>
#include <linux/io.h>

#include <mach/rk2818_nand.h>

#define PROGRAM_BUSY_COUNT   10000
#define ERASE_BUSY_COUNT	    20000
#define READ_BUSY_COUNT   	    5000

/* Define delays in microsec for NAND device operations */
#define TROP_US_DELAY   2000

struct rk2818_nand_mtd {
	struct mtd_info		mtd;
	struct nand_chip		nand;
	struct mtd_partition	*parts;
	struct device		*dev;
       const struct rk2818_nand_flash *flash_info;

	struct clk		*clk;
	void __iomem		*regs;
	uint16_t			col_addr;	
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
	
	pRK28NC ->FMCTL |= 0x1<<chip;
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
      	pRK28NC->FLCTL &= ~FL_BYPASS;  // bypass mode
      	ret = (u_char)(pRK28NC ->chip[0].data);
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

      	pRK28NC->FLCTL &= ~FL_BYPASS;  // bypass mode
	  
      	tmp1 = (u_char)(pRK28NC ->chip[0].data);
      	tmp2 = (u_char)(pRK28NC ->chip[0].data);

      	ret =   (tmp2 <<8)|tmp1;
	  
      	return ret;
}

static void rk2818_nand_read_buf(struct mtd_info *mtd, u_char* const buf, int len)
{
       struct nand_chip *nand_chip = mtd->priv;
	struct rk2818_nand_mtd *master = nand_chip->priv;
	pNANDC pRK28NC=  (pNANDC)(master->regs);
	uint32_t  i;
	
   
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
	
	return;	

}

static void rk2818_nand_write_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
       struct nand_chip *nand_chip = mtd->priv;
	struct rk2818_nand_mtd *master = nand_chip->priv;
	pNANDC pRK28NC=  (pNANDC)(master->regs);
     
	uint32_t  i = 0;

         pRK28NC->FLCTL |= FL_BYPASS;  // dma mode
	  for(i=0;i<mtd->writesize/0x400;i++)
	    {
	       memcpy((u_char *)(pRK28NC->buf),buf+i*0x400,0x400);  //  only use nandc sram0	
		pRK28NC ->BCHCTL =BCH_WR|BCH_RST;		
		pRK28NC ->FLCTL = (0<<4)|FL_COR_EN|0x1<<5|FL_RDN|FL_BYPASS|FL_START;
		wait_op_done(mtd,TROP_US_DELAY,0);	
	    }

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
	   	pRK28NC ->chip[0].cmd = command;
		pRK28NC ->chip[0].addr = 0x0;
		while (timeout>0)
		{
                 timeout --;
		   udelay(1);  
	          if(pRK28NC->FLCTL&FL_INTCLR)
			 break;
		  
		}
		
	   	break;
		
       case NAND_CMD_READ0:
              pRK28NC ->chip[0].cmd = command;
	       if ( column>= 0 )
	         {
                   pRK28NC ->chip[0].addr = column & 0xff;	
                   if( mtd->writesize > 512) 
		         pRK28NC ->chip[0].addr = (column >> 8) & 0xff;
	         }
		if ( page_addr>=0 )
		   {
			pRK28NC ->chip[0].addr = page_addr & 0xff;
			pRK28NC ->chip[0].addr = (page_addr >> 8) & 0xFF;
			pRK28NC ->chip[0].addr = (page_addr >> 16) & 0xff;
		   }
		if( mtd->writesize > 512)
		    pRK28NC ->chip[0].cmd = NAND_CMD_READSTART;

		rk2818_nand_wait_busy(mtd,READ_BUSY_COUNT);
		
	   	break;
		
	case NAND_CMD_READ1:
              pRK28NC ->chip[0].cmd = command;
		break;
		
       case NAND_CMD_READOOB:
		pRK28NC ->BCHCTL = 0x0;		
		if( mtd->writesize > 512 )
			command = NAND_CMD_READ0;  // 全部读，包括读oob
    		
		pRK28NC ->chip[0].cmd = command;  

              if ( mtd->writesize >512 )
               {
			if ( column>= 0 )
		         {
	                   pRK28NC ->chip[0].addr = (column + mtd->writesize) & 0xff;	
			     pRK28NC ->chip[0].addr = ( (column + mtd->writesize)  >> 8) & 0xff;
		         }
			if ( page_addr>=0 )
			   {
				pRK28NC ->chip[0].addr = page_addr & 0xff;
				pRK28NC ->chip[0].addr = (page_addr >> 8) & 0xFF;
				pRK28NC ->chip[0].addr = (page_addr >> 16) & 0xff;
			   }
		    	pRK28NC ->chip[0].cmd = NAND_CMD_READSTART;
              }
		else
		{
		   pRK28NC ->chip[0].addr = column;
		}
			
		rk2818_nand_wait_busy(mtd,READ_BUSY_COUNT);
		
	 
	   	break;	
		
	case NAND_CMD_PAGEPROG:
		pRK28NC ->FMCTL |= FMC_WP;  //解除写保护
		pRK28NC ->chip[0].cmd = command;
		rk2818_nand_wait_busy(mtd,PROGRAM_BUSY_COUNT);
		
		pRK28NC ->chip[0].cmd  = NAND_CMD_STATUS;
		status = pRK28NC ->chip[0].data;
		
		if(status&0x1)
			ret = -1;
		else
			ret =0;
		
		break;
		
	case NAND_CMD_ERASE1:
		pRK28NC ->FMCTL |= FMC_WP;  //解除写保护
		pRK28NC ->BCHCTL = 0x0;
		pRK28NC ->chip[0].cmd  = command;
		if ( page_addr>=0 )
		   {
			pRK28NC ->chip[0].addr = page_addr & 0xff;
			pRK28NC ->chip[0].addr = (page_addr>>8)&0xff;
			pRK28NC ->chip[0].addr = (page_addr>>16)&0xff;
		   }  
		break;
		
	case NAND_CMD_ERASE2:
		pRK28NC ->FMCTL |= FMC_WP;  //解除写保护
		pRK28NC ->chip[0].cmd  = command;	       
		rk2818_nand_wait_busy(mtd,ERASE_BUSY_COUNT);
		pRK28NC ->chip[0].cmd  = NAND_CMD_STATUS;
		status = pRK28NC ->chip[0].data;
		
		if(status&0x1)
			ret = -1;
		else
			ret =0;
		
		break;
		
	case NAND_CMD_SEQIN:
		pRK28NC ->FMCTL |= FMC_WP;  //解除写保护
		pRK28NC ->chip[0].cmd  = command;
	       udelay(1);
		if ( column>= 0 )
		  {
                   pRK28NC ->chip[0].addr = column;
		     if( mtd->writesize > 512) 
		       pRK28NC ->chip[0].addr = (column >> 8) & 0xff;
		  }
		if( page_addr>=0 )
		  {
			pRK28NC ->chip[0].addr = page_addr & 0xff;
			pRK28NC ->chip[0].addr = (page_addr>>8)&0xff;
			pRK28NC ->chip[0].addr = (page_addr>>16)&0xff;
		 }
		
		break;
		
	case NAND_CMD_STATUS:
		pRK28NC ->BCHCTL = 0x0;
		pRK28NC ->chip[0].cmd = command;
		break;

	case NAND_CMD_RESET:
		pRK28NC ->chip[0].cmd = command;
		break;

	/* This applies to read commands */
	default:
	       pRK28NC ->chip[0].cmd = command;
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

	  
	int i;
	

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
		
    return 0;
	
 }

void  rk2818_nand_write_page(struct mtd_info *mtd,struct nand_chip *chip,const uint8_t *buf)
 {
       struct nand_chip *nand_chip = mtd->priv;
	struct rk2818_nand_mtd *master = nand_chip->priv;
	pNANDC pRK28NC=  (pNANDC)(master->regs);
       uint32_t  i = 0;


	
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
	  
	
    return;
	  
 }

int rk2818_nand_read_oob(struct mtd_info *mtd, struct nand_chip *chip, int page, int sndcmd)
{	
       struct nand_chip *nand_chip = mtd->priv;
     	struct rk2818_nand_mtd *master = nand_chip->priv;
     	pNANDC pRK28NC=  (pNANDC)(master->regs);
       int i;

	if (sndcmd) {
		chip->cmdfunc(mtd, NAND_CMD_READOOB, 0, page);
		sndcmd = 0;
	}
		
	rk2818_nand_wait_busy(mtd,READ_BUSY_COUNT);
	   
       pRK28NC->FLCTL |= FL_BYPASS;  // dma mode

	for(i=0;i<mtd->writesize/0x400;i++)
	{
	       pRK28NC ->BCHCTL = BCH_RST;
	       pRK28NC ->FLCTL = (0<<4)|FL_COR_EN|(0x1<<5)|FL_BYPASS|FL_START ; 	
		wait_op_done(mtd,TROP_US_DELAY,0);   
		rk2818_nand_wait_bchdone(mtd,TROP_US_DELAY) ;          
         //     memcpy(buf+i*0x400,(u_char *)(pRK28NC->buf),0x400);  //  only use nandc sram0
              if(i==0)
                 memcpy((u_char *)(chip->oob_poi+ chip->ops.ooboffs),(u_char *)(pRK28NC->spare),4); 
	}


	return sndcmd;
}

int	rk2818_nand_read_page_raw(struct mtd_info *mtd, struct nand_chip *chip, uint8_t *buf, int page)
{
       struct nand_chip *nand_chip = mtd->priv;
     	struct rk2818_nand_mtd *master = nand_chip->priv;
     	pNANDC pRK28NC=  (pNANDC)(master->regs);

	  
	int i;
	

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
		
    return 0;
}


static int rk2818_nand_probe(struct platform_device *pdev)
{
       struct nand_chip *this;
	struct mtd_info *mtd;
	struct rk2818_nand_platform_data *pdata = pdev->dev.platform_data;
	struct rk2818_nand_mtd *master;
	struct resource *res;
	int err = 0;
	pNANDC pRK28NC;

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

       pRK28NC =  (pNANDC)(master->regs);
       pRK28NC ->FMCTL = FMC_WP|FMC_FRDY|(0x1<<0);
       pRK28NC ->FMWAIT |=  (1<<FMW_RWCS_OFFSET)|(4<<FMW_RWPW_OFFSET)|(1<<FMW_CSRW_OFFSET);
	pRK28NC ->BCHCTL = 0x1;
	
	/* Reset NAND */
	this->cmdfunc(mtd, NAND_CMD_RESET, -1, -1);
	/* NAND bus width determines access funtions used by upper layer */
	if (pdata->width == 2) {
		this->options |= NAND_BUSWIDTH_16;
		this->ecc.layout = &nand_hw_eccoob_16;
	}

	/* Scan to find existence of the device */
	if (nand_scan(mtd, 1)) {
		DEBUG(MTD_DEBUG_LEVEL0,
		      "RK2818 NAND: Unable to find any NAND device.\n");
		err = -ENXIO;
		goto outscan;
	}

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

	nand_release(&master->mtd);
	iounmap(master->regs);
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

module_init(rk2818_nand_init);
module_exit(rk2818_nand_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("hxy <hxy@rock-chips.com>");
MODULE_DESCRIPTION("MTD NAND driver for rk2818 device");

