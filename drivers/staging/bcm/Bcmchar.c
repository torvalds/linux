#include <linux/fs.h>

#include "headers.h"
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
static struct class *bcm_class = NULL;
static int bcm_char_open(struct inode *inode, struct file * filp)
{
	PMINI_ADAPTER 		Adapter = NULL;
    PPER_TARANG_DATA 	pTarang = NULL;

	Adapter = GET_BCM_ADAPTER(gblpnetdev);
    pTarang = (PPER_TARANG_DATA)kmalloc(sizeof(PER_TARANG_DATA), GFP_KERNEL);
    if (!pTarang)
        return -ENOMEM;

	memset (pTarang, 0, sizeof(PER_TARANG_DATA));
    pTarang->Adapter = Adapter;
	pTarang->RxCntrlMsgBitMask = 0xFFFFFFFF & ~(1 << 0xB) ;

	down(&Adapter->RxAppControlQueuelock);
    pTarang->next = Adapter->pTarangs;
    Adapter->pTarangs = pTarang;
	up(&Adapter->RxAppControlQueuelock);

	/* Store the Adapter structure */
	filp->private_data = pTarang;

	/*Start Queuing the control response Packets*/
	atomic_inc(&Adapter->ApplicationRunning);

	nonseekable_open(inode, filp);
	return 0;
}
static int bcm_char_release(struct inode *inode, struct file *filp)
{
    PPER_TARANG_DATA pTarang, tmp, ptmp;
	PMINI_ADAPTER Adapter=NULL;
    struct sk_buff * pkt, * npkt;

    pTarang = (PPER_TARANG_DATA)filp->private_data;

    if(pTarang == NULL)
	{
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "ptarang is null\n");
	return 0;
	}

	Adapter = pTarang->Adapter;

    down( &Adapter->RxAppControlQueuelock);

    tmp = Adapter->pTarangs;
    for ( ptmp = NULL; tmp; ptmp = tmp, tmp = tmp->next )
	{
        if ( tmp == pTarang )
			break;
	}

    if ( tmp )
	{
        if ( !ptmp )
            Adapter->pTarangs = tmp->next;
        else
            ptmp->next = tmp->next;
	}

    else
	{
    	up( &Adapter->RxAppControlQueuelock);
	return 0;
	}

    pkt = pTarang->RxAppControlHead;
    while ( pkt )
	{
        npkt = pkt->next;
        kfree_skb(pkt);
        pkt = npkt;
	}

    up( &Adapter->RxAppControlQueuelock);

    /*Stop Queuing the control response Packets*/
    atomic_dec(&Adapter->ApplicationRunning);

    bcm_kfree(pTarang);

	/* remove this filp from the asynchronously notified filp's */
    filp->private_data = NULL;
    return 0;
}

static ssize_t bcm_char_read(struct file *filp, char __user *buf, size_t size, loff_t *f_pos)
{
    PPER_TARANG_DATA pTarang = (PPER_TARANG_DATA)filp->private_data;
	PMINI_ADAPTER	Adapter = pTarang->Adapter;
    struct sk_buff* Packet = NULL;
    UINT            PktLen = 0;
	int 			wait_ret_val=0;

	wait_ret_val = wait_event_interruptible(Adapter->process_read_wait_queue,
		(pTarang->RxAppControlHead || Adapter->device_removed));
	if((wait_ret_val == -ERESTARTSYS))
	{
   		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "Exiting as i've been asked to exit!!!\n");
		return wait_ret_val;
	}

	if(Adapter->device_removed)
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "Device Removed... Killing the Apps...\n");
		return -ENODEV;
	}

	if(FALSE == Adapter->fw_download_done)
		return -EACCES;

    down( &Adapter->RxAppControlQueuelock);

	if(pTarang->RxAppControlHead)
	{
		Packet = pTarang->RxAppControlHead;
		DEQUEUEPACKET(pTarang->RxAppControlHead,pTarang->RxAppControlTail);
		pTarang->AppCtrlQueueLen--;
	}

    up(&Adapter->RxAppControlQueuelock);

	if(Packet)
	{
		PktLen = Packet->len;
		if(copy_to_user(buf, Packet->data, PktLen))
		{
			bcm_kfree_skb(Packet);
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "\nReturning from copy to user failure \n");
			return -EFAULT;
		}
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "Read %d Bytes From Adapter packet = 0x%p by process %d!\n", PktLen, Packet, current->pid);
		bcm_kfree_skb(Packet);
	}

    BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "<====\n");
    return PktLen;
}

