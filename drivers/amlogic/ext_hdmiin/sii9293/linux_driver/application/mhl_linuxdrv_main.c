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
 * @file mhl_driver_main.c
 *
 * @brief Main entry point of the Linux driver for Silicon Image MHL transmitters.
 *
 * $Author: Tiger Qin
 * $Rev: $
 * $Date: Aug. 20, 2011
 *
 *****************************************************************************/

/***** #include statements ***************************************************/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include "mhl_linuxdrv.h"
#include "osal/include/osal.h"
#include "si_common.h"
#include <linux/of.h>
#include <linux/i2c-aml.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/platform_device.h>

#include <linux/amlogic/tvin/tvin_v4l2.h>
#include "../../../../../../../../../hardware/tvin/tvin_frontend.h"

#include "vdin_interface.h"
#include "sii5293_interface.h"
#include "../platform/hal/sii_hal_priv.h"




/***** local macro definitions ***********************************************/

/***** local variable declarations *******************************************/
/***** global variable declarations *******************************************/

MHL_DRIVER_CONTEXT_T gDriverContext = {0};
struct device_info *devinfo = NULL;

/* Module parameters that can be provided on insmod */
int debug_level = 0;
module_param(debug_level, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug_level, "debug level (default: 0)");

int output_format = 0;	/* Set device output format */
module_param(output_format, int, S_IRUGO);
MODULE_PARM_DESC(output_format, "Output format(default: 0-RGB)");

int input_dev_rap = 1;	/* RAP Input Device */
module_param(input_dev_rap, int, S_IRUGO);
MODULE_PARM_DESC(input_dev_rap, "RAP Input Device (default: 1)");

int input_dev_rcp = 1;	/* RCP Input Device */
module_param(input_dev_rcp, int, S_IRUGO);
MODULE_PARM_DESC(input_dev_rcp, "RCP Input Device (default: 1)");

int input_dev_ucp = 1;	/* UCP Input Device */
module_param(input_dev_ucp, int, S_IRUGO);
MODULE_PARM_DESC(input_dev_ucp, "UCP Input Device (default: 1)");



const char strVersion[] = "CP5293-v0.90.01";

static char BUILT_TIME[64];

int32_t StartMhlTxDevice(void);
int32_t StopMhlTxDevice(void);
#ifdef HDMIIN_FRAME_SKIP_MECHANISM

unsigned int flag_skip_status = SKIP_STATUS_NORMAL;
unsigned int flag_skip_enable = 0;

sii9293_frame_skip_t sii9293_skip;

#endif


#ifdef CONFIG_HAS_EARLYSUSPEND
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// early_suspend/late_resume
#include <linux/earlysuspend.h>
static void sii9293_early_suspend(struct early_suspend *h)
{
	int ret = 0;

	ret = StopMhlTxDevice();

	sii_set_standby(1);

	return ;
}

static void sii9293_late_resume(struct early_suspend *h)
{
	int ret = 0;

	sii_set_standby(0);

	ret = StartMhlTxDevice();

	return ;
}

static struct early_suspend sii9293_early_suspend_handler = {
    .level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 10,
    .suspend = sii9293_early_suspend,
    .resume = sii9293_late_resume,
    .param = &devinfo,
};

#endif


/*****************************************************************************
 *  @brief Start the MHL transmitter device
 *
 *  This function is called during driver startup to initialize control of the
 *  MHL transmitter device by the driver.
 *
 *  @return     0 if successful, negative error code otherwise
 *
 *****************************************************************************/
int32_t StartMhlTxDevice(void)
{
	halReturn_t		halStatus;
	SiiOsStatus_t	osalStatus;

    pr_info("sii5293, Starting %s\n", MHL_DEVICE_NAME);

    // Initialize the OS Abstraction Layer (OSAL) support.
    osalStatus = SiiOsInit(0);
    if (osalStatus != SII_OS_STATUS_SUCCESS)
    {
    	SII_DEBUG_PRINT(MSG_ERR,"Initialization of OSAL failed, error code: %d\n",osalStatus);
    	return -EIO;
    }

    halStatus = HalInit();
    if (halStatus != HAL_RET_SUCCESS)
    {
    	SII_DEBUG_PRINT(MSG_ERR,"Initialization of HAL failed, error code: %d\n",halStatus);
    	SiiOsTerm();
    	return -EIO;
    }

    halStatus = HalOpenI2cDevice(MHL_DEVICE_NAME, MHL_DRIVER_NAME);
    if (halStatus != HAL_RET_SUCCESS)
    {
    	SII_DEBUG_PRINT(MSG_ERR,"Opening of I2c device %s failed, error code: %d\n",
    			MHL_DEVICE_NAME, halStatus);
    	HalTerm();
    	SiiOsTerm();
    	return -EIO;
    }

	HalAcquireIsrLock();
    /* Initialize the 5293 & power up. */
    //SiiMhlTxInitialize(&DebugSW);
    SiiDrvDeviceInitialize();

    HalReleaseIsrLock();

    halStatus = HalInstallIrqHandler(SiiDrvDeviceManageInterrupts);
    if (halStatus != HAL_RET_SUCCESS)
    {
    	SII_DEBUG_PRINT(MSG_ERR,"Initialization of HAL interrupt support failed, error code: %d\n",
    			halStatus);
    	HalCloseI2cDevice();
    	HalTerm();
    	SiiOsTerm();
    	return -EIO;
    }
	printk("sii5293 [%s] end!\n", __FUNCTION__);

    return 0;
}



/**
 * @brief Stop the MHL transmitter device
 * This function shuts down control of the transmitter device so that
 * the driver can exit
 * 
 * 
 * @return 0 if successful, negative error code otherwise
 * 
 * @param void
 * 
 */
int32_t StopMhlTxDevice(void)
{
	halReturn_t		halStatus;

	pr_info("sii5293, Stopping %s\n", MHL_DEVICE_NAME);

    HalAcquireIsrLock();
	HalRemoveIrqHandler();

    SiiDrvDeviceRelease();
    HalReleaseIsrLock();

	halStatus = HalCloseI2cDevice();
    if (halStatus != HAL_RET_SUCCESS)
    {
    	SII_DEBUG_PRINT(MSG_ERR,
    			"Closing of I2c device failed, error code: %d\n",halStatus);
    	return -EIO;
    }

	halStatus = HalTerm();
    if (halStatus != HAL_RET_SUCCESS)
    {
    	SII_DEBUG_PRINT(MSG_ERR,
    			"Termination of HAL failed, error code: %d\n",halStatus);
    	return -EIO;
    }

	SiiOsTerm();
	return 0;
}

/***** public functions ******************************************************/


#define MAX_EVENT_STRING_LEN 512


/* MHL device initialization and release */
int mhl_dev_add(struct device_info *dev_info)
{
	int retval = 0;

	if (NULL == dev_info) {
		pr_info("sii5293, Invalid devinfo pointer\n");
		retval = -EFAULT;
		goto failed;
	}
	dev_info->mhl = kzalloc(sizeof(*dev_info->mhl), GFP_KERNEL);
	if (NULL == dev_info->mhl) {
		pr_info("sii5293, Out of memory\n");
		retval = -ENOMEM;
		goto failed;
	}

	dev_info->mhl->devnum = MKDEV(MAJOR(dev_info->devnum), 2);

	dev_info->mhl->cdev = cdev_alloc();
	dev_info->mhl->cdev->owner = THIS_MODULE;
	retval = cdev_add(dev_info->mhl->cdev, dev_info->mhl->devnum, 1);
	if (retval) {
		goto failed;
	}

	return 0;

failed:
	return retval;
}

/*
 *  MHL Attributes
 */
static ssize_t get_connection_state(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int retval;
	if (SiiMhlRxCbusConnected()) {
		retval = scnprintf(buf, PAGE_SIZE, "%s", "connected");
	} else {
		retval = scnprintf(buf, PAGE_SIZE, "%s", "not connected");
	}
	return retval;
}


/*
 * Declare the sysfs entries for MHL Attributes.
 * These macros create instances of:
 *   dev_attr_connection_state
 */
static DEVICE_ATTR(connection_state, S_IRUGO, get_connection_state, NULL);

static struct attribute *mhl_attrs[] = {
	&dev_attr_connection_state.attr,
	NULL,	/* need to NULL terminate the list of attributes */
};

static struct attribute_group mhl_attr_group = {
	.attrs = mhl_attrs,
};


/*
 *  MHL Devcap Group Attributes
 */
static ssize_t get_mhl_devcap_local(struct device *dev,
				struct device_attribute *attr, char *buf)
{
    int retval = 0;

    if(HalAcquireIsrLock() != HAL_RET_SUCCESS)
    {
        retval = -ERESTARTSYS;
        goto done;
    }

    retval = scnprintf(buf, PAGE_SIZE, "0x%02X", SiiRegRead(REG_CBUS_DEVICE_CAP_0 + gDriverContext.devcap_local_offset) );

	HalReleaseIsrLock();

done:
    return retval;
}

static ssize_t set_mhl_devcap_local(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	ssize_t retval = count;
	unsigned long value = 0;
	int rv = 0;

	rv = strict_strtoul(buf, 0, &value);
	if (rv) {
		pr_info("sii5293, Invalid MHL Local Device Capability Value %s", buf);
		retval = rv;
		goto done;
	}

	if (0xFF < value) {
		pr_info("sii5293, Invalid MHL Local Device Capability Value %lu\n", value);
		retval = -EINVAL;
		goto done;
	}


    if(HalAcquireIsrLock() != HAL_RET_SUCCESS)
    {
        retval = -ERESTARTSYS;
        goto done;
    }
    SiiRegWrite(REG_CBUS_DEVICE_CAP_0+gDriverContext.devcap_local_offset, value);
    
	HalReleaseIsrLock();
done:
	return retval;
}

static ssize_t get_mhl_devcap_local_offset(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "0x%02X", gDriverContext.devcap_local_offset);
}

static ssize_t set_mhl_devcap_local_offset(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	ssize_t retval = count;
	int rv = 0;
	unsigned long offset = 0;

	rv = strict_strtoul(buf, 0, &offset);
	if (rv) {
		pr_info("sii5293, Invalid MHL Local Device Capability Offset %s", buf);
		retval = rv;
		goto done;
	}

	if (0x0F < offset) {
		pr_info("sii5293, Invalid MHL Local Device Capability Offset %lu\n", offset);
		retval = -EINVAL;
		goto done;
	}

	gDriverContext.devcap_local_offset = offset;

done:
	return retval;
}

