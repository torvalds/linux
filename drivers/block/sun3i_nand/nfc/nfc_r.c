/*********************************************************************************
*                                           NAND FLASH DRIVER
*								(c) Copyright 2008, SoftWinners Co,Ld.
*                                          All Right Reserved
*file : nfc_r.c
*description : this file provides some physic functions for upper nand driver layer.
*history :
*	v0.1  2008-03-26 Richard
*	        offer direct accsee read method to nand flash control machine.
*   v0.2  2009.09.09 penggang
**********************************************************************************/
#include "nfc_i.h"
//#include "dma_for_nand.h"
#include <mach/dma.h>

__u32	nand_board_version;
__u32 	pagesize;
__hdle 	dma_hdle;
volatile __u32 irq_value;


/*******************wait nfc********************************************/
__s32 _wait_cmdfifo_free(void)
{
	__s32 timeout = 0xffff;

	while ( (timeout--) && (NFC_READ_REG(NFC_REG_ST) & NFC_CMD_FIFO_STATUS) );
	if (timeout <= 0)
		return -ERR_TIMEOUT;
	return 0;
}

__s32 _wait_cmd_finish(void)
{
	__s32 timeout = 0xffff;
	while( (timeout--) && !(NFC_READ_REG(NFC_REG_ST) & NFC_CMD_INT_FLAG) );
	if (timeout <= 0)
		return -ERR_TIMEOUT;

	NFC_WRITE_REG(NFC_REG_ST, NFC_READ_REG(NFC_REG_ST) & NFC_CMD_INT_FLAG);
	return 0;
}

#if 1
void _dma_config_start(__u8 rw, __u32 buff_addr, __u32 len)
{
	struct dma_hw_conf nand_hwconf = {
		.xfer_type = DMAXFER_D_BWORD_S_BWORD,
		.hf_irq = SW_DMA_IRQ_FULL,
		.cmbk = 0x7f077f07,
	};

	nand_hwconf.dir = rw+1;

	if(rw == 0){
		nand_hwconf.from = 0x01C03030,
		nand_hwconf.address_type = DMAADDRT_D_LN_S_IO,
		nand_hwconf.drqsrc_type = DRQ_TYPE_NAND;
	} else {
		nand_hwconf.to = 0x01C03030,
		nand_hwconf.address_type = DMAADDRT_D_IO_S_LN,
		nand_hwconf.drqdst_type = DRQ_TYPE_NAND;
	}

	NAND_SettingDMA(dma_hdle, (void*)&nand_hwconf);
	NAND_DMAEqueueBuf(dma_hdle, buff_addr, len);
}

__s32 _wait_dma_end(void)
{
	__s32 timeout = 0xffff;

	while( (timeout--) && ( NAND_QueryDmaStat(dma_hdle)) );
	if (timeout <= 0)
		return -ERR_TIMEOUT;

	return 0;
}

#endif

#if 0
#define NFC_DDMA_ID    1
#define NFC_DMA_BASE    0xf1C02000

#define NFC_DMA_INT_CTL	(NFC_DMA_BASE + 0x00)
#define NFC_DMA_INT_STA	(NFC_DMA_BASE + 0x04)

#define NFC_DDMA_CFG		(NFC_DMA_BASE + 0x300)
#define NFC_DDMA_SRC    (NFC_DMA_BASE + 0x304)
#define NFC_DDMA_DES    (NFC_DMA_BASE + 0x308)
#define NFC_DDMA_CNT    (NFC_DMA_BASE + 0x30c)
#define NFC_DDMA_PAR    (NFC_DMA_BASE + 0x318)

#define NFC_IO_DATA			0x01c03030

#define nfc_read_w(n)                   (*((volatile __u32 *)(n)))          /* word input */
#define nfc_write_w(n,c)                (*((volatile __u32 *)(n)) = (c))    /* word output */

extern void eLIBs_CleanFlushDCacheRegion(void *adr, __u32 bytes);