static long bcm_char_ioctl(struct file *filp, UINT cmd, ULONG arg)
{
    PPER_TARANG_DATA  pTarang = (PPER_TARANG_DATA)filp->private_data;
	PMINI_ADAPTER 	Adapter = pTarang->Adapter;
	INT  			Status = STATUS_FAILURE;
	IOCTL_BUFFER 	IoBuffer={0};
#ifndef BCM_SHM_INTERFACE
    int timeout = 0;
#endif


	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "Parameters Passed to control IOCTL cmd=0x%X arg=0x%lX", cmd, arg);

	if(_IOC_TYPE(cmd) != BCM_IOCTL)
		return -EFAULT;
	if(_IOC_DIR(cmd) & _IOC_READ)
		Status = !access_ok(VERIFY_WRITE, (PVOID)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
	    Status = !access_ok(VERIFY_READ, (PVOID)arg, _IOC_SIZE(cmd));
	else if (_IOC_NONE == (_IOC_DIR(cmd) & _IOC_NONE))
	    Status = STATUS_SUCCESS;

	if(Status)
		return -EFAULT;

	if(Adapter->device_removed)
	{
		return -EFAULT;
	}

	if(FALSE == Adapter->fw_download_done)
	{
		switch (cmd)
		{
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

	Status = vendorextnIoctl(Adapter, cmd, arg);
	if(Status != CONTINUE_COMMON_PATH )
	{
		 return Status;
	}

	switch(cmd){
		// Rdms for Swin Idle...
		case IOCTL_BCM_REGISTER_READ_PRIVATE:
		{
			RDM_BUFFER  sRdmBuffer = {0};
			PCHAR temp_buff = NULL;
			UINT Bufflen = 0;
			/* Copy Ioctl Buffer structure */
			if(copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg,
				sizeof(IOCTL_BUFFER)))
			{
				Status = -EFAULT;
				break;
			}

			Bufflen = IoBuffer.OutputLength + (4 - IoBuffer.OutputLength%4)%4;
			temp_buff = (PCHAR)kmalloc(Bufflen, GFP_KERNEL);
			if(!temp_buff)
			{
				return STATUS_FAILURE;
			}
			if(copy_from_user(&sRdmBuffer, IoBuffer.InputBuffer,
				IoBuffer.InputLength))
			{
				Status = -EFAULT;
				break;
			}
			Status = rdmalt(Adapter, (UINT)sRdmBuffer.Register,
					(PUINT)temp_buff, Bufflen);
			if(Status != STATUS_SUCCESS)
			{
				bcm_kfree(temp_buff);
				return Status;
			}
			if(copy_to_user((PCHAR)IoBuffer.OutputBuffer,
				(PCHAR)temp_buff, (UINT)IoBuffer.OutputLength))
			{
				Status = -EFAULT;
			}
			bcm_kfree(temp_buff);
			break;
		}
		case IOCTL_BCM_REGISTER_WRITE_PRIVATE:
		{
			WRM_BUFFER  sWrmBuffer = {0};
			UINT uiTempVar=0;
			/* Copy Ioctl Buffer structure */

			if(copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg,
				sizeof(IOCTL_BUFFER)))
			{
				Status = -EFAULT;
				break;
			}
			/* Get WrmBuffer structure */
			if(copy_from_user(&sWrmBuffer, IoBuffer.InputBuffer,
				IoBuffer.InputLength))
			{
				Status = -EFAULT;
				break;
			}
			uiTempVar = sWrmBuffer.Register & EEPROM_REJECT_MASK;
			if(!((Adapter->pstargetparams->m_u32Customize) & VSG_MODE) &&
			 	((uiTempVar == EEPROM_REJECT_REG_1)||
				(uiTempVar == EEPROM_REJECT_REG_2) ||
				(uiTempVar == EEPROM_REJECT_REG_3) ||
				(uiTempVar == EEPROM_REJECT_REG_4)))
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "EEPROM Access Denied, not in VSG Mode\n");
				Status = -EFAULT;
				break;
			}
			Status = wrmalt(Adapter, (UINT)sWrmBuffer.Register,
						(PUINT)sWrmBuffer.Data, sizeof(ULONG));
			if(Status == STATUS_SUCCESS)
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"WRM Done\n");
			}
			else
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "WRM Failed\n");
				Status = -EFAULT;
			}
			break;
		}

		case IOCTL_BCM_REGISTER_READ:
		case IOCTL_BCM_EEPROM_REGISTER_READ:
		{
			RDM_BUFFER  sRdmBuffer = {0};
			PCHAR temp_buff = NULL;
			UINT uiTempVar = 0;
			if((Adapter->IdleMode == TRUE) ||
				(Adapter->bShutStatus ==TRUE) ||
				(Adapter->bPreparingForLowPowerMode ==TRUE))
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Device in Idle Mode, Blocking Rdms\n");
				Status = -EACCES;
				break;
			}
			/* Copy Ioctl Buffer structure */
			if(copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg,
				sizeof(IOCTL_BUFFER)))
			{
				Status = -EFAULT;
				break;
			}

			temp_buff = (PCHAR)kmalloc(IoBuffer.OutputLength, GFP_KERNEL);
			if(!temp_buff)
			{
				return STATUS_FAILURE;
			}
			if(copy_from_user(&sRdmBuffer, IoBuffer.InputBuffer,
				IoBuffer.InputLength))
			{
				Status = -EFAULT;
				break;
			}

			if(
#if !defined(BCM_SHM_INTERFACE)
				(((ULONG)sRdmBuffer.Register & 0x0F000000) != 0x0F000000) ||
#endif
					((ULONG)sRdmBuffer.Register & 0x3)
			  )
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "RDM Done On invalid Address : %x Access Denied.\n",
					(int)sRdmBuffer.Register);
				Status = -EINVAL;
				break;
			}

			uiTempVar = sRdmBuffer.Register & EEPROM_REJECT_MASK;
			Status = rdmaltWithLock(Adapter, (UINT)sRdmBuffer.Register,
						(PUINT)temp_buff, IoBuffer.OutputLength);
			if(Status != STATUS_SUCCESS)
			{
				bcm_kfree(temp_buff);
				return Status;
			}
			if(copy_to_user((PCHAR)IoBuffer.OutputBuffer,
				(PCHAR)temp_buff, (UINT)IoBuffer.OutputLength))
			{
				Status = -EFAULT;
			}
			bcm_kfree(temp_buff);
			break;
		}
		case IOCTL_BCM_REGISTER_WRITE:
		case IOCTL_BCM_EEPROM_REGISTER_WRITE:
		{
			WRM_BUFFER  sWrmBuffer = {0};
			UINT uiTempVar=0;
			if((Adapter->IdleMode == TRUE) ||
				(Adapter->bShutStatus ==TRUE) ||
				(Adapter->bPreparingForLowPowerMode ==TRUE))
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Device in Idle Mode, Blocking Wrms\n");
				Status = -EACCES;
				break;
			}
			/* Copy Ioctl Buffer structure */
			if(copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg,
					sizeof(IOCTL_BUFFER)))
			{
				Status = -EFAULT;
				break;
			}
			/* Get WrmBuffer structure */
			if(copy_from_user(&sWrmBuffer, IoBuffer.InputBuffer,
				IoBuffer.InputLength))
			{
				Status = -EFAULT;
				break;
			}
			if(
#if !defined(BCM_SHM_INTERFACE)

				(((ULONG)sWrmBuffer.Register & 0x0F000000) != 0x0F000000) ||
#endif
					((ULONG)sWrmBuffer.Register & 0x3)
			 )
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "WRM Done On invalid Address : %x Access Denied.\n",
						(int)sWrmBuffer.Register);
				Status = -EINVAL;
				break;
			}
			uiTempVar = sWrmBuffer.Register & EEPROM_REJECT_MASK;
			if(!((Adapter->pstargetparams->m_u32Customize) & VSG_MODE) &&
				((uiTempVar == EEPROM_REJECT_REG_1)||
				(uiTempVar == EEPROM_REJECT_REG_2) ||
				(uiTempVar == EEPROM_REJECT_REG_3) ||
				(uiTempVar == EEPROM_REJECT_REG_4)) &&
				(cmd == IOCTL_BCM_REGISTER_WRITE))
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "EEPROM Access Denied, not in VSG Mode\n");
				Status = -EFAULT;
				break;
			}

			Status = wrmaltWithLock(Adapter, (UINT)sWrmBuffer.Register,
							(PUINT)sWrmBuffer.Data, sWrmBuffer.Length);
			if(Status == STATUS_SUCCESS)
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, OSAL_DBG, DBG_LVL_ALL, "WRM Done\n");
			}
			else
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "WRM Failed\n");
				Status = -EFAULT;
			}
			break;
		}
		case IOCTL_BCM_GPIO_SET_REQUEST:
		{
			UCHAR ucResetValue[4];
			UINT value =0;
			UINT uiBit = 0;
	        UINT uiOperation = 0;

			GPIO_INFO   gpio_info = {0};
			if((Adapter->IdleMode == TRUE) ||
				(Adapter->bShutStatus ==TRUE) ||
				(Adapter->bPreparingForLowPowerMode ==TRUE))
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"GPIO Can't be set/clear in Low power Mode");
				Status = -EACCES;
				break;
			}
			if(copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg, sizeof(IOCTL_BUFFER)))
			{
				Status = -EFAULT;
				break;
		    }
			if(copy_from_user(&gpio_info, IoBuffer.InputBuffer, IoBuffer.InputLength))
			{
				Status = -EFAULT;
				break;
			}
			uiBit  = gpio_info.uiGpioNumber;
			uiOperation = gpio_info.uiGpioValue;

			value= (1<<uiBit);

			if(IsReqGpioIsLedInNVM(Adapter,value) ==FALSE)
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "Sorry, Requested GPIO<0x%X> is not correspond to LED !!!",value);
				Status = -EINVAL;
				break;
			}


			if(uiOperation)//Set - setting 1
			{
				//Set the gpio output register
				Status = wrmaltWithLock(Adapter,BCM_GPIO_OUTPUT_SET_REG ,
						(PUINT)(&value), sizeof(UINT));
				if(Status == STATUS_SUCCESS)
				{
               	    BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "Set the GPIO bit\n");
				}
        	    else
		        {
                   	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"Failed to set the %dth GPIO \n",uiBit);
                   	break;
               	}
			}
			else//Unset - setting 0
			{
				//Set the gpio output register
				Status = wrmaltWithLock(Adapter,BCM_GPIO_OUTPUT_CLR_REG ,
						(PUINT)(&value), sizeof(UINT));
				if(Status == STATUS_SUCCESS)
				{
               	    BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"Set the GPIO bit\n");
				}
        	    else
		        {
                   	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"Failed to clear the %dth GPIO \n",uiBit);
                   	break;
               	}
			}

			Status = rdmaltWithLock(Adapter, (UINT)GPIO_MODE_REGISTER,
					(PUINT)ucResetValue, sizeof(UINT));
			if (STATUS_SUCCESS != Status)
            {
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"GPIO_MODE_REGISTER read failed");
				break;
			}
			//Set the gpio mode register to output
			*(UINT*)ucResetValue |= (1<<uiBit);
			Status = wrmaltWithLock(Adapter,GPIO_MODE_REGISTER ,
					(PUINT)ucResetValue, sizeof(UINT));
			if(Status == STATUS_SUCCESS)
			{
            	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"Set the GPIO to output Mode\n");
			}
            else
            {
            	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "Failed to put GPIO in Output Mode\n");
                break;
            }
		}
		break;
		case BCM_LED_THREAD_STATE_CHANGE_REQ:
		{

			USER_THREAD_REQ threadReq = {0};
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"User made LED thread InActive");

			if((Adapter->IdleMode == TRUE) ||
				(Adapter->bShutStatus ==TRUE) ||
				(Adapter->bPreparingForLowPowerMode ==TRUE))
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"GPIO Can't be set/clear in Low power Mode");
				Status = -EACCES;
				break;
			}
			Status =copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg, sizeof(IOCTL_BUFFER));
			if(Status)
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Failed while copying the IOBufer from user space err:%d",Status);
				break;
			}

			Status= copy_from_user(&threadReq, IoBuffer.InputBuffer, IoBuffer.InputLength);
			if(Status)
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Failed while copying the InputBuffer from user space err:%d",Status);
				break;
			}
			//if LED thread is running(Actively or Inactively) set it state to make inactive
			if(Adapter->LEDInfo.led_thread_running)
			{
				if(threadReq.ThreadState == LED_THREAD_ACTIVATION_REQ)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"Activating thread req");
					Adapter->DriverState = LED_THREAD_ACTIVE;
				}
				else
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"DeActivating Thread req.....");
					Adapter->DriverState = LED_THREAD_INACTIVE;
				}

				//signal thread.
				wake_up(&Adapter->LEDInfo.notify_led_event);

			}
		}
		break;
		case IOCTL_BCM_GPIO_STATUS_REQUEST:
		{
			ULONG uiBit = 0;
			UCHAR ucRead[4];
			GPIO_INFO   gpio_info = {0};
			if((Adapter->IdleMode == TRUE) ||
				(Adapter->bShutStatus ==TRUE) ||
				(Adapter->bPreparingForLowPowerMode ==TRUE))
			{
				Status = -EACCES;
				break;
			}
			if(copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg, sizeof(IOCTL_BUFFER)))
            {
            	Status = -EFAULT;
                    break;
                }
                if(copy_from_user(&gpio_info, IoBuffer.InputBuffer, IoBuffer.InputLength))
                {
                    Status = -EFAULT;
                    break;
                }
                uiBit  = gpio_info.uiGpioNumber;
				  //Set the gpio output register
				Status = rdmaltWithLock(Adapter, (UINT)GPIO_PIN_STATE_REGISTER,
                	(PUINT)ucRead, sizeof(UINT));
                if(Status != STATUS_SUCCESS)
                {
                    BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "RDM Failed\n");
					return Status;
                }

			}
			break;
			case IOCTL_BCM_GPIO_MULTI_REQUEST:
			{
				UCHAR ucResetValue[4];
				GPIO_MULTI_INFO gpio_multi_info[MAX_IDX];
				PGPIO_MULTI_INFO pgpio_multi_info = (PGPIO_MULTI_INFO)gpio_multi_info;

				memset( pgpio_multi_info, 0, MAX_IDX * sizeof( GPIO_MULTI_INFO));

				if((Adapter->IdleMode == TRUE) ||
				(Adapter->bShutStatus ==TRUE) ||
				(Adapter->bPreparingForLowPowerMode ==TRUE))
				{
					Status = -EINVAL;
					break;
				}
				Status = copy_from_user( (PCHAR)&IoBuffer, ( PCHAR)arg, sizeof( IOCTL_BUFFER));
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Failed while copying the IOBufer from user space err:%d",Status);
					break;
				}

				Status = copy_from_user( &gpio_multi_info, IoBuffer.InputBuffer, IoBuffer.InputLength);
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Failed while copying the IOBufer Contents from user space err:%d",Status);
					break;
				}
				if(IsReqGpioIsLedInNVM(Adapter,pgpio_multi_info[WIMAX_IDX].uiGPIOMask)== FALSE)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "Sorry, Requested GPIO<0x%X> is not correspond to NVM LED bit map<0x%X>!!!",pgpio_multi_info[WIMAX_IDX].uiGPIOMask,Adapter->gpioBitMap);
					Status = -EINVAL;
					break;
				}

				/* Set the gpio output register */

				if( ( pgpio_multi_info[WIMAX_IDX].uiGPIOMask) &
					( pgpio_multi_info[WIMAX_IDX].uiGPIOCommand))
				{
					/* Set 1's in GPIO OUTPUT REGISTER */
					*(UINT*) ucResetValue =  pgpio_multi_info[WIMAX_IDX].uiGPIOMask &
					        				 pgpio_multi_info[WIMAX_IDX].uiGPIOCommand &
											 pgpio_multi_info[WIMAX_IDX].uiGPIOValue;

					if( *(UINT*) ucResetValue)
						Status = wrmaltWithLock( Adapter, BCM_GPIO_OUTPUT_SET_REG , (PUINT) ucResetValue, sizeof(ULONG));

					if( Status != STATUS_SUCCESS)
					{
						BCM_DEBUG_PRINT( Adapter,DBG_TYPE_PRINTK, 0, 0,"WRM to BCM_GPIO_OUTPUT_SET_REG Failed.");
						return Status;
					}

					/* Clear to 0's in GPIO OUTPUT REGISTER */
					*(UINT*) ucResetValue = (pgpio_multi_info[WIMAX_IDX].uiGPIOMask &
							pgpio_multi_info[WIMAX_IDX].uiGPIOCommand &
							( ~( pgpio_multi_info[WIMAX_IDX].uiGPIOValue)));

					if( *(UINT*) ucResetValue)
						Status = wrmaltWithLock( Adapter, BCM_GPIO_OUTPUT_CLR_REG , (PUINT) ucResetValue, sizeof(ULONG));

					if( Status != STATUS_SUCCESS)
					{
						BCM_DEBUG_PRINT( Adapter,DBG_TYPE_PRINTK, 0, 0,"WRM to BCM_GPIO_OUTPUT_CLR_REG Failed." );
						return Status;
					}
				}

				if( pgpio_multi_info[WIMAX_IDX].uiGPIOMask)
				{
					Status = rdmaltWithLock(Adapter, (UINT)GPIO_PIN_STATE_REGISTER, (PUINT)ucResetValue, sizeof(UINT));

					if(Status != STATUS_SUCCESS)
					{
						BCM_DEBUG_PRINT( Adapter,DBG_TYPE_PRINTK, 0, 0,"RDM to GPIO_PIN_STATE_REGISTER Failed.");
						return Status;
					}

					pgpio_multi_info[WIMAX_IDX].uiGPIOValue = ( *(UINT*)ucResetValue &
											pgpio_multi_info[WIMAX_IDX].uiGPIOMask);
				}

				Status = copy_to_user( (PCHAR)IoBuffer.OutputBuffer, &gpio_multi_info, IoBuffer.OutputLength);
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Failed while copying Content to IOBufer for user space err:%d",Status);
					break;
				}
			}
			break;
		case IOCTL_BCM_GPIO_MODE_REQUEST:
		{
			UCHAR ucResetValue[4];
			GPIO_MULTI_MODE gpio_multi_mode[MAX_IDX];
			PGPIO_MULTI_MODE pgpio_multi_mode = ( PGPIO_MULTI_MODE) gpio_multi_mode;

			if((Adapter->IdleMode == TRUE) ||
				(Adapter->bShutStatus ==TRUE) ||
				(Adapter->bPreparingForLowPowerMode ==TRUE))
			{
					Status = -EINVAL;
					break;
			}
			Status = copy_from_user( (PCHAR)&IoBuffer, ( PCHAR)arg, sizeof( IOCTL_BUFFER));
			if(Status)
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Failed while copying the IOBufer from user space err:%d",Status);
				break;
			}

			Status = copy_from_user( &gpio_multi_mode, IoBuffer.InputBuffer, IoBuffer.InputLength);
			if(Status)
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Failed while copying the IOBufer Contents from user space err:%d",Status);
				break;
			}

			Status = rdmaltWithLock( Adapter, ( UINT) GPIO_MODE_REGISTER, ( PUINT) ucResetValue, sizeof( UINT));
			if( STATUS_SUCCESS != Status)
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Read of GPIO_MODE_REGISTER failed");
				return Status;
			}

			//Validating the request
			if(IsReqGpioIsLedInNVM(Adapter,pgpio_multi_mode[WIMAX_IDX].uiGPIOMask)== FALSE)
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "Sorry, Requested GPIO<0x%X> is not correspond to NVM LED bit map<0x%X>!!!",pgpio_multi_mode[WIMAX_IDX].uiGPIOMask,Adapter->gpioBitMap);
				Status = -EINVAL;
				break;
			}

			if( pgpio_multi_mode[WIMAX_IDX].uiGPIOMask)
			{
				/* write all OUT's (1's) */
				*( UINT*) ucResetValue |= ( pgpio_multi_mode[WIMAX_IDX].uiGPIOMode &
								pgpio_multi_mode[WIMAX_IDX].uiGPIOMask);
				/* write all IN's (0's) */
				*( UINT*) ucResetValue &= ~( ( ~pgpio_multi_mode[WIMAX_IDX].uiGPIOMode) &
								pgpio_multi_mode[WIMAX_IDX].uiGPIOMask);

				/* Currently implemented return the modes of all GPIO's
				 * else needs to bit AND with  mask
				 * */
				pgpio_multi_mode[WIMAX_IDX].uiGPIOMode = *(UINT*)ucResetValue;

				Status = wrmaltWithLock( Adapter, GPIO_MODE_REGISTER , ( PUINT) ucResetValue, sizeof( ULONG));
				if( Status == STATUS_SUCCESS)
				{
					BCM_DEBUG_PRINT( Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "WRM to GPIO_MODE_REGISTER Done");
				}
				else
				{
					BCM_DEBUG_PRINT( Adapter,DBG_TYPE_PRINTK, 0, 0,"WRM to GPIO_MODE_REGISTER Failed");
					Status = -EFAULT;
					break;
				}
			}
			else /* if uiGPIOMask is 0 then return mode register configuration */
			{
				pgpio_multi_mode[WIMAX_IDX].uiGPIOMode = *( UINT*) ucResetValue;
			}
			Status = copy_to_user( (PCHAR)IoBuffer.OutputBuffer, &gpio_multi_mode, IoBuffer.OutputLength);
			if(Status)
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Failed while copying Content to IOBufer for user space err:%d",Status);
				break;
			}
		}
		break;

		case IOCTL_MAC_ADDR_REQ:
		case IOCTL_LINK_REQ:
		case IOCTL_CM_REQUEST:
		case IOCTL_SS_INFO_REQ:
		case IOCTL_SEND_CONTROL_MESSAGE:
		case IOCTL_IDLE_REQ:
		{
			PVOID pvBuffer=NULL;
			/* Copy Ioctl Buffer structure */
			if(copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg,
							sizeof(IOCTL_BUFFER)))
			{
				Status = -EFAULT;
				break;
			}
			pvBuffer=kmalloc(IoBuffer.InputLength, GFP_KERNEL);
			if(!pvBuffer)
			{
				return -ENOMEM;
			}

			if(copy_from_user(pvBuffer, IoBuffer.InputBuffer,
					IoBuffer.InputLength))
			{
				Status = -EFAULT;
				bcm_kfree(pvBuffer);
				break;
			}

			down(&Adapter->LowPowerModeSync);
			Status = wait_event_interruptible_timeout(Adapter->lowpower_mode_wait_queue,
													!Adapter->bPreparingForLowPowerMode,
													(1 * HZ));
			if(Status == -ERESTARTSYS)
					goto cntrlEnd;

			if(Adapter->bPreparingForLowPowerMode)
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "Preparing Idle Mode is still True - Hence Rejecting control message\n");
				Status = STATUS_FAILURE ;
				goto cntrlEnd ;
			}
			Status = CopyBufferToControlPacket(Adapter, (PVOID)pvBuffer);
		cntrlEnd:
			up(&Adapter->LowPowerModeSync);
			bcm_kfree(pvBuffer);
			break;
		}