static ssize_t get_mhl_devcap_remote(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int retval = 0;

	if (!SiiMhlRxCbusConnected()) {
		pr_info("sii5293, MHL Remote Device Capabilities not available in cbus not connected.\n");
		retval = -ENODEV;
		goto done;
	}

	retval = scnprintf(buf, PAGE_SIZE, "0x%02X", SiiCbusRemoteDcapGet(gDriverContext.devcap_remote_offset));

done:
	return retval;
}

static ssize_t get_mhl_devcap_remote_offset(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "0x%02X", gDriverContext.devcap_remote_offset);
}

static ssize_t set_mhl_devcap_remote_offset(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	ssize_t retval = count;
	int rv = 0;
	unsigned long offset = 0;

	rv = strict_strtoul(buf, 0, &offset);
	if (rv) {
		pr_info("sii5293, Invalid MHL Remote Device Capability Offset %s", buf);
		retval = rv;
		goto done;
	}

	if (0x0F < offset) {
		pr_info("sii5293, Invalid MHL Remote Device Capability Offset %lu\n", offset);
		retval = -EINVAL;
		goto done;
	}

	gDriverContext.devcap_remote_offset = offset;

done:
	return retval;
}


/*
 * Declare the sysfs entries forMHL Devcap Group Attributes.
 */
static struct device_attribute dev_attr_devcap_local =
	__ATTR(local, S_IRUGO | S_IWUGO,
		get_mhl_devcap_local,
		set_mhl_devcap_local);
static struct device_attribute dev_attr_devcap_local_offset =
	__ATTR(local_offset, S_IRUGO | S_IWUGO,
		get_mhl_devcap_local_offset,
		set_mhl_devcap_local_offset);
static struct device_attribute dev_attr_devcap_remote =
	__ATTR(remote, S_IRUGO, get_mhl_devcap_remote, NULL);
static struct device_attribute dev_attr_devcap_remote_offset =
	__ATTR(remote_offset, S_IRUGO | S_IWUGO,
		get_mhl_devcap_remote_offset,
		set_mhl_devcap_remote_offset);

static struct attribute *mhl_devcap_attrs[] = {
	&dev_attr_devcap_local.attr,
	&dev_attr_devcap_local_offset.attr,
	&dev_attr_devcap_remote.attr,
	&dev_attr_devcap_remote_offset.attr,
	NULL,	/* need to NULL terminate the list of attributes */
};

static struct attribute_group mhl_devcap_attr_group = {
	.name = __stringify(devcap),
	.attrs = mhl_devcap_attrs,
};

/*
 * MHL RAP Group Attributes
 */
static ssize_t get_mhl_rap_in(struct device *dev,
				struct device_attribute *attr, char *buf)
{
    return scnprintf(buf,PAGE_SIZE,"0x%02X",gDriverContext.rap_in_keycode);
}

static ssize_t set_mhl_rap_in_status(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned long param;
    ssize_t status = count;

	if (!SiiMhlRxCbusConnected()) {
		pr_info("sii5293, Command not available in cbus not connected.\n");
		return -ENODEV;
	}
    
    if(HalAcquireIsrLock() != HAL_RET_SUCCESS)
    {
        return -ERESTARTSYS;
    }
    param = simple_strtoul(buf, NULL, 0);
    switch(param)
    {
        case 0x00:
        case 0x03:
            SiiMhlRxSendRapk(param);
            break;
        default:
            DEBUG_PRINT( MSG_ERR, "Invalid parameter %s received\n", buf);
            status = -EINVAL;
    }
	HalReleaseIsrLock();
	return status;
}

static ssize_t get_mhl_rap_out(struct device *dev,
				struct device_attribute *attr, char *buf)
{
    return scnprintf(buf,PAGE_SIZE,"0x%02X",gDriverContext.rap_out_keycode);
}

static ssize_t set_mhl_rap_out(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned long param;
    ssize_t status = count;

	if (!SiiMhlRxCbusConnected()) {
		pr_info("sii5293, Command not available in cbus not connected.\n");
		return -ENODEV;
	}
    
    if(HalAcquireIsrLock() != HAL_RET_SUCCESS)
    {
        return -ERESTARTSYS;
    }
    param = simple_strtoul(buf, NULL, 0);
    switch(param)
    {
        case MHL_RAP_CMD_POLL:
        case MHL_RAP_CONTENT_ON:
        case MHL_RAP_CONTENT_OFF:
            SiiMhlRxSendRAPCmd(param);
            gDriverContext.rap_out_keycode = param;
            gDriverContext.rap_out_statecode = MHL_MSC_MSG_RAP_NO_ERROR;
            break;
        default:
            DEBUG_PRINT( MSG_ERR, "Invalid parameter %s received\n", buf);
            status = -EINVAL;
    }
	HalReleaseIsrLock();
	return status;
}

static ssize_t get_mhl_rap_out_status(struct device *dev,
				struct device_attribute *attr, char *buf)
{
    return scnprintf(buf,PAGE_SIZE,"0x%02X",gDriverContext.rap_out_statecode);
}

static ssize_t get_mhl_rap_input_dev(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return scnprintf(buf,PAGE_SIZE,"%d",input_dev_rap);
}

static ssize_t set_mhl_rap_input_dev(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int status;
    unsigned long param;

	/* Assume success */
    status = count;
    param = simple_strtoul(buf, NULL, 0);
    switch(param)
    {
        case 0:
        case 1:
            input_dev_rap = param;
            break;
        default:
            DEBUG_PRINT( MSG_ERR, "Invalid parameter %s received\n", buf);
            status = -EINVAL;
    }
	return status;
}

/*
 * Declare the sysfs entries for MHL RAP Group Attributes.
 */
static struct device_attribute dev_attr_rap_in =
	__ATTR(in, S_IRUGO, get_mhl_rap_in, NULL);
static struct device_attribute dev_attr_rap_in_status =
	__ATTR(in_status, S_IWUGO, NULL, set_mhl_rap_in_status);
static struct device_attribute dev_attr_rap_out =
	__ATTR(out, S_IRUGO | S_IWUGO,
		get_mhl_rap_out,
		set_mhl_rap_out);
static struct device_attribute dev_attr_rap_out_status =
	__ATTR(out_status, S_IRUGO, get_mhl_rap_out_status, NULL);
static struct device_attribute dev_attr_rap_input_dev =
	__ATTR(input_dev, S_IRUGO | S_IWUGO,
		get_mhl_rap_input_dev,
		set_mhl_rap_input_dev);

static struct attribute *mhl_rap_attrs[] = {
	&dev_attr_rap_in.attr,
	&dev_attr_rap_in_status.attr,
	&dev_attr_rap_out.attr,
	&dev_attr_rap_out_status.attr,
	&dev_attr_rap_input_dev.attr,
	NULL,	/* need to NULL terminate the list of attributes */
};

static struct attribute_group mhl_rap_attr_group = {
	.name = __stringify(rap),
	.attrs = mhl_rap_attrs,
};


/*
 * MHL RCP Group Attributes
 */
static ssize_t get_mhl_rcp_in(struct device *dev,
				struct device_attribute *attr, char *buf)
{
    return scnprintf(buf,PAGE_SIZE,"0x%02X",gDriverContext.rcp_in_keycode);
}

static ssize_t set_mhl_rcp_in_status(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned long param;
    ssize_t status = count;

	if (!SiiMhlRxCbusConnected()) {
		pr_info("sii5293, Command not available in cbus not connected.\n");
		return -ENODEV;
	}
    
    if(HalAcquireIsrLock() != HAL_RET_SUCCESS)
    {
        return -ERESTARTSYS;
    }
    param = simple_strtoul(buf, NULL, 0);
    switch(param)
    {
        case 0x00:
            SiiMhlRxSendRcpk(gDriverContext.rcp_in_keycode);
            break;
        case 0x01:
        case 0x02:
            SiiMhlRxSendRcpe(param);
            SiiMhlRxSendRcpk(gDriverContext.rcp_in_keycode);
            break;
        default:
            DEBUG_PRINT( MSG_ERR, "Invalid parameter %s received\n", buf);
            status = -EINVAL;
    }
	HalReleaseIsrLock();
	return status;
}

static ssize_t get_mhl_rcp_out(struct device *dev,
				struct device_attribute *attr, char *buf)
{
    return scnprintf(buf,PAGE_SIZE,"0x%02X",gDriverContext.rcp_out_keycode);
}

static ssize_t set_mhl_rcp_out(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned long param;
    ssize_t status = count;

	if (!SiiMhlRxCbusConnected()) {
		pr_info("sii5293, Command not available in cbus not connected.\n");
		return -ENODEV;
	}
    
    if(HalAcquireIsrLock() != HAL_RET_SUCCESS)
    {
        return -ERESTARTSYS;
    }
    param = simple_strtoul(buf, NULL, 0);
    if (param > 0xFF)
    {
        DEBUG_PRINT( MSG_ERR, "Invalid parameter %s received\n", buf);
        status = -EINVAL;
    }else
    {
        SiiMhlRxSendRCPCmd(param);
        gDriverContext.rcp_out_keycode = param;
        gDriverContext.rcp_out_statecode = MHL_MSC_MSG_RCP_NO_ERROR;
    }
	HalReleaseIsrLock();
	return status;
}

static ssize_t get_mhl_rcp_out_status(struct device *dev,
				struct device_attribute *attr, char *buf)
{
    return scnprintf(buf,PAGE_SIZE,"0x%02X",gDriverContext.rcp_out_statecode);
}

static ssize_t get_mhl_rcp_input_dev(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return scnprintf(buf,PAGE_SIZE,"%d",input_dev_rcp);
}

static ssize_t set_mhl_rcp_input_dev(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int status;
    unsigned long param;

	/* Assume success */
    status = count;
    param = simple_strtoul(buf, NULL, 0);
    switch(param)
    {
        case 0:
        case 1:
            input_dev_rcp = param;
            break;
        default:
            DEBUG_PRINT( MSG_ERR, "Invalid parameter %s received\n", buf);
            status = -EINVAL;
    }
	return status;
}

/*
 * Declare the sysfs entries for MHL RCP Group Attributes.
 */
static struct device_attribute dev_attr_rcp_in =
	__ATTR(in, S_IRUGO, get_mhl_rcp_in, NULL);
