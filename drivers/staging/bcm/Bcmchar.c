#include <linux/fs.h>

#include "headers.h"

static int bcm_handle_nvm_read_cmd(struct bcm_mini_adapter *ad,
				   PUCHAR read_data,
				   struct bcm_nvm_readwrite *nvm_rw)
{
	INT status = STATUS_FAILURE;

	down(&ad->NVMRdmWrmLock);

	if ((ad->IdleMode == TRUE) || (ad->bShutStatus == TRUE) ||
			(ad->bPreparingForLowPowerMode == TRUE)) {

		BCM_DEBUG_PRINT(ad,
			DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"Device is in Idle/Shutdown Mode\n");
		up(&ad->NVMRdmWrmLock);
		kfree(read_data);
		return -EACCES;
	}

	status = BeceemNVMRead(ad, (PUINT)read_data,
			       nvm_rw->uiOffset,
			       nvm_rw->uiNumBytes);
	up(&ad->NVMRdmWrmLock);

	if (status != STATUS_SUCCESS) {
		kfree(read_data);
		return status;
	}

	if (copy_to_user(nvm_rw->pBuffer, read_data, nvm_rw->uiNumBytes)) {
		kfree(read_data);
		return -EFAULT;
	}

	return STATUS_SUCCESS;
}

static int handle_flash2x_adapter(struct bcm_mini_adapter *ad,
				  PUCHAR read_data,
				  struct bcm_nvm_readwrite *nvm_rw)
{
	/*
	 * New Requirement:-
	 * DSD section updation will be allowed in two case:-
	 * 1.  if DSD sig is present in DSD header means dongle
	 * is ok and updation is fruitfull
	 * 2.  if point 1 failes then user buff should have
	 * DSD sig. this point ensures that if dongle is
	 * corrupted then user space program first modify
	 * the DSD header with valid DSD sig so that this
	 * as well as further write may be worthwhile.
	 *
	 * This restriction has been put assuming that
	 * if DSD sig is corrupted, DSD data won't be
	 * considered valid.
	 */
	INT status;
	ULONG dsd_magic_num_in_usr_buff = 0;

	status = BcmFlash2xCorruptSig(ad, ad->eActiveDSD);
	if (status == STATUS_SUCCESS)
		return STATUS_SUCCESS;

	if (((nvm_rw->uiOffset + nvm_rw->uiNumBytes) !=
			ad->uiNVMDSDSize) ||
			(nvm_rw->uiNumBytes < SIGNATURE_SIZE)) {

		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
				"DSD Sig is present neither in Flash nor User provided Input..");
		up(&ad->NVMRdmWrmLock);
		kfree(read_data);
		return status;
	}

	dsd_magic_num_in_usr_buff =
		ntohl(*(PUINT)(read_data + nvm_rw->uiNumBytes -
		      SIGNATURE_SIZE));
	if (dsd_magic_num_in_usr_buff != DSD_IMAGE_MAGIC_NUMBER) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
				"DSD Sig is present neither in Flash nor User provided Input..");
		up(&ad->NVMRdmWrmLock);
		kfree(read_data);
		return status;
	}

	return STATUS_SUCCESS;
}

/***************************************************************
* Function	  - bcm_char_open()
*
* Description - This is the "open" entry point for the character
*				driver.
*
* Parameters  - inode: Pointer to the Inode structure of char device
*				filp : File pointer of the char device
*
* Returns	  - Zero(Success)
****************************************************************/

static int bcm_char_open(struct inode *inode, struct file *filp)
{
	struct bcm_mini_adapter *ad = NULL;
	struct bcm_tarang_data *tarang = NULL;

	ad = GET_BCM_ADAPTER(gblpnetdev);
	tarang = kzalloc(sizeof(struct bcm_tarang_data), GFP_KERNEL);
	if (!tarang)
		return -ENOMEM;

	tarang->Adapter = ad;
	tarang->RxCntrlMsgBitMask = 0xFFFFFFFF & ~(1 << 0xB);

	down(&ad->RxAppControlQueuelock);
	tarang->next = ad->pTarangs;
	ad->pTarangs = tarang;
	up(&ad->RxAppControlQueuelock);

	/* Store the Adapter structure */
	filp->private_data = tarang;

	/* Start Queuing the control response Packets */
	atomic_inc(&ad->ApplicationRunning);

	nonseekable_open(inode, filp);
	return 0;
}

static int bcm_char_release(struct inode *inode, struct file *filp)
{
	struct bcm_tarang_data *tarang, *tmp, *ptmp;
	struct bcm_mini_adapter *ad = NULL;
	struct sk_buff *pkt, *npkt;

	tarang = (struct bcm_tarang_data *)filp->private_data;

	if (tarang == NULL)
		return 0;

	ad = tarang->Adapter;

	down(&ad->RxAppControlQueuelock);

	tmp = ad->pTarangs;
	for (ptmp = NULL; tmp; ptmp = tmp, tmp = tmp->next) {
		if (tmp == tarang)
			break;
	}

	if (tmp) {
		if (!ptmp)
			ad->pTarangs = tmp->next;
		else
			ptmp->next = tmp->next;
	} else {
		up(&ad->RxAppControlQueuelock);
		return 0;
	}

	pkt = tarang->RxAppControlHead;
	while (pkt) {
		npkt = pkt->next;
		kfree_skb(pkt);
		pkt = npkt;
	}

	up(&ad->RxAppControlQueuelock);

	/* Stop Queuing the control response Packets */
	atomic_dec(&ad->ApplicationRunning);

	kfree(tarang);

	/* remove this filp from the asynchronously notified filp's */
	filp->private_data = NULL;
	return 0;
}

static ssize_t bcm_char_read(struct file *filp,
			     char __user *buf,
			     size_t size,
			     loff_t *f_pos)
{
	struct bcm_tarang_data *tarang = filp->private_data;
	struct bcm_mini_adapter *ad = tarang->Adapter;
	struct sk_buff *packet = NULL;
	ssize_t pkt_len = 0;
	int wait_ret_val = 0;
	unsigned long ret = 0;

	wait_ret_val = wait_event_interruptible(
				ad->process_read_wait_queue,
				(tarang->RxAppControlHead ||
				ad->device_removed));

	if ((wait_ret_val == -ERESTARTSYS)) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
				"Exiting as i've been asked to exit!!!\n");
		return wait_ret_val;
	}

	if (ad->device_removed) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
				"Device Removed... Killing the Apps...\n");
		return -ENODEV;
	}

	if (false == ad->fw_download_done)
		return -EACCES;

	down(&ad->RxAppControlQueuelock);

	if (tarang->RxAppControlHead) {
		packet = tarang->RxAppControlHead;
		DEQUEUEPACKET(tarang->RxAppControlHead,
			      tarang->RxAppControlTail);
		tarang->AppCtrlQueueLen--;
	}

	up(&ad->RxAppControlQueuelock);

	if (packet) {
		pkt_len = packet->len;
		ret = copy_to_user(buf, packet->data,
				   min_t(size_t, pkt_len, size));
		if (ret) {
			dev_kfree_skb(packet);
			BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
					"Returning from copy to user failure\n");
			return -EFAULT;
		}
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
				"Read %zd Bytes From Adapter packet = %p by process %d!\n",
				pkt_len, packet, current->pid);
		dev_kfree_skb(packet);
	}

	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "<\n");
	return pkt_len;
}

static int bcm_char_ioctl_reg_read_private(void __user *argp,
					   struct bcm_mini_adapter *ad)
{
	struct bcm_rdm_buffer rdm_buff = {0};
	struct bcm_ioctl_buffer io_buff;
	PCHAR temp_buff;
	INT status = STATUS_FAILURE;
	UINT buff_len;
	u16 temp_value;
	int bytes;

	/* Copy Ioctl Buffer structure */
	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	if (io_buff.InputLength > sizeof(rdm_buff))
		return -EINVAL;

	if (copy_from_user(&rdm_buff, io_buff.InputBuffer,
		io_buff.InputLength))
		return -EFAULT;

	if (io_buff.OutputLength > USHRT_MAX ||
		io_buff.OutputLength == 0) {
		return -EINVAL;
	}

	buff_len = io_buff.OutputLength;
	temp_value = 4 - (buff_len % 4);
	buff_len += temp_value % 4;

	temp_buff = kmalloc(buff_len, GFP_KERNEL);
	if (!temp_buff)
		return -ENOMEM;

	bytes = rdmalt(ad, (UINT)rdm_buff.Register,
			(PUINT)temp_buff, buff_len);
	if (bytes > 0) {
		status = STATUS_SUCCESS;
		if (copy_to_user(io_buff.OutputBuffer, temp_buff, bytes)) {
			kfree(temp_buff);
			return -EFAULT;
		}
	} else {
		status = bytes;
	}

	kfree(temp_buff);
	return status;
}

static int bcm_char_ioctl_reg_write_private(void __user *argp,
					    struct bcm_mini_adapter *ad)
{
	struct bcm_wrm_buffer wrm_buff = {0};
	struct bcm_ioctl_buffer io_buff;
	UINT tmp = 0;
	INT status;

	/* Copy Ioctl Buffer structure */

	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	if (io_buff.InputLength > sizeof(wrm_buff))
		return -EINVAL;

	/* Get WrmBuffer structure */
	if (copy_from_user(&wrm_buff, io_buff.InputBuffer,
		io_buff.InputLength))
		return -EFAULT;

	tmp = wrm_buff.Register & EEPROM_REJECT_MASK;
	if (!((ad->pstargetparams->m_u32Customize) & VSG_MODE) &&
		((tmp == EEPROM_REJECT_REG_1) ||
			(tmp == EEPROM_REJECT_REG_2) ||
			(tmp == EEPROM_REJECT_REG_3) ||
			(tmp == EEPROM_REJECT_REG_4))) {

		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"EEPROM Access Denied, not in VSG Mode\n");
		return -EFAULT;
	}

	status = wrmalt(ad, (UINT)wrm_buff.Register,
			(PUINT)wrm_buff.Data, sizeof(ULONG));

	if (status == STATUS_SUCCESS) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG,
				DBG_LVL_ALL, "WRM Done\n");
	} else {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG,
				DBG_LVL_ALL, "WRM Failed\n");
		status = -EFAULT;
	}
	return status;
}

static int bcm_char_ioctl_eeprom_reg_read(void __user *argp,
					  struct bcm_mini_adapter *ad)
{
	struct bcm_rdm_buffer rdm_buff = {0};
	struct bcm_ioctl_buffer io_buff;
	PCHAR temp_buff = NULL;
	UINT tmp = 0;
	INT status;
	int bytes;

