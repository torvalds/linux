
/*
 * drivers/mtd/nand/rk29_nand.c
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

#include <mach/rk29_nand.h>
#include <mach/rk29_iomap.h>
#include <mach/iomux.h>

#define PROGRAM_BUSY_COUNT   10000
#define ERASE_BUSY_COUNT	    20000
#define READ_BUSY_COUNT   	    5000
#define RESET_BUSY_COUNT			20000

/* Define delays in microsec for NAND device operations */
#define TROP_US_DELAY   2000
#define NAND_FLAG_WRITE 1

#if 1
#define FLASH_DEBUG(x...) do{printk(x);}while(0)
#else
#define FLASH_DEBUG(s,x...)
#endif

#ifdef CONFIG_DM9000_USE_NAND_CONTROL
static DEFINE_MUTEX(rknand_mutex);
#define RKNAND_LOCK()   do { int panic = in_interrupt() | in_atomic(); if (!panic) mutex_lock(&rknand_mutex); } while (0)
#define RKNAND_UNLOCK() do { int panic = in_interrupt() | in_atomic(); if (!panic) mutex_unlock(&rknand_mutex); } while (0)
#else
#define RKNAND_LOCK()   do {} while (0)
#define RKNAND_UNLOCK() do {} while (0)
#endif

struct rk29_nand_mtd {
	struct mtd_info		mtd;
	struct nand_chip		nand;
	struct mtd_partition	*parts;
	struct device		*dev;
       const struct rk29_nand_flash *flash_info;

	struct clk			*clk;
	unsigned long		 	clk_rate;
	void __iomem			*regs;
	int					cs;	   		// support muliple nand chip,record current chip select
	u_char 				accesstime;
#ifdef CONFIG_CPU_FREQ
	struct notifier_block	freq_transition;
#endif

};
static int read_in_refresh = 0;
static int gbRefresh = 0;

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

static void rk29_nand_wait_ready( struct mtd_info *mtd )
{
	struct nand_chip *nand_chip = mtd->priv;
	struct rk29_nand_mtd *master = nand_chip->priv;
	pNANDC pRK29NC=  (pNANDC)(master->regs);
	uint32_t timeout = 1000;
	
	while (timeout>0)
	{
             timeout --;		   
          if(pRK29NC->FMCTL&FMC_FRDY)
		 break;
	   udelay(1);  	  
	}
	return;
}

static void rk29_nand_wait_busy(struct mtd_info *mtd, uint32_t timeout)
{
      
    struct nand_chip *nand_chip = mtd->priv;
	struct rk29_nand_mtd *master = nand_chip->priv;
	pNANDC pRK29NC=  (pNANDC)(master->regs);
	
	while (timeout > 0)
	{
		timeout--;
		udelay(1);
		if ( pRK29NC->FMCTL& FMC_FRDY) 
			break;
		
	}
	
    return;
}

static void rk29_nand_wait_bchdone(struct mtd_info *mtd, uint32_t timeout)
{
      
    struct nand_chip *nand_chip = mtd->priv;
	struct rk29_nand_mtd *master = nand_chip->priv;
	pNANDC pRK29NC=  (pNANDC)(master->regs);
	
	while (timeout > 0)
	{
		timeout--;
		udelay(1);
		if(pRK29NC->BCHST[0] &(1<<1))
			break;		
	}
	
    return;
}

// only for dma mode 
static void wait_op_done(struct mtd_info *mtd, int max_retries, uint16_t param)
{
       struct nand_chip *nand_chip = mtd->priv;
	struct rk29_nand_mtd *master = nand_chip->priv;
	pNANDC pRK29NC=  (pNANDC)(master->regs);
	
	while (max_retries-- > 0) {
		udelay(1);
		if (pRK29NC->FLCTL & FL_RDY)
			break;		
	}	      
}

static int rk29_nand_dev_ready(struct mtd_info *mtd)
{
      	struct nand_chip *nand_chip = mtd->priv;
	struct rk29_nand_mtd *master = nand_chip->priv;
	pNANDC pRK29NC=  (pNANDC)(master->regs);
	  
      	if(pRK29NC->FMCTL& FMC_FRDY)
	   return 1;
	else
	   return 0;
}
void mark_reserve_region(struct mtd_info *mtd,struct nand_bbt_descr *td,struct nand_bbt_descr *md)
{
    int i, block, nrblocks, tdblock, update = 0;
    struct nand_chip *this = mtd->priv;
    uint8_t oldval, newval;

    tdblock = (td->maxblocks >= md->maxblocks)?td->maxblocks:md->maxblocks;
    nrblocks = (int)(mtd->size >> this->bbt_erase_shift);
    block = nrblocks - tdblock - RK29_RESERVE_BLOCK_NUM;
    block <<= 1;

    for(i=0; i<RK29_RESERVE_BLOCK_NUM; i++) {
            oldval = this->bbt[(block>>3)];
            newval = oldval|(0x2 << (block & 0x06));
            this->bbt[(block>>3)] = newval;
            if (oldval != newval)
                update = 1;
            block += 2;
        }
  
    if(update&&td->reserved_block_code)
    {
        printk("mark_reserve_region need update!\n");
        nand_update_bbt(mtd, (loff_t)(block - 2) <<
                (this->bbt_erase_shift - 1));
    }
}

EXPORT_SYMBOL_GPL(mark_reserve_region);

static int rk29_nand_erase(struct mtd_info *mtd, int srcAddr)
{
    struct nand_chip *this = mtd->priv;
    int status;

    //printk(">>>>>>> erase page [%d]\n", srcAddr>>this->page_shift);
    this->select_chip(mtd, 0);
    this->cmdfunc(mtd, NAND_CMD_ERASE1, -1, srcAddr>>this->page_shift);
	this->cmdfunc(mtd, NAND_CMD_ERASE2, -1, -1);
    status = this->waitfunc(mtd, this);
    if(status&NAND_STATUS_FAIL){
        FLASH_DEBUG("%s: %s erase failed!\n", __FILE__,__FUNCTION__);
        return -1;
        }
    return 0;
}