static struct device_attribute dev_attr_rcp_in_status =
	__ATTR(in_status, S_IWUGO, NULL, set_mhl_rcp_in_status);
static struct device_attribute dev_attr_rcp_out =
	__ATTR(out, S_IRUGO | S_IWUGO,
		get_mhl_rcp_out,
		set_mhl_rcp_out);
static struct device_attribute dev_attr_rcp_out_status =
	__ATTR(out_status, S_IRUGO, get_mhl_rcp_out_status, NULL);
static struct device_attribute dev_attr_rcp_input_dev =
	__ATTR(input_dev, S_IRUGO | S_IWUGO,
		get_mhl_rcp_input_dev,
		set_mhl_rcp_input_dev);

static struct attribute *mhl_rcp_attrs[] = {
	&dev_attr_rcp_in.attr,
	&dev_attr_rcp_in_status.attr,
	&dev_attr_rcp_out.attr,
	&dev_attr_rcp_out_status.attr,
	&dev_attr_rcp_input_dev.attr,
	NULL,	/* need to NULL terminate the list of attributes */
};

static struct attribute_group mhl_rcp_attr_group = {
	.name = __stringify(rcp),
	.attrs = mhl_rcp_attrs,
};


/*
 * MHL UCP Group Attributes
 */
static ssize_t get_mhl_ucp_in(struct device *dev,
				struct device_attribute *attr, char *buf)
{
    return scnprintf(buf,PAGE_SIZE,"0x%02X",gDriverContext.ucp_in_keycode);
}

static ssize_t set_mhl_ucp_in_status(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned long param;
    ssize_t status = count;

	if (!SiiMhlRxCbusConnected()) {
		pr_info("sii5293, Command not available in cbus not connected.\n");
		return -ENODEV;
	}
    
    if(HalAcquireIsrLock() != HAL_RET_SUCCESS)
    {
        return -ERESTARTSYS;
    }
    param = simple_strtoul(buf, NULL, 0);
    switch(param)
    {
        case 0x00:
            SiiMhlRxSendUcpk(gDriverContext.ucp_in_keycode);
            break;
        case 0x01:
            SiiMhlRxSendUcpe(param);
            SiiMhlRxSendUcpk(gDriverContext.ucp_in_keycode);
            break;
        default:
            DEBUG_PRINT( MSG_ERR, "Invalid parameter %s received\n", buf);
            status = -EINVAL;
    }
	HalReleaseIsrLock();
	return status;
}

static ssize_t get_mhl_ucp_out(struct device *dev,
				struct device_attribute *attr, char *buf)
{
    return scnprintf(buf,PAGE_SIZE,"0x%02X",gDriverContext.ucp_out_keycode);
}

static ssize_t set_mhl_ucp_out(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned long param;
    ssize_t status = count;

	if (!SiiMhlRxCbusConnected()) {
		pr_info("sii5293, Command not available in cbus not connected.\n");
		return -ENODEV;
	}
    
    if(HalAcquireIsrLock() != HAL_RET_SUCCESS)
    {
        return -ERESTARTSYS;
    }
    param = simple_strtoul(buf, NULL, 0);
    if (param > 0xFF)
    {
        DEBUG_PRINT( MSG_ERR, "Invalid parameter %s received\n", buf);
        status = -EINVAL;
    }else
    {
        SiiMhlRxSendUCPCmd(param);
        gDriverContext.ucp_out_keycode = param;
        gDriverContext.ucp_out_statecode = MHL_MSC_MSG_UCP_NO_ERROR;
    }
	HalReleaseIsrLock();
	return status;
}

static ssize_t get_mhl_ucp_out_status(struct device *dev,
				struct device_attribute *attr, char *buf)
{
    return scnprintf(buf,PAGE_SIZE,"0x%02X",gDriverContext.ucp_out_statecode);
}

static ssize_t get_mhl_ucp_input_dev(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return scnprintf(buf,PAGE_SIZE,"%d",input_dev_ucp);
}

static ssize_t set_mhl_ucp_input_dev(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int status;
    unsigned long param;

	/* Assume success */
    status = count;
    param = simple_strtoul(buf, NULL, 0);
    switch(param)
    {
        case 0:
        case 1:
            input_dev_ucp = param;
            break;
        default:
            DEBUG_PRINT( MSG_ERR, "Invalid parameter %s received\n", buf);
            status = -EINVAL;
    }
	return status;
}

/*
 * Declare the sysfs entries for MHL UCP Group Attributes.
 */
static struct device_attribute dev_attr_ucp_in =
	__ATTR(in, S_IRUGO, get_mhl_ucp_in, NULL);
static struct device_attribute dev_attr_ucp_in_status =
	__ATTR(in_status, S_IWUGO, NULL, set_mhl_ucp_in_status);
static struct device_attribute dev_attr_ucp_out =
	__ATTR(out, S_IRUGO | S_IWUGO,
		get_mhl_ucp_out,
		set_mhl_ucp_out);
static struct device_attribute dev_attr_ucp_out_status =
	__ATTR(out_status, S_IRUGO, get_mhl_ucp_out_status, NULL);
static struct device_attribute dev_attr_ucp_input_dev =
	__ATTR(input_dev, S_IRUGO | S_IWUGO,
		get_mhl_ucp_input_dev,
		set_mhl_ucp_input_dev);

static struct attribute *mhl_ucp_attrs[] = {
	&dev_attr_ucp_in.attr,
	&dev_attr_ucp_in_status.attr,
	&dev_attr_ucp_out.attr,
	&dev_attr_ucp_out_status.attr,
	&dev_attr_ucp_input_dev.attr,
	NULL,	/* need to NULL terminate the list of attributes */
};

static struct attribute_group mhl_ucp_attr_group = {
	.name = __stringify(ucp),
	.attrs = mhl_ucp_attrs,
};


int mhl_dev_init(struct device_info *dev_info)
{
	int retval = 0;

	if ((NULL == dev_info) || (NULL == dev_info->mhl)) {
		pr_info("sii5293, Invalid devinfo pointer\n");
		retval = -EFAULT;
		goto failed;
	}

    dev_info->dev_class->dev_attrs = NULL;
	dev_info->mhl->device = device_create(dev_info->dev_class, dev_info->device,
				dev_info->mhl->devnum, NULL, MHL_DEVNAME);
	if (IS_ERR(dev_info->mhl->device)) {
		retval = PTR_ERR(dev_info->mhl->device);
		goto failed;
	}

	retval = sysfs_create_group(&dev_info->mhl->device->kobj, &mhl_attr_group);
	if (retval < 0) {
		pr_info("sii5293, failed to create MHL attribute group - continuing without\n");
	}

	retval = sysfs_create_group(&dev_info->mhl->device->kobj, &mhl_devcap_attr_group);
	if (retval < 0) {
		pr_info("sii5293, failed to create MHL devcap attribute group - continuing without\n");
	}

	retval = sysfs_create_group(&dev_info->mhl->device->kobj, &mhl_rap_attr_group);
	if (retval < 0) {
		pr_info("sii5293, failed to create MHL rap attribute group - continuing without\n");
	}

	retval = sysfs_create_group(&dev_info->mhl->device->kobj, &mhl_rcp_attr_group);
	if (retval < 0) {
		pr_info("sii5293, failed to create MHL rcp attribute group - continuing without\n");
	}

	retval = sysfs_create_group(&dev_info->mhl->device->kobj, &mhl_ucp_attr_group);
	if (retval < 0) {
		pr_info("sii5293, failed to create MHL ucp attribute group - continuing without\n");
	}

	return 0;

failed:
	return retval;
}

void mhl_dev_exit(struct device_info *dev_info)
{
	if ((NULL == dev_info) || (NULL == dev_info->mhl)) {
		pr_info("sii5293, Invalid devinfo pointer\n");
		return;
	}

	sysfs_remove_group(&dev_info->mhl->device->kobj, &mhl_attr_group);
	sysfs_remove_group(&dev_info->mhl->device->kobj, &mhl_devcap_attr_group);
	sysfs_remove_group(&dev_info->mhl->device->kobj, &mhl_rap_attr_group);
	sysfs_remove_group(&dev_info->mhl->device->kobj, &mhl_rcp_attr_group);
	sysfs_remove_group(&dev_info->mhl->device->kobj, &mhl_ucp_attr_group);

	device_unregister(dev_info->mhl->device);
	device_destroy(dev_info->dev_class, dev_info->mhl->devnum);
}


/*
 * Sii5293 Attributes
 */
static ssize_t get_chip_version(struct device *dev,
				struct device_attribute *attr, char *buf)
{
    if (gDriverContext.chip_revision == 0xFF)
        return scnprintf(buf, PAGE_SIZE, "%s", "");
    else
        return scnprintf(buf, PAGE_SIZE, "%d", gDriverContext.chip_revision);
}

static ssize_t get_input_video_mode(struct device *dev,
				struct device_attribute *attr, char *buf)
{
    uint8_t vic4x3, vic16x9;
    if (0 == gDriverContext.input_video_mode)
    {
        return scnprintf(buf, PAGE_SIZE, "not stable");
    }
    else if (SI_VIDEO_MODE_NON_STD == gDriverContext.input_video_mode)
    {
        return scnprintf(buf, PAGE_SIZE, "out of range");
    }
    else if (SI_VIDEO_MODE_PC_OTHER == gDriverContext.input_video_mode)
    {
        return scnprintf(buf, PAGE_SIZE, "pc resolution");
    }
    else
    {
        gDriverContext.input_video_mode &= 0x7f;
        if (gDriverContext.input_video_mode >= NMB_OF_CEA861_VIDEO_MODES)
        {
            return scnprintf(buf, PAGE_SIZE, "HDMI VIC %d", VideoModeTable[gDriverContext.input_video_mode].HdmiVic);
        }
        else
        {
            vic4x3 = VideoModeTable[gDriverContext.input_video_mode].Vic4x3;
            vic16x9 = VideoModeTable[gDriverContext.input_video_mode].Vic16x9;
            if (vic4x3 && vic16x9)
                return scnprintf(buf, PAGE_SIZE, "CEA VIC %d %d", vic4x3, vic16x9);
            else if (vic4x3)
                return scnprintf(buf, PAGE_SIZE, "CEA VIC %d", vic4x3);
            else if (vic16x9)
                return scnprintf(buf, PAGE_SIZE, "CEA VIC %d", vic16x9);
            else
                return scnprintf(buf, PAGE_SIZE, "%s", "");
        }
    }
}

