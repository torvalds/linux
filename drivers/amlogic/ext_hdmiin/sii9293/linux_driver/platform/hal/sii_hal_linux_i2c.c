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
 * @file sii_hal_linux_i2c.c
 *
 * @brief Linux implementation of I2c access functions required by Silicon Image
 *        MHL devices.
 *
 * $Author: Tiger Qin
 * $Rev: $
 * $Date: Aug. 24, 2011
 *
 *****************************************************************************/

#define SII_HAL_LINUX_I2C_C

/***** #include statements ***************************************************/
#include <linux/i2c.h>
#include <linux/slab.h>
#include "sii_hal.h"
#include "sii_hal_priv.h"
#include "si_platform.h"
#include "si_drv_cra_cfg.h"
#include "si_cra_internal.h"
#include "mhl_linuxdrv.h"
#include <linux/amlogic/aml_gpio_consumer.h>
#include <mach/irqs.h>

/***** local macro definitions ***********************************************/


/***** local type definitions ************************************************/


/***** local variable declarations *******************************************/
struct i2c_dev_info {
	uint8_t				dev_addr;
	struct i2c_client	*client;
};

#define I2C_DEV_INFO(addr) \
	{.dev_addr = addr >> 1, .client = NULL}

static struct i2c_dev_info device_addresses[] = {
	I2C_DEV_INFO(DEV_PAGE_PP_0),
	I2C_DEV_INFO(DEV_PAGE_PP_1),
	I2C_DEV_INFO(DEV_PAGE_PP_2),
	I2C_DEV_INFO(DEV_PAGE_PP_9),
	I2C_DEV_INFO(DEV_PAGE_PP_C),
	I2C_DEV_INFO(DEV_PAGE_PP_3),    //For evita usage
	I2C_DEV_INFO(DEV_PAGE_PP_4),    //For evita usage
};

/***** local function prototypes *********************************************/

static int32_t MhlI2cProbe(struct i2c_client *client, const struct i2c_device_id *id);
static int32_t MhlI2cRemove(struct i2c_client *client);


/***** global variable declarations *******************************************/


/***** local functions *******************************************************/

/**
 *  @brief Standard Linux probe callback.
 *  
 *  Probe is called if the I2C device name passed to HalOpenI2cDevice matches
 *  the name of an I2C device on the system.
 *  
 *  All we need to do is store the passed in i2c_client* needed when performing
 *  I2C bus transactions with the device.
 *
 *  @param[in]      client     		Pointer to i2c client structure of the matching
 *  								I2C device.
 *  @param[in]      i2c_device_id	Index within MhlI2cIdTable of the matching
 *  								I2C device.
 *
 *  @return     Always returns zero to indicate success.
 *
 *****************************************************************************/
static int32_t MhlI2cProbe(struct i2c_client *client, const struct i2c_device_id *id)
{
	//gMhlDevice.pI2cClient = client;
    return 0;
}


/**
 *  @brief Standard Linux remove callback.
 *  
 *  Remove would be called if the I2C device were removed from the system (very unlikley).
 *  
 *  All we need to do is clear our copy of the i2c_client pointer to indicate we no longer
 *  have an I2C device to work with.
 *
 *  @param[in]	client	Pointer to the client structure representing the I2C device that
 *  					was removed.
 *
 *  @return     Always returns zero to indicate success.
 *
 *****************************************************************************/
static int32_t MhlI2cRemove(struct i2c_client *client)
{
	//gMhlDevice.pI2cClient = NULL;
    return 0;
}


/***** public functions ******************************************************/


/*****************************************************************************/
/**
 *  @brief Check if I2c access is allowed.
 *
 *****************************************************************************/
halReturn_t I2cAccessCheck(void)
{
	halReturn_t		retStatus;

	retStatus = HalInitCheck();
	if (retStatus != HAL_RET_SUCCESS)
	{
		return retStatus;
	}

	if(gMhlDevice.pI2cClient == NULL)
	{
		pr_info("I2C device not currently open\n");
		retStatus = HAL_RET_DEVICE_NOT_OPEN;
	}
	return retStatus;
}