static int rk29_get_swap_block_erased(struct mtd_info *mtd, int bdown)
{
    struct nand_chip *this = mtd->priv;
    struct nand_bbt_descr *td = this->bbt_td;
    struct nand_bbt_descr *md = this->bbt_md;
    int nrblocks, block, tdblock, startblock, i, fward;

    tdblock = (td->maxblocks > md->maxblocks)?td->maxblocks:md->maxblocks;
    nrblocks = (int)(mtd->size >> this->bbt_erase_shift);
    if(bdown){
        startblock = nrblocks-tdblock-1;
        fward = -1;
        }
    else{
        startblock = nrblocks-tdblock-RK29_RESERVE_BLOCK_NUM;
        fward = 1;
        }
    
    for(i=0; i<RK29_RESERVE_BLOCK_NUM; i++){
        block = startblock + fward*i;
        if(((this->bbt[block>>2]>>(2*(block & 0x03)))&0x03)==0x02){
            if(rk29_nand_erase(mtd, block<<this->phys_erase_shift)){
                mtd->block_markbad(mtd, block<<this->phys_erase_shift);
                FLASH_DEBUG("%s: %s erase failed!\n", __FILE__,__FUNCTION__);
                }
            else{
                return block<<this->phys_erase_shift;
                }
        }
    }
    return 0;
}

static int rk29_block_copy(struct mtd_info *mtd, int srcAddr, int dstAddr, int bSetFlag)
{
    struct nand_chip *this = mtd->priv;
    uint8_t *buf=(uint8_t*)kmalloc(mtd->writesize+32, GFP_KERNEL);
    int    i,status,pagePblock,src_page,dst_page,src_block;
    u_char oob[4], oob_bak[4];

    if(!buf){
        printk("%s:kmalloc failed!\n", __FUNCTION__);
        return -1;
        }
    
    pagePblock = mtd->erasesize/mtd->writesize;
    src_page = srcAddr>>this->page_shift;
    src_block = srcAddr>>this->phys_erase_shift;
    dst_page = dstAddr>>this->page_shift;
    
    memcpy(oob_bak, (u_char *)(this->oob_poi + this->ops.ooboffs),4);
    if(bSetFlag){
        uint8_t block_1_8, block_2_8;
        
        if(src_block >= 65535){
            printk("block num err\n");
            kfree(buf);
            return -1;
            }

        block_1_8 = src_block&0xFF;
        block_2_8 = (src_block>>8)&0xFF;
        
        oob[0]='S';
        oob[1]='W';
        oob[2]=block_1_8;
        oob[3]=block_2_8;
        }
    else{
        oob[0]=0xFF;
        oob[1]=0xFF;
        oob[2]=0xFF;
        oob[3]=0xFF;
        }
    memcpy((u_char *)(this->oob_poi + this->ops.ooboffs),(u_char *)oob,4);  

    this->select_chip(mtd, 0);
    for(i=0;i<pagePblock;i++){    
        this->cmdfunc(mtd, NAND_CMD_READ0, 0x00, src_page);
        status = this->ecc.read_page(mtd, this, buf, src_page);
        if(status==-2){
            FLASH_DEBUG("%s: %s read_page failed status=[%d]!\n", __FILE__,__FUNCTION__, status);
            kfree(buf);
            return -1;
        }

        this->cmdfunc(mtd, NAND_CMD_SEQIN, 0x00, dst_page);
        this->ecc.write_page_raw(mtd, this, buf);
        this->cmdfunc(mtd, NAND_CMD_PAGEPROG, -1, -1);
        status = this->waitfunc(mtd, this);
        if(status&NAND_STATUS_FAIL){
            FLASH_DEBUG("%s: %s write_page failed status=[%d]!\n", __FILE__,__FUNCTION__, status);
            kfree(buf);
            return -1;
            }
            
        src_page++;
        dst_page++;
    }

    kfree(buf);
    memcpy((u_char *)(this->oob_poi + this->ops.ooboffs), oob_bak, 4);
    
    return 0;
}

#if NAND_FLAG_WRITE
static int rk29_flag_check(struct mtd_info *mtd, uint8_t *buf)
{
    int i;
    
    if(buf[0] == 'R' && buf[1] == 'K' 
        && buf[2] == '2' && buf[3] == '9' 
        && buf[4] == '1' && buf[5] == '8')
        {
        return 0;
        }
     else{
        for(i=0;i<mtd->writesize;i++){
            if(buf[i]!=0xFF)
                return 1;
            }
        return 2;
        }
}

static int rk29_get_flag_page(struct mtd_info *mtd, int bdown)
{
    struct nand_chip *this = mtd->priv;
    struct nand_bbt_descr *td = this->bbt_td;
    struct nand_bbt_descr *md = this->bbt_md;
    int nrblocks, block, tdblock, startblock, i, status, fward, j, src_page, pageState;
    uint8_t *buf=(uint8_t*)kmalloc(mtd->writesize+32, GFP_KERNEL);

    if(!buf){
        printk("%s:kmalloc failed!\n", __FUNCTION__);
        return 0;
        }
        
    tdblock = (td->maxblocks > md->maxblocks)?td->maxblocks:md->maxblocks;
    nrblocks = (int)(mtd->size >> this->bbt_erase_shift);
    if(bdown){
        startblock = nrblocks-tdblock-1;
        fward = -1;
        }
    else{
        startblock = nrblocks-tdblock-RK29_RESERVE_BLOCK_NUM;
        fward = 1;
        }
    this->select_chip(mtd, 0);
    for(i=0; i<RK29_RESERVE_BLOCK_NUM - 3; i++){
        block = startblock + fward*i;
        if(((this->bbt[block>>2]>>(2*(block & 0x03)))&0x03)==0x02){
            for(j=0; j<(1<<(this->phys_erase_shift-this->page_shift)); j++){
                src_page = (block<<(this->phys_erase_shift-this->page_shift))+j;
                this->cmdfunc(mtd, NAND_CMD_READ0, 0x00, src_page);
                status = this->ecc.read_page_raw(mtd, this, buf, src_page);
                if(status==-2){
                    FLASH_DEBUG("%s: %s read_page failed status=[%d]!\n", __FILE__,__FUNCTION__, status);
                    kfree(buf);
                    return 0;
                }
                if(j==0){
                    u_char oob[4];
                    memcpy(oob, (u_char *)(this->oob_poi + this->ops.ooboffs),4);
                    if((oob[0]!='R')||(oob[1]!='K')||(oob[2]!='F')||(oob[3]!='G')){
                        if(rk29_nand_erase(mtd, block<<this->phys_erase_shift)){
                            mtd->block_markbad(mtd, block<<this->phys_erase_shift);
                            break;
                            }
                        //printk("get a free block [%d]!\n", block);
                        }
                    this->cmdfunc(mtd, NAND_CMD_READ0, 0x00, src_page);
                    status = this->ecc.read_page_raw(mtd, this, buf, src_page);
                    if(status==-2){
                        FLASH_DEBUG("%s: %s read_page failed status=[%d]!\n", __FILE__,__FUNCTION__, status);
                        kfree(buf);
                        return 0;
                        }
                    }
                pageState = rk29_flag_check(mtd, buf);
                //printk("src_page = [%d] pageState = [%d]\n", src_page, pageState);
                if(pageState == 0){
                    continue;
                    }
                else if(pageState == 1){
                    if(rk29_nand_erase(mtd, block<<this->phys_erase_shift)){
                        mtd->block_markbad(mtd, block<<this->phys_erase_shift);
                        break;
                        }
                    kfree(buf);
                    //printk("rk29_get_flag_page: block<<(this->phys_erase_shift-this->page_shift = [%d]\n", block<<(this->phys_erase_shift-this->page_shift));
                    return block<<(this->phys_erase_shift-this->page_shift);
                    }
                else{
                    kfree(buf);
                    //printk("rk29_get_flag_page: src_page = [%d]\n", src_page);
                    return src_page;
                    }
            }
        }
    }
    kfree(buf);
    return 0;
}

