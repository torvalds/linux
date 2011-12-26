/*
*********************************************************************************************************
*											        eBIOS
*						            the Easy Portable/Player Develop Kits
*									           dma sub system
*
*						        (c) Copyright 2006-2008, David China
*											All	Rights Reserved
*
* File    : int_for_nand.c
* By      : Richard
* Version : V1.00
*********************************************************************************************************
*/
#include "nand_private.h"
#include <linux/wait.h>
#include <linux/sched.h>
#include "../nfc/nfc.h"
#include "../nfc/nfc_i.h"

static int nandrb_ready_flag = 1;
static DECLARE_WAIT_QUEUE_HEAD(NAND_RB_WAIT);

//#define RB_INT_MSG_ON
#ifdef  RB_INT_MSG_ON
#define dbg_rbint(fmt, args...) printk(fmt, ## args)
#else
#define dbg_rbint(fmt, ...)  ({})
#endif

//#define RB_INT_WRN_ON
#ifdef  RB_INT_WRN_ON
#define dbg_rbint_wrn(fmt, args...) printk(fmt, ## args)
#else
#define dbg_rbint_wrn(fmt, ...)  ({})
#endif


void NAND_EnRbInt(void)
{
	//clear interrupt
	NFC_WRITE_REG(NFC_REG_ST,NFC_RB_B2R);
	if(NFC_READ_REG(NFC_REG_ST)&NFC_RB_B2R)
	{
		dbg_rbint_wrn("nand clear rb int status error in int enable \n");
		dbg_rbint_wrn("rb status: 0x%x\n", NFC_READ_REG(NFC_REG_ST));
	}
	
	nandrb_ready_flag = 0;

	//enable interrupt
	NFC_WRITE_REG(NFC_REG_INT, NFC_B2R_INT_ENABLE);

	dbg_rbint("rb int en\n");
}


void NAND_ClearRbInt(void)
{
    
	//disable interrupt
	NFC_WRITE_REG(NFC_REG_INT, 0);

	dbg_rbint("rb int clear\n");

	//clear interrupt
	NFC_WRITE_REG(NFC_REG_ST,NFC_READ_REG(NFC_REG_ST));
	if(NFC_READ_REG(NFC_REG_ST)&NFC_RB_B2R)
	{
		dbg_rbint_wrn("nand clear rb int status error in int clear \n");
		dbg_rbint_wrn("rb status: 0x%x\n", NFC_READ_REG(NFC_REG_ST));
	}
	
	nandrb_ready_flag = 0;
}


void NAND_RbInterrupt(void)
{

	dbg_rbint("rb int occor! \n");
	if(!(NFC_READ_REG(NFC_REG_ST)&NFC_RB_B2R))
	{
		dbg_rbint_wrn("nand rb int late, rb status: 0x%x, rb int en: 0x%x \n",NFC_READ_REG(NFC_REG_ST),NFC_READ_REG(NFC_REG_INT));
	}
    
    NAND_ClearRbInt();
    
    nandrb_ready_flag = 1;
	wake_up( &NAND_RB_WAIT );

}


__s32 NAND_WaitRbReady(void)
{
	__u32 rb;
	
	NAND_EnRbInt();
	
	//wait_event(NAND_RB_WAIT, nandrb_ready_flag);
	dbg_rbint("rb wait, nfc_ctl: 0x%x, rb status: 0x%x, rb int en: 0x%x\n", NFC_READ_REG(NFC_REG_CTL), NFC_READ_REG(NFC_REG_ST), NFC_READ_REG(NFC_REG_INT));

	if(nandrb_ready_flag)
	{
		dbg_rbint("fast rb int\n");
		NAND_ClearRbInt();
		return 0;
	}

	rb=  ( NFC_READ_REG(NFC_REG_CTL) & NFC_RB_SEL ) >>3;
	if(!rb)
	{
		if(NFC_READ_REG(NFC_REG_ST) & NFC_RB_STATE0)
		{
			dbg_rbint_wrn("rb0 fast ready \n");
			dbg_rbint_wrn("nfc_ctl: 0x%x, rb status: 0x%x, rb int en: 0x%x\n", NFC_READ_REG(NFC_REG_CTL), NFC_READ_REG(NFC_REG_ST), NFC_READ_REG(NFC_REG_INT));
			NAND_ClearRbInt();
			return 0;
		}
			
	}
	else
	{
		if(NFC_READ_REG(NFC_REG_ST) & NFC_RB_STATE1)
		{
			dbg_rbint_wrn("rb1 fast ready \n");
			dbg_rbint_wrn("nfc_ctl: 0x%x, rb status: 0x%x, rb int en: 0x%x\n", NFC_READ_REG(NFC_REG_CTL), NFC_READ_REG(NFC_REG_ST), NFC_READ_REG(NFC_REG_INT));
			NAND_ClearRbInt();
			return 0;
		}
	}
	
	if(wait_event_timeout(NAND_RB_WAIT, nandrb_ready_flag, 1*HZ)==0)
	{
		dbg_rbint_wrn("nand wait rb ready time out\n");
		dbg_rbint_wrn("rb wait time out, nfc_ctl: 0x%x, rb status: 0x%x, rb int en: 0x%x\n", NFC_READ_REG(NFC_REG_CTL), NFC_READ_REG(NFC_REG_ST), NFC_READ_REG(NFC_REG_INT));
		NAND_ClearRbInt();
	}
	else
	{
		dbg_rbint("nand wait rb ready ok\n");
	}
	
    return 0;
}

/*
__s32 NAND_WaitRbReady(void)
{

	//wait_event(NAND_RB_WAIT, nandrb_ready_flag);
	printk("rb wait, nfc_ctl: 0x%x, rb status:0x%x, rb int en: 0x%x\n", NFC_READ_REG(NFC_REG_CTL), NFC_READ_REG(NFC_REG_ST), NFC_READ_REG(NFC_REG_INT));
    wait_event(NAND_RB_WAIT, nandrb_ready_flag);
	printk("rb wait ok\n");
	
    return 0;
}
*/