static ssize_t get_device_connection_state(struct device *dev,
					struct device_attribute *attr, char *buf)
{
    ssize_t status = 0;
    switch(gDriverContext.connection_state)
    {
        case MHL_CONN:
            status = scnprintf(buf, PAGE_SIZE, "%s", "mhl source connected");
            break;
        case HDMI_CONN:
            status = scnprintf(buf, PAGE_SIZE, "%s", "hdmi source connected");
            break;
        case NO_CONN:
            status = scnprintf(buf, PAGE_SIZE, "%s", "no source connected");
            break;
        default:
            status = scnprintf(buf, PAGE_SIZE, "%s", "");
            break;
    }
    return status;
}

static ssize_t get_debug_level(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d", debug_level);
}

static ssize_t set_debug_level(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	ssize_t retval = count;
	int rv = 0;
	long new_debug_level = 0;

	rv = strict_strtol(buf, 0, &new_debug_level);
	if (rv) {
		pr_info("sii5293, Invalid Debug Level input: %s", buf);
		retval = rv;
		goto done;
	}

	if (new_debug_level < -1 || new_debug_level > 2) {
		printk("Invalid Debug Level input: %d\n", (int)new_debug_level);
		retval = -EINVAL;
		goto done;
	}

	debug_level = (int)new_debug_level;

done:
	return retval;
}


/*
 * Declare the sysfs entries for Sii5293 Attributes.
 * These macros create instances of:
 *   dev_attr_chip_version
 *   dev_attr_input_video_mode
 *   dev_attr_device_connection_state
 *   dev_attr_debug_level
 */
static DEVICE_ATTR(chip_version, S_IRUGO, get_chip_version, NULL);
static DEVICE_ATTR(input_video_mode, S_IRUGO, get_input_video_mode, NULL);
static DEVICE_ATTR(device_connection_state, S_IRUGO, get_device_connection_state, NULL);
static DEVICE_ATTR(debug_level, S_IRUGO | S_IWUGO, get_debug_level, set_debug_level);

static struct attribute *sii5293_attrs[] = {
    &dev_attr_chip_version.attr,
    &dev_attr_input_video_mode.attr,
    &dev_attr_device_connection_state.attr,
    &dev_attr_debug_level.attr,
    NULL,	/* need to NULL terminate the list of attributes */
};

static struct attribute_group sii5293_attr_group = {
	.attrs = sii5293_attrs,
};

#ifdef DEBUG

static ssize_t get_pwr5v_state(struct device *dev,
				struct device_attribute *attr, char *buf)
{
    if (gDriverContext.pwr5v_state)
        return scnprintf(buf, PAGE_SIZE, "%s", "in");
    else
        return scnprintf(buf, PAGE_SIZE, "%s", "out");
}

static ssize_t get_mhl_cable_state(struct device *dev,
				struct device_attribute *attr, char *buf)
{
    if (gDriverContext.mhl_cable_state)
        return scnprintf(buf, PAGE_SIZE, "%s", "in");
    else
        return scnprintf(buf, PAGE_SIZE, "%s", "out");
}

static ssize_t get_rx_term_state(struct device *dev,
					struct device_attribute *attr, char *buf)
{
    int		status = 0;

    if(HalAcquireIsrLock() != HAL_RET_SUCCESS)
    {
        return -ERESTARTSYS;
    }

    status = scnprintf(buf, PAGE_SIZE, "%d", SiiRegRead(REG_RX_CTRL5)&0x03 );

    HalReleaseIsrLock();
    return status;
}

static ssize_t set_rx_term_state(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
    int status = 0;
    long new_term = 0;

    status = strict_strtol(buf, 0, &new_term);
    if (status) {
        pr_info("sii5293, Invalid Debug Level input: %s", buf);
        return status;
    }

    if (new_term < 0 || new_term > 3) {
        pr_info("sii5293, Invalid Debug Level input: %d", (int)new_term);
        return -EINVAL;
    }

    if(HalAcquireIsrLock() != HAL_RET_SUCCESS)
    {
        return -ERESTARTSYS;
    }

    SiiRegModify(REG_RX_CTRL5, 0x03, new_term);

    HalReleaseIsrLock();
    return count;
}

static ssize_t get_hdcp_state(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	int		status = 0;

	if(HalAcquireIsrLock() != HAL_RET_SUCCESS)
	{
		return -ERESTARTSYS;
	}

    status = scnprintf(buf, PAGE_SIZE, "%s", SiiRegRead(RX_A__HDCP_STAT)&RX_M__HDCP_STAT__AUTHENTICATED ? "authenticated":"not authenticated" );

	HalReleaseIsrLock();
	return status;
}

static ssize_t get_hpd_state(struct device *dev,
					struct device_attribute *attr, char *buf)
{
    int		status = 0;
    uint8_t regVal;

    if(HalAcquireIsrLock() != HAL_RET_SUCCESS)
    {
        return -ERESTARTSYS;
    }

    regVal = SiiRegRead(REG_HP_CTRL)&0x03;

    HalReleaseIsrLock();

    if ( regVal == 2)
    {
        if (SiiMhlRxCbusConnected())
        {
            status = scnprintf(buf, PAGE_SIZE, "%s", "high");
        }else
        {
            status = scnprintf(buf, PAGE_SIZE, "%s", "low");
        }
    }
    else if (regVal == 0)
    {
        status = scnprintf(buf, PAGE_SIZE, "%s", "low");
    }
    else if (regVal == 1)
    {
        status = scnprintf(buf, PAGE_SIZE, "%s", "high");
    }
    else
    {
        status = scnprintf(buf, PAGE_SIZE, "%s", "");
    }

    return status;
}

static ssize_t set_hpd_state(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
    int status = 0;
    long new_hpd = 0;

    status = strict_strtol(buf, 0, &new_hpd);
    if (status) {
        pr_info("sii5293, Invalid Debug Level input: %s", buf);
        return status;
    }

    if (new_hpd < 0 || new_hpd > 2) {
        pr_info("sii5293, Invalid Debug Level input: %d", (int)new_hpd);
        return -EINVAL;
    }

    if(HalAcquireIsrLock() != HAL_RET_SUCCESS)
    {
        return -ERESTARTSYS;
    }

    SiiRegModify(REG_HP_CTRL, 0x03, new_hpd);

    HalReleaseIsrLock();
    return count;
}


#define MAX_DEBUG_TRANSFER_SIZE 16
static ssize_t get_debug_register(struct device *dev,
					struct device_attribute *attr, char *buf)
{
    uint8_t					data[MAX_DEBUG_TRANSFER_SIZE];
    uint8_t					idx, j;
    int						status = -EINVAL;

    DEBUG_PRINT(MSG_DBG, "called\n");

    if (gDriverContext.debug_i2c_address == 0)
        gDriverContext.debug_i2c_address = DEV_PAGE_PP_0;

    if(HalAcquireIsrLock() != HAL_RET_SUCCESS)
    {
        return -ERESTARTSYS;
    }

    status = scnprintf(buf, PAGE_SIZE, "address:0x%02X offset:0x%02X " \
					   "length:0x%02X data:",
						gDriverContext.debug_i2c_address,
						gDriverContext.debug_i2c_offset,
						gDriverContext.debug_i2c_xfer_length);
    if (gDriverContext.debug_i2c_xfer_length == 0)
    {
        for (j=0;j<16;j++)
        {
            if (0 != CraReadBlockI2c( DEV_I2C_0, gDriverContext.debug_i2c_address, 16*j, data, 16))
                    return -EINVAL;
            status += scnprintf(&buf[status], PAGE_SIZE, "\n");
            for (idx = 0; idx < 16; idx++)
            {
                status += scnprintf(&buf[status], PAGE_SIZE, "0x%02X ",
                data[idx]);
            }
        }
    }else
    {
        if (0 != CraReadBlockI2c( DEV_I2C_0, gDriverContext.debug_i2c_address, gDriverContext.debug_i2c_offset, data, gDriverContext.debug_i2c_xfer_length))
                return -EINVAL;
        for (idx = 0; idx < gDriverContext.debug_i2c_xfer_length; idx++) {
            status += scnprintf(&buf[status], PAGE_SIZE, "0x%02X ",
            data[idx]);
	    }
    }

    HalReleaseIsrLock();

    return status;
}

static ssize_t set_debug_register(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	static char *white_space = "' ', '\t'";
	unsigned long	address = 0x100;		/* initialize with invalid values */
	unsigned long	offset = 0x100;
	unsigned long	length = 0x100;
	unsigned long	value;
	uint8_t				data[MAX_DEBUG_TRANSFER_SIZE];
	uint8_t				idx;
	char			*str;
	char			*endptr;
	int				status = -EINVAL;

	DEBUG_PRINT(MSG_DBG, "received string: ""%s""\n", buf);

	/*
	 * Parse the input string and extract the scratch pad register selection
	 * parameters
	 */
	str = strstr(buf, "address=");
	if (str != NULL) {
		address = simple_strtoul(str + 8, NULL, 0);
		if (address > 0xFF) {
			DEBUG_PRINT(MSG_ERR, "Invalid page address: 0x%02lX" \
					"specified\n", address);
			goto err_exit;
		}
	} else {
		DEBUG_PRINT(MSG_ERR, "Invalid string format, can't "\
				"find ""address"" parameter\n");
		goto err_exit;
	}

	str = strstr(buf, "offset=");
	if (str != NULL) {
		offset = simple_strtoul(str + 7, NULL, 0);
		if (offset > 0xFF) {
			DEBUG_PRINT(MSG_ERR, "Invalid page offset: 0x%02lX" \
					"specified\n", offset);
			goto err_exit;
		}
	} else {
		DEBUG_PRINT(MSG_ERR, "Invalid string format, can't "\
				"find ""offset"" value\n");
		goto err_exit;
	}

	str = strstr(buf, "length=");
	if (str != NULL) {
		length = simple_strtoul(str + 7, NULL, 0);
		if (length > MAX_DEBUG_TRANSFER_SIZE) {
			DEBUG_PRINT(MSG_ERR, "Transfer size 0x%02lX is too "\
					"large\n", length);
			goto err_exit;
		}
	} else {
		DEBUG_PRINT(MSG_ERR, "Invalid string format, can't "\
				"find ""length"" value\n");
		goto err_exit;
	}

	str = strstr(buf, "data=");
	if (str != NULL) {

		str += 5;
		endptr = str;
		for(idx = 0; idx < length; idx++) {
			endptr += strspn(endptr, white_space);
			str = endptr;
			if (*str == 0) {
				DEBUG_PRINT(MSG_ERR, "Too few data values provided\n");
				goto err_exit;
			}

			value = simple_strtoul(str, &endptr, 0);

			if (value > 0xFF) {
				DEBUG_PRINT(MSG_ERR, "Invalid register data "\
						"value detected\n");
				goto err_exit;
			}

			data[idx] = value;
		}
		

	} else {
		idx = 0;
	}

	if ((offset + length) > 0x100) {
		DEBUG_PRINT(MSG_ERR
			, "Invalid offset/length combination entered 0x%02X/0x%02X"
			, offset, length);
		goto err_exit;
	}

	gDriverContext.debug_i2c_address = address;
	gDriverContext.debug_i2c_offset = offset;
	gDriverContext.debug_i2c_xfer_length = length;

	if (idx == 0) {
		DEBUG_PRINT(MSG_ERR, "No data specified, storing address "\
				 "offset and length for subsequent debug read\n");
		goto err_exit;
	}

    if(HalAcquireIsrLock() != HAL_RET_SUCCESS)
    {
        return -ERESTARTSYS;
    }

    status =  CraWriteBlockI2c( DEV_I2C_0, address, offset, data, length);

	if (status == 0)
		status = count;

	HalReleaseIsrLock();

err_exit:
	return status;
}