static struct i2c_board_info si_5293_i2c_boardinfo[] = {
	{
	   	I2C_BOARD_INFO(MHL_DEVICE_NAME, (DEV_PAGE_PP_0 >> 1)),
     	.flags = I2C_CLIENT_WAKE,
		.irq = INT_GPIO_0,
	}
};





/*****************************************************************************/
/**
 * @brief Request access to the specified I2c device.
 * 
 * @param DeviceName, the name  must be same as which is registered in your kernel code; 
 * we use probe I2C mode , so  
 *  
 * @param DriverName, iceman driver name
 * 
 * @return
 * 
 */
halReturn_t HalOpenI2cDevice(char const *DeviceName, char const *DriverName)
{
    int idx;
	int ret = -EFAULT;
	halReturn_t		retStatus;
    int32_t 		retVal;

	retStatus = HalInitCheck();
	if (retStatus != HAL_RET_SUCCESS)
	{
		return retStatus;
	}

    retVal = strnlen(DeviceName, I2C_NAME_SIZE);
    if (retVal >= I2C_NAME_SIZE)
    {
    	pr_info("I2c device name too long!\n");
    	return HAL_RET_PARAMETER_ERROR;
    }


    memcpy(gMhlI2cIdTable[0].name, DeviceName, retVal);
    gMhlI2cIdTable[0].name[retVal] = 0;
    gMhlI2cIdTable[0].driver_data = 0;

    gMhlDevice.driver.driver.name = DriverName;
    gMhlDevice.driver.id_table = gMhlI2cIdTable;
    gMhlDevice.driver.probe = MhlI2cProbe;
    gMhlDevice.driver.remove = MhlI2cRemove;
#if 0    
    retVal = i2c_add_driver(&gMhlDevice.driver);
    if (retVal != 0)
    {
    	pr_info("I2C driver add failed\n");
        retStatus = HAL_RET_FAILURE;
    }
    else
    {
    	if (gMhlDevice.pI2cClient == NULL)
        {
            i2c_del_driver(&gMhlDevice.driver);
            pr_info("I2C driver add failed\n");
            retStatus = HAL_RET_NO_DEVICE;
        }
    	else
    	{
    		retStatus = HAL_RET_SUCCESS;
    	}
    }
    return retStatus;
#endif

    /* "Hotplug" the MHL device onto the 2nd I2C bus */
    gMhlDevice.pI2cAdapter = i2c_get_adapter(devinfo->config.i2c_bus_index);
	if (gMhlDevice.pI2cAdapter == NULL) {
		pr_err ("%s() failed to get i2c adapter\n", __func__);
		goto done;
	}

	for (idx = 0; idx < ARRAY_SIZE(device_addresses); idx++) {
		if (idx ==0 ) {
			gMhlDevice.pI2cClient = i2c_new_device(gMhlDevice.pI2cAdapter, &si_5293_i2c_boardinfo[idx]);
			device_addresses[idx].client = gMhlDevice.pI2cClient;
		} else {
			device_addresses[idx].client = i2c_new_dummy(gMhlDevice.pI2cAdapter,
											device_addresses[idx].dev_addr);
		}
		if (device_addresses[idx].client == NULL){
			goto err_exit;
		}
	}

	ret = i2c_add_driver(&gMhlDevice.driver);
	if (ret < 0) {
		pr_err("[ERROR] %s():%d failed !\n", __func__, __LINE__);
        retStatus = HAL_RET_FAILURE;
		goto err_exit;
	}

	goto done;

err_exit:
	for (idx = 0; idx < ARRAY_SIZE(device_addresses); idx++) {
		if (device_addresses[idx].client != NULL)
			i2c_unregister_device(device_addresses[idx].client);
	}

done:
	return retStatus;

}

/*****************************************************************************/
/**
 * @brief Terminate access to the specified I2c device.
 *
 *****************************************************************************/