	if ((ad->IdleMode == TRUE) ||
		(ad->bShutStatus == TRUE) ||
		(ad->bPreparingForLowPowerMode == TRUE)) {

		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Device in Idle Mode, Blocking Rdms\n");
		return -EACCES;
	}

	/* Copy Ioctl Buffer structure */
	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	if (io_buff.InputLength > sizeof(rdm_buff))
		return -EINVAL;

	if (copy_from_user(&rdm_buff, io_buff.InputBuffer,
		io_buff.InputLength))
		return -EFAULT;

	if (io_buff.OutputLength > USHRT_MAX ||
		io_buff.OutputLength == 0) {
		return -EINVAL;
	}

	temp_buff = kmalloc(io_buff.OutputLength, GFP_KERNEL);
	if (!temp_buff)
		return STATUS_FAILURE;

	if ((((ULONG)rdm_buff.Register & 0x0F000000) != 0x0F000000) ||
		((ULONG)rdm_buff.Register & 0x3)) {

		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"RDM Done On invalid Address : %x Access Denied.\n",
				(int)rdm_buff.Register);

		kfree(temp_buff);
		return -EINVAL;
	}

	tmp = rdm_buff.Register & EEPROM_REJECT_MASK;
	bytes = rdmaltWithLock(ad, (UINT)rdm_buff.Register,
			       (PUINT)temp_buff, io_buff.OutputLength);

	if (bytes > 0) {
		status = STATUS_SUCCESS;
		if (copy_to_user(io_buff.OutputBuffer, temp_buff, bytes)) {
			kfree(temp_buff);
			return -EFAULT;
		}
	} else {
		status = bytes;
	}

	kfree(temp_buff);
	return status;
}

static int bcm_char_ioctl_eeprom_reg_write(void __user *argp,
					   struct bcm_mini_adapter *ad,
					   UINT cmd)
{
	struct bcm_wrm_buffer wrm_buff = {0};
	struct bcm_ioctl_buffer io_buff;
	UINT tmp = 0;
	INT status;

	if ((ad->IdleMode == TRUE) ||
		(ad->bShutStatus == TRUE) ||
		(ad->bPreparingForLowPowerMode == TRUE)) {

		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Device in Idle Mode, Blocking Wrms\n");
		return -EACCES;
	}

	/* Copy Ioctl Buffer structure */
	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	if (io_buff.InputLength > sizeof(wrm_buff))
		return -EINVAL;

	/* Get WrmBuffer structure */
	if (copy_from_user(&wrm_buff, io_buff.InputBuffer,
		io_buff.InputLength))
		return -EFAULT;

	if ((((ULONG)wrm_buff.Register & 0x0F000000) != 0x0F000000) ||
		((ULONG)wrm_buff.Register & 0x3)) {

		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"WRM Done On invalid Address : %x Access Denied.\n",
				(int)wrm_buff.Register);
		return -EINVAL;
	}

	tmp = wrm_buff.Register & EEPROM_REJECT_MASK;
	if (!((ad->pstargetparams->m_u32Customize) & VSG_MODE) &&
			((tmp == EEPROM_REJECT_REG_1) ||
			(tmp == EEPROM_REJECT_REG_2) ||
			(tmp == EEPROM_REJECT_REG_3) ||
			(tmp == EEPROM_REJECT_REG_4)) &&
			(cmd == IOCTL_BCM_REGISTER_WRITE)) {

			BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
					"EEPROM Access Denied, not in VSG Mode\n");
			return -EFAULT;
	}

	status = wrmaltWithLock(ad, (UINT)wrm_buff.Register,
				(PUINT)wrm_buff.Data,
				wrm_buff.Length);

	if (status == STATUS_SUCCESS) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, OSAL_DBG,
				DBG_LVL_ALL, "WRM Done\n");
	} else {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG,
				DBG_LVL_ALL, "WRM Failed\n");
		status = -EFAULT;
	}
	return status;
}

static int bcm_char_ioctl_gpio_set_request(void __user *argp,
					   struct bcm_mini_adapter *ad)
{
	struct bcm_gpio_info gpio_info = {0};
	struct bcm_ioctl_buffer io_buff;
	UCHAR reset_val[4];
	UINT value = 0;
	UINT bit = 0;
	UINT operation = 0;
	INT status;
	int bytes;

	if ((ad->IdleMode == TRUE) ||
		(ad->bShutStatus == TRUE) ||
		(ad->bPreparingForLowPowerMode == TRUE)) {

		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG,
				DBG_LVL_ALL,
				"GPIO Can't be set/clear in Low power Mode");
		return -EACCES;
	}

	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	if (io_buff.InputLength > sizeof(gpio_info))
		return -EINVAL;

	if (copy_from_user(&gpio_info, io_buff.InputBuffer,
			   io_buff.InputLength))
		return -EFAULT;

	bit  = gpio_info.uiGpioNumber;
	operation = gpio_info.uiGpioValue;
	value = (1<<bit);

	if (IsReqGpioIsLedInNVM(ad, value) == false) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG,
				DBG_LVL_ALL,
				"Sorry, Requested GPIO<0x%X> is not correspond to LED !!!",
				value);
		return -EINVAL;
	}

	/* Set - setting 1 */
	if (operation) {
		/* Set the gpio output register */
		status = wrmaltWithLock(ad,
					BCM_GPIO_OUTPUT_SET_REG,
					(PUINT)(&value), sizeof(UINT));

		if (status == STATUS_SUCCESS) {
			BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS,
					OSAL_DBG, DBG_LVL_ALL,
					"Set the GPIO bit\n");
		} else {
			BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS,
					OSAL_DBG, DBG_LVL_ALL,
					"Failed to set the %dth GPIO\n",
					bit);
			return status;
		}
	} else {
		/* Set the gpio output register */
		status = wrmaltWithLock(ad,
					BCM_GPIO_OUTPUT_CLR_REG,
					(PUINT)(&value), sizeof(UINT));

		if (status == STATUS_SUCCESS) {
			BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS,
					OSAL_DBG, DBG_LVL_ALL,
					"Set the GPIO bit\n");
		} else {
			BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS,
					OSAL_DBG, DBG_LVL_ALL,
					"Failed to clear the %dth GPIO\n",
					bit);
			return status;
		}
	}

	bytes = rdmaltWithLock(ad, (UINT)GPIO_MODE_REGISTER,
			       (PUINT)reset_val, sizeof(UINT));
	if (bytes < 0) {
		status = bytes;
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
				"GPIO_MODE_REGISTER read failed");
		return status;
	}
	status = STATUS_SUCCESS;

	/* Set the gpio mode register to output */
	*(UINT *)reset_val |= (1<<bit);
	status = wrmaltWithLock(ad, GPIO_MODE_REGISTER,
				(PUINT)reset_val, sizeof(UINT));

	if (status == STATUS_SUCCESS) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG,
				DBG_LVL_ALL,
				"Set the GPIO to output Mode\n");
	} else {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG,
				DBG_LVL_ALL,
				"Failed to put GPIO in Output Mode\n");
	}

	return status;
}

static int bcm_char_ioctl_led_thread_state_change_req(void __user *argp,
		struct bcm_mini_adapter *ad)
{
	struct bcm_user_thread_req thread_req = {0};
	struct bcm_ioctl_buffer io_buff;

	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"User made LED thread InActive");

	if ((ad->IdleMode == TRUE) ||
		(ad->bShutStatus == TRUE) ||
		(ad->bPreparingForLowPowerMode == TRUE)) {

		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG,
				DBG_LVL_ALL,
				"GPIO Can't be set/clear in Low power Mode");
		return -EACCES;
	}

	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	if (io_buff.InputLength > sizeof(thread_req))
		return -EINVAL;

	if (copy_from_user(&thread_req, io_buff.InputBuffer,
			   io_buff.InputLength))
		return -EFAULT;

	/* if LED thread is running(Actively or Inactively)
	 * set it state to make inactive
	 */
	if (ad->LEDInfo.led_thread_running) {
		if (thread_req.ThreadState == LED_THREAD_ACTIVATION_REQ) {
			BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS,
					OSAL_DBG, DBG_LVL_ALL,
					"Activating thread req");
			ad->DriverState = LED_THREAD_ACTIVE;
		} else {
			BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS,
					OSAL_DBG, DBG_LVL_ALL,
					"DeActivating Thread req.....");
			ad->DriverState = LED_THREAD_INACTIVE;
		}

		/* signal thread. */
		wake_up(&ad->LEDInfo.notify_led_event);
	}
	return STATUS_SUCCESS;
}

static int bcm_char_ioctl_gpio_status_request(void __user *argp,
					      struct bcm_mini_adapter *ad)
{
	struct bcm_gpio_info gpio_info = {0};
	struct bcm_ioctl_buffer io_buff;
	ULONG bit = 0;
	UCHAR read[4];
	INT status;
	int bytes;

	if ((ad->IdleMode == TRUE) ||
		(ad->bShutStatus == TRUE) ||
		(ad->bPreparingForLowPowerMode == TRUE))
		return -EACCES;

	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	if (io_buff.InputLength > sizeof(gpio_info))
		return -EINVAL;

	if (copy_from_user(&gpio_info, io_buff.InputBuffer,
		io_buff.InputLength))
		return -EFAULT;

	bit = gpio_info.uiGpioNumber;

	/* Set the gpio output register */
	bytes = rdmaltWithLock(ad, (UINT)GPIO_PIN_STATE_REGISTER,
				(PUINT)read, sizeof(UINT));

	if (bytes < 0) {
		status = bytes;
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"RDM Failed\n");
		return status;
	}
	status = STATUS_SUCCESS;
	return status;
}

static int bcm_char_ioctl_gpio_multi_request(void __user *argp,
					     struct bcm_mini_adapter *ad)
{
	struct bcm_gpio_multi_info gpio_multi_info[MAX_IDX];
	struct bcm_gpio_multi_info *pgpio_multi_info =
		(struct bcm_gpio_multi_info *)gpio_multi_info;
	struct bcm_ioctl_buffer io_buff;
	UCHAR reset_val[4];
	INT status = STATUS_FAILURE;
	int bytes;

	memset(pgpio_multi_info, 0,
	       MAX_IDX * sizeof(struct bcm_gpio_multi_info));

	if ((ad->IdleMode == TRUE) ||
		(ad->bShutStatus == TRUE) ||
		(ad->bPreparingForLowPowerMode == TRUE))
		return -EINVAL;

	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	if (io_buff.InputLength > sizeof(gpio_multi_info))
		return -EINVAL;
	if (io_buff.OutputLength > sizeof(gpio_multi_info))
		io_buff.OutputLength = sizeof(gpio_multi_info);

