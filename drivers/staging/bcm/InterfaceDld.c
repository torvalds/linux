#include "headers.h"

int InterfaceFileDownload(PVOID arg, struct file *flp, unsigned int on_chip_loc)
{
	/* unsigned int reg = 0; */
	mm_segment_t oldfs = {0};
	int errno = 0, len = 0; /* ,is_config_file = 0 */
	loff_t pos = 0;
	struct bcm_interface_adapter *psIntfAdapter = arg;
	/* struct bcm_mini_adapter *Adapter = psIntfAdapter->psAdapter; */
	char *buff = kmalloc(MAX_TRANSFER_CTRL_BYTE_USB, GFP_KERNEL);

	if (!buff)
		return -ENOMEM;

	while (1) {
		oldfs = get_fs();
		set_fs(get_ds());
		len = vfs_read(flp, (void __force __user *)buff,
			MAX_TRANSFER_CTRL_BYTE_USB, &pos);
		set_fs(oldfs);
		if (len <= 0) {
			if (len < 0)
				errno = len;
			else
				errno = 0;
			break;
		}
		/* BCM_DEBUG_PRINT_BUFFER(Adapter,DBG_TYPE_INITEXIT, MP_INIT,
		 *			  DBG_LVL_ALL, buff,
		 *			  MAX_TRANSFER_CTRL_BYTE_USB);
		 */
		errno = InterfaceWRM(psIntfAdapter, on_chip_loc, buff, len);
		if (errno)
			break;
		on_chip_loc += MAX_TRANSFER_CTRL_BYTE_USB;
	}

	kfree(buff);
	return errno;
}

int InterfaceFileReadbackFromChip(PVOID arg, struct file *flp,
				unsigned int on_chip_loc)
{
	char *buff, *buff_readback;
	unsigned int reg = 0;
	mm_segment_t oldfs = {0};
	int errno = 0, len = 0, is_config_file = 0;
	loff_t pos = 0;
	static int fw_down;
	INT Status = STATUS_SUCCESS;
	struct bcm_interface_adapter *psIntfAdapter = arg;
	int bytes;

	buff = kzalloc(MAX_TRANSFER_CTRL_BYTE_USB, GFP_DMA);
	buff_readback = kzalloc(MAX_TRANSFER_CTRL_BYTE_USB , GFP_DMA);
	if (!buff || !buff_readback) {
		kfree(buff);
		kfree(buff_readback);

		return -ENOMEM;
	}

	is_config_file = (on_chip_loc == CONFIG_BEGIN_ADDR) ? 1 : 0;

	while (1) {
		oldfs = get_fs();
		set_fs(get_ds());
		len = vfs_read(flp, (void __force __user *)buff,
				MAX_TRANSFER_CTRL_BYTE_USB, &pos);
		set_fs(oldfs);
		fw_down++;

		if (len <= 0) {
			if (len < 0)
				errno = len;
			else
				errno = 0;
			break;
		}

		bytes = InterfaceRDM(psIntfAdapter, on_chip_loc,
					buff_readback, len);
		if (bytes < 0) {
			Status = bytes;
			goto exit;
		}
		reg++;
		if ((len-sizeof(unsigned int)) < 4) {
			if (memcmp(buff_readback, buff, len)) {
				Status = -EIO;
				goto exit;
			}
		} else {
			len -= 4;

			while (len) {
				if (*(unsigned int *)&buff_readback[len] !=
						 *(unsigned int *)&buff[len]) {
					Status = -EIO;
					goto exit;
				}
				len -= 4;
			}
		}
		on_chip_loc += MAX_TRANSFER_CTRL_BYTE_USB;
	} /* End of while(1) */

exit:
	kfree(buff);
	kfree(buff_readback);
	return Status;
}

static int bcm_download_config_file(struct bcm_mini_adapter *Adapter,
				struct bcm_firmware_info *psFwInfo)
{
	int retval = STATUS_SUCCESS;
	B_UINT32 value = 0;

	if (Adapter->pstargetparams == NULL) {
		Adapter->pstargetparams =
			kmalloc(sizeof(struct bcm_target_params), GFP_KERNEL);
		if (Adapter->pstargetparams == NULL)
			return -ENOMEM;
	}

	if (psFwInfo->u32FirmwareLength != sizeof(struct bcm_target_params))
		return -EIO;

	retval = copy_from_user(Adapter->pstargetparams,
			psFwInfo->pvMappedFirmwareAddress,
			psFwInfo->u32FirmwareLength);
	if (retval) {
		kfree(Adapter->pstargetparams);
		Adapter->pstargetparams = NULL;
		return -EFAULT;
	}

	/* Parse the structure and then Download the Firmware */
	beceem_parse_target_struct(Adapter);

	/* Initializing the NVM. */
	BcmInitNVM(Adapter);
	retval = InitLedSettings(Adapter);

	if (retval)
		return retval;

	if (Adapter->LEDInfo.led_thread_running &
			BCM_LED_THREAD_RUNNING_ACTIVELY) {
		Adapter->LEDInfo.bLedInitDone = false;
		Adapter->DriverState = DRIVER_INIT;
		wake_up(&Adapter->LEDInfo.notify_led_event);
	}