static int rk29_nand_refresh_flag(struct mtd_info *mtd, int srcAddr, int swapAddr)
{
    int flagAddr, status;
    struct nand_chip *this = mtd->priv;
    uint8_t *buf=(uint8_t*)kmalloc(mtd->writesize+32, GFP_KERNEL);
    u_char oob[4];
    
    if(!buf){
        printk("%s:kmalloc failed!\n", __FUNCTION__);
        return 0;
        }
    
    
    flagAddr = rk29_get_flag_page(mtd, 1);
    if(flagAddr){
        buf[0] = 'R';
        buf[1] = 'K';
        buf[2] = '2';
        buf[3] = '9';
        buf[4] = '1';
        buf[5] = '8';
        buf[6] = (uint8_t)(srcAddr&0xFF);
        buf[7] = (uint8_t)((srcAddr>>8)&0xFF);
        buf[8] = (uint8_t)((srcAddr>>16)&0xFF);
        buf[9] = (uint8_t)((srcAddr>>24)&0xFF);
        memset(&buf[10], 0x88, mtd->writesize-10);

        oob[0]='R';
        oob[1]='K';
        oob[2]='F';
        oob[3]='G';
        memcpy((u_char *)(this->oob_poi + this->ops.ooboffs),oob,4);
        
        this->cmdfunc(mtd, NAND_CMD_SEQIN, 0x00, flagAddr);
        this->ecc.write_page_raw(mtd, this, buf);
        this->cmdfunc(mtd, NAND_CMD_PAGEPROG, -1, -1);
        status = this->waitfunc(mtd, this);
        if(status&NAND_STATUS_FAIL){
            FLASH_DEBUG("%s: %s write_page failed status=[%d]!\n", __FILE__,__FUNCTION__, status);
            kfree(buf);
            return -1;
            }
        //printk("rk29_nand_refresh_flag: page = [%d]\n", flagAddr);
        }
    kfree(buf);
    return 0;
}
#endif

int rk29_nand_refresh(struct mtd_info *mtd, int srcAddr)
{
    struct nand_chip *this = mtd->priv;
    int swapAddr;
    int ret = 0;

    if(!gbRefresh){
        printk("bbt is not ready!\n");
        return 0;
        }

    srcAddr = (srcAddr>>this->phys_erase_shift)<<this->phys_erase_shift;
    
    swapAddr = rk29_get_swap_block_erased(mtd, 0);
    printk("%s swapAddr[%d] srcAddr[%d]\n", __FUNCTION__, swapAddr, srcAddr);

#if NAND_FLAG_WRITE    
    rk29_nand_refresh_flag(mtd, srcAddr, swapAddr);
#endif
    
    read_in_refresh = 1;
    if(!swapAddr){
        printk("no swap block fined!!!\n");
        ret = -1;
        goto nand_refresh_error;
        }

    if(rk29_nand_erase(mtd, swapAddr)){
        printk("rk29_nand_erase[0x%x] failed!\n", srcAddr);
        ret = -1;
        goto nand_refresh_error;
        }
        
    if(rk29_block_copy(mtd, srcAddr, swapAddr, 1)){
        printk("rk29_block_copy[0x%x ---> 0x%x] failed!\n", srcAddr, swapAddr);
        ret = -1;
        goto nand_refresh_error;
        }

    if(rk29_nand_erase(mtd, srcAddr)){
        printk("rk29_nand_erase[0x%x] failed!\n", srcAddr);
        ret = -1;
        goto nand_refresh_error;
        }
        
    if(rk29_block_copy(mtd, swapAddr, srcAddr, 0)){
        printk("rk29_block_copy[0x%x ---> 0x%x] failed!\n", swapAddr, srcAddr);
        ret = -1;
        goto nand_refresh_error;
        }
    if(rk29_nand_erase(mtd, swapAddr)){
        printk("rk29_nand_erase[0x%x] failed!\n", srcAddr);
        ret = -1;
        goto nand_refresh_error;
        }
nand_refresh_error:
    read_in_refresh = 0;
    return ret;
}

EXPORT_SYMBOL_GPL(rk29_nand_refresh);