	if (copy_from_user(&gpio_multi_info, io_buff.InputBuffer,
			   io_buff.InputLength))
		return -EFAULT;

	if (IsReqGpioIsLedInNVM(ad, pgpio_multi_info[WIMAX_IDX].uiGPIOMask)
			== false) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG,
				DBG_LVL_ALL,
				"Sorry, Requested GPIO<0x%X> is not correspond to NVM LED bit map<0x%X>!!!",
				pgpio_multi_info[WIMAX_IDX].uiGPIOMask,
				ad->gpioBitMap);
		return -EINVAL;
	}

	/* Set the gpio output register */
	if ((pgpio_multi_info[WIMAX_IDX].uiGPIOMask) &
		(pgpio_multi_info[WIMAX_IDX].uiGPIOCommand)) {
		/* Set 1's in GPIO OUTPUT REGISTER */
		*(UINT *)reset_val = pgpio_multi_info[WIMAX_IDX].uiGPIOMask &
			pgpio_multi_info[WIMAX_IDX].uiGPIOCommand &
			pgpio_multi_info[WIMAX_IDX].uiGPIOValue;

		if (*(UINT *) reset_val)
			status = wrmaltWithLock(ad,
				BCM_GPIO_OUTPUT_SET_REG,
				(PUINT)reset_val, sizeof(ULONG));

		if (status != STATUS_SUCCESS) {
			BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"WRM to BCM_GPIO_OUTPUT_SET_REG Failed.");
			return status;
		}

		/* Clear to 0's in GPIO OUTPUT REGISTER */
		*(UINT *)reset_val =
			(pgpio_multi_info[WIMAX_IDX].uiGPIOMask &
			pgpio_multi_info[WIMAX_IDX].uiGPIOCommand &
			(~(pgpio_multi_info[WIMAX_IDX].uiGPIOValue)));

		if (*(UINT *) reset_val)
			status = wrmaltWithLock(ad,
				BCM_GPIO_OUTPUT_CLR_REG, (PUINT)reset_val,
				sizeof(ULONG));

		if (status != STATUS_SUCCESS) {
			BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
					"WRM to BCM_GPIO_OUTPUT_CLR_REG Failed.");
			return status;
		}
	}

	if (pgpio_multi_info[WIMAX_IDX].uiGPIOMask) {
		bytes = rdmaltWithLock(ad, (UINT)GPIO_PIN_STATE_REGISTER,
				       (PUINT)reset_val, sizeof(UINT));

		if (bytes < 0) {
			status = bytes;
			BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
					"RDM to GPIO_PIN_STATE_REGISTER Failed.");
			return status;
		}
		status = STATUS_SUCCESS;

		pgpio_multi_info[WIMAX_IDX].uiGPIOValue =
			(*(UINT *)reset_val &
			pgpio_multi_info[WIMAX_IDX].uiGPIOMask);
	}

	status = copy_to_user(io_buff.OutputBuffer, &gpio_multi_info,
		io_buff.OutputLength);
	if (status) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
			"Failed while copying Content to IOBufer for user space err:%d",
			status);
		return -EFAULT;
	}
	return status;
}

static int bcm_char_ioctl_gpio_mode_request(void __user *argp,
					    struct bcm_mini_adapter *ad)
{
	struct bcm_gpio_multi_mode gpio_multi_mode[MAX_IDX];
	struct bcm_gpio_multi_mode *pgpio_multi_mode =
		(struct bcm_gpio_multi_mode *)gpio_multi_mode;
	struct bcm_ioctl_buffer io_buff;
	UCHAR reset_val[4];
	INT status;
	int bytes;

	if ((ad->IdleMode == TRUE) ||
		(ad->bShutStatus == TRUE) ||
		(ad->bPreparingForLowPowerMode == TRUE))
		return -EINVAL;

	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	if (io_buff.InputLength > sizeof(gpio_multi_mode))
		return -EINVAL;
	if (io_buff.OutputLength > sizeof(gpio_multi_mode))
		io_buff.OutputLength = sizeof(gpio_multi_mode);

	if (copy_from_user(&gpio_multi_mode, io_buff.InputBuffer,
		io_buff.InputLength))
		return -EFAULT;

	bytes = rdmaltWithLock(ad, (UINT)GPIO_MODE_REGISTER,
		(PUINT)reset_val, sizeof(UINT));

	if (bytes < 0) {
		status = bytes;
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
			"Read of GPIO_MODE_REGISTER failed");
		return status;
	}
	status = STATUS_SUCCESS;

	/* Validating the request */
	if (IsReqGpioIsLedInNVM(ad, pgpio_multi_mode[WIMAX_IDX].uiGPIOMask)
			== false) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
				"Sorry, Requested GPIO<0x%X> is not correspond to NVM LED bit map<0x%X>!!!",
				pgpio_multi_mode[WIMAX_IDX].uiGPIOMask,
				ad->gpioBitMap);
		return -EINVAL;
	}

	if (pgpio_multi_mode[WIMAX_IDX].uiGPIOMask) {
		/* write all OUT's (1's) */
		*(UINT *) reset_val |=
			(pgpio_multi_mode[WIMAX_IDX].uiGPIOMode &
					pgpio_multi_mode[WIMAX_IDX].uiGPIOMask);

		/* write all IN's (0's) */
		*(UINT *) reset_val &=
			~((~pgpio_multi_mode[WIMAX_IDX].uiGPIOMode) &
					pgpio_multi_mode[WIMAX_IDX].uiGPIOMask);

		/* Currently implemented return the modes of all GPIO's
		 * else needs to bit AND with  mask
		 */
		pgpio_multi_mode[WIMAX_IDX].uiGPIOMode = *(UINT *)reset_val;

		status = wrmaltWithLock(ad, GPIO_MODE_REGISTER,
			(PUINT)reset_val, sizeof(ULONG));
		if (status == STATUS_SUCCESS) {
			BCM_DEBUG_PRINT(ad,
				DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
				"WRM to GPIO_MODE_REGISTER Done");
		} else {
			BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
					"WRM to GPIO_MODE_REGISTER Failed");
			return -EFAULT;
		}
	} else {
		/* if uiGPIOMask is 0 then return mode register configuration */
		pgpio_multi_mode[WIMAX_IDX].uiGPIOMode = *(UINT *)reset_val;
	}

	status = copy_to_user(io_buff.OutputBuffer, &gpio_multi_mode,
		io_buff.OutputLength);
	if (status) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
			"Failed while copying Content to IOBufer for user space err:%d",
			status);
		return -EFAULT;
	}
	return status;
}

static int bcm_char_ioctl_misc_request(void __user *argp,
				       struct bcm_mini_adapter *ad)
{
	struct bcm_ioctl_buffer io_buff;
	PVOID buff = NULL;
	INT status;

	/* Copy Ioctl Buffer structure */
	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	if (io_buff.InputLength < sizeof(struct bcm_link_request))
		return -EINVAL;

	if (io_buff.InputLength > MAX_CNTL_PKT_SIZE)
		return -EINVAL;

	buff = memdup_user(io_buff.InputBuffer,
			       io_buff.InputLength);
	if (IS_ERR(buff))
		return PTR_ERR(buff);

	down(&ad->LowPowerModeSync);
	status = wait_event_interruptible_timeout(
			ad->lowpower_mode_wait_queue,
			!ad->bPreparingForLowPowerMode,
			(1 * HZ));

	if (status == -ERESTARTSYS)
		goto cntrlEnd;

	if (ad->bPreparingForLowPowerMode) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
				"Preparing Idle Mode is still True - Hence Rejecting control message\n");
		status = STATUS_FAILURE;
		goto cntrlEnd;
	}
	status = CopyBufferToControlPacket(ad, (PVOID)buff);

cntrlEnd:
	up(&ad->LowPowerModeSync);
	kfree(buff);
	return status;
}

static int bcm_char_ioctl_buffer_download_start(
		struct bcm_mini_adapter *ad)
{
	INT status;

	if (down_trylock(&ad->NVMRdmWrmLock)) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
				"IOCTL_BCM_CHIP_RESET not allowed as EEPROM Read/Write is in progress\n");
		return -EACCES;
	}

	BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
			"Starting the firmware download PID =0x%x!!!!\n",
			current->pid);

	if (down_trylock(&ad->fw_download_sema))
		return -EBUSY;

	ad->bBinDownloaded = false;
	ad->fw_download_process_pid = current->pid;
	ad->bCfgDownloaded = false;
	ad->fw_download_done = false;
	netif_carrier_off(ad->dev);
	netif_stop_queue(ad->dev);
	status = reset_card_proc(ad);
	if (status) {
		pr_err(PFX "%s: reset_card_proc Failed!\n", ad->dev->name);
		up(&ad->fw_download_sema);
		up(&ad->NVMRdmWrmLock);
		return status;
	}
	mdelay(10);

	up(&ad->NVMRdmWrmLock);
	return status;
}

static int bcm_char_ioctl_buffer_download(void __user *argp,
					  struct bcm_mini_adapter *ad)
{
	struct bcm_firmware_info *fw_info = NULL;
	struct bcm_ioctl_buffer io_buff;
	INT status;

	BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
		"Starting the firmware download PID =0x%x!!!!\n", current->pid);

	if (!down_trylock(&ad->fw_download_sema)) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Invalid way to download buffer. Use Start and then call this!!!\n");
		up(&ad->fw_download_sema);
		return -EINVAL;
	}

	/* Copy Ioctl Buffer structure */
	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer))) {
		up(&ad->fw_download_sema);
		return -EFAULT;
	}

	BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
			"Length for FW DLD is : %lx\n", io_buff.InputLength);

	if (io_buff.InputLength > sizeof(struct bcm_firmware_info)) {
		up(&ad->fw_download_sema);
		return -EINVAL;
	}

	fw_info = kmalloc(sizeof(*fw_info), GFP_KERNEL);
	if (!fw_info) {
		up(&ad->fw_download_sema);
		return -ENOMEM;
	}

	if (copy_from_user(fw_info, io_buff.InputBuffer,
		io_buff.InputLength)) {
		up(&ad->fw_download_sema);
		kfree(fw_info);
		return -EFAULT;
	}

	if (!fw_info->pvMappedFirmwareAddress ||
		(fw_info->u32FirmwareLength == 0)) {

		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Something else is wrong %lu\n",
				fw_info->u32FirmwareLength);
		up(&ad->fw_download_sema);
		kfree(fw_info);
		status = -EINVAL;
		return status;
	}

	status = bcm_ioctl_fw_download(ad, fw_info);

	if (status != STATUS_SUCCESS) {
		if (fw_info->u32StartingAddress == CONFIG_BEGIN_ADDR)
			BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"IOCTL: Configuration File Upload Failed\n");
		else
			BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"IOCTL: Firmware File Upload Failed\n");

		/* up(&ad->fw_download_sema); */

		if (ad->LEDInfo.led_thread_running &
			BCM_LED_THREAD_RUNNING_ACTIVELY) {
			ad->DriverState = DRIVER_INIT;
			ad->LEDInfo.bLedInitDone = false;
			wake_up(&ad->LEDInfo.notify_led_event);
		}
	}

	if (status != STATUS_SUCCESS)
		up(&ad->fw_download_sema);

	BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, OSAL_DBG, DBG_LVL_ALL,
		"IOCTL: Firmware File Uploaded\n");
	kfree(fw_info);
	return status;
}

