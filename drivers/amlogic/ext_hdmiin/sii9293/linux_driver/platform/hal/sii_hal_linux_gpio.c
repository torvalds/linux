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
 * @file sii_hal_linux_gpio.c
 *
 * @brief Linux implementation of GPIO pin support needed by Silicon Image
 *        MHL devices.
 *
 * $Author: Tiger qin
 * $Rev: $
 * $Date: June. 9, 2011
 *
 *****************************************************************************/

#define SII_HAL_LINUX_GPIO_C

/***** #include statements ***************************************************/
#include "sii_hal.h"
#include "sii_hal_priv.h"
#include "mhl_linuxdrv.h"
#include <linux/ioport.h>
#include <asm/io.h>
/***** local macro definitions ***********************************************/


/***** local type definitions ************************************************/


/***** local variable declarations *******************************************/


/***** local function prototypes *********************************************/


/***** global variable declarations *******************************************/
/***** local functions *******************************************************/
typedef enum 
{
    DIRECTION_IN =0,
    DIRECTION_OUT,
}GPIODirection_t;

typedef struct tagGPIOInfo
{
    GpioIndex_t index;
    int gpio_number;
    char gpio_descripion[40];
    GPIODirection_t gpio_direction;
    int init_value;
}GPIOInfo_t;