static DEVICE_ATTR(hpd_state, S_IRUGO, get_hpd_state, set_hpd_state);
static DEVICE_ATTR(hdcp_state, S_IRUGO, get_hdcp_state, NULL);
static DEVICE_ATTR(pwr5v_state, S_IRUGO, get_pwr5v_state, NULL);
static DEVICE_ATTR(mhl_cable_state, S_IRUGO, get_mhl_cable_state, NULL);
static DEVICE_ATTR(rx_term_state, S_IRUGO, get_rx_term_state, set_rx_term_state);
static DEVICE_ATTR(register, S_IRUGO | S_IWUGO, get_debug_register, set_debug_register);

static struct attribute *sii5293_debug_attrs[] = {
    &dev_attr_pwr5v_state.attr,
    &dev_attr_mhl_cable_state.attr,
    &dev_attr_rx_term_state.attr,
    &dev_attr_hpd_state.attr,
    &dev_attr_hdcp_state.attr,
    &dev_attr_register.attr,
    NULL,	/* need to NULL terminate the list of attributes */
};

static struct attribute_group sii5293_debug_attr_group = {
	.name = __stringify(debug),
	.attrs = sii5293_debug_attrs,
};
#endif

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// according to the <<CEA-861-D>>
typedef enum
{
	CEA_480P60	= 2,
	CEA_720P60	= 4,
	CEA_1080I60	= 5,
	CEA_480I60	= 6,

	CEA_1080P60	= 16,
	CEA_576P50	= 17,
	CEA_720P50	= 19,
	CEA_1080I50	= 20,
	CEA_576I50	= 21,

	CEA_1080P50	= 31,

	CEA_MAX = 60
}SII5293_VIDEO_MODE;

static unsigned int vdin_state = 0;
sii5293_vdin sii5293_vdin_info;
sii9293_info_t sii9293_info;

void dump_input_video_info(void)
{
	unsigned char index = 0;

	index = gDriverContext.input_video_mode;

	if (0 == index)
    {
        printk("sii5293 input video not stable!\n");
    }
    else if (SI_VIDEO_MODE_NON_STD == index)
    {
        printk("sii5293 input video out of range!\n");
    }
    else if (SI_VIDEO_MODE_PC_OTHER == index)
    {
        printk("sii5293 input pc resolution!\n");
    }
    else
    {
    	index = gDriverContext.input_video_mode & 0x7f;
    	if( index >= NMB_OF_CEA861_VIDEO_MODES )
    		printk("sii5293 input video index = %d\n", index);
    	else
    	{
			int height,width,h_total,v_total;
			int hs_fp,hs_width,hs_bp;
			int vs_fp,vs_width,vs_bp;
			int clk,h_freq,v_freq,interlaced;

			height = sii_get_h_active();
			width = sii_get_v_active();

			h_total = sii_get_h_total();
			v_total = sii_get_v_total();

			hs_fp = sii_get_hs_frontporch();
			hs_width = sii_get_hs_width();
			hs_bp = sii_get_hs_backporch();

			vs_fp = sii_get_vs_frontporch();
			vs_width = sii_get_vs_width();
			vs_bp = sii_get_vs_backporch();
			clk = sii_get_pixel_clock();
			h_freq = sii_get_h_freq();
			v_freq = sii_get_v_freq();
			interlaced = sii_get_interlaced();

			printk("sii5293 hdmi-in video info:\n\n\
	height * width = %4d x %4d, ( %4d x %4d )\n\
	h sync = %4d, %4d, %4d\n\
	v sync = %4d, %4d, %4d\n\
	pixel_clk = %9d, h_freq = %5d, v_freq = %2d\n\
	interlaced = %d\n",
				height,width,h_total,v_total,
				hs_fp,hs_width,hs_bp,
				vs_fp,vs_width,vs_bp,
				clk,h_freq,v_freq,interlaced
				);
		}
	}
}

unsigned int sii5293_get_output_mode(void)
{
	unsigned int h_active,h_total,v_active,v_total;
	unsigned int mode = 0;

	if( (gDriverContext.input_video_mode == 0) || (gDriverContext.input_video_mode >= NMB_OF_CEA861_VIDEO_MODES) )
		return mode;

	switch( gDriverContext.input_video_mode & 0x7f )
	{
		case 11:		mode = CEA_1080P60;		break;
		case 24:		mode = CEA_1080P50;		break;
		case  3:		mode = CEA_1080I60;		break;
		case 14:		mode = CEA_1080I50;		break;
		case  2:		mode = CEA_720P60;		break;
		case 13:		mode = CEA_720P50;		break;
		case 12:		mode = CEA_576P50;		break;
		case  1:		mode = CEA_480P60;		break;
		case 15:		mode = CEA_576I50;		break;
		case  4:		mode = CEA_480I60;		break;
	}

	return mode;
}

static void sii5293_start_vdin_mode(unsigned int mode)
{
	unsigned int height = 0, width = 0, frame_rate = 0, field_flag = 0;

	printk("[%s], start with mode = %d\n", __FUNCTION__, mode);
	switch(mode)
	{
		case CEA_480I60:
			width = 720;	height = 240;	frame_rate = 60;	field_flag = 1;		
			break;
		
		case CEA_480P60:
			width = 720;	height = 480;	frame_rate = 60;	field_flag = 0;		
			break;
		
		case CEA_576I50:
			width = 720;	height = 288;	frame_rate = 50;	field_flag = 1;		
			break;
		
		case CEA_576P50:
			width = 720;	height = 576;	frame_rate = 50;	field_flag = 0;		
			break;
		
		case CEA_720P50:
			width = 1280;	height = 720;	frame_rate = 50;	field_flag = 0;		
			break;
		
		case CEA_720P60:
			width = 1280;	height = 720;	frame_rate = 60;	field_flag = 0;		
			break;
		
		case CEA_1080I60:
			width = 1920;	height = 1080;	frame_rate = 60;	field_flag = 1;		
			break;
		
		case CEA_1080P60:
			width = 1920;	height = 1080;	frame_rate = 60;	field_flag = 0;		
			break;
		
		case CEA_1080I50:
			width = 1920;	height = 1080;	frame_rate = 50;	field_flag = 1;		
			break;
		
		case CEA_1080P50:
			width = 1920;	height = 1080;	frame_rate = 50;	field_flag = 0;		
			break;
		
		default:
			printk("[%s], invalid video mode!\n",__FUNCTION__);
			return ;
	}

	sii5293_start_vdin(&sii5293_vdin_info,width,height,frame_rate,field_flag);

	return ;
}

static unsigned int sii_output_mode = 0xff;
void sii5293_output_mode_trigger(unsigned int flag)
{
	unsigned int mode = 0xff;

	sii9293_info.signal_status = flag;
	printk("[%s] set signal_status = %d\n", __FUNCTION__, sii9293_info.signal_status);

	if( (sii9293_info.user_cmd==0) || (sii9293_info.user_cmd==0x4) || (sii9293_info.user_cmd==0xff) )
		return ;

	if( (0==flag) && ((sii9293_info.user_cmd==1)||(sii9293_info.user_cmd==3)) )
	{
		printk("[%s], lost signal, stop vdin!\n", __FUNCTION__);
		sii_output_mode = 0xff;
		sii5293_stop_vdin(&sii5293_vdin_info);
		return ;
	}

	if( (1==flag) && ((sii9293_info.user_cmd==2)||(sii9293_info.user_cmd==3)) )
	{
		mode = sii5293_get_output_mode();
		if( mode != sii_output_mode )
		{
			printk("[%s], trigger new mode = %d, old mode = %d\n", __FUNCTION__, mode, sii_output_mode);
			if( mode < CEA_MAX )
			{
				sii5293_start_vdin_mode(mode);
				sii_output_mode = mode;
			}
		}
	}

	return ;
}

#ifdef HDMIIN_FRAME_SKIP_MECHANISM
static unsigned int cable_status_old = 1;
#endif

void sii9293_cable_status_notify(unsigned int cable_status)
{
	sii9293_info.cable_status = cable_status;
#ifdef HDMIIN_FRAME_SKIP_MECHANISM
	if( (0==cable_status_old) && (1==cable_status) )
		flag_skip_status = SKIP_STATUS_CABLE;
	cable_status_old = cable_status;
#endif

	return ;
}


static ssize_t user_enable_show(struct class *class, struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "sii9293 user eanble = %d\n", sii9293_info.user_cmd);
}