void _dma_config_start(__u8 rw, __u32 buff_addr, __u32 len)
{
	__u32 reg_val;
	__u32 dma_offset;
	__u32 mem_adr, bcnt;

	mem_adr = __pa(buff_addr);
	bcnt = len;
	dma_offset = NFC_DDMA_ID*0x20;

	eLIBs_CleanFlushDCacheRegion((void *)buff_addr, len);

	//reset DMA
	nfc_write_w(NFC_DDMA_CFG + dma_offset, 0x0);
	nfc_write_w(NFC_DMA_INT_STA, (0x3<<(2*NFC_DDMA_ID + 16)));


	//setup DMA engine
	if(rw)
	{
		reg_val = mem_adr;
		nfc_write_w(NFC_DDMA_SRC + dma_offset, reg_val);		//DMA source address
		reg_val = NFC_IO_DATA;
		nfc_write_w(NFC_DDMA_DES + dma_offset, reg_val);		//DMA destinaiton address
		reg_val = bcnt;												//DMA byte counter
		nfc_write_w(NFC_DDMA_CNT + dma_offset, reg_val);

		if(bcnt > 512)
			bcnt = 512;

		reg_val = ((bcnt>>2) -1)<<8;
		reg_val |= (reg_val<<16);
		reg_val |=0x70000;
		nfc_write_w(NFC_DDMA_PAR + dma_offset, reg_val);

		reg_val = 0x82a30280;
		if(mem_adr&0x80000000)
			reg_val |= 0x1;
		nfc_write_w(NFC_DDMA_CFG + dma_offset, reg_val);
	}
	else
	{
		reg_val = NFC_IO_DATA;
		nfc_write_w(NFC_DDMA_SRC + dma_offset, reg_val);		//DMA source address
		reg_val = mem_adr;
		nfc_write_w(NFC_DDMA_DES + dma_offset, reg_val);		//DMA destinaiton address
		reg_val = bcnt;												//DMA byte counter
		nfc_write_w(NFC_DDMA_CNT + dma_offset, reg_val);

		if(bcnt > 512)
			bcnt = 512;

		reg_val = ((bcnt>>2) - 1)<<8;
		reg_val |= (reg_val<<16);
		reg_val |=0x7;
		nfc_write_w(NFC_DDMA_PAR + dma_offset, reg_val);

		reg_val = 0x828002a3;
		if(mem_adr&0x80000000)
			reg_val |= 0x1<<16;
		nfc_write_w(NFC_DDMA_CFG + dma_offset, reg_val);
	}
}

__s32 _wait_dma_end(void)
{
	__u32 dma_offset;
	__s32 timeout = 0xffff;

	dma_offset = (NFC_DDMA_ID)*0x20;
	while(nfc_read_w(NFC_DDMA_CFG + dma_offset) & 0x80000000)
	{
		timeout--;
		if (timeout <= 0)
		return -ERR_TIMEOUT;
	}
	nfc_write_w(NFC_DMA_INT_STA, (0x3<<(2*NFC_DDMA_ID + 16)) );
}

#endif


__s32 _reset(void)
{
	__u32 cfg;

	__s32 timeout = 0xffff;

	/*reset NFC*/
	cfg = NFC_READ_REG(NFC_REG_CTL);
	cfg |= NFC_RESET;
	NFC_WRITE_REG(NFC_REG_CTL, cfg);
	//waiting reset operation end
	while((timeout--) && (NFC_READ_REG(NFC_REG_CTL) & NFC_RESET));
	if (timeout <= 0)
		return -ERR_TIMEOUT;

	return 0;
}