static int rk29_nand_check_hwecc(struct mtd_info *mtd, int page)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct rk29_nand_mtd *master = nand_chip->priv;
	pNANDC pRK29NC=  (pNANDC)(master->regs);

       if((pRK29NC->BCHST[0]&0x1) && (pRK29NC->BCHST[0]&0x4))
        {
        FLASH_DEBUG("%s: %s BCH FAIL!!!page=[%d]\n", __FILE__,__FUNCTION__, page);
        dump_stack();
        return 2;
        }
    
    if((((pRK29NC->BCHST[0])>>3)&0x1F) >= 12 /*|| refreshTestCnt++%10000 == 0*/)
    {
        return 1;
    }

    if(pRK29NC->BCHST[0]&0x2){
	    return 0;
	    }
	else{
	    FLASH_DEBUG("%s: %s Flash BCH no done!!!\n", __FILE__,__FUNCTION__);
	    return 2;
	    }
}
/*
*  设置片选
*/
static void rk29_nand_select_chip(struct mtd_info *mtd, int chip)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct rk29_nand_mtd *master = nand_chip->priv;
	pNANDC pRK29NC=  (pNANDC)(master->regs);

       	   
	if( chip<0 )
	     pRK29NC->FMCTL &=0xffffff00;   // release chip select
	else
	  {
	       master->cs = chip;
		pRK29NC->FMCTL &=0xffffff00;
		pRK29NC ->FMCTL |= 0x1<<chip;  // select chip
	  }
	
}

/*
 *   读一个字节数据
*/
static u_char rk29_nand_read_byte(struct mtd_info *mtd)
{
     	struct nand_chip *nand_chip = mtd->priv;
	struct rk29_nand_mtd *master = nand_chip->priv;
	pNANDC pRK29NC=  (pNANDC)(master->regs);
	
      	u_char ret = 0; 
	  
      	ret = (u_char)(pRK29NC ->chip[master->cs].data);

      	return ret;
}

/*
 *   读一个word 长度数据
*/
static u16 rk29_nand_read_word(struct mtd_info *mtd)
{
     	struct nand_chip *nand_chip = mtd->priv;
	struct rk29_nand_mtd *master = nand_chip->priv;
	pNANDC pRK29NC=  (pNANDC)(master->regs);

	
      	u_char tmp1 = 0,tmp2=0;
      	u16 ret=0;
	
      	tmp1 = (u_char)(pRK29NC ->chip[master->cs].data);
      	tmp2 = (u_char)(pRK29NC ->chip[master->cs].data);

      	ret =   (tmp2 <<8)|tmp1;
	
      	return ret;
}

static void rk29_nand_read_buf(struct mtd_info *mtd, u_char* const buf, int len)
{
       struct nand_chip *nand_chip = mtd->priv;
	struct rk29_nand_mtd *master = nand_chip->priv;
	pNANDC pRK29NC=  (pNANDC)(master->regs);
	uint32_t  i, chipnr;
	 
       RKNAND_LOCK();

       chipnr = master->cs ;
	   
	rk29_nand_select_chip(mtd,chipnr);
	
	
	
	if ( len < mtd->writesize )   // read oob
	{
	 	pRK29NC ->BCHCTL = BCH_RST;
	       pRK29NC ->FLCTL = (0<<4)|FL_COR_EN|(0x1<<5)|FL_BYPASS|FL_START ; 	
		wait_op_done(mtd,TROP_US_DELAY,0);
		rk29_nand_wait_bchdone(mtd,TROP_US_DELAY) ;  
		memcpy(buf,(u_char *)(pRK29NC->spare),4);  //  only use nandc sram0
	}
	else
       {
           pRK29NC->FLCTL |= FL_BYPASS;  // dma mode           
	    for(i=0;i<mtd->writesize/0x400;i++)
		{
		       pRK29NC ->BCHCTL = BCH_RST;
	       	pRK29NC ->FLCTL = (0<<4)|FL_COR_EN|(0x1<<5)|FL_BYPASS|FL_START ; 	
			wait_op_done(mtd,TROP_US_DELAY,0);
			rk29_nand_wait_bchdone(mtd,TROP_US_DELAY) ;
              	memcpy(buf+i*0x400,(u_char *)(pRK29NC->buf),0x400);  //  only use nandc sram0
		}
	}

	
	
	rk29_nand_select_chip(mtd,-1);

	RKNAND_UNLOCK();

	
	return;	

}

static void rk29_nand_write_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
       struct nand_chip *nand_chip = mtd->priv;
	struct rk29_nand_mtd *master = nand_chip->priv;
	pNANDC pRK29NC=  (pNANDC)(master->regs);
     
	uint32_t  i = 0, chipnr;
	   
    	 
   	 RKNAND_LOCK();

	 chipnr = master->cs ;

	rk29_nand_select_chip(mtd,chipnr);
	
         pRK29NC->FLCTL |= FL_BYPASS;  // dma mode
         
          
	  for(i=0;i<mtd->writesize/0x400;i++)
	    {
	       memcpy((u_char *)(pRK29NC->buf),buf+i*0x400,0x400);  //  only use nandc sram0	
		pRK29NC ->BCHCTL =BCH_WR|BCH_RST;		
		pRK29NC ->FLCTL = (0<<4)|FL_COR_EN|0x1<<5|FL_RDN|FL_BYPASS|FL_START;
		wait_op_done(mtd,TROP_US_DELAY,0);	
	    }
	

	  
       rk29_nand_select_chip(mtd,-1);
	  
	  RKNAND_UNLOCK();


}