static ssize_t user_enable_store(struct class *class, struct class_attribute *attr,
									const char *buf, size_t count)
{
	int argn;
	char *p=NULL, *para=NULL, *argv[5] = {NULL,NULL,NULL,NULL,NULL};
	unsigned int mode = 0, enable=0, height = 0, width = 0, frame_rate = 0, field_flag = 0;
	char *vmode[10] = {"480i\n","480p\n","576i\n","576p\n","720p50\n","720p\n","1080i\n","1080p\n","1080i50\n","1080p50\n"};
	int i = 0;

	p = kstrdup(buf, GFP_KERNEL);
	for( argn=0; argn<4; argn++ )
	{
		para = strsep(&p, " ");
		if( para == NULL )
			break;
		argv[argn] = para;
	}

//	printk("argn = %d, \"%s\", \"%s\", \"%s\", \"%s\", \"%s\"\n", argn, argv[0], argv[1], argv[2], argv[3], argv[4] );
	if( !strcmp(argv[0], "0\n") ) // disable
		enable = 0;
	else if( !strcmp(argv[0], "1\n") ) // enable, driver will trigger to vdin-stop
		enable = 1;
	else if( !strcmp(argv[0], "2\n") ) // enable, driver will trigger to vdin-start
		enable = 2;
	else if( !strcmp(argv[0], "3\n") ) // enable, driver will trigger to vdin-start/vdin-stop
		enable = 3;
	else if( !strcmp(argv[0], "4\n") ) // enable, driver will not trigger to vdin-start/vdin-stop
		enable = 4;
	else
	{
		for( i=0; i<10; i++ )
		{
			if( !strcmp(argv[0], vmode[i]) )
			{
				mode = i;
				enable = 0xff;
			}
		}
	}

	sii9293_info.user_cmd = enable;

	if( (enable==1) && (argn!=5) && (argn!=1) )
	{
		printk("invalid parameters to enable cmd !\n");
		return count;
	}

	if( (enable==0) && (sii5293_vdin_info.vdin_started==1) )
	{
		sii5293_stop_vdin(&sii5293_vdin_info);
		printk("sii9293 disable dvin !\n");
	}
	else if( ( (enable==1)||(enable==2)||(enable==3)||(enable==4) ) && (sii5293_vdin_info.vdin_started==0) )
	{
		mode = sii5293_get_output_mode();
		sii5293_start_vdin_mode(mode);
		printk("sii9293 enable(0x%x) dvin !\n", enable);
	}
	else if( (enable==0xff) && (sii5293_vdin_info.vdin_started==0) )
	{
		
		switch(mode)
		{
			case 0: // 480i
				mode = CEA_480I60;		break;
			case 1: // 480p
				mode = CEA_480P60;		break;
			case 2: // 576i
				mode = CEA_576I50;		break;
			case 3: // 576p
				mode = CEA_576P50;		break;
			case 4: // 720p50
				mode = CEA_720P50;		break;
			case 5: // 720p60
			default:
				mode = CEA_720P60;		break;
			case 6: // 1080i60
				mode = CEA_1080I60;		break;
			case 7: // 1080p60
				mode = CEA_1080P60;		break;
			case 8: // 1080i50
				mode = CEA_1080I50;		break;
			case 9: // 1080p50
				mode = CEA_1080P50;		break;
		}

		sii5293_start_vdin_mode(mode);
		printk("sii9293 enable(0x%x) dvin !\n", enable);
	}

	return count;
}

static ssize_t debug_show(struct class *class, struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "r reg_addr / w reg_addr value / dump reg_start reg_end\n\
		reg_addr = page_addr|reg_offset\n");
}

static int sii9293_test_get_id(void)
{
	unsigned char high,low;

	high = SiiRegRead(REG_DEV_IDH_RX);
	low = SiiRegRead(REG_DEV_IDL_RX);

	return ( (high<<8) | low );
}
#define MSK__VID_H_RES_BIT8_12		0x1F
static int sii9293_test_get_h_total(void)
{
	unsigned char high,low;

	high = SiiRegRead(RX_A__H_RESH)&MSK__VID_H_RES_BIT8_12;
	low = SiiRegRead(RX_A__H_RESL);

	return ( (high<<8) | low );
}

static ssize_t debug_store(struct class *class, struct class_attribute *attr,
									const char *buf, size_t count)
{
	int argn;
	char *p=NULL, *para=NULL, *argv[4] = {NULL,NULL,NULL,NULL};
	unsigned int cmd=0, reg_start = 0, reg_end = 0, length = 0, value = 0xff;
	char i2c_buf[2] = {0,0};
	int ret = 0;

	p = kstrdup(buf, GFP_KERNEL);
	for( argn=0; argn<4; argn++ )
	{
		para = strsep(&p, " ");
		if( para == NULL )
			break;
		argv[argn] = para;
	}

//	printk("get para: %s %s %s %s!\n",argv[0],argv[1],argv[2],argv[3]);

	if( !strcmp(argv[0], "r") )
		cmd = 0;
	else if( !strcmp(argv[0], "dump") )
		cmd = 1;
	else if( !strcmp(argv[0], "w") )
		cmd = 2;
	else if( !strncmp(argv[0], "vinfo", strlen("vinfo")) )
		cmd = 3;
	else if( !strncmp(argv[0], "tt", strlen("tt")) )
	{
		cmd = 4;
	}
	else
	{
		printk("invalid cmd = %s\n", argv[0]);
		return count;
	}

	
	printk(" cmd = %d - \"%s\"\n", cmd, argv[0]);
	if( (argn<1) || ((cmd==0)&&argn!=2) || ((cmd==1)&&(argn!=3)) || ((cmd==2)&&(argn!=3)) )
	{
		printk("invalid command format!\n");
		kfree(p);
		return count;
	}

	if( cmd == 0 ) // read
	{
		reg_start = (unsigned int)simple_strtoul(argv[1],NULL,16);
		value = SiiRegRead(reg_start);
		printk("\nsii5293 read reg[0x%x] = 0x%x\n", reg_start, value);

	}
	else if( cmd == 1 ) // dump
	{
		unsigned int i = 0;

		reg_start 	= (unsigned int)simple_strtoul(argv[1],NULL,16);
		reg_end 	= (unsigned int)simple_strtoul(argv[2],NULL,16);
		printk("sii5293 dump reg, [ 0x%x, 0x%x] :\n", reg_start, reg_end);
		
		for( i=reg_start; i<=reg_end; i++ )
		{
			value = SiiRegRead(i);
			if( i%0x10 == 0 )
					printk("%.2X:", i);
			printk(" %.2X",value);
			if( (i+1)%0x10 == 0 )
				printk("\n");
		}
		printk("\n");
	}
	else if( cmd == 2 ) // write
	{
		reg_start 	= (unsigned int)simple_strtoul(argv[1],NULL,16);
		value 		= (unsigned int)simple_strtoul(argv[2],NULL,16); 

		SiiRegWrite(reg_start, value);
		printk("sii5293 write reg[0x%x] = 0x%x\n", reg_start, value);
		value = SiiRegRead(reg_start);
		printk("hdmirx i2c read back reg[0x%x] = 0x%x\n", reg_start, value);
	}
	else if( cmd == 3 ) // vinfo
	{
		printk("\nbegin dump hdmi-in video info:\n");
		dump_input_video_info();
	}
	else if( cmd == 4 ) // tt, for loop test of 9293 i2c
	{
		unsigned int type = 255, count = 0, i = 0, v1 = 0, v2 = 0;
		unsigned int err1 = 0, err2 = 0, sum = 0, sum_failed = 0;

		type = (unsigned int )simple_strtoul(argv[1], NULL, 10);
		count = (unsigned int)simple_strtoul(argv[2], NULL, 10);

		printk("9293 i2c stability test: type = %d, count = %d\n", type, count);

		if( type == 0 ) // 0x2/0x3 = 9392
		{
			unsigned int i = 0, v1 = 0, v2 = 0;
			unsigned int err1 = 0, err2 = 0, sum_failed = 0;
			for( i=0; i<count; i++ )
			{
				msleep(2);
				v1 = sii9293_test_get_id();
				msleep(2);
				v2 = sii9293_test_get_h_total();
				
				if( v1 != 0x9293 )
				{
					err1 ++;
					printk("sii2ctest, ID failed: [%d], [%d] = 0x%x\n", i, err1, v1);
				}
				if( v2 != 0x672 )
				{
					err2 ++;
					printk("sii2ctest, RES_H failed: [%d], [%d] = 0x%x\n", i, err2, v2);
				}

				printk("sii2ctest, [%d]: err1 = %d, v1 = 0x%x, err2 = %d v2 = 0x%x\n", i, err1, v1, err2, v2);
			}
		}
		else
			printk("sii2ctest invalid type!\n");
	}

	kfree(p);
	return count;
}

static ssize_t sii5293_input_mode_show(struct class *class, struct class_attribute *attr, char *buf)
{
	unsigned int mode = 0xff;
	char hdmi_mode_str[16], mode_str[16] ;
	unsigned char value;

	value = SiiRegRead(RX_A__AUDP_STAT)&RX_M__AUDP_STAT__HDMI_MODE_ENABLED;

	memset(hdmi_mode_str, 0x00, 16);
	memset(mode_str, 0x00, 8);

	strcpy(hdmi_mode_str,(value==0)?"DVI:":"HDMI:");

	mode = sii5293_get_output_mode();

	switch(mode)
	{
		case CEA_480I60:	strcpy(mode_str, "480i");		break;
		case CEA_480P60:	strcpy(mode_str, "480p");		break;
		case CEA_576I50:	strcpy(mode_str, "576i");		break;
		case CEA_576P50:	strcpy(mode_str, "576p");		break;
		case CEA_720P60:	strcpy(mode_str, "720p");		break;
		case CEA_720P50:	strcpy(mode_str, "720p50hz");	break;
		case CEA_1080I60:	strcpy(mode_str, "1080i");		break;
		case CEA_1080I50:	strcpy(mode_str, "1080i50hz");	break;
		case CEA_1080P60:	strcpy(mode_str, "1080p");		break;
		case CEA_1080P50:	strcpy(mode_str, "1080p50hz");	break;
		default:			strcpy(mode_str, "invalid");	break;
	}

	if( strcmp(mode_str, "invalid") != 0 )
		strcat(hdmi_mode_str, mode_str);
	else
		strcpy(hdmi_mode_str, mode_str);
	return sprintf(buf, "%s\n", hdmi_mode_str);
}