static int bcm_char_ioctl_buffer_download_stop(void __user *argp,
					       struct bcm_mini_adapter *ad)
{
	INT status;
	int timeout = 0;

	if (!down_trylock(&ad->fw_download_sema)) {
		up(&ad->fw_download_sema);
		return -EINVAL;
	}

	if (down_trylock(&ad->NVMRdmWrmLock)) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"FW download blocked as EEPROM Read/Write is in progress\n");
		up(&ad->fw_download_sema);
		return -EACCES;
	}

	ad->bBinDownloaded = TRUE;
	ad->bCfgDownloaded = TRUE;
	atomic_set(&ad->CurrNumFreeTxDesc, 0);
	ad->CurrNumRecvDescs = 0;
	ad->downloadDDR = 0;

	/* setting the Mips to Run */
	status = run_card_proc(ad);

	if (status) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
			"Firm Download Failed\n");
		up(&ad->fw_download_sema);
		up(&ad->NVMRdmWrmLock);
		return status;
	}
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG,
			DBG_LVL_ALL, "Firm Download Over...\n");

	mdelay(10);

	/* Wait for MailBox Interrupt */
	if (StartInterruptUrb((struct bcm_interface_adapter *)ad->pvInterfaceAdapter))
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
			"Unable to send interrupt...\n");

	timeout = 5*HZ;
	ad->waiting_to_fw_download_done = false;
	wait_event_timeout(ad->ioctl_fw_dnld_wait_queue,
			ad->waiting_to_fw_download_done, timeout);
	ad->fw_download_process_pid = INVALID_PID;
	ad->fw_download_done = TRUE;
	atomic_set(&ad->CurrNumFreeTxDesc, 0);
	ad->CurrNumRecvDescs = 0;
	ad->PrevNumRecvDescs = 0;
	atomic_set(&ad->cntrlpktCnt, 0);
	ad->LinkUpStatus = 0;
	ad->LinkStatus = 0;

	if (ad->LEDInfo.led_thread_running &
		BCM_LED_THREAD_RUNNING_ACTIVELY) {
		ad->DriverState = FW_DOWNLOAD_DONE;
		wake_up(&ad->LEDInfo.notify_led_event);
	}

	if (!timeout)
		status = -ENODEV;

	up(&ad->fw_download_sema);
	up(&ad->NVMRdmWrmLock);
	return status;
}

static int bcm_char_ioctl_chip_reset(struct bcm_mini_adapter *ad)
{
	INT status;
	INT nvm_access;

	nvm_access = down_trylock(&ad->NVMRdmWrmLock);
	if (nvm_access) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
			" IOCTL_BCM_CHIP_RESET not allowed as EEPROM Read/Write is in progress\n");
		return -EACCES;
	}

	down(&ad->RxAppControlQueuelock);
	status = reset_card_proc(ad);
	flushAllAppQ();
	up(&ad->RxAppControlQueuelock);
	up(&ad->NVMRdmWrmLock);
	ResetCounters(ad);
	return status;
}

static int bcm_char_ioctl_qos_threshold(ULONG arg,
					struct bcm_mini_adapter *ad)
{
	USHORT i;

	for (i = 0; i < NO_OF_QUEUES; i++) {
		if (get_user(ad->PackInfo[i].uiThreshold,
				(unsigned long __user *)arg)) {
			return -EFAULT;
		}
	}
	return 0;
}

static int bcm_char_ioctl_switch_transfer_mode(void __user *argp,
					       struct bcm_mini_adapter *ad)
{
	UINT data = 0;

	if (copy_from_user(&data, argp, sizeof(UINT)))
		return -EFAULT;

	if (data) {
		/* Allow All Packets */
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"IOCTL_BCM_SWITCH_TRANSFER_MODE: ETH_PACKET_TUNNELING_MODE\n");
			ad->TransferMode = ETH_PACKET_TUNNELING_MODE;
	} else {
		/* Allow IP only Packets */
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"IOCTL_BCM_SWITCH_TRANSFER_MODE: IP_PACKET_ONLY_MODE\n");
		ad->TransferMode = IP_PACKET_ONLY_MODE;
	}
	return STATUS_SUCCESS;
}

static int bcm_char_ioctl_get_driver_version(void __user *argp)
{
	struct bcm_ioctl_buffer io_buff;
	ulong len;

	/* Copy Ioctl Buffer structure */
	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	len = min_t(ulong, io_buff.OutputLength, strlen(DRV_VERSION) + 1);

	if (copy_to_user(io_buff.OutputBuffer, DRV_VERSION, len))
		return -EFAULT;

	return STATUS_SUCCESS;
}

static int bcm_char_ioctl_get_current_status(void __user *argp,
					     struct bcm_mini_adapter *ad)
{
	struct bcm_link_state link_state;
	struct bcm_ioctl_buffer io_buff;

	/* Copy Ioctl Buffer structure */
	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer))) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
			"copy_from_user failed..\n");
		return -EFAULT;
	}

	if (io_buff.OutputLength != sizeof(link_state))
		return -EINVAL;

	memset(&link_state, 0, sizeof(link_state));
	link_state.bIdleMode = ad->IdleMode;
	link_state.bShutdownMode = ad->bShutStatus;
	link_state.ucLinkStatus = ad->LinkStatus;

	if (copy_to_user(io_buff.OutputBuffer, &link_state, min_t(size_t,
		sizeof(link_state), io_buff.OutputLength))) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
			"Copy_to_user Failed..\n");
		return -EFAULT;
	}
	return STATUS_SUCCESS;
}


static int bcm_char_ioctl_set_mac_tracing(void __user *argp,
					  struct bcm_mini_adapter *ad)
{
	struct bcm_ioctl_buffer io_buff;
	UINT tracing_flag;

	/* copy ioctl Buffer structure */
	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	if (copy_from_user(&tracing_flag, io_buff.InputBuffer, sizeof(UINT)))
		return -EFAULT;

	if (tracing_flag)
		ad->pTarangs->MacTracingEnabled = TRUE;
	else
		ad->pTarangs->MacTracingEnabled = false;

	return STATUS_SUCCESS;
}

static int bcm_char_ioctl_get_dsx_indication(void __user *argp,
					     struct bcm_mini_adapter *ad)
{
	struct bcm_ioctl_buffer io_buff;
	ULONG sf_id = 0;

	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	if (io_buff.OutputLength < sizeof(struct bcm_add_indication_alt)) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
			"Mismatch req: %lx needed is =0x%zx!!!",
			io_buff.OutputLength,
			sizeof(struct bcm_add_indication_alt));
		return -EINVAL;
	}

	if (copy_from_user(&sf_id, io_buff.InputBuffer, sizeof(sf_id)))
		return -EFAULT;

	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
		"Get DSX Data SF ID is =%lx\n", sf_id);
	get_dsx_sf_data_to_application(ad, sf_id, io_buff.OutputBuffer);
	return STATUS_SUCCESS;
}

static int bcm_char_ioctl_get_host_mibs(void __user *argp,
					struct bcm_mini_adapter *ad,
					struct bcm_tarang_data *tarang)
{
	struct bcm_ioctl_buffer io_buff;
	INT status = STATUS_FAILURE;
	PVOID temp_buff;

	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	if (io_buff.OutputLength != sizeof(struct bcm_host_stats_mibs)) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
			"Length Check failed %lu %zd\n", io_buff.OutputLength,
			sizeof(struct bcm_host_stats_mibs));
		return -EINVAL;
	}

	/* FIXME: HOST_STATS are too big for kmalloc (122048)! */
	temp_buff = kzalloc(sizeof(struct bcm_host_stats_mibs), GFP_KERNEL);
	if (!temp_buff)
		return STATUS_FAILURE;

	status = ProcessGetHostMibs(ad, temp_buff);
	GetDroppedAppCntrlPktMibs(temp_buff, tarang);

	if (status != STATUS_FAILURE) {
		if (copy_to_user(io_buff.OutputBuffer, temp_buff,
			sizeof(struct bcm_host_stats_mibs))) {
			kfree(temp_buff);
			return -EFAULT;
		}
	}

	kfree(temp_buff);
	return status;
}

static int bcm_char_ioctl_bulk_wrm(void __user *argp,
				   struct bcm_mini_adapter *ad, UINT cmd)
{
	struct bcm_bulk_wrm_buffer *bulk_buff;
	struct bcm_ioctl_buffer io_buff;
	UINT tmp = 0;
	INT status = STATUS_FAILURE;
	PCHAR buff = NULL;

	if ((ad->IdleMode == TRUE) ||
		(ad->bShutStatus == TRUE) ||
		(ad->bPreparingForLowPowerMode == TRUE)) {

		BCM_DEBUG_PRINT (ad, DBG_TYPE_PRINTK, 0, 0,
			"Device in Idle/Shutdown Mode, Blocking Wrms\n");
		return -EACCES;
	}

	/* Copy Ioctl Buffer structure */
	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	if (io_buff.InputLength < sizeof(ULONG) * 2)
		return -EINVAL;

	buff = memdup_user(io_buff.InputBuffer,
			       io_buff.InputLength);
	if (IS_ERR(buff))
		return PTR_ERR(buff);

	bulk_buff = (struct bcm_bulk_wrm_buffer *)buff;

	if (((ULONG)bulk_buff->Register & 0x0F000000) != 0x0F000000 ||
		((ULONG)bulk_buff->Register & 0x3)) {
		BCM_DEBUG_PRINT (ad, DBG_TYPE_PRINTK, 0, 0,
			"WRM Done On invalid Address : %x Access Denied.\n",
			(int)bulk_buff->Register);
		kfree(buff);
		return -EINVAL;
	}