/***************ecc function*****************************************/
__s32 _check_ecc(__u32 eblock_cnt)
{
	__u32 i;
	__u32 ecc_mode;
	__u32 max_ecc_bit_cnt = 16;
	__u32 cfg;
	__u8 ecc_cnt[16];

	ecc_mode = (NFC_READ_REG(NFC_REG_ECC_CTL)>>12)&0xf;
	if(ecc_mode == 0)
		max_ecc_bit_cnt = 16;
	if(ecc_mode == 1)
		max_ecc_bit_cnt = 24;
	if(ecc_mode == 2)
		max_ecc_bit_cnt = 28;
	if(ecc_mode == 3)
		max_ecc_bit_cnt = 32;
	if(ecc_mode == 4)
		max_ecc_bit_cnt = 40;
	if(ecc_mode == 5)
		max_ecc_bit_cnt = 48;
	if(ecc_mode == 6)
		max_ecc_bit_cnt = 56;
    if(ecc_mode == 7)
		max_ecc_bit_cnt = 60;
    if(ecc_mode == 8)
		max_ecc_bit_cnt = 64;

	//check ecc errro
	cfg = NFC_READ_REG(NFC_REG_ECC_ST)&0xffff;
	for (i = 0; i < eblock_cnt; i++)
	{
		if (cfg & (1<<i))
				return -ERR_ECC;
	}

	//check ecc limit
	cfg = NFC_READ_REG(NFC_REG_ECC_CNT0);
	ecc_cnt[0] = (__u8)((cfg>>0)&0xff);
	ecc_cnt[1] = (__u8)((cfg>>8)&0xff);
	ecc_cnt[2] = (__u8)((cfg>>16)&0xff);
	ecc_cnt[3] = (__u8)((cfg>>24)&0xff);

	cfg = NFC_READ_REG(NFC_REG_ECC_CNT1);
	ecc_cnt[4] = (__u8)((cfg>>0)&0xff);
	ecc_cnt[5] = (__u8)((cfg>>8)&0xff);
	ecc_cnt[6] = (__u8)((cfg>>16)&0xff);
	ecc_cnt[7] = (__u8)((cfg>>24)&0xff);

	cfg = NFC_READ_REG(NFC_REG_ECC_CNT2);
	ecc_cnt[8] = (__u8)((cfg>>0)&0xff);
	ecc_cnt[9] = (__u8)((cfg>>8)&0xff);
	ecc_cnt[10] = (__u8)((cfg>>16)&0xff);
	ecc_cnt[11] = (__u8)((cfg>>24)&0xff);

	cfg = NFC_READ_REG(NFC_REG_ECC_CNT3);
	ecc_cnt[12] = (__u8)((cfg>>0)&0xff);
	ecc_cnt[13] = (__u8)((cfg>>8)&0xff);
	ecc_cnt[14] = (__u8)((cfg>>16)&0xff);
	ecc_cnt[15] = (__u8)((cfg>>24)&0xff);

	for (i = 0; i < eblock_cnt; i++)
	{
		if((max_ecc_bit_cnt - 4) <= ecc_cnt[i])
			return ECC_LIMIT;
	}

	return 0;
}

void _disable_ecc(void)
{
	__u32 cfg = NFC_READ_REG(NFC_REG_ECC_CTL);
	cfg &= ( (~NFC_ECC_EN)&0xffffffff );
	NFC_WRITE_REG(NFC_REG_ECC_CTL, cfg);
}

void _enable_ecc(__u32 pipline)
{
	__u32 cfg = NFC_READ_REG(NFC_REG_ECC_CTL);
	if (pipline ==1 )
		cfg |= NFC_ECC_PIPELINE;
	else
		cfg &= ((~NFC_ECC_PIPELINE)&0xffffffff);


	/*after erased, all data is 0xff, but ecc is not 0xff,
			so ecc asume it is right*/
	cfg |= (1 << 4);

	//cfg |= (1 << 1); 16 bit ecc

	cfg |= NFC_ECC_EN;
	NFC_WRITE_REG(NFC_REG_ECC_CTL, cfg);
}
#if 0
/**************************save and restore irq*************************/
__s32 _save_irq(void)
{

	__u32 temp;


	__asm{MRS temp,CPSR};
	irq_value = temp;
	/*diable irq*/
	__asm{
		ORR temp,temp,#0x80
		MSR CPSR_c,temp
	};


	return 0;
}

__s32 _restore_irq(void)
{
	__u32 temp;

	temp = irq_value;

	/*enable irq*/
	__asm{
		MSR CPSR_c,temp
	};

	return 0;
}
#endif
__s32 _enter_nand_critical(void)
{
    // NAND_GetPin();
    //_save_irq();

	return 0;
}

__s32 _exit_nand_critical(void)
{
    // _restore_irq();
    // NAND_ReleasePin();

	return 0;
}