static void dump_dvin_pinmux(void)
{
	printk(" dvin pinmux config:\n\
				LCD:	%d%d%d%d%d%d%d%d\n\
				TCON:	%d%d%d%d%d%d%d%d%d%d\n\
				ENC:	%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d\n\
				UART:	%d%d%d%d\n\
				PWM:	%d%d%d%d%d%d\n\
				VGA:	%d%d\n\
				DVIN:	%d%d%d%d%d\n",
				READ_CBUS_REG(PERIPHS_PIN_MUX_0)&1 ,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_0)>>1)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_0)>>2)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_0)>>3)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_0)>>4)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_0)>>5)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_0)>>18)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_0)>>19)&1,

				(READ_CBUS_REG(PERIPHS_PIN_MUX_8)>>19)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_8)>>20)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_8)>>21)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_8)>>22)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_8)>>23)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_8)>>24)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_8)>>25)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_8)>>26)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_8)>>27)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_8)>>28)&1,

				READ_CBUS_REG(PERIPHS_PIN_MUX_7)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_7)>>1)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_7)>>2)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_7)>>3)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_7)>>4)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_7)>>5)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_7)>>6)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_7)>>7)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_7)>>8)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_7)>>9)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_7)>>10)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_7)>>11)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_7)>>12)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_7)>>13)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_7)>>14)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_7)>>15)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_7)>>16)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_7)>>17)&1,

				(READ_CBUS_REG(PERIPHS_PIN_MUX_6)>>20)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_6)>>21)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_6)>>22)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_6)>>23)&1,

				(READ_CBUS_REG(PERIPHS_PIN_MUX_3)>>24)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_3)>>25)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_3)>>26)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_7)>>26)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_7)>>27)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_7)>>28)&1,

				(READ_CBUS_REG(PERIPHS_PIN_MUX_0)>>20)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_0)>>21)&1,

				(READ_CBUS_REG(PERIPHS_PIN_MUX_0)>>6)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_0)>>7)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_0)>>8)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_0)>>9)&1,
				(READ_CBUS_REG(PERIPHS_PIN_MUX_0)>>10)&1
				);
}

static ssize_t pinmux_show(struct class *class, struct class_attribute *attr, char *buf)
{
	dump_dvin_pinmux();
	return sprintf(buf, "dump pinmux end !!! \n");
}

extern void enable_vdin_pinmux(void);
static ssize_t pinmux_store(struct class *class, struct class_attribute *attr,
									const char *buf, size_t count)
{
	unsigned char enable = 0;

	enable = (unsigned char)simple_strtoul(buf, NULL, 0);

	enable_vdin_pinmux();

	printk("enable vdin pinmux GPIODV_0 ~ GPIODV_29 !\n");

	return count;
}

static ssize_t sii9293_cable_status_show(struct class *class, struct class_attribute *attr, char *buf)
{
	sii9293_info.cable_status = sii_get_pwr5v_status();
	return sprintf(buf, "%d\n", sii9293_info.cable_status);
}

static ssize_t sii9293_signal_status_show(struct class *class, struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", sii9293_info.signal_status);
}

static ssize_t sii9293_audio_sr_show(struct class *class, struct class_attribute *attr, char *buf)
{
	int audio_sr;
	char *audio_sr_array[] =
	{
		"44.1 kHz",			// 0x0
		"Not indicated",	// 0x1
		"48 kHz",			// 0x2
		"32 kHz",			// 0x3
		"22.05 kHz",		// 0x4
		"reserved",			// 0x5
		"24 kHz",			// 0x6
		"reserved",			// 0x7
		"88.2 kHz",			// 0x8
		"768 kHz (192*4)",	// 0x9
		"96 kHz",			// 0xa
		"reserved",			// 0xb
		"176.4 kHz",		// 0xc
		"reserved",			// 0xd
		"192 kHz",			// 0xe
		"reserved"			// 0xf
	};

	audio_sr = sii_get_audio_sampling_freq()&0xf;

	return sprintf(buf, "%s\n", audio_sr_array[audio_sr]);
}

#ifdef HDMIIN_FRAME_SKIP_MECHANISM

static void sii9293_frame_skip_default(void)
{
	sii9293_skip.skip_num_normal = FRAME_SKIP_NUM_NORMAL;
	sii9293_skip.skip_num_standby = FRAME_SKIP_NUM_STANDBY;
	sii9293_skip.skip_num_cable = FRAME_SKIP_NUM_CABLE;

	return ;
}

static ssize_t sii9293_frame_skip_show(struct class *class, struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "normal=%d, standby=%d, cable=%d\n",
		sii9293_skip.skip_num_normal, sii9293_skip.skip_num_standby, sii9293_skip.skip_num_cable);
}

static ssize_t sii9293_frame_skip_store(struct class *class, struct class_attribute *attr,
									const char *buf, size_t count)

{
	int argn;
	char *p=NULL, *para=NULL, *argv[4] = {NULL,NULL,NULL,NULL};
	unsigned int skip_normal, skip_standby, skip_cable, skip_signal;
	int ret = 0;

	p = kstrdup(buf, GFP_KERNEL);
	for( argn=0; argn<3; argn++ )
	{
		para = strsep(&p, " ");
		if( para == NULL )
			break;
		argv[argn] = para;
	}

	if( argn != 3 )
	{
		printk("please input 3 skip num!\n");
		return count;
	}

	skip_normal 	= (unsigned int)simple_strtoul(argv[0],NULL,10);
	skip_standby 	= (unsigned int)simple_strtoul(argv[1],NULL,10);
	skip_cable 		= (unsigned int)simple_strtoul(argv[2],NULL,10);

	sii9293_skip.skip_num_normal 	= skip_normal;
	sii9293_skip.skip_num_standby 	= skip_standby;
	sii9293_skip.skip_num_cable 	= skip_cable;

	printk("reconfig skip num: normal=%d, standby=%d, cable=%d\n",
		sii9293_skip.skip_num_normal, sii9293_skip.skip_num_standby, sii9293_skip.skip_num_cable);

	return count;
}
#endif


static ssize_t sii9293_drv_init_flag_show(struct class *class, struct class_attribute *attr, char *buf)
{
 return sprintf(buf, "drv_init_flag = %d\n", gHalInitedFlag);
}


static CLASS_ATTR(enable, 				S_IRUGO | S_IWUGO,	user_enable_show,			user_enable_store);
static CLASS_ATTR(debug, 				S_IRUGO | S_IWUGO,	debug_show,					debug_store);
//static CLASS_ATTR(pinmux,				S_IRUGO | S_IWUGO,	pinmux_show,				pinmux_store);
static CLASS_ATTR(input_mode, 			S_IRUGO,			sii5293_input_mode_show,	NULL);
static CLASS_ATTR(cable_status, 		S_IRUGO,			sii9293_cable_status_show,	NULL);
static CLASS_ATTR(signal_status, 		S_IRUGO,			sii9293_signal_status_show,	NULL);
static CLASS_ATTR(audio_sample_rate, 	S_IRUGO,			sii9293_audio_sr_show,		NULL);
static CLASS_ATTR(drv_init_flag, 		S_IRUGO, 			sii9293_drv_init_flag_show, NULL);
#ifdef HDMIIN_FRAME_SKIP_MECHANISM
static CLASS_ATTR(skip,				S_IRUGO | S_IWUGO,	sii9293_frame_skip_show,	sii9293_frame_skip_store);
#endif

static int aml_sii5293_create_attrs(struct class *cls)
{
	int ret = 0;

	if( cls == NULL )
		return 1;

	ret = class_create_file(cls, &class_attr_enable);	
	ret |= class_create_file(cls, &class_attr_debug);
//	ret |= class_create_file(cls, &class_attr_pinmux);
	ret |= class_create_file(cls, &class_attr_input_mode);
	ret |= class_create_file(cls, &class_attr_cable_status);
	ret |= class_create_file(cls, &class_attr_signal_status);
	ret |= class_create_file(cls, &class_attr_audio_sample_rate);
	ret |= class_create_file(cls, &class_attr_drv_init_flag);
#ifdef HDMIIN_FRAME_SKIP_MECHANISM
	ret |= class_create_file(cls, &class_attr_skip);
#endif

	return ret;
}

static void aml_sii5293_remove_attrs(struct class *cls)
{
	if( cls == NULL )
		return ;

	class_remove_file(cls, &class_attr_enable);	
	class_remove_file(cls, &class_attr_debug);
//	class_remove_file(cls, &class_attr_pinmux);
	class_remove_file(cls, &class_attr_input_mode);
	class_remove_file(cls, &class_attr_cable_status);
	class_remove_file(cls, &class_attr_signal_status);
	class_remove_file(cls, &class_attr_audio_sample_rate);
	class_remove_file(cls, &class_attr_drv_init_flag);
#ifdef HDMIIN_FRAME_SKIP_MECHANISM
	class_remove_file(cls, &class_attr_skip);
#endif

	return ;
}
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii5293 platform-driver

#ifdef CONFIG_USE_OF
static int sii5293_get_of_data(struct device_node *pnode)
{
	struct device_node *sii5293_node = pnode;
//	struct i2c_board_info board_info;
//	struct i2c_adapter *adapter;
	unsigned int i2c_index;
	
	const char *str;
	int ret = 0;

// for i2c bus
	ret = of_property_read_string(sii5293_node, "i2c_bus", &str);
	if (ret)
	{
		printk("[%s]: faild to get i2c_bus str!\n", __FUNCTION__);
		return -1;
	}
	else
	{
		if (!strncmp(str, "i2c_bus_ao", 9))
			i2c_index = AML_I2C_MASTER_AO;
		else if (!strncmp(str, "i2c_bus_a", 9))
			i2c_index = AML_I2C_MASTER_A;
		else if (!strncmp(str, "i2c_bus_b", 9))
			i2c_index = AML_I2C_MASTER_B;
		else if (!strncmp(str, "i2c_bus_c", 9))
			i2c_index = AML_I2C_MASTER_C;
		else if (!strncmp(str, "i2c_bus_d", 9))
			i2c_index = AML_I2C_MASTER_D;
		else
			return -1; 
	}
	
	devinfo->config.i2c_bus_index = i2c_index;

// for gpio_reset
	ret = of_property_read_string(sii5293_node, "gpio_reset", &str);
	if (ret)
	{
		printk("[%s]: faild to get gpio_rst!\n", __FUNCTION__);
		return -2;
	}

	ret = amlogic_gpio_name_map_num(str);
	if (ret < 0)
	{
		printk("[%s]: faild to map gpio_rst !\n", __FUNCTION__);
	}
	else
		devinfo->config.gpio_reset = ret;

// for irq
	ret = of_property_read_string(sii5293_node,"gpio_intr",&str);
	if(ret)
	{
		printk("[%s]: failed to get INT!\n", __FUNCTION__);
		return -3;
	}

	ret = amlogic_gpio_name_map_num(str);
	if(ret < 0 )
	{
		printk("[%s]: failed to map gpio_intr!\n", __FUNCTION__);
	}
	else
		devinfo->config.gpio_intr = ret;

#if 0
	memset(&board_info, 0x0, sizeof(board_info));
	strncpy(board_info.type, HDMIRX_SII9233A_NAME, I2C_NAME_SIZE);
	adapter = i2c_get_adapter(i2c_index);
	board_info.addr = SII9233A_I2C_ADDR;
	board_info.platform_data = &hdmirx_info;

	hdmirx_info.i2c_client = i2c_new_device(adapter, &board_info);
	printk("[%s] new i2c device i2c_client = 0x%x\n",hdmirx_info.i2c_client);
#endif
	printk("sii5293 get i2c_idx = %d, gpio_reset = %d, gpio_irq = %d\n", 
		devinfo->config.i2c_bus_index, devinfo->config.gpio_reset, devinfo->config.gpio_intr);
	return 0;
}
#endif

