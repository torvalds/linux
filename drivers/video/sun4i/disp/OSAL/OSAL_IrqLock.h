/*
*************************************************************************************
*                         			eBsp
*					   Operation System Adapter Layer
*
*				(c) Copyright 2006-2010, All winners Co,Ld.
*							All	Rights Reserved
*
* File Name 	: OSAL_IrqLock.h
*
* Author 		: javen
*
* Description 	: ÁÙ½çÇø²Ù×÷
*
* History 		:
*      <author>    		<time>       	<version >    		<desc>
*       javen     	   2010-09-07          1.0         create this word
*
*************************************************************************************
*/
#ifndef  __OSAL_IRQLOCK_H__
#define  __OSAL_IRQLOCK_H__

void OSAL_IrqLock(__u32 *cpu_sr);
void OSAL_IrqUnLock(__u32 cpu_sr);
#define OSAL_IRQ_RETURN IRQ_HANDLED

#endif   //__OSAL_IRQLOCK_H__