void _set_addr(__u8 *addr, __u8 cnt)
{
	__u32 i;
	__u32 addr_low = 0;
	__u32 addr_high = 0;

	for (i = 0; i < cnt; i++){
		if (i < 4)
			addr_low |= (addr[i] << (i*8) );
		else
			addr_high |= (addr[i] << ((i - 4)*8));
	}

	NFC_WRITE_REG(NFC_REG_ADDR_LOW, addr_low);
	NFC_WRITE_REG(NFC_REG_ADDR_HIGH, addr_high);
}

__s32 _read_in_page_mode(NFC_CMD_LIST  *rcmd,void *mainbuf,void *sparebuf,__u8 dma_wait_mode)
{
	__s32 ret,ret1;
	__s32 i;
	__u32 cfg;
	NFC_CMD_LIST *cur_cmd,*read_addr_cmd;
	__u32 read_data_cmd,random_read_cmd0,random_read_cmd1;

	ret = 0;
	read_addr_cmd = rcmd;
	cur_cmd = rcmd;
	cur_cmd = cur_cmd->next;
	random_read_cmd0 = cur_cmd->value;
	cur_cmd = cur_cmd->next;
	random_read_cmd1 = cur_cmd->value;
	cur_cmd = cur_cmd->next;
	read_data_cmd = cur_cmd->value;

	//access NFC internal RAM by DMA bus
	NFC_WRITE_REG(NFC_REG_CTL, (NFC_READ_REG(NFC_REG_CTL)) | NFC_RAM_METHOD);

	/*set dma and run*/
//	/*sdram*/
//	if (NFC_IS_SDRAM((__u32)mainbuf))
//		attr = 0x2810293;
//	/*sram*/
//	else
//		attr = 0x2800293;
	//printk("fill with: %x\n", 0xaaaaaaa);
	//*((int*)mainbuf) = 0xaaaaaaa;
	_dma_config_start(0, (__u32)mainbuf, pagesize);

	/*wait cmd fifo free*/
	ret = _wait_cmdfifo_free();
	if (ret)
		return ret;

	/*set NFC_REG_CNT*/
	NFC_WRITE_REG(NFC_REG_CNT,1024);

	/*set NFC_REG_RCMD_SET*/
	cfg = 0;
	cfg |= (read_data_cmd & 0xff);
	cfg |= ((random_read_cmd0 & 0xff) << 8);
	cfg |= ((random_read_cmd1 & 0xff) << 16);
	NFC_WRITE_REG(NFC_REG_RCMD_SET, cfg);

	/*set NFC_REG_SECTOR_NUM*/
	NFC_WRITE_REG(NFC_REG_SECTOR_NUM, pagesize/1024);

	/*set addr*/
	_set_addr(read_addr_cmd->addr,read_addr_cmd->addr_cycle);

	/*set NFC_REG_CMD*/
	cfg  = 0;
	cfg |= read_addr_cmd->value;
	/*set sequence mode*/
	//cfg |= 0x1<<25;
	cfg |= ( (read_addr_cmd->addr_cycle - 1) << 16);
	cfg |= (NFC_SEND_ADR | NFC_DATA_TRANS | NFC_SEND_CMD1 | NFC_SEND_CMD2 | NFC_WAIT_FLAG | NFC_DATA_SWAP_METHOD);
	cfg |= ((__u32)0x2 << 30);//page command

	if (pagesize/1024 == 1)
		cfg |= NFC_SEQ;

	/*enable ecc*/
	_enable_ecc(1);
	NFC_WRITE_REG(NFC_REG_CMD,cfg);

    NAND_WaitDmaFinish();

	/*wait cmd fifo free and cmd finish*/
	ret = _wait_cmdfifo_free();
	ret |= _wait_cmd_finish();
	if (ret){
		_disable_ecc();
		return ret;
	}
	/*get user data*/
	for (i = 0; i < pagesize/1024;  i++){
		*(((__u32*) sparebuf)+i) = NFC_READ_REG(NFC_REG_USER_DATA(i));
	}

	/*ecc check and disable ecc*/
	ret = _check_ecc(pagesize/1024);
	_disable_ecc();

	/*if dma mode is wait*/
	if(0 == dma_wait_mode){
		ret1 = _wait_dma_end();
		if (ret1)
			return ret1;
	}

	return ret;
}