	tmp = bulk_buff->Register & EEPROM_REJECT_MASK;
	if (!((ad->pstargetparams->m_u32Customize)&VSG_MODE) &&
		((tmp == EEPROM_REJECT_REG_1) ||
			(tmp == EEPROM_REJECT_REG_2) ||
			(tmp == EEPROM_REJECT_REG_3) ||
			(tmp == EEPROM_REJECT_REG_4)) &&
		(cmd == IOCTL_BCM_REGISTER_WRITE)) {

		kfree(buff);
		BCM_DEBUG_PRINT (ad, DBG_TYPE_PRINTK, 0, 0,
			"EEPROM Access Denied, not in VSG Mode\n");
		return -EFAULT;
	}

	if (bulk_buff->SwapEndian == false)
		status = wrmWithLock(ad, (UINT)bulk_buff->Register,
			(PCHAR)bulk_buff->Values,
			io_buff.InputLength - 2*sizeof(ULONG));
	else
		status = wrmaltWithLock(ad, (UINT)bulk_buff->Register,
			(PUINT)bulk_buff->Values,
			io_buff.InputLength - 2*sizeof(ULONG));

	if (status != STATUS_SUCCESS)
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0, "WRM Failed\n");

	kfree(buff);
	return status;
}

static int bcm_char_ioctl_get_nvm_size(void __user *argp,
				       struct bcm_mini_adapter *ad)
{
	struct bcm_ioctl_buffer io_buff;

	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	if (ad->eNVMType == NVM_EEPROM || ad->eNVMType == NVM_FLASH) {
		if (copy_to_user(io_buff.OutputBuffer, &ad->uiNVMDSDSize,
			sizeof(UINT)))
			return -EFAULT;
	}

	return STATUS_SUCCESS;
}

static int bcm_char_ioctl_cal_init(void __user *argp,
				   struct bcm_mini_adapter *ad)
{
	struct bcm_ioctl_buffer io_buff;
	UINT sector_size = 0;
	INT status = STATUS_FAILURE;

	if (ad->eNVMType == NVM_FLASH) {
		if (copy_from_user(&io_buff, argp,
			sizeof(struct bcm_ioctl_buffer)))
			return -EFAULT;

		if (copy_from_user(&sector_size, io_buff.InputBuffer,
			sizeof(UINT)))
			return -EFAULT;

		if ((sector_size < MIN_SECTOR_SIZE) ||
			(sector_size > MAX_SECTOR_SIZE)) {
			if (copy_to_user(io_buff.OutputBuffer,
				&ad->uiSectorSize, sizeof(UINT)))
				return -EFAULT;
		} else {
			if (IsFlash2x(ad)) {
				if (copy_to_user(io_buff.OutputBuffer,
					&ad->uiSectorSize, sizeof(UINT)))
					return -EFAULT;
			} else {
				if ((TRUE == ad->bShutStatus) ||
					(TRUE == ad->IdleMode)) {
					BCM_DEBUG_PRINT(ad,
						DBG_TYPE_PRINTK, 0, 0,
						"Device is in Idle/Shutdown Mode\n");
					return -EACCES;
				}

				ad->uiSectorSize = sector_size;
				BcmUpdateSectorSize(ad,
					ad->uiSectorSize);
			}
		}
		status = STATUS_SUCCESS;
	} else {
		status = STATUS_FAILURE;
	}
	return status;
}

static int bcm_char_ioctl_set_debug(void __user *argp,
				    struct bcm_mini_adapter *ad)
{
#ifdef DEBUG
	struct bcm_ioctl_buffer io_buff;
	struct bcm_user_debug_state user_debug_state;

	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
		"In SET_DEBUG ioctl\n");
	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	if (copy_from_user(&user_debug_state, io_buff.InputBuffer,
		sizeof(struct bcm_user_debug_state)))
		return -EFAULT;

	BCM_DEBUG_PRINT (ad, DBG_TYPE_PRINTK, 0, 0,
			"IOCTL_BCM_SET_DEBUG: OnOff=%d Type = 0x%x ",
			user_debug_state.OnOff, user_debug_state.Type);
	/* user_debug_state.Subtype <<= 1; */
	user_debug_state.Subtype = 1 << user_debug_state.Subtype;
	BCM_DEBUG_PRINT (ad, DBG_TYPE_PRINTK, 0, 0,
		"actual Subtype=0x%x\n", user_debug_state.Subtype);

	/* Update new 'DebugState' in the ad */
	ad->stDebugState.type |= user_debug_state.Type;
	/* Subtype: A bitmap of 32 bits for Subtype per Type.
	 * Valid indexes in 'subtype' array: 1,2,4,8
	 * corresponding to valid Type values. Hence we can use the 'Type' field
	 * as the index value, ignoring the array entries 0,3,5,6,7 !
	 */
	if (user_debug_state.OnOff)
		ad->stDebugState.subtype[user_debug_state.Type] |=
			user_debug_state.Subtype;
	else
		ad->stDebugState.subtype[user_debug_state.Type] &=
			~user_debug_state.Subtype;

	BCM_SHOW_DEBUG_BITMAP(ad);
#endif
	return STATUS_SUCCESS;
}

static int bcm_char_ioctl_nvm_rw(void __user *argp,
				 struct bcm_mini_adapter *ad, UINT cmd)
{
	struct bcm_nvm_readwrite nvm_rw;
	struct timeval tv0, tv1;
	struct bcm_ioctl_buffer io_buff;
	PUCHAR read_data = NULL;
	INT status = STATUS_FAILURE;

	memset(&tv0, 0, sizeof(struct timeval));
	memset(&tv1, 0, sizeof(struct timeval));
	if ((ad->eNVMType == NVM_FLASH) &&
		(ad->uiFlashLayoutMajorVersion == 0)) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
			"The Flash Control Section is Corrupted. Hence Rejection on NVM Read/Write\n");
		return -EFAULT;
	}

	if (IsFlash2x(ad)) {
		if ((ad->eActiveDSD != DSD0) &&
			(ad->eActiveDSD != DSD1) &&
			(ad->eActiveDSD != DSD2)) {

			BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"No DSD is active..hence NVM Command is blocked");
			return STATUS_FAILURE;
		}
	}

	/* Copy Ioctl Buffer structure */
	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	if (copy_from_user(&nvm_rw,
				(IOCTL_BCM_NVM_READ == cmd) ?
				io_buff.OutputBuffer : io_buff.InputBuffer,
				sizeof(struct bcm_nvm_readwrite)))
		return -EFAULT;

	/*
	 * Deny the access if the offset crosses the cal area limit.
	 */
	if (nvm_rw.uiNumBytes > ad->uiNVMDSDSize)
		return STATUS_FAILURE;

	if (nvm_rw.uiOffset >
		ad->uiNVMDSDSize - nvm_rw.uiNumBytes)
		return STATUS_FAILURE;

	read_data = memdup_user(nvm_rw.pBuffer,
				nvm_rw.uiNumBytes);
	if (IS_ERR(read_data))
		return PTR_ERR(read_data);

	do_gettimeofday(&tv0);
	if (IOCTL_BCM_NVM_READ == cmd) {
		int ret = bcm_handle_nvm_read_cmd(ad, read_data,
				&nvm_rw);
		if (ret != STATUS_SUCCESS)
			return ret;
	} else {
		down(&ad->NVMRdmWrmLock);

		if ((ad->IdleMode == TRUE) ||
			(ad->bShutStatus == TRUE) ||
			(ad->bPreparingForLowPowerMode == TRUE)) {

			BCM_DEBUG_PRINT(ad,
				DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
				"Device is in Idle/Shutdown Mode\n");
			up(&ad->NVMRdmWrmLock);
			kfree(read_data);
			return -EACCES;
		}

		ad->bHeaderChangeAllowed = TRUE;
		if (IsFlash2x(ad)) {
			int ret = handle_flash2x_adapter(ad,
							read_data,
							&nvm_rw);
			if (ret != STATUS_SUCCESS)
				return ret;
		}

		status = BeceemNVMWrite(ad, (PUINT)read_data,
			nvm_rw.uiOffset, nvm_rw.uiNumBytes,
			nvm_rw.bVerify);
		if (IsFlash2x(ad))
			BcmFlash2xWriteSig(ad, ad->eActiveDSD);

		ad->bHeaderChangeAllowed = false;

		up(&ad->NVMRdmWrmLock);

		if (status != STATUS_SUCCESS) {
			kfree(read_data);
			return status;
		}
	}

	do_gettimeofday(&tv1);
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
		" timetaken by Write/read :%ld msec\n",
		(tv1.tv_sec - tv0.tv_sec)*1000 +
		(tv1.tv_usec - tv0.tv_usec)/1000);

	kfree(read_data);
	return STATUS_SUCCESS;
}

static int bcm_char_ioctl_flash2x_section_read(void __user *argp,
	struct bcm_mini_adapter *ad)
{
	struct bcm_flash2x_readwrite flash_2x_read = {0};
	struct bcm_ioctl_buffer io_buff;
	PUCHAR read_buff = NULL;
	UINT nob = 0;
	UINT buff_size = 0;
	UINT read_bytes = 0;
	UINT read_offset = 0;
	INT status = STATUS_FAILURE;
	void __user *OutPutBuff;

	if (IsFlash2x(ad) != TRUE)	{
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
			"Flash Does not have 2.x map");
		return -EINVAL;
	}

	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG,
		DBG_LVL_ALL, "IOCTL_BCM_FLASH2X_SECTION_READ Called");
	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	/* Reading FLASH 2.x READ structure */
	if (copy_from_user(&flash_2x_read, io_buff.InputBuffer,
		sizeof(struct bcm_flash2x_readwrite)))
		return -EFAULT;

	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"\nflash_2x_read.Section :%x",
			flash_2x_read.Section);
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"\nflash_2x_read.offset :%x",
			flash_2x_read.offset);
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"\nflash_2x_read.numOfBytes :%x",
			flash_2x_read.numOfBytes);
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"\nflash_2x_read.bVerify :%x\n",
			flash_2x_read.bVerify);

	/* This was internal to driver for raw read.
	 * now it has ben exposed to user space app.
	 */
	if (validateFlash2xReadWrite(ad, &flash_2x_read) == false)
		return STATUS_FAILURE;

	nob = flash_2x_read.numOfBytes;
	if (nob > ad->uiSectorSize)
		buff_size = ad->uiSectorSize;
	else
		buff_size = nob;

	read_offset = flash_2x_read.offset;
	OutPutBuff = io_buff.OutputBuffer;
	read_buff = kzalloc(buff_size , GFP_KERNEL);

	if (read_buff == NULL) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Memory allocation failed for Flash 2.x Read Structure");
		return -ENOMEM;
	}
	down(&ad->NVMRdmWrmLock);

	if ((ad->IdleMode == TRUE) ||
		(ad->bShutStatus == TRUE) ||
		(ad->bPreparingForLowPowerMode == TRUE)) {

		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG,
				DBG_LVL_ALL,
				"Device is in Idle/Shutdown Mode\n");
		up(&ad->NVMRdmWrmLock);
		kfree(read_buff);
		return -EACCES;
	}

	while (nob) {
		if (nob > ad->uiSectorSize)
			read_bytes = ad->uiSectorSize;
		else
			read_bytes = nob;

		/* Reading the data from Flash 2.x */
		status = BcmFlash2xBulkRead(ad, (PUINT)read_buff,
			flash_2x_read.Section, read_offset, read_bytes);
		if (status) {
			BCM_DEBUG_PRINT(ad,
				DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
				"Flash 2x read err with status :%d",
				status);
			break;
		}

		BCM_DEBUG_PRINT_BUFFER(ad, DBG_TYPE_OTHERS, OSAL_DBG,
			DBG_LVL_ALL, read_buff, read_bytes);

		status = copy_to_user(OutPutBuff, read_buff, read_bytes);
		if (status) {
			BCM_DEBUG_PRINT(ad,
				DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
				"Copy to use failed with status :%d", status);
			up(&ad->NVMRdmWrmLock);
			kfree(read_buff);
			return -EFAULT;
		}
		nob = nob - read_bytes;
		if (nob) {
			read_offset = read_offset + read_bytes;
			OutPutBuff = OutPutBuff + read_bytes;
		}
	}

	up(&ad->NVMRdmWrmLock);
	kfree(read_buff);
	return status;
}