static void rk29_nand_cmdfunc(struct mtd_info *mtd, unsigned command,int column, int page_addr)
{
       struct nand_chip *nand_chip = mtd->priv;
	struct rk29_nand_mtd *master = nand_chip->priv;
	pNANDC pRK29NC=  (pNANDC)(master->regs);
	
	switch (command) {

       case NAND_CMD_READID:
	   	pRK29NC ->chip[master->cs].cmd = command;
		pRK29NC ->chip[master->cs].addr = 0x0;
              udelay(1);
		rk29_nand_wait_busy(mtd,READ_BUSY_COUNT);
	   	break;
		
       case NAND_CMD_READ0:
              pRK29NC ->chip[master->cs].cmd = command;
	       if ( column>= 0 )
	         {
                   pRK29NC ->chip[master->cs].addr = column & 0xff;	
                   if( mtd->writesize > 512) 
		         pRK29NC ->chip[master->cs].addr = (column >> 8) & 0xff;
	         }
		if ( page_addr>=0 )
		   {
			pRK29NC ->chip[master->cs].addr = page_addr & 0xff;
			pRK29NC ->chip[master->cs].addr = (page_addr >> 8) & 0xFF;
			pRK29NC ->chip[master->cs].addr = (page_addr >> 16) & 0xff;
		   }
		if( mtd->writesize > 512)
		    pRK29NC ->chip[master->cs].cmd = NAND_CMD_READSTART;

              udelay(1);
		rk29_nand_wait_busy(mtd,READ_BUSY_COUNT);
		
	   	break;
		
	case NAND_CMD_READ1:
              pRK29NC ->chip[master->cs].cmd = command;
			rk29_nand_wait_ready(mtd);
		break;
		
       case NAND_CMD_READOOB:
		pRK29NC ->BCHCTL = 0x0;		
		if( mtd->writesize > 512 )
			command = NAND_CMD_READ0;  // 全部读，包括读oob
    		
		pRK29NC ->chip[master->cs].cmd = command;  

              if ( mtd->writesize >512 )
               {
			if ( column>= 0 )
		         {
	                   pRK29NC ->chip[master->cs].addr = column  & 0xff;	
			     pRK29NC ->chip[master->cs].addr = ( column   >> 8) & 0xff;
		         }
			if ( page_addr>=0 )
			   {
				pRK29NC ->chip[master->cs].addr = page_addr & 0xff;
				pRK29NC ->chip[master->cs].addr = (page_addr >> 8) & 0xFF;
				pRK29NC ->chip[master->cs].addr = (page_addr >> 16) & 0xff;
			   }
		    	pRK29NC ->chip[master->cs].cmd = NAND_CMD_READSTART;
              }
		else
		{
		   pRK29NC ->chip[master->cs].addr = column;
		}
		udelay(1);
		rk29_nand_wait_busy(mtd,READ_BUSY_COUNT);
		
	 
	   	break;	
		
	case NAND_CMD_PAGEPROG:
		pRK29NC ->FMCTL |= FMC_WP;  //解除写保护
		pRK29NC ->chip[master->cs].cmd = command;
		udelay(1);
		rk29_nand_wait_busy(mtd,PROGRAM_BUSY_COUNT);
		break;
		
	case NAND_CMD_ERASE1:
		pRK29NC ->FMCTL |= FMC_WP;  //解除写保护
		pRK29NC ->BCHCTL = 0x0;
		pRK29NC ->chip[master->cs].cmd  = command;
		if ( page_addr>=0 )
		   {
			pRK29NC ->chip[master->cs].addr = page_addr & 0xff;
			pRK29NC ->chip[master->cs].addr = (page_addr>>8)&0xff;
			pRK29NC ->chip[master->cs].addr = (page_addr>>16)&0xff;
		   }  
		break;
		
	case NAND_CMD_ERASE2:
		pRK29NC ->FMCTL |= FMC_WP;  //解除写保护
		pRK29NC ->chip[master->cs].cmd  = command;	       
		rk29_nand_wait_busy(mtd,ERASE_BUSY_COUNT);
		break;
		
	case NAND_CMD_SEQIN:
		pRK29NC ->FMCTL |= FMC_WP;  //解除写保护
		pRK29NC ->chip[master->cs].cmd  = command;
		if ( column>= 0 )
		  {
                   pRK29NC ->chip[master->cs].addr = column;
		     if( mtd->writesize > 512) 
		       pRK29NC ->chip[master->cs].addr = (column >> 8) & 0xff;
		  }
		if( page_addr>=0 )
		  {
			pRK29NC ->chip[master->cs].addr = page_addr & 0xff;
			pRK29NC ->chip[master->cs].addr = (page_addr>>8)&0xff;
			pRK29NC ->chip[master->cs].addr = (page_addr>>16)&0xff;
		 }
		
		break;
		
	case NAND_CMD_STATUS:
		pRK29NC ->BCHCTL = 0x0;
		pRK29NC ->chip[master->cs].cmd = command;
		break;

	case NAND_CMD_RESET:
		pRK29NC ->chip[master->cs].cmd = command;
		udelay(1);  
		rk29_nand_wait_busy(mtd,RESET_BUSY_COUNT);
		break;

	/* This applies to read commands */
	default:
	       pRK29NC ->chip[master->cs].cmd = command;
		break;
	}

   
}