halReturn_t HalCloseI2cDevice(void)
{
	halReturn_t		retStatus;
	int	idx;

	retStatus = HalInitCheck();
	if (retStatus != HAL_RET_SUCCESS)
	{
		return retStatus;
	}

	if(gMhlDevice.pI2cClient == NULL)
	{
		pr_info("I2C device not currently open\n");
        retStatus = HAL_RET_DEVICE_NOT_OPEN;
	}
	else
	{
		i2c_del_driver(&gMhlDevice.driver);
		gMhlDevice.pI2cClient = NULL;
        for (idx = 0; idx < ARRAY_SIZE(device_addresses); idx++) {
            if (device_addresses[idx].client != NULL){
                i2c_unregister_device(device_addresses[idx].client);
            }
        }
		retStatus = HAL_RET_SUCCESS;
	}
	return retStatus;
}
#if 0
/*****************************************************************************/
/**
 * @brief Read a single byte from a register within an I2c device.
 *
 *****************************************************************************/
uint8_t I2C_ReadByte(uint8_t deviceID, uint8_t offset)
{
	uint8_t					accessI2cAddr;
//	uint8_t					addrOffset;
	union i2c_smbus_data	data;
	int32_t					status;


	if (I2cAccessCheck() != HAL_RET_SUCCESS)
	{
		/* Driver expects failed I2C reads to return 0xFF */
		return 0xFF;
	}

    accessI2cAddr = deviceID>>1;  
    status = i2c_smbus_xfer(gMhlDevice.pI2cClient->adapter, accessI2cAddr,
    						0, I2C_SMBUS_READ, offset, I2C_SMBUS_BYTE_DATA,
    						&data);
	if (status < 0)
	{
        if(deviceID != 0xfc)//void much message
        {
            pr_info("I2C_ReadByte(0x%02x, 0x%02x), i2c_transfer error: %d\n",
                            deviceID, offset, status);
        }
		data.byte = 0xFF;
	}

	return data.byte;
}

/*****************************************************************************/
/**
 * @brief Write a single byte to a register within an I2c device.
 *
 *****************************************************************************/
void I2C_WriteByte(uint8_t deviceID, uint8_t offset, uint8_t value)
{
	uint8_t					accessI2cAddr;
	union i2c_smbus_data	data;
	int32_t					status;

	if (I2cAccessCheck() != HAL_RET_SUCCESS)
	{
		return;
	}
    accessI2cAddr = deviceID>>1;
	data.byte = value;

    status = i2c_smbus_xfer(gMhlDevice.pI2cClient->adapter, accessI2cAddr,
    						0, I2C_SMBUS_WRITE, offset, I2C_SMBUS_BYTE_DATA,
    						&data);
	if (status < 0)
	{
		pr_info("I2C_WriteByte(0x%02x, 0x%02x, 0x%02x), i2c_transfer error: %d\n",
						deviceID, offset, value, status);
	}
}

/*****************************************************************************/
/**
 * @brief Read some bytes from a register within an I2c device.
 *
 *****************************************************************************/
uint8_t I2C_ReadBlock(uint8_t deviceID, uint8_t offset,uint8_t *buf, uint8_t len)
{
    int i;
	uint8_t					accessI2cAddr;
	union i2c_smbus_data	data;
	int32_t					status;


	if (I2cAccessCheck() != HAL_RET_SUCCESS)
	{
		/* Driver expects failed I2C reads to return 0xFF */
		return 0x00;
	}

    accessI2cAddr = deviceID>>1; 
    memset(buf,0xff,len);

    for(i = 0 ;i < len;i++)
    {
        status = i2c_smbus_xfer(gMhlDevice.pI2cClient->adapter, accessI2cAddr,
        						0, I2C_SMBUS_READ, offset + i, I2C_SMBUS_BYTE_DATA,
        						&data);
    	if (status < 0)
    	{
            return 0;//if  error , return 
    	}

        *buf = data.byte;
        buf++;
    }
    return len;
}

/*****************************************************************************/
/**
 * @brief Write some bytes to a register within an I2c device.
 *
 *****************************************************************************/