static int sii5293_probe(struct platform_device *pdev)
{
	int ret = 0;

	devinfo->config.i2c_bus_index = 0xff;
	devinfo->config.gpio_reset = 0;
	devinfo->config.gpio_intr = 0;

#ifdef CONFIG_USE_OF
	sii5293_get_of_data(pdev->dev.of_node);
#endif

	memset((void*)&sii9293_info, 0x00, sizeof(sii9293_info_t));

	//amlogic_gpio_request(hdmirx_info.gpio_reset, HDMIRX_SII9233A_NAME);

	ret = sii5293_register_tvin_frontend(&(sii5293_vdin_info.tvin_frontend));
	if( ret < 0 )
	{
		printk("[%s] register tvin frontend failed !\n", __FUNCTION__);
	}

	return ret;
}

static int sii5293_remove(struct platform_device *pdev)
{
	return 0;	
}

#ifdef CONFIG_USE_OF
static const struct of_device_id sii5293_dt_match[] = {
	{
		.compatible			= "amlogic,sii9293",
	},
};
#endif

static struct platform_driver sii5293_driver = 
{
	.probe		= sii5293_probe,
	.remove 	= sii5293_remove,
	.driver 	= {
					.name 				= MHL_DEVICE_NAME,
					.owner				= THIS_MODULE,
#ifdef CONFIG_USE_OF
					.of_match_table 	= sii5293_dt_match, 
#endif
					}
};

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

/*
* modelue init interface 
*/
static int __init SiiMhlInit(void)
{
    int32_t	ret = -1;
	pr_info("sii5293, %s driver starting!\n", MHL_DRIVER_NAME);
    sprintf(BUILT_TIME,"Build: %s", __TIME__"-"__DATE__);
    pr_info("sii5293, Version: %s, %s \n",strVersion,BUILT_TIME);
    /* register chrdev */
    pr_info("sii5293, register_chrdev %s\n", DEVNAME);

    devinfo = kzalloc(sizeof(*devinfo), GFP_KERNEL);
    if (NULL == devinfo)
    {
        pr_info("sii5293, Out of memory!\n");
        return ret;
    }
    devinfo->mhl = NULL;

    ret = alloc_chrdev_region(&devinfo->devnum,
                        0, NUMBER_OF_DEVS,
                        DEVNAME);
    if (ret) {
    	pr_info("sii5293, register_chrdev %s failed, error code: %d\n",
    					MHL_DRIVER_NAME, ret);
        goto free_devinfo;
    }

    devinfo->cdev = cdev_alloc();
    devinfo->cdev->owner = THIS_MODULE;
    ret = cdev_add(devinfo->cdev, devinfo->devnum, MHL_DRIVER_MINOR_MAX);
    if (ret) {
    	pr_info("sii5293, cdev_add %s failed %d\n", MHL_DRIVER_NAME, ret);
        goto free_chrdev;
    }

    ret = mhl_dev_add(devinfo);
    if (ret) {
        goto free_cdev;
    }

    devinfo->dev_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(devinfo->dev_class)) {
    	pr_info("sii5293, class_create failed %d\n", ret);
        ret = PTR_ERR(devinfo->dev_class);
        goto free_mhl_cdev;
    }

    //devinfo->dev_class->dev_attrs = driver_attribs;

    devinfo->device  = device_create(devinfo->dev_class, NULL,
    									 devinfo->devnum,  NULL,
    									 "%s", DEVNAME);
    if (IS_ERR(devinfo->device)) {
    	pr_info("sii5293, class_device_create failed %s %d\n", DEVNAME, ret);
        ret = PTR_ERR(devinfo->device);
        goto free_class;
    }

    // add sii5293 platform-driver to get dt resources.
    ret = platform_driver_register(&sii5293_driver);
	if (ret)
	{
        pr_info("sii5293, failed to register sii5293 module\n");
        goto free_class;
    }


    ret = sysfs_create_group(&devinfo->device->kobj, &sii5293_attr_group);
    if (ret) {
        pr_info("sii5293, failed to create root attribute group - continuing without\n");
        goto free_dev;
    }

#ifdef DEBUG
    ret = sysfs_create_group(&devinfo->device->kobj, &sii5293_debug_attr_group);
    if (ret) {
        pr_info("sii5293, failed to create debug attribute group - continuing without\n");
        goto free_dev;
    }
#endif

	ret = aml_sii5293_create_attrs(devinfo->dev_class);
	if(ret)
	{
		pr_info("sii5293, [%s] failed(%d) to create aml sii5293 attrs!\n", __FUNCTION__, ret);
		goto free_dev;
	}


    ret = mhl_dev_init(devinfo);
    if (ret) {
        goto free_dev;
    }

#ifdef HDMIIN_FRAME_SKIP_MECHANISM
	sii9293_frame_skip_default();
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	register_early_suspend(&sii9293_early_suspend_handler);
#endif

    ret = StartMhlTxDevice();
    if(ret == 0) {
        printk(KERN_NOTICE"sii5293 mhldrv initialized successfully !\n");
    	return 0;
    } else {
    	// Transmitter startup failed so fail the driver load.
    	printk(KERN_NOTICE"sii5293 mhldrv initialized failed !\n");
    	mhl_dev_exit(devinfo);
    }

free_dev:
    if (devinfo->device)
    {
    	aml_sii5293_remove_attrs(devinfo->dev_class);
#ifdef DEBUG
        sysfs_remove_group(&devinfo->device->kobj, &sii5293_debug_attr_group);
#endif
        sysfs_remove_group(&devinfo->device->kobj, &sii5293_attr_group);
        device_unregister(devinfo->device);
        devinfo->device = NULL;
    }
    device_destroy(devinfo->dev_class, devinfo->devnum);

free_class:
	class_destroy(devinfo->dev_class);

free_mhl_cdev:
    cdev_del(devinfo->mhl->cdev);

free_cdev:
	cdev_del(devinfo->cdev);

free_chrdev:
	unregister_chrdev_region(devinfo->devnum, MHL_DRIVER_MINOR_MAX);

free_devinfo:
    if (devinfo)
    {
        if (devinfo->mhl)
            kfree(devinfo->mhl);
        kfree(devinfo);
    }
    devinfo = NULL;
    printk("sii5293 [%s] something wrong !\n", __FUNCTION__);
	return ret;
}
/*
* modelue remove interface 
*/
static void __exit SiiMhlExit(void)
{
	pr_info("sii5293, %s driver exiting!\n", MHL_DRIVER_NAME);
	StopMhlTxDevice();
    mhl_dev_exit(devinfo);
    if (devinfo->device)
    {
    	aml_sii5293_remove_attrs(devinfo->dev_class);
#ifdef DEBUG
        sysfs_remove_group(&devinfo->device->kobj, &sii5293_debug_attr_group);
#endif
        sysfs_remove_group(&devinfo->device->kobj, &sii5293_attr_group);
        device_unregister(devinfo->device);
        devinfo->device = NULL;
    }
	device_destroy(devinfo->dev_class, devinfo->devnum);
    class_destroy(devinfo->dev_class);
    cdev_del(devinfo->cdev);
    unregister_chrdev_region(devinfo->devnum, MHL_DRIVER_MINOR_MAX);
    if (devinfo)
    {
        if (devinfo->mhl)
            kfree(devinfo->mhl);
        kfree(devinfo);
    }
    devinfo = NULL;
	pr_info("sii5293, %s driver successfully exited!\n", MHL_DRIVER_NAME);
}

module_init(SiiMhlInit);
module_exit(SiiMhlExit);

int send_sii5293_uevent(struct device *device, const char *event_cat,
			const char *event_type, const char *event_data)
{
	int retval = 0;
	char event_string[MAX_EVENT_STRING_LEN];
	char *envp[] = {event_string, NULL};

	if ((NULL == event_cat) || (NULL == event_type)) 
	{
		pr_info("sii5293, Invalid parameters\n");
		retval = -EINVAL;
        return retval;
	}


	scnprintf(event_string, MAX_EVENT_STRING_LEN-1,
		"%s={\"event\":\"%s\",\"data\":%s}",
		event_cat, event_type, (NULL != event_data) ? event_data : "null");


	kobject_uevent_env(&device->kobj, KOBJ_CHANGE, envp);

	return retval;
}


void SiiConnectionStateNotify(bool_t connect)
{
    #define MAX_REPORT_DATA_STRING_SIZE 20
    SourceConnection_t new_state;
    char str[MAX_REPORT_DATA_STRING_SIZE];

    if (connect)
    {
        if (SiiMhlRxCbusConnected())
        {
            new_state = MHL_CONN;
            scnprintf(str,	MAX_REPORT_DATA_STRING_SIZE, "mhl source connected");
        }else
        {
            new_state = HDMI_CONN;
            scnprintf(str,	MAX_REPORT_DATA_STRING_SIZE, "hdmi source connected");
        }
    }else
    {
        new_state = NO_CONN;
        scnprintf(str,	MAX_REPORT_DATA_STRING_SIZE, "no source connected");
    }

    if (new_state != gDriverContext.connection_state)
    {
        gDriverContext.connection_state = new_state;
        sysfs_notify(&devinfo->device->kobj, NULL, "device_connection_state");
        send_sii5293_uevent(devinfo->device, DEVICE_EVENT, DEV_CONNECTION_CHANGE_EVENT, str);
    }
}


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Silicon Image <http://www.siliconimage.com>");
MODULE_DESCRIPTION("Silicon Image MHL/HDMI Receiver driver");