#ifndef BCM_SHM_INTERFACE
		case IOCTL_BCM_BUFFER_DOWNLOAD_START:
		{
			INT NVMAccess = down_trylock(&Adapter->NVMRdmWrmLock) ;
			if(NVMAccess)
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, " IOCTL_BCM_CHIP_RESET not allowed as EEPROM Read/Write is in progress\n");
				return -EACCES;
			}
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Starting the firmware download PID =0x%x!!!!\n", current->pid);
		    if(!down_trylock(&Adapter->fw_download_sema))
			{
				Adapter->bBinDownloaded=FALSE;
				Adapter->fw_download_process_pid=current->pid;
				Adapter->bCfgDownloaded=FALSE;
				Adapter->fw_download_done=FALSE;
				netif_carrier_off(Adapter->dev);
				netif_stop_queue(Adapter->dev);
				Status = reset_card_proc(Adapter);
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "reset_card_proc Failed!\n");
					up(&Adapter->fw_download_sema);
					up(&Adapter->NVMRdmWrmLock);
					break;
				}
				mdelay(10);
			}
			else
			{

				Status = -EBUSY;

			}
			up(&Adapter->NVMRdmWrmLock);
			break;
		}
		case IOCTL_BCM_BUFFER_DOWNLOAD:
			{
				FIRMWARE_INFO 	*psFwInfo=NULL;
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Starting the firmware download PID =0x%x!!!!\n", current->pid);
			do{
				if(!down_trylock(&Adapter->fw_download_sema))
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Invalid way to download buffer. Use Start and then call this!!!\n");
					Status=-EINVAL;
					break;
				}
				/* Copy Ioctl Buffer structure */
				if(copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg,
						sizeof(IOCTL_BUFFER)))
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "copy_from_user 1 failed\n");
					Status = -EFAULT;
					break;
				}
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Length for FW DLD is : %lx\n",
										IoBuffer.InputLength);
				psFwInfo=kmalloc(sizeof(*psFwInfo), GFP_KERNEL);
				if(!psFwInfo)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Failed to allocate buffer!!!!\n");
					Status = -ENOMEM;
					break;
				}
				if(copy_from_user(psFwInfo, IoBuffer.InputBuffer,
							IoBuffer.InputLength))
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Copy_from_user 2 failed\n");
					Status = -EFAULT;
					break;
				}

				if(!psFwInfo->pvMappedFirmwareAddress ||
						(psFwInfo->u32FirmwareLength == 0))
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Something else is wrong %lu\n",
					psFwInfo->u32FirmwareLength);
					Status = -EINVAL;
					break;
				}
				Status = bcm_ioctl_fw_download(Adapter, psFwInfo);
				if(Status != STATUS_SUCCESS)
				{
					if(psFwInfo->u32StartingAddress==CONFIG_BEGIN_ADDR)
					{
						BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "IOCTL: Configuration File Upload Failed\n");
					}
					else
					{
						BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "IOCTL: Firmware File Upload Failed\n");
					}
					//up(&Adapter->fw_download_sema);

					if(Adapter->LEDInfo.led_thread_running & BCM_LED_THREAD_RUNNING_ACTIVELY)
					{
						Adapter->DriverState = DRIVER_INIT;
						Adapter->LEDInfo.bLedInitDone = FALSE;
						wake_up(&Adapter->LEDInfo.notify_led_event);
					}
				}
				break ;
			  }while(0);

			  if(Status != STATUS_SUCCESS)
					up(&Adapter->fw_download_sema);
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, OSAL_DBG, DBG_LVL_ALL, "IOCTL: Firmware File Uploaded\n");
				bcm_kfree(psFwInfo);
				break;
			}
		case IOCTL_BCM_BUFFER_DOWNLOAD_STOP:
		{
			INT NVMAccess = down_trylock(&Adapter->NVMRdmWrmLock);
			if(NVMAccess)
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, " FW download blocked as EEPROM Read/Write is in progress\n");
				up(&Adapter->fw_download_sema);
				return -EACCES;
			}
			if(down_trylock(&Adapter->fw_download_sema))
			{
				Adapter->bBinDownloaded=TRUE;
				Adapter->bCfgDownloaded=TRUE;
				atomic_set(&Adapter->CurrNumFreeTxDesc, 0);
				atomic_set(&Adapter->RxRollOverCount, 0);
				Adapter->CurrNumRecvDescs=0;
				Adapter->downloadDDR = 0;

				//setting the Mips to Run
				Status = run_card_proc(Adapter);
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Firm Download Failed\n");
					up(&Adapter->fw_download_sema);
					up(&Adapter->NVMRdmWrmLock);
					break;
				}
				else
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "Firm Download Over...\n");
				mdelay(10);
				/* Wait for MailBox Interrupt */
				if(StartInterruptUrb((PS_INTERFACE_ADAPTER)Adapter->pvInterfaceAdapter))
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Unable to send interrupt...\n");
				}
				timeout = 5*HZ;
				Adapter->waiting_to_fw_download_done = FALSE;
				wait_event_timeout(Adapter->ioctl_fw_dnld_wait_queue,
					Adapter->waiting_to_fw_download_done, timeout);
				Adapter->fw_download_process_pid=INVALID_PID;
				Adapter->fw_download_done=TRUE;
				atomic_set(&Adapter->CurrNumFreeTxDesc, 0);
				Adapter->CurrNumRecvDescs = 0;
				Adapter->PrevNumRecvDescs = 0;
				atomic_set(&Adapter->cntrlpktCnt,0);
                Adapter->LinkUpStatus = 0;
                Adapter->LinkStatus = 0;

				if(Adapter->LEDInfo.led_thread_running & BCM_LED_THREAD_RUNNING_ACTIVELY)
				{
					Adapter->DriverState = FW_DOWNLOAD_DONE;
					wake_up(&Adapter->LEDInfo.notify_led_event);
				}

				if(!timeout)
				{
					Status = -ENODEV;
				}
			}
			else
			{
			   	Status = -EINVAL;
			}
			up(&Adapter->fw_download_sema);
			up(&Adapter->NVMRdmWrmLock);
			break;
		}