void I2C_WriteBlock(uint8_t deviceID, uint8_t offset, uint8_t *buf, uint8_t len)
{
    int i;
	uint8_t					accessI2cAddr;
	union i2c_smbus_data	data;
	int32_t					status;


	if (I2cAccessCheck() != HAL_RET_SUCCESS)
	{
		/* Driver expects failed I2C reads to return 0xFF */
		return ;
	}

    accessI2cAddr = deviceID>>1; 

    for(i = 0 ;i < len;i++)
    {
        data.byte = *buf;
        status = i2c_smbus_xfer(gMhlDevice.pI2cClient->adapter, accessI2cAddr,
        						0, I2C_SMBUS_WRITE, offset + i, I2C_SMBUS_BYTE_DATA,
        						&data);
    	if (status < 0)
    	{
            return ;
    	}
        buf++;
    }
    return ;
}
#endif
/*****************************************************************************/
/**
 * @brief Read some bytes from a register within an I2c device.
 *
 *****************************************************************************/
uint8_t I2C_ReadBlock(uint8_t deviceID, uint8_t offset,uint8_t *buf, uint8_t len)
{
	SiiI2cMsg_t			msgs[2];
	SiiPlatformStatus_t	platformStatus = PLATFORM_SUCCESS;
    int retryTimes = 3;
    msgs[0].addr = deviceID;
    msgs[0].cmdFlags = 0;
    msgs[0].len = 1;
    msgs[0].pBuf = &offset;

    msgs[1].addr = deviceID;
    msgs[1].cmdFlags = SII_MI2C_RD;
    msgs[1].len = len;
    msgs[1].pBuf = buf;

    do
    {
        platformStatus = SiiMasterI2cTransfer(0, msgs, 2);
        if (!platformStatus)
            break;
    }while(retryTimes--);
	platformStatus = SiiMasterI2cTransfer(0, msgs, 2);
    if(platformStatus)
    {
        memset(buf,0xFF,len);
    }
    return( platformStatus );
}


/*****************************************************************************/
/**
 * @brief Write some bytes to a register within an I2c device.
 *
 *****************************************************************************/
uint8_t I2C_WriteBlock(uint8_t deviceID, uint8_t offset, uint8_t *buf, uint8_t len)
{
	SiiI2cMsg_t			msgs[2];
	SiiPlatformStatus_t	platformStatus = PLATFORM_SUCCESS;

    msgs[0].addr = deviceID;
    msgs[0].cmdFlags = SII_MI2C_APPEND_NEXT_MSG;
    msgs[0].len = 1;
    msgs[0].pBuf = &offset;

    msgs[1].addr = 0;
    msgs[1].cmdFlags = 0;
    msgs[1].len = len;
    msgs[1].pBuf = (uint8_t*)buf;	// cast gets rid of const warning

	platformStatus = SiiMasterI2cTransfer(0, msgs, 2);
    return( platformStatus );
}

/*****************************************************************************/
/**
 * @brief Write a single byte to a register within an I2c device.
 *
 *****************************************************************************/
uint8_t I2C_WriteByte(uint8_t deviceID, uint8_t offset, uint8_t value)
{
    return I2C_WriteBlock(deviceID, offset, &value,1);

}

/*****************************************************************************/
/**
 * @brief Read a single byte from a register within an I2c device.
 *
 *****************************************************************************/
uint8_t I2C_ReadByte(uint8_t deviceID, uint8_t offset)
{
    uint8_t val;
    I2C_ReadBlock(deviceID, offset,&val,  1);
    return val;
}

/*****************************************************************************/
/**
 * @brief Linux implementation of CRA driver platform interface function
 * 		  SiiMasterI2cTransfer.
 *
 *****************************************************************************/