int rk29_nand_calculate_ecc(struct mtd_info *mtd,const uint8_t *dat,uint8_t *ecc_code)
{
     struct nand_chip *nand_chip = mtd->priv;
     struct rk29_nand_mtd *master = nand_chip->priv;
     pNANDC pRK29NC=  (pNANDC)(master->regs);
 
     int eccdata[7],i;
	 
    FLASH_DEBUG("%s:%s, %d\n", __FILE__,__FUNCTION__, __LINE__);
	for(i=0;i<7;i++) 
	 {
	    eccdata[i] = pRK29NC->spare[i+1];

		   
	    ecc_code[i*4] = eccdata[i]& 0xff;
	    ecc_code[i*4+1] = (eccdata[i]>> 8)& 0xff;
	    ecc_code[i*4+2] = (eccdata[i]>>16)& 0xff;
	    ecc_code[i*4+3] = (eccdata[i]>>24)& 0xff;
		  
	  }		
	
     return 0;
}

 void rk29_nand_hwctl_ecc(struct mtd_info *mtd, int mode)
 {
       struct nand_chip *nand_chip = mtd->priv;
     	struct rk29_nand_mtd *master = nand_chip->priv;
     	pNANDC pRK29NC=  (pNANDC)(master->regs);

    FLASH_DEBUG("%s:%s, %d\n", __FILE__,__FUNCTION__, __LINE__);
	pRK29NC->BCHCTL = 1;  // reset bch and enable hw ecc
		
       return;
 }
 
 int rk29_nand_correct_data(struct mtd_info *mtd, uint8_t *dat, uint8_t *read_ecc,uint8_t *calc_ecc)
 {
       struct nand_chip *nand_chip = mtd->priv;
     	struct rk29_nand_mtd *master = nand_chip->priv;
     	pNANDC pRK29NC=  (pNANDC)(master->regs);		

	// hw correct data
       if( pRK29NC->BCHST[0] & (1<<2) )
	 {
        FLASH_DEBUG("%s: %s BCH FAILED!!!\n", __FILE__,__FUNCTION__);
		return -1;
	}
	
       return 0;
 }
 
 int rk29_nand_read_page(struct mtd_info *mtd,struct nand_chip *chip,uint8_t *buf, int page)
 {
       struct nand_chip *nand_chip = mtd->priv;
     	struct rk29_nand_mtd *master = nand_chip->priv;
     	pNANDC pRK29NC=  (pNANDC)(master->regs);
	  
	int i,chipnr, ecc = 0;

	   
	RKNAND_LOCK();

	chipnr = master->cs ;
	
	rk29_nand_select_chip(mtd,chipnr);

	   
       pRK29NC->FLCTL |= FL_BYPASS;  // dma mode
	
	if(chip->options&NAND_BUSWIDTH_16)
	{
		pRK29NC ->FMCTL |= FMC_WIDTH_16;  // 设置为16位
	}	 
	  
	for(i=0;i<mtd->writesize/0x400;i++)
	{
	       pRK29NC ->BCHCTL = BCH_RST;
	       pRK29NC ->FLCTL = (0<<4)|FL_COR_EN|(0x1<<5)|FL_BYPASS|FL_START ; 	
		wait_op_done(mtd,TROP_US_DELAY,0);   
		rk29_nand_wait_bchdone(mtd,TROP_US_DELAY) ;
        ecc |= rk29_nand_check_hwecc(mtd, page);
              memcpy(buf+i*0x400,(u_char *)(pRK29NC->buf),0x400);  //  only use nandc sram0
	}
	
    if(ecc & 0x2){
        mtd->ecc_stats.failed++;
		return -EBADMSG;
        }
    else if(ecc & 0x1){
        if(!read_in_refresh){
            //FLASH_DEBUG("Flash need fresh srcAddr = [%d]\n", ((page*mtd->writesize)/mtd->erasesize)*mtd->erasesize);
            return -1;
            }
        }
		
	rk29_nand_select_chip(mtd,-1);

	RKNAND_UNLOCK();
	//t2 = ktime_get();
	//delta = ktime_sub(t2, t1);
	//FLASH_DEBUG("%s:%s [%lli nsec]\r\n",__FILE__,__FUNCTION__, (long long)ktime_to_ns(delta));
    return 0;
	
 }

void  rk29_nand_write_page(struct mtd_info *mtd,struct nand_chip *chip,const uint8_t *buf)
 {
       struct nand_chip *nand_chip = mtd->priv;
	struct rk29_nand_mtd *master = nand_chip->priv;
	pNANDC pRK29NC=  (pNANDC)(master->regs);
       uint32_t  i = 0, chipnr;
	   
	RKNAND_LOCK();

       chipnr = master->cs ;
	   
	rk29_nand_select_chip(mtd,chipnr);
	
	pRK29NC->FLCTL |= FL_BYPASS;  // dma mode

  if(chip->options&NAND_BUSWIDTH_16)
	{
		pRK29NC ->FMCTL |= FMC_WIDTH_16;  // 设置为16位
	}	 
	 		
	  for(i=0;i<mtd->writesize/0x400;i++)
	   {
		pRK29NC ->BCHCTL = BCH_WR|BCH_RST;		
	       memcpy((u_char *)(pRK29NC->buf),(buf+i*0x400),0x400);  //  only use nandc sram0		
	       if(i==0)
		   memcpy((u_char *)(pRK29NC->spare),(u_char *)(chip->oob_poi + chip->ops.ooboffs),4);  
		   	
		pRK29NC ->FLCTL = (0<<4)|FL_COR_EN|(0x1<<5)|FL_RDN|FL_BYPASS|FL_START;
		wait_op_done(mtd,TROP_US_DELAY,0);	
	   }

         pRK29NC ->chip[chipnr].cmd = NAND_CMD_PAGEPROG;
	 

	  
	  rk29_nand_wait_busy(mtd,PROGRAM_BUSY_COUNT);
	  
	 rk29_nand_select_chip(mtd,-1);

     RKNAND_UNLOCK();
	
    return;
	  
 }

int rk29_nand_read_oob(struct mtd_info *mtd, struct nand_chip *chip, int page, int sndcmd)
{	
       struct nand_chip *nand_chip = mtd->priv;
     	struct rk29_nand_mtd *master = nand_chip->priv;
     	pNANDC pRK29NC=  (pNANDC)(master->regs);
    int i,chipnr,ecc=0;
    	RKNAND_LOCK();
	chipnr = master->cs ;
	
	rk29_nand_select_chip(mtd,chipnr);

	  
	if (sndcmd) {
		chip->cmdfunc(mtd, NAND_CMD_READOOB, 0, page);
		sndcmd = 0;
	}

	rk29_nand_wait_busy(mtd,READ_BUSY_COUNT);

	
       pRK29NC->FLCTL |= FL_BYPASS;  // dma mode

	if(chip->options&NAND_BUSWIDTH_16)
	{
		pRK29NC ->FMCTL |= FMC_WIDTH_16;  // 设置为16位
	}	 

	
	for(i=0;i<mtd->writesize/0x400;i++)
	{
	       pRK29NC ->BCHCTL = BCH_RST;
	       pRK29NC ->FLCTL = (0<<4)|FL_COR_EN|(0x1<<5)|FL_BYPASS|FL_START ; 	
		wait_op_done(mtd,TROP_US_DELAY,0);   
		rk29_nand_wait_bchdone(mtd,TROP_US_DELAY) ;
        ecc |= rk29_nand_check_hwecc(mtd, page);
              if(i==0)
                 memcpy((u_char *)(chip->oob_poi+ chip->ops.ooboffs),(u_char *)(pRK29NC->spare),4); 
	}

    if(ecc & 0x2){
        mtd->ecc_stats.failed++;
		return -EBADMSG;
        }
    else if(ecc & 0x1){
        if(!read_in_refresh){
            //FLASH_DEBUG("Flash need fresh srcAddr = [%d]\n", ((page*mtd->writesize)/mtd->erasesize)*mtd->erasesize);
            return -1;
            }
        }
	   
 	 rk29_nand_select_chip(mtd,-1);

	 RKNAND_UNLOCK();


	return sndcmd;
}