#define GPIO_ITEM(a,b,c,d) \
{.index = (a),\
 .gpio_number = (b), \
 .gpio_descripion = (#a), \
 .gpio_direction = (c), \
 .init_value = (d),  } 


static GPIOInfo_t GPIO_List[]=
{
//    GPIO_ITEM(GPIO_136,             136,DIRECTION_OUT,0),   //configure GPIO 135 134 as input at trainner board
//    GPIO_ITEM(GPIO_140,             140,DIRECTION_OUT,1),   //configure GPIO 138 139 as output at trainner board
//    GPIO_ITEM(GPIO_INT,             94,DIRECTION_IN,0),
    GPIO_ITEM(GPIO_RST,             93,DIRECTION_OUT,1),//init high
};

static GPIOInfo_t  * GetGPIOInfo(int gpio)
{
    int i;
    for(i =0; i< ARRAY_SIZE(GPIO_List);i++)
    {
        if(gpio == GPIO_List[i].index)
        {
            return &GPIO_List[i];
        }
    }
    return NULL;
}


/*
 * IEN  - Input Enable
 * IDIS - Input Disable
 * PTD  - Pull type Down
 * PTU  - Pull type Up
 * DIS  - Pull type selection is inactive
 * EN   - Pull type selection is active
 * M0   - Mode 0
 */

#define IEN     (1 << 8)

#define IDIS    (0 << 8)
#define PTU     (1 << 4)
#define PTD     (0 << 4)
#define EN      (1 << 3)
#define DIS     (0 << 3)

#define M0      0
#define M1      1
#define M2      2
#define M3      3
#define M4      4
#define M5      5
#define M6      6
#define M7      7

#define IO_PHY_ADDRESS  0x48000000
#define PAD_CONF_OFFSET  0x2030

/*****************************************************************************/
/**
 * @brief set GPIO Pin MUX, it is also can be done in kernel ,
 * we put it here , make it sure that all pin mux is configured crrectly; 
 * *
 *****************************************************************************/
#if 0 // pinmux need not be configed here in aml platform. by Jets, Nov/25/2013
static void HalSetPinMux(void)
{
#define PADCONF_SDMMC2_CLK_OFFSET			0x128   //GPIO130 OFFSET
    int i;
    unsigned short x,old;
    void * base = NULL;
    base = ioremap(IO_PHY_ADDRESS, 
                          0x10000);

    if (base == NULL)
    {
        SII_DEBUG_PRINT(MSG_ERR,"IO Mapping failed\n");
        return ;
    } 
    else
    {
        SII_DEBUG_PRINT(MSG_STAT,"iobase = 0x%x\n",base);
    }

    for(i = 0 ;i<10;i++) //GPIO130 ~~ GPIO139
    {
        old = ioread16(base + PAD_CONF_OFFSET + PADCONF_SDMMC2_CLK_OFFSET + i*2);

        switch(i)
        {
        case 5: 
                x = (IEN | EN | PTU | M4 );break; 


        default:               
                x = (M4 | IDIS);break;
        }

        iowrite16(x,base + PAD_CONF_OFFSET + PADCONF_SDMMC2_CLK_OFFSET + i*2);

//        SII_DEBUG_PRINT(SII_OSAL_DEBUG_TRACE,"GPIO%d {0x%x}:0x%x => 0x%x ;\n",130+ i,(int)(base + 0x2030 + 0x128 + i*2),old,x);
    }

    iounmap(base);
}
#endif

/*****************************************************************************/
/**
 * @brief when SilMon is open , disable ARM I2C R/W; 
 * configure I2C pin to output mode when I2c disable ; 
 * *
 *****************************************************************************/
#if 0 // this api is not used. by jets, Nov/23/2013
halReturn_t HalEnableI2C(int bEnable )
{
#define PADCONF_I2C2_SCL_OFFSET			0x18e
#define PADCONF_I2C2_SDA_OFFSET			0x190

    unsigned short x,old;
    void * base = NULL;
    base = ioremap(IO_PHY_ADDRESS, 
                          0x10000);

    if (base == NULL)
    {
        SII_DEBUG_PRINT(MSG_ERR,"IO Mapping failed\n");
        return HAL_RET_FAILURE;
    } 

    old = ioread16(base + PAD_CONF_OFFSET + PADCONF_I2C2_SCL_OFFSET);

    if(bEnable)
    {
        x = (IEN  | PTU | EN  | M0 );
    }
    else
    {
        x = (EN | PTD | M4 | IDIS);
    }

    iowrite16(x,base + PAD_CONF_OFFSET + PADCONF_I2C2_SCL_OFFSET);
//    SII_DEBUG_PRINT(SII_OSAL_DEBUG_TRACE,"GPIO%d {0x%x}:0x%x => 0x%x ;\n",168,pinconf,old,x);

    old = ioread16(base + PAD_CONF_OFFSET + PADCONF_I2C2_SDA_OFFSET);

    if(bEnable)
    {
        x = (IEN | M0 );
    }
    else
    {
        x = (EN | PTD | M4 | IDIS);
    }

    iowrite16(x,base + PAD_CONF_OFFSET + PADCONF_I2C2_SDA_OFFSET);
//    SII_DEBUG_PRINT(SII_OSAL_DEBUG_TRACE,"GPIO%d {0x%x}:0x%x => 0x%x ;\n",183,pinconf,old,x);
    iounmap(base);
   	return HAL_RET_SUCCESS;
}
#endif

// get gpio configuration from device tree.
static void aml_get_gpio(void)
{
	// for gpio reset
	GPIO_List[0].gpio_number = devinfo->config.gpio_reset;

	return ;
}

/*****************************************************************************/
/**
 * @brief Configure platform GPIOs needed by the MHL device.
 *
 *****************************************************************************/
halReturn_t HalGpioInit(void)
{
	int status;
    int i ,j;

#if 0 // pinmux need not be configed here in aml platform. by Jets, Nov/25/2013
    HalSetPinMux();
#endif

	aml_get_gpio();

    for(i =0; i< ARRAY_SIZE(GPIO_List);i++)
    {
    	/* Request  GPIO . */
        status = gpio_request(GPIO_List[i].gpio_number, GPIO_List[i].gpio_descripion);
    	if (status < 0 && status != -EBUSY)
    	{
    		SII_DEBUG_PRINT(MSG_ERR,"HalInit gpio_request for GPIO %d (H/W Reset) failed, status: %d\n", GPIO_List[i].gpio_number, status);
            for(j = 0; j < i;j++)
            {
                gpio_free(GPIO_List[j].gpio_number);
            }
            return HAL_RET_FAILURE;
    	}

        if(GPIO_List[i].gpio_direction == DIRECTION_OUT)
        {
            status = gpio_direction_output(GPIO_List[i].gpio_number, GPIO_List[i].init_value);
        }
        else
        {
            status = gpio_direction_input(GPIO_List[i].gpio_number);
        }

    	if (status < 0)
    	{
            SII_DEBUG_PRINT(MSG_ERR,"HalInit gpio_direction_output for GPIO %d (H/W Reset) failed, status: %d\n", GPIO_List[i].gpio_number, status);
            for(j = 0; j <= i;j++)
            {
                gpio_free(GPIO_List[j].gpio_number);
            }
    		return HAL_RET_FAILURE;
    	}
//        SII_DEBUG_PRINT(SII_OSAL_DEBUG_TRACE,"initialize %s successfully\n",GPIO_List[i].gpio_descripion);
    }



   	return HAL_RET_SUCCESS;
}

/*****************************************************************************/
/**
 * @brief Release GPIO pins needed by the MHL device.
 *
 *****************************************************************************/
halReturn_t HalGpioTerm(void)
{
	halReturn_t 	halRet;
    int index;
	halRet = HalInitCheck();
	if(halRet != HAL_RET_SUCCESS)
	{
		return halRet;
	}

    for(index = 0; index < ARRAY_SIZE(GPIO_List);index++)
    {
        gpio_free(GPIO_List[index].gpio_number);
    }

	return HAL_RET_SUCCESS;
}

/*****************************************************************************/
/**
 * @brief Platform specific function to set the output pin to control the MHL
 * 		  transmitter device.
 *
 *****************************************************************************/
halReturn_t HalGpioSetPin(GpioIndex_t gpio,int value)
{
	halReturn_t 	halRet;
    GPIOInfo_t      *pGpioInfo;
	halRet = HalInitCheck();
	if(halRet != HAL_RET_SUCCESS)
	{
		return halRet;
	}
    pGpioInfo = GetGPIOInfo(gpio);
    if(!pGpioInfo)
    {
        SII_DEBUG_PRINT(MSG_ERR,"%d is NOT right gpio_index!\n",(int)gpio);
        return HAL_RET_FAILURE;
    }
    if(pGpioInfo->gpio_direction != DIRECTION_OUT)
    {
        SII_DEBUG_PRINT(MSG_ERR,"gpio(%d) is NOT ouput gpio!\n",pGpioInfo->gpio_number);
        return HAL_RET_FAILURE;
    }

	gpio_set_value(pGpioInfo->gpio_number, value ? 1 : 0);


    if(value)
    {
       SII_DEBUG_PRINT(MSG_STAT,">> %s to HIGH <<\n",pGpioInfo->gpio_descripion);
    }
    else
    {
       SII_DEBUG_PRINT(MSG_STAT,">> %s to LOW <<\n",pGpioInfo->gpio_descripion);
    }

	return HAL_RET_SUCCESS;
}


/*****************************************************************************/
/**
 * @brief Platform specific function to get the input pin value of the MHL
 * 		  transmitter device.
 *
 *****************************************************************************/
halReturn_t HalGpioGetPin(GpioIndex_t gpio,int * value)
{
	halReturn_t 	halRet;
    GPIOInfo_t      *pGpioInfo;

	halRet = HalInitCheck();
	if(halRet != HAL_RET_SUCCESS)
	{
		return halRet;
	}

    pGpioInfo = GetGPIOInfo(gpio);
    if(!pGpioInfo)
    {
        SII_DEBUG_PRINT(MSG_ERR,"%d is NOT right gpio_index!\n",(int)gpio);
        return HAL_RET_FAILURE;
    }
    if(pGpioInfo->gpio_direction != DIRECTION_IN)
    {
        SII_DEBUG_PRINT(MSG_ERR,"gpio(%d) is NOT input gpio!\n",pGpioInfo->gpio_number);
        return HAL_RET_FAILURE;
    }

	*value = gpio_get_value(pGpioInfo->gpio_number);
	return HAL_RET_SUCCESS;
}
/*****************************************************************************/
/**
 * @brief HalGetGpioIrqNumber
 * request one irq number FROM GPIO 
 *
 *****************************************************************************/
#if 0 // this api is not used. by jets, Nov/23/2013
halReturn_t HalGetGpioIrqNumber(GpioIndex_t gpio, unsigned int * irqNumber)
{
	halReturn_t 	halRet;
    GPIOInfo_t      *pGpioInfo;

	halRet = HalInitCheck();
	if(halRet != HAL_RET_SUCCESS)
	{
		return halRet;
	}

    pGpioInfo = GetGPIOInfo(gpio);
    if(!pGpioInfo)
    {
        SII_DEBUG_PRINT(MSG_ERR,"%d is NOT right gpio_index!\n",(int)gpio);
        return HAL_RET_FAILURE;
    }

    *irqNumber = gpio_to_irq(pGpioInfo->gpio_number);

	if(*irqNumber >=0)
    {
//        SII_DEBUG_PRINT(SII_OSAL_DEBUG_TRACE,"%s(%d)-->IRQ(%d) \n",pGpioInfo->gpio_descripion,pGpioInfo->gpio_number,*irqNumber);
        return HAL_RET_SUCCESS;

    }

    return HAL_RET_FAILURE;
}
#endif