SiiPlatformStatus_t SiiMasterI2cTransfer(deviceBusTypes_t busIndex,
										 SiiI2cMsg_t *pMsgs, uint8_t msgNum)
{
	uint8_t				idx;
	uint8_t				msgCount = 0;
    struct i2c_msg		i2cMsg[MAX_I2C_MESSAGES];
	uint8_t				*pBuffer = NULL;
    SiiPlatformStatus_t	siiStatus = PLATFORM_FAIL;
    int					i2cStatus;


    do {
    	if (I2cAccessCheck() != HAL_RET_SUCCESS)
    	{
    		break;
    	}

    	if(busIndex != DEV_I2C_0)
    	{
        	SII_DEBUG_PRINT(MSG_ERR,
        			"SiiMasterI2cTransfer error: implementation supports" \
        			"only one I2C bus\n");
    		break;
    	}

    	if(msgNum > MAX_I2C_MESSAGES)
    	{
        	SII_DEBUG_PRINT(MSG_ERR,
        			"SiiMasterI2cTransfer error: implementation supports" \
        			"only %d message segments\n", MAX_I2C_MESSAGES);
    		break;
    	}

    	// Function parameter checks passed, assume at this point that the
    	// function will complete successfully.
    	siiStatus = PLATFORM_SUCCESS;

    	for(idx=0; idx < msgNum; idx++) {
    		i2cMsg[idx].addr	= pMsgs[idx].addr >> 1;
    		i2cMsg[idx].buf		= pMsgs[idx].pBuf;
    		i2cMsg[idx].len		= pMsgs[idx].len;
    		i2cMsg[idx].flags	= (pMsgs[idx].cmdFlags & SII_MI2C_RD) ? I2C_M_RD : 0;
            i2cMsg[idx].flags   |= (1<<1); // 1 for 50k
    		if(pMsgs[idx].cmdFlags & SII_MI2C_TEN) {
    			pMsgs[idx].cmdFlags |= I2C_M_TEN;
    		}
    		if(pMsgs[idx].cmdFlags & SII_MI2C_APPEND_NEXT_MSG) {
    			// Caller is asking that we append the buffer from the next
    			// message to this one.  We will do this IF there is a next
    			// message AND the direction of the two messages is the same
    			// AND we haven't already appended a message.

    			siiStatus = PLATFORM_INVALID_PARAMETER;
    			if(idx+1 < msgNum && pBuffer == NULL) {
    				if(!((pMsgs[idx].cmdFlags ^ pMsgs[idx+1].cmdFlags) & SII_MI2C_RD)) {

    					i2cMsg[idx].len += pMsgs[idx+1].len;

    				    pBuffer = kmalloc(i2cMsg[idx].len, GFP_KERNEL);
    				    if(pBuffer == NULL) {
    				    	siiStatus = PLATFORM_FAIL;
    				    	break;
    				    }

    				    i2cMsg[idx].buf = pBuffer;
    				    memmove(pBuffer, pMsgs[idx].pBuf, pMsgs[idx].len);
    				    memmove(&pBuffer[pMsgs[idx].len], pMsgs[idx+1].pBuf, pMsgs[idx+1].len);

    				    idx += 1;
    				    siiStatus = PLATFORM_SUCCESS;
    				}
    			}
    		}
    		msgCount++;
    	}

    	if(siiStatus != PLATFORM_SUCCESS) {
        	SII_DEBUG_PRINT(MSG_ERR,
        			"SiiMasterI2cTransfer failed, returning error: %d\n", siiStatus);

    		if(pBuffer != NULL) {
        		kfree(pBuffer);
        	}

    		return siiStatus;
    	}

    	i2cStatus = i2c_transfer(gMhlDevice.pI2cClient->adapter, i2cMsg, msgCount);

    	if(pBuffer != NULL) {
    		kfree(pBuffer);
    	}

    	if(i2cStatus < msgCount)
    	{
    		// All the messages were not transferred, some sort of error occurred.
    		// Try to return the most appropriate error code to the caller.
    		if (i2cStatus < 0)
    		{
    	    	SII_DEBUG_PRINT(MSG_ERR,
    	    			"SiiMasterI2cTransfer, i2c_transfer error: %d  " \
    	    			"deviceId: 0x%02x regOffset: 0x%02x\n",
    	    			i2cStatus, pMsgs->addr, *pMsgs->pBuf);
    			siiStatus = PLATFORM_FAIL;
    		}
    		else
    		{
    			// One or more messages transferred so error probably occurred on the
    			// first unsent message.  Look to see if the message was a read or write
    			// and set the appropriate return code.
    			if(pMsgs[i2cStatus].cmdFlags & SII_MI2C_RD)
    			{
    				siiStatus = PLATFORM_I2C_READ_FAIL;
    			}
    			else
    			{
    				siiStatus = PLATFORM_I2C_WRITE_FAIL;
    			}
    		}
    	}

	} while(0);

	return siiStatus;
}