__s32 _read_in_normal_mode(NFC_CMD_LIST  *rcmd, __u8 *mainbuf, __u8 *sparebuf,__u8 dma_wait_mode)
{
	__s32 ret,ret1;
	__u32 cfg;
	NFC_CMD_LIST *cur_cmd;

	ret = 0;
	cur_cmd = rcmd;

	/*enable ecc*/
	_enable_ecc(1);
	while (cur_cmd != NULL)
	{
		cfg = 0;
		/*wait cmd fifo free*/
		ret1 = _wait_cmdfifo_free();
		if (ret1)
			return ret1;

		/*1. set ecc/dma/comamnd mod, if tranfer data*/
		if (cur_cmd->data_fetch_flag){

			if (cur_cmd->main_data_fetch){

				/*set NFC_REG_CNT*/
				NFC_WRITE_REG(NFC_REG_CNT,1024);
			}
			else{
				//access NFC internal RAM by DMA bus
	 			NFC_WRITE_REG(NFC_REG_CTL, (NFC_READ_REG(NFC_REG_CTL)) | NFC_RAM_METHOD);
				/*set dma and run*/
//				/*sdram*/
//				if (NFC_IS_SDRAM((__u32)mainbuf))
//					attr = 0x2810293;
//				/*sram*/
//				else
//					attr = 0x2800293;
				_dma_config_start(0, (__u32)mainbuf, 1024);

				cfg |= NFC_DATA_SWAP_METHOD;

				/*spare command type*/
				cfg |= (0x01 << 30);
			}

			cfg |= NFC_DATA_TRANS;

		}

		/*2. set access address?*/
		if (cur_cmd->addr_cycle){
			_set_addr(cur_cmd->addr,cur_cmd->addr_cycle);
			cfg |= ( (cur_cmd->addr_cycle - 1) << 16);
			cfg |= NFC_SEND_ADR;
		}

		/*3. set NFC_REG_CMD*/
		/*set sequence mode*/
		//cfg |= 0x1<<25;
		/*wait rb?*/
		if (cur_cmd->wait_rb_flag){
			cfg |= NFC_WAIT_FLAG;
		}
		/*set cmd value*/
		cfg |= cur_cmd->value;
		/*send command*/
		cfg |= NFC_SEND_CMD1;

		NFC_WRITE_REG(NFC_REG_CMD, cfg);

		/* 3. deal with dma and ecc if main data and spare data transfer finish,that is to say one sector
			was finished*/
		if ( (cur_cmd->data_fetch_flag) && (!(cur_cmd->main_data_fetch))){
			/*wait cmd fifo free and cmd finish*/
			ret1 = _wait_cmdfifo_free();
			ret1 |= _wait_cmd_finish();
			if (ret1)
				return ret1;

			*((__u32 *)sparebuf) = NFC_READ_REG(NFC_REG_USER_DATA(0));
			/*ecc check and disable ecc*/
			ret |= _check_ecc(1);

			/*start dma?*/
			/*if dma mode is wait*/
			if(0 == dma_wait_mode){
				ret1 = _wait_dma_end();
				if (ret1)
					return ret1;
			}
		}
		/*next comamnd*/
		cur_cmd = cur_cmd->next;
	}

	_disable_ecc();
	return ret;
}

/*******************************************************************************
*								NFC_Read
*
* Description 	: read some sectors data from flash in single plane mode.
* Arguments	: *rcmd	-- the read command sequence list head¡£
*			  *mainbuf	-- point to data buffer address, 	it must be four bytes align.
*                     *sparebuf	-- point to spare buffer address.
*                     dma_wait_mode	-- how to deal when dma start, 0 = wait till dma finish,
							    1 = dma interrupt was set and now sleep till interrupt occurs.
*			  page_mode  -- 0 = normal command, 1 = page mode
* Returns		: 0 = success.
			  1 = success & ecc limit.
			  -1 = too much ecc err.
* Notes		:  if align page data required£¬page command mode is used., if the commands do
			   not fetch data£¬ecc is not neccesary.
********************************************************************************/
__s32 NFC_Read(NFC_CMD_LIST  *rcmd, void *mainbuf, void *sparebuf, __u8 dma_wait_mode,__u8 page_mode )
{

	__u32 ret ;

	_enter_nand_critical();

	if (page_mode){
		ret = _read_in_page_mode(rcmd, mainbuf,sparebuf, dma_wait_mode);
	}

	else{
		ret = _read_in_normal_mode(rcmd,mainbuf,sparebuf,dma_wait_mode);
	}

	/*switch to ahb*/
	NFC_WRITE_REG(NFC_REG_CTL, (NFC_READ_REG(NFC_REG_CTL)) & (~NFC_RAM_METHOD));

	_exit_nand_critical();


	return ret;
}