	if (Adapter->LEDInfo.led_thread_running &
			BCM_LED_THREAD_RUNNING_ACTIVELY) {
		Adapter->DriverState = FW_DOWNLOAD;
		wake_up(&Adapter->LEDInfo.notify_led_event);
	}

	/* Initialize the DDR Controller */
	retval = ddr_init(Adapter);
	if (retval)
		return retval;

	value = 0;
	wrmalt(Adapter, EEPROM_CAL_DATA_INTERNAL_LOC - 4,
				&value, sizeof(value));
	wrmalt(Adapter, EEPROM_CAL_DATA_INTERNAL_LOC - 8,
				&value, sizeof(value));

	if (Adapter->eNVMType == NVM_FLASH) {
		retval = PropagateCalParamsFromFlashToMemory(Adapter);
		if (retval)
			return retval;
	}

	retval = buffDnldVerify(Adapter, (PUCHAR)Adapter->pstargetparams,
			sizeof(struct bcm_target_params), CONFIG_BEGIN_ADDR);

	if (retval)
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_INITEXIT,
				MP_INIT, DBG_LVL_ALL,
				"configuration file not downloaded properly");
	else
		Adapter->bCfgDownloaded = TRUE;

	return retval;
}

int bcm_ioctl_fw_download(struct bcm_mini_adapter *Adapter,
			struct bcm_firmware_info *psFwInfo)
{
	int retval = STATUS_SUCCESS;
	PUCHAR buff = NULL;

	/* Config File is needed for the Driver to download the Config file and
	 * Firmware. Check for the Config file to be first to be sent from the
	 * Application
	 */
	atomic_set(&Adapter->uiMBupdate, false);
	if (!Adapter->bCfgDownloaded &&
		psFwInfo->u32StartingAddress != CONFIG_BEGIN_ADDR) {
		/* Can't Download Firmware. */
		return -EINVAL;
	}

	/* If Config File, Finish the DDR Settings and then Download CFG File */
	if (psFwInfo->u32StartingAddress == CONFIG_BEGIN_ADDR) {
		retval = bcm_download_config_file(Adapter, psFwInfo);
	} else {
		buff = kzalloc(psFwInfo->u32FirmwareLength, GFP_KERNEL);
		if (buff == NULL)
			return -ENOMEM;

		retval = copy_from_user(buff,
			psFwInfo->pvMappedFirmwareAddress,
			psFwInfo->u32FirmwareLength);
		if (retval != STATUS_SUCCESS) {
			retval = -EFAULT;
			goto error;
		}

		retval = buffDnldVerify(Adapter,
					buff,
					psFwInfo->u32FirmwareLength,
					psFwInfo->u32StartingAddress);

		if (retval != STATUS_SUCCESS)
			goto error;
	}

error:
	kfree(buff);
	return retval;
}

static INT buffDnld(struct bcm_mini_adapter *Adapter,
			PUCHAR mappedbuffer, UINT u32FirmwareLength,
			ULONG u32StartingAddress)
{
	unsigned int len = 0;
	int retval = STATUS_SUCCESS;

	len = u32FirmwareLength;

	while (u32FirmwareLength) {
		len = MIN_VAL(u32FirmwareLength, MAX_TRANSFER_CTRL_BYTE_USB);
		retval = wrm(Adapter, u32StartingAddress, mappedbuffer, len);

		if (retval)
			break;
		u32StartingAddress += len;
		u32FirmwareLength -= len;
		mappedbuffer += len;
	}
	return retval;
}

static INT buffRdbkVerify(struct bcm_mini_adapter *Adapter,
			PUCHAR mappedbuffer, UINT u32FirmwareLength,
			ULONG u32StartingAddress)
{
	UINT len = u32FirmwareLength;
	INT retval = STATUS_SUCCESS;
	PUCHAR readbackbuff = kzalloc(MAX_TRANSFER_CTRL_BYTE_USB, GFP_KERNEL);
	int bytes;

	if (NULL == readbackbuff)
		return -ENOMEM;

	while (u32FirmwareLength && !retval) {
		len = MIN_VAL(u32FirmwareLength, MAX_TRANSFER_CTRL_BYTE_USB);
		bytes = rdm(Adapter, u32StartingAddress, readbackbuff, len);

		if (bytes < 0) {
			retval = bytes;
			break;
		}

		if (memcmp(readbackbuff, mappedbuffer, len) != 0) {
			pr_err("%s() failed.  The firmware doesn't match what was written",
			       __func__);
			retval = -EIO;
		}

		u32StartingAddress += len;
		u32FirmwareLength -= len;
		mappedbuffer += len;

	} /* end of while (u32FirmwareLength && !retval) */
	kfree(readbackbuff);
	return retval;
}

INT buffDnldVerify(struct bcm_mini_adapter *Adapter,
			unsigned char *mappedbuffer,
			unsigned int u32FirmwareLength,
			unsigned long u32StartingAddress)
{
	INT status = STATUS_SUCCESS;

	status = buffDnld(Adapter, mappedbuffer,
			u32FirmwareLength, u32StartingAddress);
	if (status != STATUS_SUCCESS)
		goto error;

	status = buffRdbkVerify(Adapter, mappedbuffer,
			u32FirmwareLength, u32StartingAddress);
	if (status != STATUS_SUCCESS)
		goto error;
error:
	return status;
}
