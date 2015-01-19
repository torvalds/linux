/*
 * SiIxxxx <Firmware or Driver>
 *
 * Copyright (C) 2011 Silicon Image Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed .as is. WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
 * PURPOSE.  See the GNU General Public License for more details.
*/

/**
 * @file sii_hal_linux_isr.c
 *
 * @brief Linux implementation of interrupt support used by Silicon Image
 *        MHL devices.
 *
 * $Author: Tiger Qin 
 * $Rev: $
 * $Date: Aug. 31, 2011
 *
 *****************************************************************************/

#define SII_HAL_LINUX_ISR_C

/***** #include statements ***************************************************/
#include "sii_hal.h"
#include "sii_hal_priv.h"
//#include "si_drvisrconfig.h"
#include "mhl_linuxdrv.h"

#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <mach/irqs.h>
#include <linux/amlogic/aml_gpio_consumer.h>


/***** local macro definitions ***********************************************/

/***** local type definitions ************************************************/

/***** local variable declarations *******************************************/

/***** local function prototypes *********************************************/

/***** global variable declarations *******************************************/


/***** local functions *******************************************************/

/*****************************************************************************/
/*
 *  @brief Interrupt handler for MHL transmitter interrupts.
 *
 *  @param[in]		irq		The number of the asserted IRQ line that caused
 *  						this handler to be called.
 *  @param[in]		data	Data pointer passed when the interrupt was enabled,
 *  						which in this case is a pointer to the
 *  						MhlDeviceContext of the I2c device.
 *
 *  @return     Always returns IRQ_HANDLED.
 *
 *****************************************************************************/
static irqreturn_t HalThreadedIrqHandler(int irq, void *data)
{
	pMhlDeviceContext	pMhlDevContext = (pMhlDeviceContext)data;

//	SII_DEBUG_PRINT(SII_OSAL_DEBUG_TRACE,"HalThreadedIrqHandler called\n");
	if (HalAcquireIsrLock() == HAL_RET_SUCCESS)
    {
/*
        if(pMhlDevContext->CheckDevice &&!pMhlDevContext->CheckDevice(0))//mhl device check;
        {
            SII_DEBUG_PRINT(SII_OSAL_DEBUG_TRACE,"mhl device errror\n");
            HalReleaseIsrLock();
            return IRQ_HANDLED;
        }
*/
        if(pMhlDevContext->irqHandler)
        {
            (pMhlDevContext->irqHandler)();
        }
		HalReleaseIsrLock();
	}
    else
    {
        SII_DEBUG_PRINT(MSG_ERR,"------------- irq missing! -------------\n");
    }

	return IRQ_HANDLED;
}

/***** public functions ******************************************************/

extern int amlogic_gpio_to_irq(unsigned int  pin,const char *owner,unsigned int flag);

extern int amlogic_gpio_request(unsigned int  pin,const char *label);
extern int amlogic_gpio_direction_input(unsigned int pin,const char *owner);
extern int amlogic_gpio_to_irq(unsigned int  pin,const char *owner,unsigned int flag);
// get gpio configuration from device tree.
static void aml_config_gpio_irq(void)
{
	int ret = 0;
	unsigned int gpio_irq;

	gpio_irq = devinfo->config.gpio_intr;
	ret = amlogic_gpio_request(gpio_irq, gMhlI2cIdTable[0].name);
	ret |= amlogic_gpio_direction_input(gpio_irq, gMhlI2cIdTable[0].name);
	ret |= amlogic_gpio_to_irq(gpio_irq, gMhlI2cIdTable[0].name, AML_GPIO_IRQ(gMhlDevice.pI2cClient->irq-INT_GPIO_0,FILTER_NUM7,GPIO_IRQ_LOW));
	printk("sii5293 config gpio_irq, irq = %d, ret = %d\n",gMhlDevice.pI2cClient->irq, ret);

	return ;
}