int	rk29_nand_read_page_raw(struct mtd_info *mtd, struct nand_chip *chip, uint8_t *buf, int page)
{
       struct nand_chip *nand_chip = mtd->priv;
     	struct rk29_nand_mtd *master = nand_chip->priv;
     	pNANDC pRK29NC=  (pNANDC)(master->regs);

    int i,chipnr,ecc=0;

	
    RKNAND_LOCK();

	chipnr = master->cs ;
	
	rk29_nand_select_chip(mtd,chipnr);

	rk29_nand_wait_busy(mtd,READ_BUSY_COUNT);
	   
       pRK29NC->FLCTL |= FL_BYPASS;  // dma mode

 	if(chip->options&NAND_BUSWIDTH_16)
	{
		pRK29NC ->FMCTL |= FMC_WIDTH_16;  // 设置为16位
	}	 
	   
	for(i=0;i<mtd->writesize/0x400;i++)
	{
	       pRK29NC ->BCHCTL = BCH_RST;
	       pRK29NC ->FLCTL = (0<<4)|FL_COR_EN|(0x1<<5)|FL_BYPASS|FL_START ; 	
		wait_op_done(mtd,TROP_US_DELAY,0);   
		 rk29_nand_wait_bchdone(mtd,TROP_US_DELAY) ;
        ecc |= rk29_nand_check_hwecc(mtd, page);
              memcpy(buf+i*0x400,(u_char *)(pRK29NC->buf),0x400);  //  only use nandc sram0
              if(i==0)
                 memcpy((u_char *)(chip->oob_poi+ chip->ops.ooboffs),(u_char *)(pRK29NC->spare),4); 
	}

    if(ecc & 0x2){
        mtd->ecc_stats.failed++;
		return -EBADMSG;
        }
    else if(ecc & 0x1){
        if(!read_in_refresh){
            //FLASH_DEBUG("Flash need fresh srcAddr = [%d]\n", ((page*mtd->writesize)/mtd->erasesize)*mtd->erasesize);
            return -1;
            }
        }
	 
	 rk29_nand_select_chip(mtd,-1);
	 RKNAND_UNLOCK();

	 
    return 0;
}
int	rk29_nand_write_page_raw(struct mtd_info *mtd, struct nand_chip *chip,const uint8_t *buf)
{
    struct nand_chip *nand_chip = mtd->priv;
    struct rk29_nand_mtd *master = nand_chip->priv;
    pNANDC pRK29NC=  (pNANDC)(master->regs);
    int i,chipnr;

    //FLASH_DEBUG("%s: %s, %d\n", __FILE__ ,__FUNCTION__, __LINE__);
    
    RKNAND_LOCK();

	chipnr = master->cs ;
	
	rk29_nand_select_chip(mtd,chipnr);

    rk29_nand_wait_busy(mtd, PROGRAM_BUSY_COUNT);
	   
       pRK29NC->FLCTL |= FL_BYPASS;  // dma mode

 	if(chip->options&NAND_BUSWIDTH_16)
	{
		pRK29NC ->FMCTL |= FMC_WIDTH_16;  // 设置为16位
	}	 

    for(i=0;i<mtd->writesize/0x400;i++)
    {
        pRK29NC->BCHCTL = BCH_WR|BCH_RST;		
        memcpy((u_char *)(pRK29NC->buf),(buf+i*0x400),0x400);
        if(i==0)
            memcpy((u_char *)(pRK29NC->spare),(u_char *)(chip->oob_poi + chip->ops.ooboffs),4);  
        pRK29NC->FLCTL = (0<<4)|FL_COR_EN|(0x1<<5)|FL_RDN|FL_BYPASS|FL_START;	
        wait_op_done(mtd,TROP_US_DELAY,0);	
    }
	 rk29_nand_select_chip(mtd,-1);

    RKNAND_UNLOCK();        
    return 0;
}


static int rk29_nand_setrate(struct rk29_nand_mtd *info)
{
	pNANDC pRK29NC=  (pNANDC)(info->regs);
	
	unsigned long clkrate = clk_get_rate(info->clk);

       u_char accesstime,rwpw,csrw,rwcs;

	unsigned int ns=0,timingcfg;



// some nand flashs have not  timing id and almost all nand flash access time is 25ns, so need to  fix accesstime to 40 ns
	accesstime = 50;

       info->clk_rate = clkrate;
	clkrate /= 1000000;	/* turn clock into MHz for ease of use */
	
	if(clkrate>0 && clkrate<=400)
	   ns= 1000/clkrate; // ns
	else
	   return -1;

	timingcfg=  accesstime/ns + 1 ;

       rwpw = (timingcfg+1)/2;  // rwpw >= timingcfg/2

	csrw = ( timingcfg/4 > 1)?(timingcfg/4):1;  // csrw >=1

	rwcs = ( (timingcfg+3)/4 >1)?((timingcfg+3)/4):1; // rwcs >=1 &&  rwcs >= timingcfg/4
	RKNAND_LOCK();

	pRK29NC ->FMWAIT &=0xFFFF0800;
	pRK29NC ->FMWAIT |=  (rwcs<<FMW_RWCS_OFFSET)|(rwpw<<FMW_RWPW_OFFSET)|(csrw<<FMW_CSRW_OFFSET);

	RKNAND_UNLOCK();


	return 0;
}

/* cpufreq driver support */

#ifdef CONFIG_CPU_FREQ

static int rk29_nand_cpufreq_transition(struct notifier_block *nb, unsigned long val, void *data)
{
	struct rk29_nand_mtd *info;
	unsigned long newclk;

	info = container_of(nb, struct rk29_nand_mtd, freq_transition);
	newclk = clk_get_rate(info->clk);

	if (val == CPUFREQ_POSTCHANGE && newclk != info->clk_rate) 
	 {
		rk29_nand_setrate(info);
	}

	return 0;
}

static inline int rk29_nand_cpufreq_register(struct rk29_nand_mtd *info)
{
	info->freq_transition.notifier_call = rk29_nand_cpufreq_transition;

	return cpufreq_register_notifier(&info->freq_transition, CPUFREQ_TRANSITION_NOTIFIER);
}

static inline void rk29_nand_cpufreq_deregister(struct rk29_nand_mtd *info)
{
	cpufreq_unregister_notifier(&info->freq_transition, CPUFREQ_TRANSITION_NOTIFIER);
}