#endif
		case IOCTL_BE_BUCKET_SIZE:
			Adapter->BEBucketSize = *(PULONG)arg;
			Status = STATUS_SUCCESS;
			break;

		case IOCTL_RTPS_BUCKET_SIZE:
			Adapter->rtPSBucketSize = *(PULONG)arg;
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_CHIP_RESET:
	    {
			INT NVMAccess = down_trylock(&Adapter->NVMRdmWrmLock);
			if(NVMAccess)
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, " IOCTL_BCM_CHIP_RESET not allowed as EEPROM Read/Write is in progress\n");
				return -EACCES;
			}
			down(&Adapter->RxAppControlQueuelock);
			Status = reset_card_proc(Adapter);
			flushAllAppQ();
			up(&Adapter->RxAppControlQueuelock);
			up(&Adapter->NVMRdmWrmLock);
			ResetCounters(Adapter);
			break;
		}
		case IOCTL_QOS_THRESHOLD:
		{
			USHORT uiLoopIndex;
			for(uiLoopIndex = 0 ; uiLoopIndex < NO_OF_QUEUES ; uiLoopIndex++)
			{
				Adapter->PackInfo[uiLoopIndex].uiThreshold = *(PULONG)arg;
			}
			Status = STATUS_SUCCESS;
			break;
		}

		case IOCTL_DUMP_PACKET_INFO:

			DumpPackInfo(Adapter);
         	DumpPhsRules(&Adapter->stBCMPhsContext);
			Status = STATUS_SUCCESS;
			break;

		case IOCTL_GET_PACK_INFO:
			if(copy_to_user((PCHAR)arg, &Adapter->PackInfo,
				sizeof(PacketInfo)*NO_OF_QUEUES))
			{
				Status = -EFAULT;
				break;
			}
			Status = STATUS_SUCCESS;
			break;
		case IOCTL_BCM_SWITCH_TRANSFER_MODE:
		{
			UINT uiData = 0;
			if(copy_from_user(&uiData, (PUINT)arg, sizeof(UINT)))
			{
				Status = -EFAULT;
				break;
			}
			if(uiData)	/* Allow All Packets */
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "IOCTL_BCM_SWITCH_TRANSFER_MODE: ETH_PACKET_TUNNELING_MODE\n");
				Adapter->TransferMode = ETH_PACKET_TUNNELING_MODE;
			}
			else	/* Allow IP only Packets */
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "IOCTL_BCM_SWITCH_TRANSFER_MODE: IP_PACKET_ONLY_MODE\n");
				Adapter->TransferMode = IP_PACKET_ONLY_MODE;
			}
			Status = STATUS_SUCCESS;
			break;
		}

		case IOCTL_BCM_GET_DRIVER_VERSION:
		{
			/* Copy Ioctl Buffer structure */
			if(copy_from_user((PCHAR)&IoBuffer,
					(PCHAR)arg, sizeof(IOCTL_BUFFER)))
			{
				Status = -EFAULT;
				break;
			}
			if(copy_to_user((PUCHAR)IoBuffer.OutputBuffer,
				VER_FILEVERSION_STR, (UINT)IoBuffer.OutputLength))
			{
				Status = -EFAULT;
				break;
			}
			Status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_BCM_GET_CURRENT_STATUS:
		{
			LINK_STATE *plink_state = NULL;
			/* Copy Ioctl Buffer structure */
			if(copy_from_user((PCHAR)&IoBuffer,
					(PCHAR)arg, sizeof(IOCTL_BUFFER)))
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "copy_from_user failed..\n");
				Status = -EFAULT;
				break;
			}
			plink_state = (LINK_STATE*)arg;
			plink_state->bIdleMode = (UCHAR)Adapter->IdleMode;
			plink_state->bShutdownMode = Adapter->bShutStatus;
			plink_state->ucLinkStatus = (UCHAR)Adapter->LinkStatus;
			if(copy_to_user((PUCHAR)IoBuffer.OutputBuffer,
				(PUCHAR)plink_state, (UINT)IoBuffer.OutputLength))
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Copy_to_user Failed..\n");
				Status = -EFAULT;
				break;
			}
			Status = STATUS_SUCCESS;
			break;
		}
        case IOCTL_BCM_SET_MAC_TRACING:
        {
            UINT  tracing_flag;
            /* copy ioctl Buffer structure */
			if(copy_from_user((PCHAR)&IoBuffer,
				(PCHAR)arg, sizeof(IOCTL_BUFFER)))
			{
				Status = -EFAULT;
				break;
			}
			if(copy_from_user((PCHAR)&tracing_flag,
                     (PCHAR)IoBuffer.InputBuffer,sizeof(UINT)))
            {
				Status = -EFAULT;
				break;
			}
            if (tracing_flag)
                Adapter->pTarangs->MacTracingEnabled = TRUE;
            else
                Adapter->pTarangs->MacTracingEnabled = FALSE;
            break;
        }
		case IOCTL_BCM_GET_DSX_INDICATION:
		{
			ULONG ulSFId=0;
			if(copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg,
					sizeof(IOCTL_BUFFER)))
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Invalid IO buffer!!!" );
				Status = -EFAULT;
				break;
			}
			if(IoBuffer.OutputLength < sizeof(stLocalSFAddIndicationAlt))
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Mismatch req: %lx needed is =0x%zx!!!",
					IoBuffer.OutputLength, sizeof(stLocalSFAddIndicationAlt));
				return -EINVAL;
			}
			if(copy_from_user((PCHAR)&ulSFId, (PCHAR)IoBuffer.InputBuffer,
					sizeof(ulSFId)))
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Invalid SFID!!! %lu", ulSFId );
				Status = -EFAULT;
				break;
			}
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "Get DSX Data SF ID is =%lx\n", ulSFId );
			get_dsx_sf_data_to_application(Adapter, ulSFId,
				IoBuffer.OutputBuffer);
			Status=STATUS_SUCCESS;
		}
		break;
		case IOCTL_BCM_GET_HOST_MIBS:
		{
			PCHAR temp_buff;

			if(copy_from_user((PCHAR)&IoBuffer,
					(PCHAR)arg, sizeof(IOCTL_BUFFER)))
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Copy_from user for IoBuff failed\n");
				Status = -EFAULT;
				break;
			}

			if(IoBuffer.OutputLength != sizeof(S_MIBS_HOST_STATS_MIBS))
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Length Check failed %lu %zd\n", IoBuffer.OutputLength,
											sizeof(S_MIBS_HOST_STATS_MIBS));
	          	return -EINVAL;
			}

			temp_buff = (PCHAR)kmalloc(IoBuffer.OutputLength, GFP_KERNEL);

			if(!temp_buff)
			{
				return STATUS_FAILURE;
			}

			Status = ProcessGetHostMibs(Adapter,
					(PUCHAR)temp_buff, IoBuffer.OutputLength);

	        Status = GetDroppedAppCntrlPktMibs((PVOID)temp_buff,
									(PPER_TARANG_DATA)filp->private_data);

			if(copy_to_user((PCHAR)IoBuffer.OutputBuffer,(PCHAR)temp_buff,
				sizeof(S_MIBS_HOST_STATS_MIBS)))
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Copy to user failed\n");
				bcm_kfree(temp_buff);
				return -EFAULT;
			}

			bcm_kfree(temp_buff);
			break;
		}

		case IOCTL_BCM_WAKE_UP_DEVICE_FROM_IDLE:
			if((FALSE == Adapter->bTriedToWakeUpFromlowPowerMode) && (TRUE==Adapter->IdleMode))
			{
				Adapter->usIdleModePattern = ABORT_IDLE_MODE;
				Adapter->bWakeUpDevice = TRUE;
				wake_up(&Adapter->process_rx_cntrlpkt);
				#if 0
				Adapter->bTriedToWakeUpFromlowPowerMode = TRUE;
				InterfaceAbortIdlemode (Adapter, Adapter->usIdleModePattern);
				#endif
			}
			Status = STATUS_SUCCESS;
			break;

		case IOCTL_BCM_BULK_WRM:
			{
				PBULKWRM_BUFFER pBulkBuffer;
				UINT uiTempVar=0;
				PCHAR pvBuffer = NULL;

				if((Adapter->IdleMode == TRUE) ||
					(Adapter->bShutStatus ==TRUE) ||
					(Adapter->bPreparingForLowPowerMode ==TRUE))
				{
                    BCM_DEBUG_PRINT (Adapter, DBG_TYPE_PRINTK, 0, 0, "Device in Idle/Shutdown Mode, Blocking Wrms\n");
					Status = -EACCES;
					break;
				}
				/* Copy Ioctl Buffer structure */
				if(copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg, sizeof(IOCTL_BUFFER)))
				{
					Status = -EFAULT;
					break;
				}

				pvBuffer=kmalloc(IoBuffer.InputLength, GFP_KERNEL);
				if(!pvBuffer)
				{
					return -ENOMEM;
					break;
				}

				/* Get WrmBuffer structure */
                if(copy_from_user(pvBuffer, IoBuffer.InputBuffer, IoBuffer.InputLength))
				{
					bcm_kfree(pvBuffer);
					Status = -EFAULT;
					break;
				}

				pBulkBuffer = (PBULKWRM_BUFFER)pvBuffer;

				if(((ULONG)pBulkBuffer->Register & 0x0F000000) != 0x0F000000 ||
					((ULONG)pBulkBuffer->Register & 0x3))
				{
					bcm_kfree(pvBuffer);
                    BCM_DEBUG_PRINT (Adapter, DBG_TYPE_PRINTK, 0, 0,"WRM Done On invalid Address : %x Access Denied.\n",(int)pBulkBuffer->Register);
					Status = -EINVAL;
					break;
				}


				uiTempVar = pBulkBuffer->Register & EEPROM_REJECT_MASK;
				if(!((Adapter->pstargetparams->m_u32Customize)&VSG_MODE)
				&& 	((uiTempVar == EEPROM_REJECT_REG_1)||
						(uiTempVar == EEPROM_REJECT_REG_2) ||
					(uiTempVar == EEPROM_REJECT_REG_3) ||
					(uiTempVar == EEPROM_REJECT_REG_4)) &&
					(cmd == IOCTL_BCM_REGISTER_WRITE))
				{
					bcm_kfree(pvBuffer);
                    BCM_DEBUG_PRINT (Adapter, DBG_TYPE_PRINTK, 0, 0,"EEPROM Access Denied, not in VSG Mode\n");
					Status = -EFAULT;
					break;
				}

				if(pBulkBuffer->SwapEndian == FALSE)
					Status = wrmWithLock(Adapter, (UINT)pBulkBuffer->Register, (PCHAR)pBulkBuffer->Values, IoBuffer.InputLength - 2*sizeof(ULONG));
				else
					Status = wrmaltWithLock(Adapter, (UINT)pBulkBuffer->Register, (PUINT)pBulkBuffer->Values, IoBuffer.InputLength - 2*sizeof(ULONG));

				if(Status != STATUS_SUCCESS)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "WRM Failed\n");
				}

				bcm_kfree(pvBuffer);
				break;
			}

		case IOCTL_BCM_GET_NVM_SIZE:
			{

			if(copy_from_user((unsigned char *)&IoBuffer,
					(unsigned char *)arg, sizeof(IOCTL_BUFFER)))
			{
				//IOLog("failed NVM first");
				Status = -EFAULT;
				break;
			}
			if(Adapter->eNVMType == NVM_EEPROM || Adapter->eNVMType == NVM_FLASH ) {
				if(copy_to_user(IoBuffer.OutputBuffer,
					(unsigned char *)&Adapter->uiNVMDSDSize, (UINT)sizeof(UINT)))
				{
						Status = -EFAULT;
						return Status;
				}
			}

			Status = STATUS_SUCCESS ;
			}
			break;

		case IOCTL_BCM_CAL_INIT :

			{
				UINT uiSectorSize = 0 ;
				if(Adapter->eNVMType == NVM_FLASH)
				{
					Status = copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg, sizeof(IOCTL_BUFFER));
					if(Status)
					{
						BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Copy From User space failed. status :%d", Status);
						return Status;
					}
					uiSectorSize = *((PUINT)(IoBuffer.InputBuffer));
					if((uiSectorSize < MIN_SECTOR_SIZE) || (uiSectorSize > MAX_SECTOR_SIZE))
					{

						Status = copy_to_user(IoBuffer.OutputBuffer,
									(unsigned char *)&Adapter->uiSectorSize ,
									(UINT)sizeof(UINT));
						if(Status)
						{
								BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Coping the sector size to use space failed. status:%d",Status);
								return Status;
						}
					}
					else
					{
						if(IsFlash2x(Adapter))
						{
							Status = copy_to_user(IoBuffer.OutputBuffer,
									(unsigned char *)&Adapter->uiSectorSize ,
									(UINT)sizeof(UINT));
							if(Status)
							{
									BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Coping the sector size to use space failed. status:%d",Status);
									return Status;
							}

						}
						else
						{
							if((TRUE == Adapter->bShutStatus) ||
							   (TRUE == Adapter->IdleMode))
							{
								BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Device is in Idle/Shutdown Mode\n");
								return -EACCES;
							}

							Adapter->uiSectorSize = uiSectorSize ;
							BcmUpdateSectorSize(Adapter,Adapter->uiSectorSize);
						}
					}
					Status = STATUS_SUCCESS ;
				}
				else
				{
					Status = STATUS_FAILURE;
				}
			}
			break;
        case IOCTL_BCM_SET_DEBUG :
            {
                USER_BCM_DBG_STATE sUserDebugState;

//				BCM_DEBUG_PRINT (Adapter, DBG_TYPE_PRINTK, 0, 0, "Entered the ioctl %x \n", IOCTL_BCM_SET_DEBUG );

				BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "In SET_DEBUG ioctl\n");
				Status = copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg, sizeof(IOCTL_BUFFER));
				if(Status)
				{
					BCM_DEBUG_PRINT (Adapter, DBG_TYPE_PRINTK, 0, 0, "Copy from user failed\n");
					break;
				}
				Status = copy_from_user(&sUserDebugState,(USER_BCM_DBG_STATE *)IoBuffer.InputBuffer, sizeof(USER_BCM_DBG_STATE));
				if(Status)
				{
					BCM_DEBUG_PRINT (Adapter, DBG_TYPE_PRINTK, 0, 0,  "Copy of IoBuffer.InputBuffer failed");
					return Status;
				}

				BCM_DEBUG_PRINT (Adapter, DBG_TYPE_PRINTK, 0, 0, "IOCTL_BCM_SET_DEBUG: OnOff=%d Type = 0x%x ",
				sUserDebugState.OnOff, sUserDebugState.Type);
				//sUserDebugState.Subtype <<= 1;
				sUserDebugState.Subtype = 1 << sUserDebugState.Subtype;
				BCM_DEBUG_PRINT (Adapter, DBG_TYPE_PRINTK, 0, 0, "actual Subtype=0x%x\n", sUserDebugState.Subtype);

				// Update new 'DebugState' in the Adapter
				Adapter->stDebugState.type |= sUserDebugState.Type;
				/* Subtype: A bitmap of 32 bits for Subtype per Type.
				 * Valid indexes in 'subtype' array: 1,2,4,8
				 * corresponding to valid Type values. Hence we can use the 'Type' field
				 * as the index value, ignoring the array entries 0,3,5,6,7 !
				 */
				if (sUserDebugState.OnOff)
					Adapter->stDebugState.subtype[sUserDebugState.Type] |= sUserDebugState.Subtype;
				else
					Adapter->stDebugState.subtype[sUserDebugState.Type] &= ~sUserDebugState.Subtype;

                BCM_SHOW_DEBUG_BITMAP(Adapter);

			}
			break;
		case IOCTL_BCM_NVM_READ:
		case IOCTL_BCM_NVM_WRITE:
			{

				NVM_READWRITE  stNVMReadWrite = {0};
				PUCHAR pReadData = NULL;
				PUCHAR pBuffertobeCopied = NULL;
				ULONG ulDSDMagicNumInUsrBuff = 0 ;
				struct timeval tv0, tv1;
				memset(&tv0,0,sizeof(struct timeval));
				memset(&tv1,0,sizeof(struct timeval));
				if((Adapter->eNVMType == NVM_FLASH) && (Adapter->uiFlashLayoutMajorVersion == 0))
				{
					BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0,"The Flash Control Section is Corrupted. Hence Rejection on NVM Read/Write\n");
					Status = -EFAULT;
					break;
				}

				if(IsFlash2x(Adapter))
				{
					if((Adapter->eActiveDSD != DSD0) &&
						(Adapter->eActiveDSD != DSD1) &&
						(Adapter->eActiveDSD != DSD2))
					{
						BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"No DSD is active..hence NVM Command is blocked");
						return STATUS_FAILURE ;
					}
				}

			/* Copy Ioctl Buffer structure */

				if(copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg, sizeof(IOCTL_BUFFER)))
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"copy_from_user failed\n");
                    Status = -EFAULT;
					break;
				}
				if(IOCTL_BCM_NVM_READ == cmd)
					pBuffertobeCopied = IoBuffer.OutputBuffer;
				else
					pBuffertobeCopied = IoBuffer.InputBuffer;

				if(copy_from_user(&stNVMReadWrite, pBuffertobeCopied,sizeof(NVM_READWRITE)))
				{
					Status = -EFAULT;
					break;
				}

				//
				// Deny the access if the offset crosses the cal area limit.
				//
				if((stNVMReadWrite.uiOffset + stNVMReadWrite.uiNumBytes) > Adapter->uiNVMDSDSize)
				{
				//BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Can't allow access beyond NVM Size: 0x%x 0x%x\n", stNVMReadWrite.uiOffset ,
//							stNVMReadWrite.uiNumBytes);
					Status = STATUS_FAILURE;
					break;
				}

				pReadData =(PCHAR)kmalloc(stNVMReadWrite.uiNumBytes, GFP_KERNEL);

				if(!pReadData)
					return -ENOMEM;

				memset(pReadData,0,stNVMReadWrite.uiNumBytes);

				if(copy_from_user(pReadData, stNVMReadWrite.pBuffer,
							stNVMReadWrite.uiNumBytes))
				{
					Status = -EFAULT;
					bcm_kfree(pReadData);
					break;
				}

				do_gettimeofday(&tv0);
				if(IOCTL_BCM_NVM_READ == cmd)
				{
					down(&Adapter->NVMRdmWrmLock);

					if((Adapter->IdleMode == TRUE) ||
						(Adapter->bShutStatus ==TRUE) ||
						(Adapter->bPreparingForLowPowerMode ==TRUE))
					{
						BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"Device is in Idle/Shutdown Mode\n");
						up(&Adapter->NVMRdmWrmLock);
						bcm_kfree(pReadData);
						return -EACCES;
					}

					Status = BeceemNVMRead(Adapter, (PUINT)pReadData,
						stNVMReadWrite.uiOffset, stNVMReadWrite.uiNumBytes);

					up(&Adapter->NVMRdmWrmLock);

					if(Status != STATUS_SUCCESS)
						{
							bcm_kfree(pReadData);
							return Status;
						}
					if(copy_to_user((PCHAR)stNVMReadWrite.pBuffer,
							(PCHAR)pReadData, (UINT)stNVMReadWrite.uiNumBytes))
						{
							bcm_kfree(pReadData);
							Status = -EFAULT;
						}
				}
				else
				{

					down(&Adapter->NVMRdmWrmLock);

					if((Adapter->IdleMode == TRUE) ||
						(Adapter->bShutStatus ==TRUE) ||
						(Adapter->bPreparingForLowPowerMode ==TRUE))
					{
						BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"Device is in Idle/Shutdown Mode\n");
						up(&Adapter->NVMRdmWrmLock);
						bcm_kfree(pReadData);
						return -EACCES;
					}

					Adapter->bHeaderChangeAllowed = TRUE ;
					if(IsFlash2x(Adapter))
					{
						/*
							New Requirement:-
							DSD section updation will be allowed in two case:-
							1.  if DSD sig is present in DSD header means dongle is ok and updation is fruitfull
							2.  if point 1 failes then user buff should have DSD sig. this point ensures that if dongle is
							      corrupted then user space program first modify the DSD header with valid DSD sig so
							      that this as well as further write may be worthwhile.

							 This restriction has been put assuming that if DSD sig is corrupted, DSD
							 data won't be considered valid.


						*/
						Status = BcmFlash2xCorruptSig(Adapter,Adapter->eActiveDSD);
						if(Status != STATUS_SUCCESS)
						{
							if(( (stNVMReadWrite.uiOffset + stNVMReadWrite.uiNumBytes) != Adapter->uiNVMDSDSize ) ||
								(stNVMReadWrite.uiNumBytes < SIGNATURE_SIZE))
							{
								BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"DSD Sig is present neither in Flash nor User provided Input..");
								up(&Adapter->NVMRdmWrmLock);
								bcm_kfree(pReadData);
								return Status;
							}

							ulDSDMagicNumInUsrBuff = ntohl(*(PUINT)(pReadData + stNVMReadWrite.uiNumBytes - SIGNATURE_SIZE));
							if(ulDSDMagicNumInUsrBuff != DSD_IMAGE_MAGIC_NUMBER)
							{
								BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"DSD Sig is present neither in Flash nor User provided Input..");
								up(&Adapter->NVMRdmWrmLock);
								bcm_kfree(pReadData);
								return Status;
							}
						}
					}
					Status = BeceemNVMWrite(Adapter, (PUINT )pReadData,
									stNVMReadWrite.uiOffset, stNVMReadWrite.uiNumBytes, stNVMReadWrite.bVerify);
					if(IsFlash2x(Adapter))
						BcmFlash2xWriteSig(Adapter,Adapter->eActiveDSD);

					Adapter->bHeaderChangeAllowed = FALSE ;

					up(&Adapter->NVMRdmWrmLock);


					if(Status != STATUS_SUCCESS)
					{
						bcm_kfree(pReadData);
						return Status;
					}
				}
				do_gettimeofday(&tv1);
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, " timetaken by Write/read :%ld msec\n",(tv1.tv_sec - tv0.tv_sec)*1000 +(tv1.tv_usec - tv0.tv_usec)/1000);


				bcm_kfree(pReadData);
				Status = STATUS_SUCCESS;
			}
			break;
		case IOCTL_BCM_FLASH2X_SECTION_READ :
			 {

				FLASH2X_READWRITE sFlash2xRead = {0};
				PUCHAR pReadBuff = NULL ;
				UINT NOB = 0;
				UINT BuffSize = 0;
				UINT ReadBytes = 0;
				UINT ReadOffset = 0;
				PUCHAR OutPutBuff = NULL;

				if(IsFlash2x(Adapter) != TRUE)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Flash Does not have 2.x map");
					return -EINVAL;
				}

				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "IOCTL_BCM_FLASH2X_SECTION_READ Called");
				Status = copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg, sizeof(IOCTL_BUFFER));
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Copy of IOCTL BUFFER failed");
					return Status ;
				}

				//Reading FLASH 2.x READ structure
				Status = copy_from_user((PUCHAR)&sFlash2xRead, (PUCHAR)IoBuffer.InputBuffer,sizeof(FLASH2X_READWRITE));
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Copy of Input Buffer failed");
					return Status ;
				}


				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"\nsFlash2xRead.Section :%x" ,sFlash2xRead.Section);
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"\nsFlash2xRead.offset :%x" ,sFlash2xRead.offset);
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"\nsFlash2xRead.numOfBytes :%x" ,sFlash2xRead.numOfBytes);
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"\nsFlash2xRead.bVerify :%x\n" ,sFlash2xRead.bVerify);

				//This was internal to driver for raw read. now it has ben exposed to user space app.
				if(validateFlash2xReadWrite(Adapter,&sFlash2xRead) == FALSE)
					return STATUS_FAILURE ;

				NOB = sFlash2xRead.numOfBytes;
				if(NOB > Adapter->uiSectorSize )
					BuffSize = Adapter->uiSectorSize;
				else
					BuffSize = NOB ;

				ReadOffset = sFlash2xRead.offset ;
				OutPutBuff = (PUCHAR)(IoBuffer.OutputBuffer) ;


				pReadBuff = (PCHAR)kzalloc(BuffSize , GFP_KERNEL);
				if(pReadBuff == NULL)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Memory allocation failed for Flash 2.x Read Structure");
					return -ENOMEM;
				}
				down(&Adapter->NVMRdmWrmLock);

				if((Adapter->IdleMode == TRUE) ||
					(Adapter->bShutStatus ==TRUE) ||
					(Adapter->bPreparingForLowPowerMode ==TRUE))
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"Device is in Idle/Shutdown Mode\n");
					up(&Adapter->NVMRdmWrmLock);
					bcm_kfree(pReadBuff);
					return -EACCES;
				}

				while(NOB)
				{

					if(NOB > Adapter->uiSectorSize )
						ReadBytes = Adapter->uiSectorSize;
					else
						ReadBytes = NOB;


					//Reading the data from Flash 2.x

					Status = BcmFlash2xBulkRead(Adapter,(PUINT)pReadBuff,sFlash2xRead.Section,ReadOffset,ReadBytes);
					if(Status)
					{
						BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"Flash 2x read err with Status :%d", Status);
						break ;
					}

					BCM_DEBUG_PRINT_BUFFER(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,pReadBuff, ReadBytes);

					Status = copy_to_user(OutPutBuff, pReadBuff,ReadBytes);
				 	if(Status)
				 	{
				 		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"Copy to use failed with status :%d", Status);
						break;
				 	}
					NOB = NOB - ReadBytes;
					if(NOB)
					{
						ReadOffset = ReadOffset + ReadBytes ;
						OutPutBuff = OutPutBuff + ReadBytes ;
					}

				}
				up(&Adapter->NVMRdmWrmLock);
				bcm_kfree(pReadBuff);

			 }
			 break ;
		case IOCTL_BCM_FLASH2X_SECTION_WRITE :
			 {
			 	FLASH2X_READWRITE sFlash2xWrite = {0};
				PUCHAR pWriteBuff = NULL;
				PUCHAR InputAddr = NULL;
				UINT NOB = 0;
				UINT BuffSize = 0;
				UINT WriteOffset = 0;
				UINT WriteBytes = 0;

				if(IsFlash2x(Adapter) != TRUE)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Flash Does not have 2.x map");
					return -EINVAL;
				}

				//First make this False so that we can enable the Sector Permission Check in BeceemFlashBulkWrite
				Adapter->bAllDSDWriteAllow = FALSE;


				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, " IOCTL_BCM_FLASH2X_SECTION_WRITE Called");
				Status = copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg, sizeof(IOCTL_BUFFER));
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Copy of IOCTL BUFFER failed");
					return Status;
				}

				//Reading FLASH 2.x READ structure
				Status = copy_from_user((PCHAR)&sFlash2xWrite, (PCHAR)IoBuffer.InputBuffer, sizeof(FLASH2X_READWRITE));
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Reading of output Buffer from IOCTL buffer fails");
					return Status;
				}

				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"\nsFlash2xRead.Section :%x" ,sFlash2xWrite.Section);
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"\nsFlash2xRead.offset :%d" ,sFlash2xWrite.offset);
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"\nsFlash2xRead.numOfBytes :%x" ,sFlash2xWrite.numOfBytes);
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"\nsFlash2xRead.bVerify :%x\n" ,sFlash2xWrite.bVerify);
				#if 0
				if((sFlash2xWrite.Section == ISO_IMAGE1) ||(sFlash2xWrite.Section == ISO_IMAGE2) ||
					(sFlash2xWrite.Section == DSD0) || (sFlash2xWrite.Section == DSD1) || (sFlash2xWrite.Section == DSD2))
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"ISO/DSD Image write is not allowed....  ");
					return STATUS_FAILURE ;
				}
				#endif
				if((sFlash2xWrite.Section != VSA0) && (sFlash2xWrite.Section != VSA1) &&
					(sFlash2xWrite.Section != VSA2) )
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"Only VSA write is allowed");
					return -EINVAL;
				}

				if(validateFlash2xReadWrite(Adapter,&sFlash2xWrite) == FALSE)
					return STATUS_FAILURE ;

				InputAddr = (PCHAR)(sFlash2xWrite.pDataBuff) ;
				WriteOffset = sFlash2xWrite.offset ;
				NOB = sFlash2xWrite.numOfBytes;

				if(NOB > Adapter->uiSectorSize )
					BuffSize = Adapter->uiSectorSize;
				else
					BuffSize = NOB ;

				pWriteBuff = (PCHAR)kmalloc(BuffSize, GFP_KERNEL);
				if(pWriteBuff == NULL)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Memory allocation failed for Flash 2.x Read Structure");
					return -ENOMEM;
				}

				//extracting the remainder of the given offset.
				WriteBytes = Adapter->uiSectorSize ;
				if(WriteOffset % Adapter->uiSectorSize)
					WriteBytes =Adapter->uiSectorSize - (WriteOffset % Adapter->uiSectorSize);
				if(NOB < WriteBytes)
					WriteBytes = NOB;

				down(&Adapter->NVMRdmWrmLock);

				if((Adapter->IdleMode == TRUE) ||
					(Adapter->bShutStatus ==TRUE) ||
					(Adapter->bPreparingForLowPowerMode ==TRUE))
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"Device is in Idle/Shutdown Mode\n");
					up(&Adapter->NVMRdmWrmLock);
					bcm_kfree(pWriteBuff);
					return -EACCES;
				}

				BcmFlash2xCorruptSig(Adapter,sFlash2xWrite.Section);
				do
				{
					Status = copy_from_user(pWriteBuff,InputAddr,WriteBytes);
				 	if(Status)
				 	{
				 		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Copy to user failed with status :%d", Status);
						break ;
				 	}
					BCM_DEBUG_PRINT_BUFFER(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,pWriteBuff,WriteBytes);
					//Writing the data from Flash 2.x
					Status = BcmFlash2xBulkWrite(Adapter,(PUINT)pWriteBuff,sFlash2xWrite.Section,WriteOffset,WriteBytes,sFlash2xWrite.bVerify);

					if(Status)
					{
						BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Flash 2x read err with Status :%d", Status);
						break ;
					}

					NOB = NOB - WriteBytes;
					if(NOB)
					{
						WriteOffset = WriteOffset + WriteBytes ;
						InputAddr = InputAddr + WriteBytes ;
						if(NOB > Adapter->uiSectorSize )
							WriteBytes = Adapter->uiSectorSize;
						else
							WriteBytes = NOB;
					}


				}	while(NOB > 0);
				BcmFlash2xWriteSig(Adapter,sFlash2xWrite.Section);
				up(&Adapter->NVMRdmWrmLock);
				bcm_kfree(pWriteBuff);
			 }
			 break ;
		case IOCTL_BCM_GET_FLASH2X_SECTION_BITMAP :
			 {

			 	PFLASH2X_BITMAP psFlash2xBitMap = NULL ;
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "IOCTL_BCM_GET_FLASH2X_SECTION_BITMAP Called");

				Status = copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg, sizeof(IOCTL_BUFFER));
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Copy of IOCTL BUFFER failed");
					return Status;
				}
				if(IoBuffer.OutputLength != sizeof(FLASH2X_BITMAP))
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Structure size mismatch Lib :0x%lx Driver :0x%zx ",IoBuffer.OutputLength, sizeof(FLASH2X_BITMAP));
					break;
				}

				psFlash2xBitMap = (PFLASH2X_BITMAP)kzalloc(sizeof(FLASH2X_BITMAP), GFP_KERNEL);
				if(psFlash2xBitMap == NULL)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Memory is not available");
					return -ENOMEM ;
				}
				//Reading the Flash Sectio Bit map
				down(&Adapter->NVMRdmWrmLock);

				if((Adapter->IdleMode == TRUE) ||
					(Adapter->bShutStatus ==TRUE) ||
					(Adapter->bPreparingForLowPowerMode ==TRUE))
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"Device is in Idle/Shutdown Mode\n");
					up(&Adapter->NVMRdmWrmLock);
					bcm_kfree(psFlash2xBitMap);
					return -EACCES;
				}

				BcmGetFlash2xSectionalBitMap(Adapter, psFlash2xBitMap);
				up(&Adapter->NVMRdmWrmLock);
				Status = copy_to_user((PCHAR)IoBuffer.OutputBuffer, (PCHAR)psFlash2xBitMap, sizeof(FLASH2X_BITMAP));
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "copying Flash2x bitMap failed");
					bcm_kfree(psFlash2xBitMap);
					return Status;
				}
				bcm_kfree(psFlash2xBitMap);
			 }
			 break ;
		case IOCTL_BCM_SET_ACTIVE_SECTION :
			 {
				FLASH2X_SECTION_VAL eFlash2xSectionVal = 0;
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "IOCTL_BCM_SET_ACTIVE_SECTION Called");

				if(IsFlash2x(Adapter) != TRUE)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Flash Does not have 2.x map");
					return -EINVAL;
				}

				Status = copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg, sizeof(IOCTL_BUFFER));
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Copy of IOCTL BUFFER failed");
					return Status;
				}

				Status = copy_from_user((PCHAR)&eFlash2xSectionVal,(PCHAR)IoBuffer.InputBuffer, sizeof(INT));
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Copy of flash section val failed");
					return Status;
				}

				down(&Adapter->NVMRdmWrmLock);

				if((Adapter->IdleMode == TRUE) ||
					(Adapter->bShutStatus ==TRUE) ||
					(Adapter->bPreparingForLowPowerMode ==TRUE))
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"Device is in Idle/Shutdown Mode\n");
					up(&Adapter->NVMRdmWrmLock);
					return -EACCES;
				}

				Status = BcmSetActiveSection(Adapter,eFlash2xSectionVal);
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Failed to make it's priority Highest. Status %d", Status);
				}
				up(&Adapter->NVMRdmWrmLock);
			}
			break ;
		case IOCTL_BCM_IDENTIFY_ACTIVE_SECTION :
			 {
			 	//Right Now we are taking care of only DSD
				Adapter->bAllDSDWriteAllow = FALSE ;
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"IOCTL_BCM_IDENTIFY_ACTIVE_SECTION called");

				#if 0
				SECTION_TYPE section = 0 ;


				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "IOCTL_BCM_IDENTIFY_ACTIVE_SECTION Called");
				Status = copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg, sizeof(IOCTL_BUFFER));
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "Copy of IOCTL BUFFER failed");
					return Status;
				}
				Status = copy_from_user((PCHAR)section,(PCHAR)&IoBuffer, sizeof(INT));
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "Copy of section type failed failed");
					return Status;
				}
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"Read Section :%d", section);
			 	if(section == DSD)
					Adapter->ulFlashCalStart = Adapter->uiActiveDSDOffsetAtFwDld ;
				else
					Status = STATUS_FAILURE ;
				#endif
				Status = STATUS_SUCCESS ;
			 }
			 break ;
		case IOCTL_BCM_COPY_SECTION :
			 {
				FLASH2X_COPY_SECTION sCopySectStrut = {0};
				Status = STATUS_SUCCESS;
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "IOCTL_BCM_COPY_SECTION  Called");

				Adapter->bAllDSDWriteAllow = FALSE ;
				if(IsFlash2x(Adapter) != TRUE)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Flash Does not have 2.x map");
					return -EINVAL;
				}

				Status = copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg, sizeof(IOCTL_BUFFER));
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Copy of IOCTL BUFFER failed Status :%d", Status);
					return Status;
				}

				Status = copy_from_user((PCHAR)&sCopySectStrut,(PCHAR)IoBuffer.InputBuffer, sizeof(FLASH2X_COPY_SECTION));
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Copy of Copy_Section_Struct failed with Status :%d", Status);
					return Status;
				}
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "Source SEction :%x", sCopySectStrut.SrcSection);
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "Destination SEction :%x", sCopySectStrut.DstSection);
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "offset :%x", sCopySectStrut.offset);
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "NOB :%x", sCopySectStrut.numOfBytes);


				if(IsSectionExistInFlash(Adapter,sCopySectStrut.SrcSection) == FALSE)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Source Section<%x> does not exixt in Flash ", sCopySectStrut.SrcSection);
					return -EINVAL;
				}

				if(IsSectionExistInFlash(Adapter,sCopySectStrut.DstSection) == FALSE)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Destinatio Section<%x> does not exixt in Flash ", sCopySectStrut.DstSection);
					return -EINVAL;
				}

				if(sCopySectStrut.SrcSection == sCopySectStrut.DstSection)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"Source and Destination section should be different");
					return -EINVAL;
				}

				down(&Adapter->NVMRdmWrmLock);

				if((Adapter->IdleMode == TRUE) ||
					(Adapter->bShutStatus ==TRUE) ||
					(Adapter->bPreparingForLowPowerMode ==TRUE))
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"Device is in Idle/Shutdown Mode\n");
					up(&Adapter->NVMRdmWrmLock);
					return -EACCES;
				}

				if(sCopySectStrut.SrcSection == ISO_IMAGE1 || sCopySectStrut.SrcSection == ISO_IMAGE2)
				{
					if(IsNonCDLessDevice(Adapter))
					{
						BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Device is Non-CDLess hence won't have ISO !!");
						Status = -EINVAL ;
					}
					else if(sCopySectStrut.numOfBytes == 0)
					{
						Status = BcmCopyISO(Adapter,sCopySectStrut);
					}
					else
					{
						BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Partial Copy of ISO section is not Allowed..");
						Status = STATUS_FAILURE ;
					}
					up(&Adapter->NVMRdmWrmLock);
					return Status;
				}

				Status = BcmCopySection(Adapter, sCopySectStrut.SrcSection,
							sCopySectStrut.DstSection,sCopySectStrut.offset,sCopySectStrut.numOfBytes);
				up(&Adapter->NVMRdmWrmLock);
			 }
			 break ;
		case IOCTL_BCM_GET_FLASH_CS_INFO :
			 {
				Status = STATUS_SUCCESS;
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, " IOCTL_BCM_GET_FLASH_CS_INFO Called");

				Status = copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg, sizeof(IOCTL_BUFFER));
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Copy of IOCTL BUFFER failed");
					break;
				}
				if(Adapter->eNVMType != NVM_FLASH)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Connected device does not have flash");
					Status = -EINVAL;
					break;
				}
				if(IsFlash2x(Adapter) == TRUE)
				{

					if(IoBuffer.OutputLength < sizeof(FLASH2X_CS_INFO))
					{
						BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0," Passed buffer size:0x%lX is insufficient for the CS structure.. \nRequired size :0x%zx ",IoBuffer.OutputLength, sizeof(FLASH2X_CS_INFO));
						Status = -EINVAL;
						break;
					}

					Status = copy_to_user((PCHAR)IoBuffer.OutputBuffer, (PCHAR)Adapter->psFlash2xCSInfo, sizeof(FLASH2X_CS_INFO));
					if(Status)
					{
						BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "copying Flash2x cs info failed");
						break;
					}
				}
				else
				{
					if(IoBuffer.OutputLength < sizeof(FLASH_CS_INFO))
					{
						BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0," Passed buffer size:0x%lX is insufficient for the CS structure.. Required size :0x%zx ",IoBuffer.OutputLength, sizeof(FLASH_CS_INFO));
						Status = -EINVAL;
						break;
					}
					Status = copy_to_user((PCHAR)IoBuffer.OutputBuffer, (PCHAR)Adapter->psFlashCSInfo, sizeof(FLASH_CS_INFO));
					if(Status)
					{
						BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "copying Flash CS info failed");
						break;
					}

			 	 }
			  }
			  break ;
		case IOCTL_BCM_SELECT_DSD :
			 {
				UINT SectOfset = 0;
				FLASH2X_SECTION_VAL eFlash2xSectionVal;
				eFlash2xSectionVal = NO_SECTION_VAL ;
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, " IOCTL_BCM_SELECT_DSD Called");

				if(IsFlash2x(Adapter) != TRUE)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Flash Does not have 2.x map");
					return -EINVAL;
				}

				Status = copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg, sizeof(IOCTL_BUFFER));
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Copy of IOCTL BUFFER failed");
					return Status;
				}
				Status = copy_from_user((PCHAR)&eFlash2xSectionVal,(PCHAR)IoBuffer.InputBuffer, sizeof(INT));
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Copy of flash section val failed");
					return Status;
				}

				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"Read Section :%d", eFlash2xSectionVal);
				if((eFlash2xSectionVal != DSD0) &&
					(eFlash2xSectionVal != DSD1) &&
					(eFlash2xSectionVal != DSD2) )
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Passed section<%x> is not DSD section", eFlash2xSectionVal);
					return STATUS_FAILURE ;
				}

				SectOfset= BcmGetSectionValStartOffset(Adapter,eFlash2xSectionVal);
				if(SectOfset == INVALID_OFFSET)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Provided Section val <%d> does not exixt in Flash 2.x", eFlash2xSectionVal);
					return -EINVAL;
				}

				Adapter->bAllDSDWriteAllow = TRUE ;

				Adapter->ulFlashCalStart = SectOfset ;
				Adapter->eActiveDSD = eFlash2xSectionVal;
			 }
			 Status = STATUS_SUCCESS ;
			 break;

		case IOCTL_BCM_NVM_RAW_READ :
			 {

				NVM_READWRITE  stNVMRead = {0};
				INT NOB ;
				INT BuffSize ;
				INT ReadOffset = 0;
				UINT ReadBytes = 0 ;
				PUCHAR pReadBuff = NULL ;
				PUCHAR OutPutBuff = NULL ;

				if(Adapter->eNVMType != NVM_FLASH)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"NVM TYPE is not Flash ");
					return -EINVAL ;
				}

				/* Copy Ioctl Buffer structure */
				if(copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg, sizeof(IOCTL_BUFFER)))
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "copy_from_user 1 failed\n");
					Status = -EFAULT;
					break;
				}

				if(copy_from_user(&stNVMRead, (PUCHAR)IoBuffer.OutputBuffer,sizeof(NVM_READWRITE)))
				{
					Status = -EFAULT;
					break;
				}

				NOB = stNVMRead.uiNumBytes;
				//In Raw-Read max Buff size : 64MB

				if(NOB > DEFAULT_BUFF_SIZE)
					BuffSize = DEFAULT_BUFF_SIZE;
				else
					BuffSize = NOB ;

				ReadOffset = stNVMRead.uiOffset ;
				OutPutBuff = (PUCHAR)(stNVMRead.pBuffer) ;


				pReadBuff = (PCHAR)kzalloc(BuffSize , GFP_KERNEL);
				if(pReadBuff == NULL)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Memory allocation failed for Flash 2.x Read Structure");
					Status = -ENOMEM;
					break;
				}
				down(&Adapter->NVMRdmWrmLock);

				if((Adapter->IdleMode == TRUE) ||
					(Adapter->bShutStatus ==TRUE) ||
					(Adapter->bPreparingForLowPowerMode ==TRUE))
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"Device is in Idle/Shutdown Mode\n");
					bcm_kfree(pReadBuff);
					up(&Adapter->NVMRdmWrmLock);
					return -EACCES;
				}

				Adapter->bFlashRawRead = TRUE ;
				while(NOB)
				{
					if(NOB > DEFAULT_BUFF_SIZE )
						ReadBytes = DEFAULT_BUFF_SIZE;
					else
						ReadBytes = NOB;

					//Reading the data from Flash 2.x
					Status = BeceemNVMRead(Adapter,(PUINT)pReadBuff,ReadOffset,ReadBytes);
					if(Status)
					{
						BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Flash 2x read err with Status :%d", Status);
						break;
					}

					BCM_DEBUG_PRINT_BUFFER(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,pReadBuff, ReadBytes);

					Status = copy_to_user(OutPutBuff, pReadBuff,ReadBytes);
				 	if(Status)
				 	{
				 		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Copy to use failed with status :%d", Status);
						break;
				 	}
					NOB = NOB - ReadBytes;
					if(NOB)
					{
						ReadOffset = ReadOffset + ReadBytes ;
						OutPutBuff = OutPutBuff + ReadBytes ;
					}

				}
				Adapter->bFlashRawRead = FALSE ;
				up(&Adapter->NVMRdmWrmLock);
				bcm_kfree(pReadBuff);
				break ;
			 }

		case IOCTL_BCM_CNTRLMSG_MASK:
			 {
				ULONG RxCntrlMsgBitMask = 0 ;

				/* Copy Ioctl Buffer structure */
				Status = copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg, sizeof(IOCTL_BUFFER));
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"copy of Ioctl buffer is failed from user space");
					break;
				}

				Status = copy_from_user(&RxCntrlMsgBitMask, IoBuffer.InputBuffer, IoBuffer.InputLength);
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"copy of control bit mask failed from user space");
					break;
				}
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"\n Got user defined cntrl msg bit mask :%lx", RxCntrlMsgBitMask);
				pTarang->RxCntrlMsgBitMask = RxCntrlMsgBitMask ;
			 }
			 break;
			case IOCTL_BCM_GET_DEVICE_DRIVER_INFO:
			{
				DEVICE_DRIVER_INFO DevInfo;

				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"Called IOCTL_BCM_GET_DEVICE_DRIVER_INFO\n");

				DevInfo.MaxRDMBufferSize = BUFFER_4K;
				DevInfo.u32DSDStartOffset = EEPROM_CALPARAM_START;
				DevInfo.u32RxAlignmentCorrection = 0;
				DevInfo.u32NVMType = Adapter->eNVMType;
				DevInfo.u32InterfaceType = BCM_USB;

				Status = copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg, sizeof(IOCTL_BUFFER));
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Copy of IOCTL BUFFER failed");
					break;
				}
				if(IoBuffer.OutputLength < sizeof(DevInfo))
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"User Passed buffer length is less than actural buffer size");
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"user passed buffer size :0x%lX, expected size :0x%zx",IoBuffer.OutputLength, sizeof(DevInfo));
					Status = -EINVAL;
					break;
				}
				Status = copy_to_user((PCHAR)IoBuffer.OutputBuffer, (PCHAR)&DevInfo, sizeof(DevInfo));
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"copying Dev info structure to user space buffer failed");
					break;
				}
			}
			break ;

			case IOCTL_BCM_TIME_SINCE_NET_ENTRY:
			{
				ST_TIME_ELAPSED stTimeElapsedSinceNetEntry = {0};
				struct timeval tv = {0} ;

				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL,"IOCTL_BCM_TIME_SINCE_NET_ENTRY called");

				Status = copy_from_user((PCHAR)&IoBuffer, (PCHAR)arg, sizeof(IOCTL_BUFFER));
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Copy of IOCTL BUFFER failed");
					break;
				}
				if(IoBuffer.OutputLength < sizeof(ST_TIME_ELAPSED))
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"User Passed buffer length:0x%lx is less than expected buff size :0x%zX",IoBuffer.OutputLength,sizeof(ST_TIME_ELAPSED));
					Status = -EINVAL;
					break;
				}

				//stTimeElapsedSinceNetEntry.ul64TimeElapsedSinceNetEntry = Adapter->liTimeSinceLastNetEntry;
				do_gettimeofday(&tv);
				stTimeElapsedSinceNetEntry.ul64TimeElapsedSinceNetEntry = tv.tv_sec - Adapter->liTimeSinceLastNetEntry;

				Status = copy_to_user((PCHAR)IoBuffer.OutputBuffer, (PCHAR)&stTimeElapsedSinceNetEntry, sizeof(ST_TIME_ELAPSED));
				if(Status)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"copying ST_TIME_ELAPSED structure to user space buffer failed");
					break;
				}

			}
			break;

		default:
            BCM_DEBUG_PRINT (Adapter, DBG_TYPE_PRINTK, 0, 0, "wrong input %x",cmd);
			BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, OSAL_DBG, DBG_LVL_ALL, "In default ioctl %d\n", cmd);
			 Status = STATUS_FAILURE;

			break;
	}
	return Status;
}