/*****************************************************************************/
/**
 * @brief Install IRQ handler.
 *
 *****************************************************************************/
halReturn_t HalInstallIrqHandler(fwIrqHandler_t irqHandler)
{
	int				retStatus;
	halReturn_t 	halRet;

	if(irqHandler == NULL)
	{
		SII_DEBUG_PRINT(MSG_ERR,"HalInstallIrqHandler: irqHandler cannot be NULL!\n");
		return HAL_RET_PARAMETER_ERROR;
	}

	halRet = I2cAccessCheck();
	if (halRet != HAL_RET_SUCCESS)
	{
		return halRet;
	}

	if(gMhlDevice.pI2cClient->irq == 0)
	{
		SII_DEBUG_PRINT(MSG_ERR,"HalInstallIrqHandler: No IRQ assigned to I2C device!\n");
		return HAL_RET_FAILURE;
	}

	gMhlDevice.irqHandler = irqHandler;

	aml_config_gpio_irq();

	retStatus = request_threaded_irq(gMhlDevice.pI2cClient->irq, NULL,
									 HalThreadedIrqHandler,
									 IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
									 gMhlI2cIdTable[0].name,
									 &gMhlDevice);

	if(retStatus != 0)
	{
		SII_DEBUG_PRINT(MSG_ERR,"sii5293 HalInstallIrqHandler: request_threaded_irq failed, status: %d\n",
				retStatus);
		gMhlDevice.irqHandler = NULL;
		return HAL_RET_FAILURE;
	}

	return HAL_RET_SUCCESS;
}

/*****************************************************************************/
/**
 * @brief Remove IRQ handler.
 *
 *****************************************************************************/
halReturn_t HalRemoveIrqHandler(void)
{
	halReturn_t 	halRet;

	halRet = I2cAccessCheck();
	if (halRet != HAL_RET_SUCCESS)
	{
		return halRet;
	}

	if(gMhlDevice.irqHandler == NULL)
	{
		SII_DEBUG_PRINT(MSG_ERR,"HalRemoveIrqHandler: no irqHandler installed!\n");
		return HAL_RET_FAILURE;
	}

	free_irq(gMhlDevice.pI2cClient->irq, &gMhlDevice);

	gMhlDevice.irqHandler = NULL;

	return HAL_RET_SUCCESS;
}

/*****************************************************************************/
/**
 * @brief ON/OFF TX chip IRQ , just be used for debugging interface;
 *
 *****************************************************************************/
void HalEnableIrq(uint8_t bEnable)
{
    if(bEnable)
    {
        enable_irq(gMhlDevice.pI2cClient->irq);
    }
    else
    {
        disable_irq(gMhlDevice.pI2cClient->irq);
    }
}

bool_t is_interrupt_asserted( void )
{
	return (amlogic_get_value(devinfo->config.gpio_intr, gMhlI2cIdTable[0].name) == INT_IS_ASSERTED );
    //return (gpio_get_value(GPIO_INT_PIN) == INT_IS_ASSERTED);	
}

#if 0
/*****************************************************************************/
/**
 * @brief SilMon IRQ handler,
 *
 *****************************************************************************/