static int bcm_char_ioctl_flash2x_section_write(void __user *argp,
	struct bcm_mini_adapter *ad)
{
	struct bcm_flash2x_readwrite sFlash2xWrite = {0};
	struct bcm_ioctl_buffer io_buff;
	PUCHAR write_buff;
	void __user *input_addr;
	UINT nob = 0;
	UINT buff_size = 0;
	UINT write_off = 0;
	UINT write_bytes = 0;
	INT status = STATUS_FAILURE;

	if (IsFlash2x(ad) != TRUE) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
			"Flash Does not have 2.x map");
		return -EINVAL;
	}

	/* First make this False so that we can enable the Sector
	 * Permission Check in BeceemFlashBulkWrite
	 */
	ad->bAllDSDWriteAllow = false;

	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
		"IOCTL_BCM_FLASH2X_SECTION_WRITE Called");

	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	/* Reading FLASH 2.x READ structure */
	if (copy_from_user(&sFlash2xWrite, io_buff.InputBuffer,
		sizeof(struct bcm_flash2x_readwrite)))
		return -EFAULT;

	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
		"\nsFlash2xWrite.Section :%x", sFlash2xWrite.Section);
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
		"\nsFlash2xWrite.offset :%d", sFlash2xWrite.offset);
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
		"\nsFlash2xWrite.numOfBytes :%x", sFlash2xWrite.numOfBytes);
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
		"\nsFlash2xWrite.bVerify :%x\n", sFlash2xWrite.bVerify);

	if ((sFlash2xWrite.Section != VSA0) && (sFlash2xWrite.Section != VSA1)
		&& (sFlash2xWrite.Section != VSA2)) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"Only VSA write is allowed");
		return -EINVAL;
	}

	if (validateFlash2xReadWrite(ad, &sFlash2xWrite) == false)
		return STATUS_FAILURE;

	input_addr = sFlash2xWrite.pDataBuff;
	write_off = sFlash2xWrite.offset;
	nob = sFlash2xWrite.numOfBytes;

	if (nob > ad->uiSectorSize)
		buff_size = ad->uiSectorSize;
	else
		buff_size = nob;

	write_buff = kmalloc(buff_size, GFP_KERNEL);

	if (write_buff == NULL)
		return -ENOMEM;

	/* extracting the remainder of the given offset. */
	write_bytes = ad->uiSectorSize;
	if (write_off % ad->uiSectorSize) {
		write_bytes = ad->uiSectorSize -
			(write_off % ad->uiSectorSize);
	}

	if (nob < write_bytes)
		write_bytes = nob;

	down(&ad->NVMRdmWrmLock);

	if ((ad->IdleMode == TRUE) ||
		(ad->bShutStatus == TRUE) ||
		(ad->bPreparingForLowPowerMode == TRUE)) {

		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"Device is in Idle/Shutdown Mode\n");
		up(&ad->NVMRdmWrmLock);
		kfree(write_buff);
		return -EACCES;
	}

	BcmFlash2xCorruptSig(ad, sFlash2xWrite.Section);
	do {
		status = copy_from_user(write_buff, input_addr, write_bytes);
		if (status) {
			BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Copy to user failed with status :%d", status);
			up(&ad->NVMRdmWrmLock);
			kfree(write_buff);
			return -EFAULT;
		}
		BCM_DEBUG_PRINT_BUFFER(ad, DBG_TYPE_OTHERS,
			OSAL_DBG, DBG_LVL_ALL, write_buff, write_bytes);

		/* Writing the data from Flash 2.x */
		status = BcmFlash2xBulkWrite(ad, (PUINT)write_buff,
					     sFlash2xWrite.Section,
					     write_off,
					     write_bytes,
					     sFlash2xWrite.bVerify);

		if (status) {
			BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Flash 2x read err with status :%d", status);
			break;
		}

		nob = nob - write_bytes;
		if (nob) {
			write_off = write_off + write_bytes;
			input_addr = input_addr + write_bytes;
			if (nob > ad->uiSectorSize)
				write_bytes = ad->uiSectorSize;
			else
				write_bytes = nob;
		}
	} while (nob > 0);

	BcmFlash2xWriteSig(ad, sFlash2xWrite.Section);
	up(&ad->NVMRdmWrmLock);
	kfree(write_buff);
	return status;
}

static int bcm_char_ioctl_flash2x_section_bitmap(void __user *argp,
	struct bcm_mini_adapter *ad)
{
	struct bcm_flash2x_bitmap *flash_2x_bit_map;
	struct bcm_ioctl_buffer io_buff;

BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
	"IOCTL_BCM_GET_FLASH2X_SECTION_BITMAP Called");

	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	if (io_buff.OutputLength != sizeof(struct bcm_flash2x_bitmap))
		return -EINVAL;

	flash_2x_bit_map = kzalloc(sizeof(struct bcm_flash2x_bitmap),
			GFP_KERNEL);

	if (flash_2x_bit_map == NULL) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
			"Memory is not available");
		return -ENOMEM;
	}

	/* Reading the Flash Sectio Bit map */
	down(&ad->NVMRdmWrmLock);

	if ((ad->IdleMode == TRUE) ||
		(ad->bShutStatus == TRUE) ||
		(ad->bPreparingForLowPowerMode == TRUE)) {

		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"Device is in Idle/Shutdown Mode\n");
		up(&ad->NVMRdmWrmLock);
		kfree(flash_2x_bit_map);
		return -EACCES;
	}

	BcmGetFlash2xSectionalBitMap(ad, flash_2x_bit_map);
	up(&ad->NVMRdmWrmLock);
	if (copy_to_user(io_buff.OutputBuffer, flash_2x_bit_map,
		sizeof(struct bcm_flash2x_bitmap))) {
		kfree(flash_2x_bit_map);
		return -EFAULT;
	}

	kfree(flash_2x_bit_map);
	return STATUS_FAILURE;
}

static int bcm_char_ioctl_set_active_section(void __user *argp,
					     struct bcm_mini_adapter *ad)
{
	enum bcm_flash2x_section_val flash_2x_section_val = 0;
	INT status = STATUS_FAILURE;
	struct bcm_ioctl_buffer io_buff;

	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"IOCTL_BCM_SET_ACTIVE_SECTION Called");

	if (IsFlash2x(ad) != TRUE) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Flash Does not have 2.x map");
		return -EINVAL;
	}

	status = copy_from_user(&io_buff, argp,
				sizeof(struct bcm_ioctl_buffer));
	if (status) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Copy of IOCTL BUFFER failed");
		return -EFAULT;
	}

	status = copy_from_user(&flash_2x_section_val,
				io_buff.InputBuffer, sizeof(INT));
	if (status) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
			"Copy of flash section val failed");
		return -EFAULT;
	}

	down(&ad->NVMRdmWrmLock);

	if ((ad->IdleMode == TRUE) ||
		(ad->bShutStatus == TRUE) ||
		(ad->bPreparingForLowPowerMode == TRUE)) {

		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
				"Device is in Idle/Shutdown Mode\n");
		up(&ad->NVMRdmWrmLock);
		return -EACCES;
	}

	status = BcmSetActiveSection(ad, flash_2x_section_val);
	if (status)
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Failed to make it's priority Highest. status %d",
				status);

	up(&ad->NVMRdmWrmLock);

	return status;
}

static int bcm_char_ioctl_copy_section(void __user *argp,
				       struct bcm_mini_adapter *ad)
{
	struct bcm_flash2x_copy_section copy_sect_strut = {0};
	struct bcm_ioctl_buffer io_buff;
	INT status = STATUS_SUCCESS;

	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"IOCTL_BCM_COPY_SECTION  Called");

	ad->bAllDSDWriteAllow = false;
	if (IsFlash2x(ad) != TRUE) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Flash Does not have 2.x map");
		return -EINVAL;
	}

	status = copy_from_user(&io_buff, argp,
				sizeof(struct bcm_ioctl_buffer));
	if (status) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Copy of IOCTL BUFFER failed status :%d",
				status);
		return -EFAULT;
	}

	status = copy_from_user(&copy_sect_strut, io_buff.InputBuffer,
				sizeof(struct bcm_flash2x_copy_section));
	if (status) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Copy of Copy_Section_Struct failed with status :%d",
				status);
		return -EFAULT;
	}

	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"Source SEction :%x", copy_sect_strut.SrcSection);
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"Destination SEction :%x", copy_sect_strut.DstSection);
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"offset :%x", copy_sect_strut.offset);
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"nob :%x", copy_sect_strut.numOfBytes);

	if (IsSectionExistInFlash(ad, copy_sect_strut.SrcSection) == false) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Source Section<%x> does not exist in Flash ",
				copy_sect_strut.SrcSection);
		return -EINVAL;
	}

	if (IsSectionExistInFlash(ad, copy_sect_strut.DstSection) == false) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Destinatio Section<%x> does not exist in Flash ",
				copy_sect_strut.DstSection);
		return -EINVAL;
	}

	if (copy_sect_strut.SrcSection == copy_sect_strut.DstSection) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
				"Source and Destination section should be different");
		return -EINVAL;
	}

	down(&ad->NVMRdmWrmLock);

	if ((ad->IdleMode == TRUE) ||
		(ad->bShutStatus == TRUE) ||
		(ad->bPreparingForLowPowerMode == TRUE)) {

		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
				"Device is in Idle/Shutdown Mode\n");
		up(&ad->NVMRdmWrmLock);
		return -EACCES;
	}

	if (copy_sect_strut.SrcSection == ISO_IMAGE1 ||
		copy_sect_strut.SrcSection == ISO_IMAGE2) {
		if (IsNonCDLessDevice(ad)) {
			BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
					"Device is Non-CDLess hence won't have ISO !!");
			status = -EINVAL;
		} else if (copy_sect_strut.numOfBytes == 0) {
			status = BcmCopyISO(ad, copy_sect_strut);
		} else {
			BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
					"Partial Copy of ISO section is not Allowed..");
			status = STATUS_FAILURE;
		}
		up(&ad->NVMRdmWrmLock);
		return status;
	}

	status = BcmCopySection(ad, copy_sect_strut.SrcSection,
				copy_sect_strut.DstSection,
				copy_sect_strut.offset,
				copy_sect_strut.numOfBytes);
	up(&ad->NVMRdmWrmLock);
	return status;
}