#else
static inline int rk29_nand_cpufreq_register(struct rk29_nand_mtd *info)
{
	return 0;
}

static inline void rk29_nand_cpufreq_deregister(struct rk29_nand_mtd *info)
{
}
#endif


static int rk29_nand_probe(struct platform_device *pdev)
{
       struct nand_chip *this;
	struct mtd_info *mtd;
	struct rk29_nand_platform_data *pdata = pdev->dev.platform_data;
	struct rk29_nand_mtd *master;
	struct resource *res;
	int err = 0;
	pNANDC pRK29NC;
	u_char  maf_id,dev_id,ext_id3,ext_id4;
    
#ifdef CONFIG_MTD_PARTITIONS
	struct mtd_partition *partitions = NULL;
	int num_partitions = 0;
#endif

      /* Allocate memory for MTD device structure and private data */
	master = kzalloc(sizeof(struct rk29_nand_mtd), GFP_KERNEL);
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
	this->dev_ready = rk29_nand_dev_ready;
	this->cmdfunc = rk29_nand_cmdfunc;
	this->select_chip = rk29_nand_select_chip;
	this->read_byte = rk29_nand_read_byte;
	this->read_word = rk29_nand_read_word;
	this->write_buf = rk29_nand_write_buf;
	this->read_buf = rk29_nand_read_buf;
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
		this->ecc.calculate = rk29_nand_calculate_ecc;
		this->ecc.hwctl = rk29_nand_hwctl_ecc;
		this->ecc.correct = rk29_nand_correct_data;
		this->ecc.mode = NAND_ECC_HW;
		this->ecc.read_page = rk29_nand_read_page;
		this->ecc.write_page = rk29_nand_write_page;
		this->ecc.read_oob = rk29_nand_read_oob;
		this->ecc.read_page_raw = rk29_nand_read_page_raw;
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
	
       pRK29NC =  (pNANDC)(master->regs);
       pRK29NC ->FMCTL = FMC_WP|FMC_FRDY;
       pRK29NC ->FMWAIT |=  (1<<FMW_RWCS_OFFSET)|(4<<FMW_RWPW_OFFSET)|(2<<FMW_CSRW_OFFSET);
	pRK29NC ->BCHCTL = 0x1;

       this->select_chip(mtd, 0);
	this->cmdfunc(mtd, NAND_CMD_READID, 0x00, -1);
	maf_id = this->read_byte(mtd);
	dev_id = this->read_byte(mtd);
       ext_id3 = this->read_byte(mtd);
	ext_id4 = this->read_byte(mtd);
	
       master->accesstime = ext_id4&0x88;
	
	rk29_nand_setrate(master);
	
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
#if 0
	   if (nand_scan(mtd, 8)) {     // rk29 nandc support max 8 cs
#else
	   if (nand_scan(mtd, 1)) {      // test for fpga board nand
#endif
		DEBUG(MTD_DEBUG_LEVEL0,
		      "RK29 NAND: Unable to find any NAND device.\n");
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

	err =rk29_nand_cpufreq_register(master);
	if (err < 0) {
		printk(KERN_ERR"rk2818 nand failed to init cpufreq support\n");
		goto outscan;
	}

	return 0;
	
outres:
outscan:
    printk("rk29_nand_probe error!!!\n");
	iounmap(master->regs);
	kfree(master);

	return err;
	
}

static int rk29_nand_remove(struct platform_device *pdev)
{
	struct rk29_nand_mtd *master = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

       if(master == NULL)
	  	return 0;

	rk29_nand_cpufreq_deregister(master);
	

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
static int rk29_nand_suspend(struct platform_device *pdev, pm_message_t state)
{
#if 0
	struct mtd_info *info = platform_get_drvdata(pdev);
	int ret = 0;

	DEBUG(MTD_DEBUG_LEVEL0, "RK2818_NAND : NAND suspend\n");
	if (info)
		ret = info->suspend(info);
	return ret;
#else
    return 0;
#endif
}

static int rk29_nand_resume(struct platform_device *pdev)
{
#if 0
	struct mtd_info *info = platform_get_drvdata(pdev);
	int ret = 0;

	DEBUG(MTD_DEBUG_LEVEL0, "RK2818_NAND : NAND resume\n");
	/* Enable the NFC clock */

	if (info)
		info->resume(info);

	return ret;
#else
    return 0;
#endif
}
#else
#define rk29_nand_suspend   NULL
#define rk29_nand_resume    NULL
#endif	/* CONFIG_PM */


static struct platform_driver rk29_nand_driver = {
	.driver = {
		   .name = "rk29-nand",
		   },
       .probe    = rk29_nand_probe,
	.remove = rk29_nand_remove,
	.suspend = rk29_nand_suspend,
	.resume = rk29_nand_resume,
};

static int __init rk29_nand_init(void)
{
	/* Register the device driver structure. */
	printk("rk29_nand_init\n");
	return platform_driver_register(&rk29_nand_driver);;
}

static void __exit rk29_nand_exit(void)
{
	/* Unregister the device structure */
	platform_driver_unregister(&rk29_nand_driver);
}

#ifdef CONFIG_DM9000_USE_NAND_CONTROL
// nandc dma cs mutex for dm9000 interface
void rk29_nand_status_mutex_lock(void)
{
     pNANDC pRK29NC=  (pNANDC)RK2818_NANDC_BASE;
     mutex_lock(&rknand_mutex);
     pRK29NC->FMCTL &=0xffffff00;   // release chip select

}

int rk29_nand_status_mutex_trylock(void)
{
     pNANDC pRK29NC=  (pNANDC)RK2818_NANDC_BASE;
     if( mutex_trylock(&rknand_mutex))
     	{
	 	pRK29NC->FMCTL &=0xffffff00;   // release chip select
	 	return 1;      // ready 
     	}
      else
	  	return 0;     // busy
}

void rk29_nand_status_mutex_unlock(void)
{
     mutex_unlock(&rknand_mutex);
     return;
}
#endif

module_init(rk29_nand_init);
module_exit(rk29_nand_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("hxy <hxy@rock-chips.com>");
MODULE_DESCRIPTION("MTD NAND driver for rk29 device");