/*finish the comand list */
__s32 nfc_set_cmd_register(NFC_CMD_LIST *cmd)
{
	__u32 cfg;
	__s32 ret;

	NFC_CMD_LIST *cur_cmd = cmd;
	while(cur_cmd != NULL){
		/*wait cmd fifo free*/
		ret = _wait_cmdfifo_free();
		if (ret)
			return ret;

		cfg = 0;
		/*set addr*/
		if (cur_cmd->addr_cycle){
			_set_addr(cur_cmd->addr,cur_cmd->addr_cycle);
			cfg |= ( (cur_cmd->addr_cycle - 1) << 16);
			cfg |= NFC_SEND_ADR;
		}

		/*set NFC_REG_CMD*/
		/*set cmd value*/
		cfg |= cur_cmd->value;
		/*set sequence mode*/
		//cfg |= 0x1<<25;
		/*wait rb?*/
		if (cur_cmd->wait_rb_flag){
			cfg |= NFC_WAIT_FLAG;
		}
		if (cur_cmd->data_fetch_flag){
			NFC_WRITE_REG(NFC_REG_CTL, (NFC_READ_REG(NFC_REG_CTL)) & (~NFC_RAM_METHOD));
			cfg |= NFC_DATA_TRANS;
			NFC_WRITE_REG(NFC_REG_CNT, cur_cmd->bytecnt);
		}
		/*send command*/
		cfg |= NFC_SEND_CMD1;
		NFC_WRITE_REG(NFC_REG_CMD, cfg);
		cur_cmd = cur_cmd ->next;
	}
	return 0;
}

/*******************************************************************************
*								NFC_GetId
*
* Description 	: get chip id.
* Arguments	: *idcmd	-- the get id command sequence list head.

* Returns		: 0 = success.
			  -1 = fail.
* Notes		:
********************************************************************************/
__s32 NFC_GetId(NFC_CMD_LIST  *idcmd ,__u8 *idbuf)
{
	__u32 i;
	__s32 ret;

	_enter_nand_critical();

	ret = nfc_set_cmd_register(idcmd);
	if (ret){
		_exit_nand_critical();
		return ret;
	}

	ret = _wait_cmdfifo_free();
	ret |= _wait_cmd_finish();

	/*get 5 bytes id value*/
	for (i = 0; i < 5; i++){
		*(idbuf + i) = NFC_READ_RAM_B(NFC_RAM0_BASE+i);
	}

	_exit_nand_critical();
	return ret;
}

/*******************************************************************************
*								NFC_GetStatus
*
* Description 	: get status.
* Arguments	: *scmd	-- the get status command sequence list head.

* Returns		: status result
* Notes		: some cmd must be sent with addr.
********************************************************************************/
__s32 NFC_GetStatus(NFC_CMD_LIST  *scmd)
{
	__s32 ret;

	_enter_nand_critical();
	ret = nfc_set_cmd_register(scmd);
	if (ret){
		_exit_nand_critical();
		return ret;
	}

	ret = _wait_cmdfifo_free();
	ret |= _wait_cmd_finish();
	if(ret){
		_exit_nand_critical();
		return ret;
	}

	_exit_nand_critical();
	return (NFC_READ_RAM_B(NFC_RAM0_BASE));

}
/*******************************************************************************
*								NFC_ResetChip
*
* Description 	: reset nand flash.
* Arguments	: *reset_cmd	-- the reset command sequence list head.

* Returns		: sucess or fail
* Notes		:
********************************************************************************/
__s32 NFC_ResetChip(NFC_CMD_LIST *reset_cmd)