static irqreturn_t HalSilMonRequestIrqHandler(int irq, void *data)
{
	pMhlDeviceContext	pMhlDevContext = (pMhlDeviceContext)data;

    int gpio_value;
	unsigned long		flags;
//  SII_DEBUG_PRINT(SII_OSAL_DEBUG_TRACE,"------------- HalSilMonRequestIrqHandler irq coming! -------------\n");
    spin_lock_irqsave(&pMhlDevContext->SilMonRequestIRQ_Lock, flags);

    if(HalGpioGetPin(GPIO_REQ_IN,&gpio_value)<0)
    {
        SII_DEBUG_PRINT(SII_OSAL_DEBUG_TRACE,"HalSilMonRequestIrqHandler GPIO(%d) get error\n",gpio_value);
        spin_unlock_irqrestore(&pMhlDevContext->SilMonRequestIRQ_Lock, flags);
        return IRQ_HANDLED;
    }

    /*
    the following is for void wrongly invoke irq, it may be caused by EMI when plug in/out MHL cable;
    */
    if((gMhlDevice.SilMonControlReleased&& gpio_value)
       ||(!gMhlDevice.SilMonControlReleased&&!gpio_value))
    {
        SII_DEBUG_PRINT(SII_OSAL_DEBUG_TRACE,"HalSilMonRequestIrqHandler, wrong IRQ coming, please check you board\n");
        spin_unlock_irqrestore(&pMhlDevContext->SilMonRequestIRQ_Lock, flags);
        return IRQ_HANDLED;
    }

    if(gpio_value)
    {
        HalGpioSetPin(GPIO_GNT,1);
        HalEnableI2C(true);
        enable_irq(pMhlDevContext->pI2cClient->irq);
        SII_DEBUG_PRINT(SII_OSAL_DEBUG_TRACE,"release SiliMon control\n");
        gMhlDevice.SilMonControlReleased = true;
    }
    else
    {
        disable_irq(pMhlDevContext->pI2cClient->irq);       
        HalEnableI2C(false);
        HalGpioSetPin(GPIO_GNT,0);
        SII_DEBUG_PRINT(SII_OSAL_DEBUG_TRACE,"under SiliMon control \n");
        gMhlDevice.SilMonControlReleased = false;
    }

    spin_unlock_irqrestore(&pMhlDevContext->SilMonRequestIRQ_Lock, flags);

	return IRQ_HANDLED;
}
/*****************************************************************************/
/**
 * @brief Install  silmon request IRQ handler.
 *
 *****************************************************************************/
halReturn_t HalInstallSilMonRequestIrqHandler(void)
{
	int				retStatus;
	halReturn_t 	halRet;

	halRet = I2cAccessCheck();
	if (halRet != HAL_RET_SUCCESS)
	{
		return halRet;
	}

    halRet = HalGetGpioIrqNumber(GPIO_REQ_IN, &gMhlDevice.SilMonRequestIRQ);
    if(halRet!= HAL_RET_SUCCESS)
    {
        return halRet;
    }   

    spin_lock_init(&gMhlDevice.SilMonRequestIRQ_Lock);
    gMhlDevice.SilMonControlReleased  = true;

	retStatus = request_threaded_irq(gMhlDevice.SilMonRequestIRQ, NULL,
									 HalSilMonRequestIrqHandler,
									 IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT,
									 gMhlI2cIdTable[0].name,
									 &gMhlDevice);
	if(retStatus != 0)
	{
		SII_DEBUG_PRINT(SII_OSAL_DEBUG_TRACE,"HalInstallIrqHandler: request_threaded_irq failed, status: %d\n",
				retStatus);
		return HAL_RET_FAILURE;
	}

	return HAL_RET_SUCCESS;
}
/*****************************************************************************/
/**
 * @brief Remove SilMonRequest IRQ handler.
 *
 *****************************************************************************/
halReturn_t HalRemoveSilMonRequestIrqHandler(void)
{
	halReturn_t 	halRet;

	halRet = I2cAccessCheck();
	if (halRet != HAL_RET_SUCCESS)
	{
		return halRet;
	}

	free_irq(gMhlDevice.SilMonRequestIRQ, &gMhlDevice);

	return HAL_RET_SUCCESS;
}
#endif
/*****************************************************************************/
/**
 * @brief check device before IRQ handling, it fixed the issue taht in some case when chip error,
 *the program can NOT exit since in IRQ dead loop; 
 *
 *****************************************************************************/
halReturn_t HalInstallCheckDeviceCB(fnCheckDevice fn)
{
    gMhlDevice.CheckDevice = fn;
	return HAL_RET_SUCCESS;
}