static struct file_operations bcm_fops = {
	.owner    = THIS_MODULE,
	.open     = bcm_char_open,
	.release  = bcm_char_release,
	.read     = bcm_char_read,
	.unlocked_ioctl    = bcm_char_ioctl,
	.llseek = no_llseek,
};


int register_control_device_interface(PMINI_ADAPTER Adapter)
{
	if(Adapter->major>0)
    	return Adapter->major;
    Adapter->major = register_chrdev(0, "tarang", &bcm_fops);
    if(Adapter->major < 0)
    {
    	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "register_chrdev:Failed to registering WiMax control char device!");
        return Adapter->major;
    }

	bcm_class = NULL;
	bcm_class = class_create (THIS_MODULE, "tarang");
	if(IS_ERR (bcm_class))
	{
    	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "Unable to create class\n");
        unregister_chrdev(Adapter->major, "tarang");
		Adapter->major = 0;
		return -ENODEV;
	}
	Adapter->pstCreatedClassDevice = device_create (bcm_class, NULL,
								MKDEV(Adapter->major, 0),
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,26)
								NULL	,
#endif
								"tarang");

	if(IS_ERR(Adapter->pstCreatedClassDevice))
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "class device did not get created : %ld", PTR_ERR(Adapter->pstCreatedClassDevice) );
	}
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "Got Major No: %d", Adapter->major);
    return 0;
}

void unregister_control_device_interface(PMINI_ADAPTER Adapter)
{
	if(Adapter->major > 0)
	{
        BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "destroying class device");
		device_destroy (bcm_class, MKDEV(Adapter->major, 0));
	}
    if(!IS_ERR(bcm_class))
	{
        BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "destroying created class ");
        class_destroy (bcm_class);
		bcm_class = NULL;
	}
	if(Adapter->major > 0)
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL,"unregistering character interface");
        unregister_chrdev(Adapter->major, "tarang");
	}

}