{
	__s32 ret;

	_enter_nand_critical();

	ret = nfc_set_cmd_register(reset_cmd);
	if (ret){
		_exit_nand_critical();
		return ret;
	}
	ret = _wait_cmdfifo_free();
	ret |= _wait_cmd_finish();
	_exit_nand_critical();
	return ret;
}

/*******************************************************************************
*								NFC_SelectChip
*
* Description 	: enable chip ce.
* Arguments	: chip	-- chip no.

* Returns		: 0 = sucess -1 = fail
* Notes		:
********************************************************************************/
__s32 NFC_SelectChip( __u32 chip)
{
	__u32 cfg;


  cfg = NFC_READ_REG(NFC_REG_CTL);
  cfg &= ( (~NFC_CE_SEL) & 0xffffffff);
  cfg |= ((chip & 0x7) << 24);
  NFC_WRITE_REG(NFC_REG_CTL,cfg);


	return 0;
}

/*******************************************************************************
*								NFC_SelectRb
*
* Description 	: select rb.
* Arguments	: rb	-- rb no.

* Returns		: 0 = sucess -1 = fail
* Notes		:
********************************************************************************/
__s32 NFC_SelectRb( __u32 rb)
{
	__s32 cfg;


	  cfg = NFC_READ_REG(NFC_REG_CTL);
	  cfg &= ( (~NFC_RB_SEL) & 0xffffffff);
	  cfg |= ((rb & 0x1) << 3);
	  NFC_WRITE_REG(NFC_REG_CTL,cfg);

	  return 0;

}





__s32 NFC_DeSelectChip( __u32 chip)
{





	return 0;
}

__s32 NFC_DeSelectRb( __u32 rb)
{





	return 0;
}


/*******************************************************************************
*								NFC_CheckRbReady
*
* Description 	: check rb if ready.
* Arguments	: rb	-- rb no.

* Returns		: 0 = sucess -1 = fail
* Notes		:
********************************************************************************/

__s32 NFC_CheckRbReady( __u32 rb)
{
	__s32 ret;
	__u32 cfg = NFC_READ_REG(NFC_REG_ST);


	cfg &= (NFC_RB_STATE0 << (rb & 0x3));

	if (cfg)
		ret = 0;
	else
		ret = -1;

	return ret;
}

/*******************************************************************************
*								NFC_ChangeMode
*
* Description 	: change serial access mode when clock change.
* Arguments	: nand_info -- structure with flash bus width,pagesize ,serial access mode and other configure parametre

* Returns		: 0 = sucess -1 = fail
* Notes		: NFC must be reset before seial access mode changes.
********************************************************************************/
__s32 NFC_ChangMode(NFC_INIT_INFO *nand_info )
{
	__u32 cfg;

	pagesize = nand_info->pagesize * 512;

	/*reset nfc*/
	_reset();

	/*set NFC_REG_CTL*/
	cfg = 0;
	cfg |= NFC_EN;
	cfg |= ( (nand_info->bus_width & 0x1) << 2);
	cfg |= ( (nand_info->ce_ctl & 0x1) << 6);
	cfg |= ( (nand_info->ce_ctl1 & 0x1) << 7);
	if(nand_info->pagesize == 2 )            /*  1K  */
	   cfg |= ( 0x0 << 8 );
	else if(nand_info->pagesize == 4 )       /*  2K  */
	   cfg |= ( 0x1 << 8 );
	else if(nand_info->pagesize == 8 )       /*  4K  */
	   cfg |= ( 0x2 << 8 );
    else if(nand_info->pagesize == 16 )       /*  8K  */
	   cfg |= ( 0x3 << 8 );
	else if(nand_info->pagesize == 32 )       /*  16K  */
	   cfg |= ( 0x4 << 8 );
	else                                      /* default 4K */
	   cfg |= ( 0x2 << 8 );
	cfg |= ( (nand_info->serial_access_mode & 0x1) << 12);
	cfg |= ( (nand_info->debug & 0x1) << 31);
	NFC_WRITE_REG(NFC_REG_CTL,cfg);
	/*set NFC_TIMING */
	NFC_WRITE_REG(NFC_REG_TIMING_CFG,0xff);
	/*set NFC_SPARE_AREA */
	NFC_WRITE_REG(NFC_REG_SPARE_AREA, pagesize);

	return 0;
}