static int bcm_char_ioctl_get_flash_cs_info(void __user *argp,
					    struct bcm_mini_adapter *ad)
{
	struct bcm_ioctl_buffer io_buff;
	INT status = STATUS_SUCCESS;

	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			" IOCTL_BCM_GET_FLASH_CS_INFO Called");

	status = copy_from_user(&io_buff, argp,
			sizeof(struct bcm_ioctl_buffer));
	if (status) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Copy of IOCTL BUFFER failed");
		return -EFAULT;
	}

	if (ad->eNVMType != NVM_FLASH) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Connected device does not have flash");
		return -EINVAL;
	}

	if (IsFlash2x(ad) == TRUE) {
		if (io_buff.OutputLength < sizeof(struct bcm_flash2x_cs_info))
			return -EINVAL;

		if (copy_to_user(io_buff.OutputBuffer,
				 ad->psFlash2xCSInfo,
				 sizeof(struct bcm_flash2x_cs_info)))
			return -EFAULT;
	} else {
		if (io_buff.OutputLength < sizeof(struct bcm_flash_cs_info))
			return -EINVAL;

		if (copy_to_user(io_buff.OutputBuffer, ad->psFlashCSInfo,
				 sizeof(struct bcm_flash_cs_info)))
			return -EFAULT;
	}
	return status;
}

static int bcm_char_ioctl_select_dsd(void __user *argp,
				     struct bcm_mini_adapter *ad)
{
	struct bcm_ioctl_buffer io_buff;
	INT status = STATUS_FAILURE;
	UINT sect_offset = 0;
	enum bcm_flash2x_section_val flash_2x_section_val;

	flash_2x_section_val = NO_SECTION_VAL;
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"IOCTL_BCM_SELECT_DSD Called");

	if (IsFlash2x(ad) != TRUE) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Flash Does not have 2.x map");
		return -EINVAL;
	}

	status = copy_from_user(&io_buff, argp,
				sizeof(struct bcm_ioctl_buffer));
	if (status) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Copy of IOCTL BUFFER failed");
		return -EFAULT;
	}
	status = copy_from_user(&flash_2x_section_val, io_buff.InputBuffer,
		sizeof(INT));
	if (status) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Copy of flash section val failed");
		return -EFAULT;
	}

	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"Read Section :%d", flash_2x_section_val);
	if ((flash_2x_section_val != DSD0) &&
		(flash_2x_section_val != DSD1) &&
		(flash_2x_section_val != DSD2)) {

		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Passed section<%x> is not DSD section",
				flash_2x_section_val);
		return STATUS_FAILURE;
	}

	sect_offset = BcmGetSectionValStartOffset(ad, flash_2x_section_val);
	if (sect_offset == INVALID_OFFSET) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Provided Section val <%d> does not exist in Flash 2.x",
				flash_2x_section_val);
		return -EINVAL;
	}

	ad->bAllDSDWriteAllow = TRUE;
	ad->ulFlashCalStart = sect_offset;
	ad->eActiveDSD = flash_2x_section_val;

	return STATUS_SUCCESS;
}

static int bcm_char_ioctl_nvm_raw_read(void __user *argp,
				       struct bcm_mini_adapter *ad)
{
	struct bcm_nvm_readwrite nvm_read;
	struct bcm_ioctl_buffer io_buff;
	unsigned int nob;
	INT buff_size;
	INT read_offset = 0;
	UINT read_bytes = 0;
	PUCHAR read_buff;
	void __user *OutPutBuff;
	INT status = STATUS_FAILURE;

	if (ad->eNVMType != NVM_FLASH) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"NVM TYPE is not Flash");
		return -EINVAL;
	}

	/* Copy Ioctl Buffer structure */
	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer))) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"copy_from_user 1 failed\n");
		return -EFAULT;
	}

	if (copy_from_user(&nvm_read, io_buff.OutputBuffer,
		sizeof(struct bcm_nvm_readwrite)))
		return -EFAULT;

	nob = nvm_read.uiNumBytes;
	/* In Raw-Read max Buff size : 64MB */

	if (nob > DEFAULT_BUFF_SIZE)
		buff_size = DEFAULT_BUFF_SIZE;
	else
		buff_size = nob;

	read_offset = nvm_read.uiOffset;
	OutPutBuff = nvm_read.pBuffer;

	read_buff = kzalloc(buff_size , GFP_KERNEL);
	if (read_buff == NULL) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
				"Memory allocation failed for Flash 2.x Read Structure");
		return -ENOMEM;
	}
	down(&ad->NVMRdmWrmLock);

	if ((ad->IdleMode == TRUE) ||
		(ad->bShutStatus == TRUE) ||
		(ad->bPreparingForLowPowerMode == TRUE)) {

		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
				"Device is in Idle/Shutdown Mode\n");
		kfree(read_buff);
		up(&ad->NVMRdmWrmLock);
		return -EACCES;
	}

	ad->bFlashRawRead = TRUE;

	while (nob) {
		if (nob > DEFAULT_BUFF_SIZE)
			read_bytes = DEFAULT_BUFF_SIZE;
		else
			read_bytes = nob;

		/* Reading the data from Flash 2.x */
		status = BeceemNVMRead(ad, (PUINT)read_buff,
			read_offset, read_bytes);
		if (status) {
			BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
					"Flash 2x read err with status :%d",
					status);
			break;
		}

		BCM_DEBUG_PRINT_BUFFER(ad, DBG_TYPE_OTHERS, OSAL_DBG,
				       DBG_LVL_ALL, read_buff, read_bytes);

		status = copy_to_user(OutPutBuff, read_buff, read_bytes);
		if (status) {
			BCM_DEBUG_PRINT(ad, DBG_TYPE_PRINTK, 0, 0,
					"Copy to use failed with status :%d",
					status);
			up(&ad->NVMRdmWrmLock);
			kfree(read_buff);
			return -EFAULT;
		}
		nob = nob - read_bytes;
		if (nob) {
			read_offset = read_offset + read_bytes;
			OutPutBuff = OutPutBuff + read_bytes;
		}
	}
	ad->bFlashRawRead = false;
	up(&ad->NVMRdmWrmLock);
	kfree(read_buff);
	return status;
}

static int bcm_char_ioctl_cntrlmsg_mask(void __user *argp,
					struct bcm_mini_adapter *ad,
					struct bcm_tarang_data *tarang)
{
	struct bcm_ioctl_buffer io_buff;
	INT status = STATUS_FAILURE;
	ULONG rx_cntrl_msg_bit_mask = 0;

	/* Copy Ioctl Buffer structure */
	status = copy_from_user(&io_buff, argp,
			sizeof(struct bcm_ioctl_buffer));
	if (status) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
				"copy of Ioctl buffer is failed from user space");
		return -EFAULT;
	}

	if (io_buff.InputLength != sizeof(unsigned long))
		return -EINVAL;

	status = copy_from_user(&rx_cntrl_msg_bit_mask, io_buff.InputBuffer,
				io_buff.InputLength);
	if (status) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
				"copy of control bit mask failed from user space");
		return -EFAULT;
	}
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"\n Got user defined cntrl msg bit mask :%lx",
			rx_cntrl_msg_bit_mask);
	tarang->RxCntrlMsgBitMask = rx_cntrl_msg_bit_mask;

	return status;
}

static int bcm_char_ioctl_get_device_driver_info(void __user *argp,
	struct bcm_mini_adapter *ad)
{
	struct bcm_driver_info dev_info;
	struct bcm_ioctl_buffer io_buff;

	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"Called IOCTL_BCM_GET_DEVICE_DRIVER_INFO\n");

	memset(&dev_info, 0, sizeof(dev_info));
	dev_info.MaxRDMBufferSize = BUFFER_4K;
	dev_info.u32DSDStartOffset = EEPROM_CALPARAM_START;
	dev_info.u32RxAlignmentCorrection = 0;
	dev_info.u32NVMType = ad->eNVMType;
	dev_info.u32InterfaceType = BCM_USB;

	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	if (io_buff.OutputLength < sizeof(dev_info))
		return -EINVAL;

	if (copy_to_user(io_buff.OutputBuffer, &dev_info, sizeof(dev_info)))
		return -EFAULT;

	return STATUS_SUCCESS;
}

static int bcm_char_ioctl_time_since_net_entry(void __user *argp,
	struct bcm_mini_adapter *ad)
{
	struct bcm_time_elapsed time_elapsed_since_net_entry = {0};
	struct bcm_ioctl_buffer io_buff;

	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"IOCTL_BCM_TIME_SINCE_NET_ENTRY called");

	if (copy_from_user(&io_buff, argp, sizeof(struct bcm_ioctl_buffer)))
		return -EFAULT;

	if (io_buff.OutputLength < sizeof(struct bcm_time_elapsed))
		return -EINVAL;

	time_elapsed_since_net_entry.ul64TimeElapsedSinceNetEntry =
		get_seconds() - ad->liTimeSinceLastNetEntry;

	if (copy_to_user(io_buff.OutputBuffer, &time_elapsed_since_net_entry,
			 sizeof(struct bcm_time_elapsed)))
		return -EFAULT;

	return STATUS_SUCCESS;
}