__s32 NFC_SetEccMode(__u8 ecc_mode)
{
    __u32 cfg = NFC_READ_REG(NFC_REG_ECC_CTL);


    cfg &=	((~NFC_ECC_MODE)&0xffffffff);
    cfg |= (NFC_ECC_MODE & (ecc_mode<<12));

	  NFC_WRITE_REG(NFC_REG_ECC_CTL, cfg);

	  return 0;
}

__s32 _SetCE4567(__u32 version)
{



	return 0;

}


/*******************************************************************************
*								NFC_Init
*
* Description 	: init hardware, set NFC, set TIMING, request dma .
* Arguments	: nand_info -- structure with flash bus width,pagesize ,serial access mode and other configure parametre

* Returns		: 0 = sucess -1 = fail
* Notes		: .
********************************************************************************/
__s32 NFC_Init(NFC_INIT_INFO *nand_info )
{
	__s32 ret;


//	nand_board_version = NAND_GetBoardVersion();
//	_SetCE4567(nand_board_version);

	NFC_SetEccMode(0);

	/*init nand control machine*/
	ret = NFC_ChangMode( nand_info);
	//for debug
	#if 1
		printk("ret of NFC_ChangMode is %x \n", ret);
		printk("dma_hdle  is %x \n", dma_hdle);
	#endif

	/*request special dma*/
	dma_hdle = NAND_RequestDMA(1);
	//for debug
	#if 1
		printk("dma_hdle  is %x \n", dma_hdle);
	#endif
	if (dma_hdle == 0)
		return -1;
	return ret;

}

/*******************************************************************************
*								NFC_Exit
*
* Description 	: free hardware resource, free dma , disable NFC.
* Arguments	: nand_info -- structure with flash bus width,pagesize ,serial access mode and other configure parametre

* Returns		: 0 = sucess -1 = fail
* Notes		: .
********************************************************************************/
void NFC_Exit( void )
{
	__u32 cfg;
	/*disable NFC*/
	cfg = NFC_READ_REG(NFC_REG_CTL);
	cfg &= ( (~NFC_EN) & 0xffffffff);
	NFC_WRITE_REG(NFC_REG_CTL,cfg);

	/*free dma*/
	NAND_ReleaseDMA(dma_hdle);
}

/*******************************************************************************
*								NFC_QueryINT
*
* Description 	: get nand interrupt info.
* Arguments	:
* Returns		: interrupt no. 0 = RB_B2R,1 = SINGLE_CMD_FINISH,2 = DMA_FINISH,
								5 = MULTI_CMD_FINISH
* Notes		:
********************************************************************************/
__u32 NFC_QueryINT( void )
{

//	__u16 i;
//	__u32 cfg;
//
//	cfg = NFC_READ_REG(NFC_REG_INT);
//	for (i = 0; i < 6; i++)
//	{
//		if (cfg & (1 << i))
//		{
//			/*clear irq*/
//			cfg &= ((~(1 << i))&0xffffffff);
//			NFC_WRITE_REG(NFC_REG_INT, cfg);
//
//			return ( (NFC_IRQ_MAJOR << 24) | (i << 16));
//		}
//	}
//
//	return (NFC_IRQ_MAJOR << 24);
  return 0;

}

void NFC_EnableInt(__u8 minor_int)
{
	/*
	NFC_WRITE_REG(INTC_REG_ENABLE1,NFC_READ_REG(INTC_REG_ENABLE1) | (1 << NFC_IRQ_MAJOR));
	NFC_WRITE_REG(NFC_REG_INT, NFC_READ_REG(NFC_REG_INT) | (1 << minor_int));
    */
}

void NFC_DisableInt(__u8 minor_int)
{
    /*
	NFC_WRITE_REG(NFC_REG_INT, NFC_READ_REG(NFC_REG_INT) | (~(1 << minor_int)));
	NFC_WRITE_REG(INTC_REG_ENABLE1,NFC_READ_REG(INTC_REG_ENABLE1) | (~(1 << NFC_IRQ_MAJOR)));
    */
}