static long bcm_char_ioctl(struct file *filp, UINT cmd, ULONG arg)
{
	struct bcm_tarang_data *tarang = filp->private_data;
	void __user *argp = (void __user *)arg;
	struct bcm_mini_adapter *ad = tarang->Adapter;
	INT status = STATUS_FAILURE;

	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
			"Parameters Passed to control IOCTL cmd=0x%X arg=0x%lX",
			cmd, arg);

	if (_IOC_TYPE(cmd) != BCM_IOCTL)
		return -EFAULT;
	if (_IOC_DIR(cmd) & _IOC_READ)
		status = !access_ok(VERIFY_WRITE, argp, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		status = !access_ok(VERIFY_READ, argp, _IOC_SIZE(cmd));
	else if (_IOC_NONE == (_IOC_DIR(cmd) & _IOC_NONE))
		status = STATUS_SUCCESS;

	if (status)
		return -EFAULT;

	if (ad->device_removed)
		return -EFAULT;

	if (false == ad->fw_download_done) {
		switch (cmd) {
		case IOCTL_MAC_ADDR_REQ:
		case IOCTL_LINK_REQ:
		case IOCTL_CM_REQUEST:
		case IOCTL_SS_INFO_REQ:
		case IOCTL_SEND_CONTROL_MESSAGE:
		case IOCTL_IDLE_REQ:
		case IOCTL_BCM_GPIO_SET_REQUEST:
		case IOCTL_BCM_GPIO_STATUS_REQUEST:
			return -EACCES;
		default:
			break;
		}
	}

	status = vendorextnIoctl(ad, cmd, arg);
	if (status != CONTINUE_COMMON_PATH)
		return status;

	switch (cmd) {
	/* Rdms for Swin Idle... */
	case IOCTL_BCM_REGISTER_READ_PRIVATE:
		status = bcm_char_ioctl_reg_read_private(argp, ad);
		return status;

	case IOCTL_BCM_REGISTER_WRITE_PRIVATE:
		status = bcm_char_ioctl_reg_write_private(argp, ad);
		return status;

	case IOCTL_BCM_REGISTER_READ:
	case IOCTL_BCM_EEPROM_REGISTER_READ:
		status = bcm_char_ioctl_eeprom_reg_read(argp, ad);
		return status;

	case IOCTL_BCM_REGISTER_WRITE:
	case IOCTL_BCM_EEPROM_REGISTER_WRITE:
		status = bcm_char_ioctl_eeprom_reg_write(argp, ad, cmd);
		return status;

	case IOCTL_BCM_GPIO_SET_REQUEST:
		status = bcm_char_ioctl_gpio_set_request(argp, ad);
		return status;

	case BCM_LED_THREAD_STATE_CHANGE_REQ:
		status = bcm_char_ioctl_led_thread_state_change_req(argp,
								    ad);
		return status;

	case IOCTL_BCM_GPIO_STATUS_REQUEST:
		status = bcm_char_ioctl_gpio_status_request(argp, ad);
		return status;

	case IOCTL_BCM_GPIO_MULTI_REQUEST:
		status = bcm_char_ioctl_gpio_multi_request(argp, ad);
		return status;

	case IOCTL_BCM_GPIO_MODE_REQUEST:
		status = bcm_char_ioctl_gpio_mode_request(argp, ad);
		return status;

	case IOCTL_MAC_ADDR_REQ:
	case IOCTL_LINK_REQ:
	case IOCTL_CM_REQUEST:
	case IOCTL_SS_INFO_REQ:
	case IOCTL_SEND_CONTROL_MESSAGE:
	case IOCTL_IDLE_REQ:
		status = bcm_char_ioctl_misc_request(argp, ad);
		return status;

	case IOCTL_BCM_BUFFER_DOWNLOAD_START:
		status = bcm_char_ioctl_buffer_download_start(ad);
		return status;

	case IOCTL_BCM_BUFFER_DOWNLOAD:
		status = bcm_char_ioctl_buffer_download(argp, ad);
		return status;

	case IOCTL_BCM_BUFFER_DOWNLOAD_STOP:
		status = bcm_char_ioctl_buffer_download_stop(argp, ad);
		return status;


	case IOCTL_BE_BUCKET_SIZE:
		status = 0;
		if (get_user(ad->BEBucketSize,
			     (unsigned long __user *)arg))
			status = -EFAULT;
		break;

	case IOCTL_RTPS_BUCKET_SIZE:
		status = 0;
		if (get_user(ad->rtPSBucketSize,
			     (unsigned long __user *)arg))
			status = -EFAULT;
		break;

	case IOCTL_CHIP_RESET:
		status = bcm_char_ioctl_chip_reset(ad);
		return status;

	case IOCTL_QOS_THRESHOLD:
		status = bcm_char_ioctl_qos_threshold(arg, ad);
		return status;

	case IOCTL_DUMP_PACKET_INFO:
		DumpPackInfo(ad);
		DumpPhsRules(&ad->stBCMPhsContext);
		status = STATUS_SUCCESS;
		break;

	case IOCTL_GET_PACK_INFO:
		if (copy_to_user(argp, &ad->PackInfo,
				 sizeof(struct bcm_packet_info)*NO_OF_QUEUES))
			return -EFAULT;
		status = STATUS_SUCCESS;
		break;

	case IOCTL_BCM_SWITCH_TRANSFER_MODE:
		status = bcm_char_ioctl_switch_transfer_mode(argp, ad);
		return status;

	case IOCTL_BCM_GET_DRIVER_VERSION:
		status = bcm_char_ioctl_get_driver_version(argp);
		return status;

	case IOCTL_BCM_GET_CURRENT_STATUS:
		status = bcm_char_ioctl_get_current_status(argp, ad);
		return status;

	case IOCTL_BCM_SET_MAC_TRACING:
		status = bcm_char_ioctl_set_mac_tracing(argp, ad);
		return status;

	case IOCTL_BCM_GET_DSX_INDICATION:
		status = bcm_char_ioctl_get_dsx_indication(argp, ad);
		return status;

	case IOCTL_BCM_GET_HOST_MIBS:
		status = bcm_char_ioctl_get_host_mibs(argp, ad, tarang);
		return status;

	case IOCTL_BCM_WAKE_UP_DEVICE_FROM_IDLE:
		if ((false == ad->bTriedToWakeUpFromlowPowerMode) &&
				(TRUE == ad->IdleMode)) {
			ad->usIdleModePattern = ABORT_IDLE_MODE;
			ad->bWakeUpDevice = TRUE;
			wake_up(&ad->process_rx_cntrlpkt);
		}

		status = STATUS_SUCCESS;
		break;

	case IOCTL_BCM_BULK_WRM:
		status = bcm_char_ioctl_bulk_wrm(argp, ad, cmd);
		return status;

	case IOCTL_BCM_GET_NVM_SIZE:
		status = bcm_char_ioctl_get_nvm_size(argp, ad);
		return status;

	case IOCTL_BCM_CAL_INIT:
		status = bcm_char_ioctl_cal_init(argp, ad);
		return status;

	case IOCTL_BCM_SET_DEBUG:
		status = bcm_char_ioctl_set_debug(argp, ad);
		return status;

	case IOCTL_BCM_NVM_READ:
	case IOCTL_BCM_NVM_WRITE:
		status = bcm_char_ioctl_nvm_rw(argp, ad, cmd);
		return status;

	case IOCTL_BCM_FLASH2X_SECTION_READ:
		status = bcm_char_ioctl_flash2x_section_read(argp, ad);
		return status;

	case IOCTL_BCM_FLASH2X_SECTION_WRITE:
		status = bcm_char_ioctl_flash2x_section_write(argp, ad);
		return status;

	case IOCTL_BCM_GET_FLASH2X_SECTION_BITMAP:
		status = bcm_char_ioctl_flash2x_section_bitmap(argp, ad);
		return status;

	case IOCTL_BCM_SET_ACTIVE_SECTION:
		status = bcm_char_ioctl_set_active_section(argp, ad);
		return status;

	case IOCTL_BCM_IDENTIFY_ACTIVE_SECTION:
		/* Right Now we are taking care of only DSD */
		ad->bAllDSDWriteAllow = false;
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
				"IOCTL_BCM_IDENTIFY_ACTIVE_SECTION called");
		status = STATUS_SUCCESS;
		break;

	case IOCTL_BCM_COPY_SECTION:
		status = bcm_char_ioctl_copy_section(argp, ad);
		return status;

	case IOCTL_BCM_GET_FLASH_CS_INFO:
		status = bcm_char_ioctl_get_flash_cs_info(argp, ad);
		return status;

	case IOCTL_BCM_SELECT_DSD:
		status = bcm_char_ioctl_select_dsd(argp, ad);
		return status;

	case IOCTL_BCM_NVM_RAW_READ:
		status = bcm_char_ioctl_nvm_raw_read(argp, ad);
		return status;

	case IOCTL_BCM_CNTRLMSG_MASK:
		status = bcm_char_ioctl_cntrlmsg_mask(argp, ad, tarang);
		return status;

	case IOCTL_BCM_GET_DEVICE_DRIVER_INFO:
		status = bcm_char_ioctl_get_device_driver_info(argp, ad);
		return status;

	case IOCTL_BCM_TIME_SINCE_NET_ENTRY:
		status = bcm_char_ioctl_time_since_net_entry(argp, ad);
		return status;

	case IOCTL_CLOSE_NOTIFICATION:
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,
				"IOCTL_CLOSE_NOTIFICATION");
		break;

	default:
		pr_info(DRV_NAME ": unknown ioctl cmd=%#x\n", cmd);
		status = STATUS_FAILURE;
		break;
	}
	return status;
}


static const struct file_operations bcm_fops = {
	.owner    = THIS_MODULE,
	.open     = bcm_char_open,
	.release  = bcm_char_release,
	.read     = bcm_char_read,
	.unlocked_ioctl    = bcm_char_ioctl,
	.llseek = no_llseek,
};

int register_control_device_interface(struct bcm_mini_adapter *ad)
{

	if (ad->major > 0)
		return ad->major;

	ad->major = register_chrdev(0, DEV_NAME, &bcm_fops);
	if (ad->major < 0) {
		pr_err(DRV_NAME ": could not created character device\n");
		return ad->major;
	}

	ad->pstCreatedClassDevice = device_create(bcm_class, NULL,
						       MKDEV(ad->major, 0),
						       ad, DEV_NAME);

	if (IS_ERR(ad->pstCreatedClassDevice)) {
		pr_err(DRV_NAME ": class device create failed\n");
		unregister_chrdev(ad->major, DEV_NAME);
		return PTR_ERR(ad->pstCreatedClassDevice);
	}

	return 0;
}

void unregister_control_device_interface(struct bcm_mini_adapter *ad)
{
	if (ad->major > 0) {
		device_destroy(bcm_class, MKDEV(ad->major, 0));
		unregister_chrdev(ad->major, DEV_NAME);
	}
}

