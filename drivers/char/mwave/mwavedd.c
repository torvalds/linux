/*
*
* mwavedd.c -- mwave device driver
*
*
* Written By: Mike Sullivan IBM Corporation
*
* Copyright (C) 1999 IBM Corporation
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* NO WARRANTY
* THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
* CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
* LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
* MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
* solely responsible for determining the appropriateness of using and
* distributing the Program and assumes all risks associated with its
* exercise of rights under this Agreement, including but not limited to
* the risks and costs of program errors, damage to or loss of data,
* programs or equipment, and unavailability or interruption of operations.
*
* DISCLAIMER OF LIABILITY
* NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
* TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
* USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
* HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*
* 10/23/2000 - Alpha Release
*	First release to the public
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/serial.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/serial_8250.h>
#include <linux/nospec.h>
#include "smapi.h"
#include "mwavedd.h"
#include "3780i.h"
#include "tp3780i.h"

MODULE_DESCRIPTION("3780i Advanced Communications Processor (Mwave) driver");
MODULE_AUTHOR("Mike Sullivan and Paul Schroeder");
MODULE_LICENSE("GPL");

/*
* These parameters support the setting of MWave resources. Note that no
* checks are made against other devices (ie. superio) for conflicts.
* We'll depend on users using the tpctl utility to do that for now
*/
static DEFINE_MUTEX(mwave_mutex);
int mwave_debug = 0;
int mwave_3780i_irq = 0;
int mwave_3780i_io = 0;
int mwave_uart_irq = 0;
int mwave_uart_io = 0;
module_param(mwave_debug, int, 0);
module_param_hw(mwave_3780i_irq, int, irq, 0);
module_param_hw(mwave_3780i_io, int, ioport, 0);
module_param_hw(mwave_uart_irq, int, irq, 0);
module_param_hw(mwave_uart_io, int, ioport, 0);

static int mwave_open(struct inode *inode, struct file *file);
static int mwave_close(struct inode *inode, struct file *file);
static long mwave_ioctl(struct file *filp, unsigned int iocmd,
							unsigned long ioarg);

MWAVE_DEVICE_DATA mwave_s_mdd;

static int mwave_open(struct inode *inode, struct file *file)
{
	unsigned int retval = 0;

	PRINTK_3(TRACE_MWAVE,
		"mwavedd::mwave_open, entry inode %p file %p\n",
		 inode, file);
	PRINTK_2(TRACE_MWAVE,
		"mwavedd::mwave_open, exit return retval %x\n", retval);

	return retval;
}

static int mwave_close(struct inode *inode, struct file *file)
{
	unsigned int retval = 0;

	PRINTK_3(TRACE_MWAVE,
		"mwavedd::mwave_close, entry inode %p file %p\n",
		 inode,  file);

	PRINTK_2(TRACE_MWAVE, "mwavedd::mwave_close, exit retval %x\n",
		retval);

	return retval;
}

static long mwave_ioctl(struct file *file, unsigned int iocmd,
							unsigned long ioarg)
{
	unsigned int retval = 0;
	pMWAVE_DEVICE_DATA pDrvData = &mwave_s_mdd;
	void __user *arg = (void __user *)ioarg;

	PRINTK_4(TRACE_MWAVE,
		"mwavedd::mwave_ioctl, entry file %p cmd %x arg %x\n",
		file, iocmd, (int) ioarg);

	switch (iocmd) {

		case IOCTL_MW_RESET:
			PRINTK_1(TRACE_MWAVE,
				"mwavedd::mwave_ioctl, IOCTL_MW_RESET"
				" calling tp3780I_ResetDSP\n");
			mutex_lock(&mwave_mutex);
			retval = tp3780I_ResetDSP(&pDrvData->rBDData);
			mutex_unlock(&mwave_mutex);
			PRINTK_2(TRACE_MWAVE,
				"mwavedd::mwave_ioctl, IOCTL_MW_RESET"
				" retval %x from tp3780I_ResetDSP\n",
				retval);
			break;
	
		case IOCTL_MW_RUN:
			PRINTK_1(TRACE_MWAVE,
				"mwavedd::mwave_ioctl, IOCTL_MW_RUN"
				" calling tp3780I_StartDSP\n");
			mutex_lock(&mwave_mutex);
			retval = tp3780I_StartDSP(&pDrvData->rBDData);
			mutex_unlock(&mwave_mutex);
			PRINTK_2(TRACE_MWAVE,
				"mwavedd::mwave_ioctl, IOCTL_MW_RUN"
				" retval %x from tp3780I_StartDSP\n",
				retval);
			break;
	
		case IOCTL_MW_DSP_ABILITIES: {
			MW_ABILITIES rAbilities;
	
			PRINTK_1(TRACE_MWAVE,
				"mwavedd::mwave_ioctl,"
				" IOCTL_MW_DSP_ABILITIES calling"
				" tp3780I_QueryAbilities\n");
			mutex_lock(&mwave_mutex);
			retval = tp3780I_QueryAbilities(&pDrvData->rBDData,
					&rAbilities);
			mutex_unlock(&mwave_mutex);
			PRINTK_2(TRACE_MWAVE,
				"mwavedd::mwave_ioctl, IOCTL_MW_DSP_ABILITIES"
				" retval %x from tp3780I_QueryAbilities\n",
				retval);
			if (retval == 0) {
				if( copy_to_user(arg, &rAbilities,
							sizeof(MW_ABILITIES)) )
					return -EFAULT;
			}
			PRINTK_2(TRACE_MWAVE,
				"mwavedd::mwave_ioctl, IOCTL_MW_DSP_ABILITIES"
				" exit retval %x\n",
				retval);
		}
			break;
	
		case IOCTL_MW_READ_DATA:
		case IOCTL_MW_READCLEAR_DATA: {
			MW_READWRITE rReadData;
			unsigned short __user *pusBuffer = NULL;
	
			if( copy_from_user(&rReadData, arg,
						sizeof(MW_READWRITE)) )
				return -EFAULT;
			pusBuffer = (unsigned short __user *) (rReadData.pBuf);
	
			PRINTK_4(TRACE_MWAVE,
				"mwavedd::mwave_ioctl IOCTL_MW_READ_DATA,"
				" size %lx, ioarg %lx pusBuffer %p\n",
				rReadData.ulDataLength, ioarg, pusBuffer);
			mutex_lock(&mwave_mutex);
			retval = tp3780I_ReadWriteDspDStore(&pDrvData->rBDData,
					iocmd,
					pusBuffer,
					rReadData.ulDataLength,
					rReadData.usDspAddress);
			mutex_unlock(&mwave_mutex);
		}
			break;
	
		case IOCTL_MW_READ_INST: {
			MW_READWRITE rReadData;
			unsigned short __user *pusBuffer = NULL;
	
			if( copy_from_user(&rReadData, arg,
						sizeof(MW_READWRITE)) )
				return -EFAULT;
			pusBuffer = (unsigned short __user *) (rReadData.pBuf);
	
			PRINTK_4(TRACE_MWAVE,
				"mwavedd::mwave_ioctl IOCTL_MW_READ_INST,"
				" size %lx, ioarg %lx pusBuffer %p\n",
				rReadData.ulDataLength / 2, ioarg,
				pusBuffer);
			mutex_lock(&mwave_mutex);
			retval = tp3780I_ReadWriteDspDStore(&pDrvData->rBDData,
				iocmd, pusBuffer,
				rReadData.ulDataLength / 2,
				rReadData.usDspAddress);
			mutex_unlock(&mwave_mutex);
		}
			break;
	
		case IOCTL_MW_WRITE_DATA: {
			MW_READWRITE rWriteData;
			unsigned short __user *pusBuffer = NULL;
	
			if( copy_from_user(&rWriteData, arg,
						sizeof(MW_READWRITE)) )
				return -EFAULT;
			pusBuffer = (unsigned short __user *) (rWriteData.pBuf);
	
			PRINTK_4(TRACE_MWAVE,
				"mwavedd::mwave_ioctl IOCTL_MW_WRITE_DATA,"
				" size %lx, ioarg %lx pusBuffer %p\n",
				rWriteData.ulDataLength, ioarg,
				pusBuffer);
			mutex_lock(&mwave_mutex);
			retval = tp3780I_ReadWriteDspDStore(&pDrvData->rBDData,
					iocmd, pusBuffer,
					rWriteData.ulDataLength,
					rWriteData.usDspAddress);
			mutex_unlock(&mwave_mutex);
		}
			break;
	
		case IOCTL_MW_WRITE_INST: {
			MW_READWRITE rWriteData;
			unsigned short __user *pusBuffer = NULL;
	
			if( copy_from_user(&rWriteData, arg,
						sizeof(MW_READWRITE)) )
				return -EFAULT;
			pusBuffer = (unsigned short __user *)(rWriteData.pBuf);
	
			PRINTK_4(TRACE_MWAVE,
				"mwavedd::mwave_ioctl IOCTL_MW_WRITE_INST,"
				" size %lx, ioarg %lx pusBuffer %p\n",
				rWriteData.ulDataLength, ioarg,
				pusBuffer);
			mutex_lock(&mwave_mutex);
			retval = tp3780I_ReadWriteDspIStore(&pDrvData->rBDData,
					iocmd, pusBuffer,
					rWriteData.ulDataLength,
					rWriteData.usDspAddress);
			mutex_unlock(&mwave_mutex);
		}
			break;
	
		case IOCTL_MW_REGISTER_IPC: {
			unsigned int ipcnum = (unsigned int) ioarg;
	
			if (ipcnum >= ARRAY_SIZE(pDrvData->IPCs)) {
				PRINTK_ERROR(KERN_ERR_MWAVE
						"mwavedd::mwave_ioctl:"
						" IOCTL_MW_REGISTER_IPC:"
						" Error: Invalid ipcnum %x\n",
						ipcnum);
				return -EINVAL;
			}
			ipcnum = array_index_nospec(ipcnum,
						    ARRAY_SIZE(pDrvData->IPCs));
			PRINTK_3(TRACE_MWAVE,
				"mwavedd::mwave_ioctl IOCTL_MW_REGISTER_IPC"
				" ipcnum %x entry usIntCount %x\n",
				ipcnum,
				pDrvData->IPCs[ipcnum].usIntCount);

			mutex_lock(&mwave_mutex);
			pDrvData->IPCs[ipcnum].bIsHere = false;
			pDrvData->IPCs[ipcnum].bIsEnabled = true;
			mutex_unlock(&mwave_mutex);
	
			PRINTK_2(TRACE_MWAVE,
				"mwavedd::mwave_ioctl IOCTL_MW_REGISTER_IPC"
				" ipcnum %x exit\n",
				ipcnum);
		}
			break;
	
		case IOCTL_MW_GET_IPC: {
			unsigned int ipcnum = (unsigned int) ioarg;
	
			if (ipcnum >= ARRAY_SIZE(pDrvData->IPCs)) {
				PRINTK_ERROR(KERN_ERR_MWAVE
						"mwavedd::mwave_ioctl:"
						" IOCTL_MW_GET_IPC: Error:"
						" Invalid ipcnum %x\n", ipcnum);
				return -EINVAL;
			}
			ipcnum = array_index_nospec(ipcnum,
						    ARRAY_SIZE(pDrvData->IPCs));
			PRINTK_3(TRACE_MWAVE,
				"mwavedd::mwave_ioctl IOCTL_MW_GET_IPC"
				" ipcnum %x, usIntCount %x\n",
				ipcnum,
				pDrvData->IPCs[ipcnum].usIntCount);
	
			mutex_lock(&mwave_mutex);
			if (pDrvData->IPCs[ipcnum].bIsEnabled == true) {
				DECLARE_WAITQUEUE(wait, current);

				PRINTK_2(TRACE_MWAVE,
					"mwavedd::mwave_ioctl, thread for"
					" ipc %x going to sleep\n",
					ipcnum);
				add_wait_queue(&pDrvData->IPCs[ipcnum].ipc_wait_queue, &wait);
				pDrvData->IPCs[ipcnum].bIsHere = true;
				set_current_state(TASK_INTERRUPTIBLE);
				/* check whether an event was signalled by */
				/* the interrupt handler while we were gone */
				if (pDrvData->IPCs[ipcnum].usIntCount == 1) {	/* first int has occurred (race condition) */
					pDrvData->IPCs[ipcnum].usIntCount = 2;	/* first int has been handled */
					PRINTK_2(TRACE_MWAVE,
						"mwavedd::mwave_ioctl"
						" IOCTL_MW_GET_IPC ipcnum %x"
						" handling first int\n",
						ipcnum);
				} else {	/* either 1st int has not yet occurred, or we have already handled the first int */
					schedule();
					if (pDrvData->IPCs[ipcnum].usIntCount == 1) {
						pDrvData->IPCs[ipcnum].usIntCount = 2;
					}
					PRINTK_2(TRACE_MWAVE,
						"mwavedd::mwave_ioctl"
						" IOCTL_MW_GET_IPC ipcnum %x"
						" woke up and returning to"
						" application\n",
						ipcnum);
				}
				pDrvData->IPCs[ipcnum].bIsHere = false;
				remove_wait_queue(&pDrvData->IPCs[ipcnum].ipc_wait_queue, &wait);
				set_current_state(TASK_RUNNING);
				PRINTK_2(TRACE_MWAVE,
					"mwavedd::mwave_ioctl IOCTL_MW_GET_IPC,"
					" returning thread for ipc %x"
					" processing\n",
					ipcnum);
			}
			mutex_unlock(&mwave_mutex);
		}
			break;
	
		case IOCTL_MW_UNREGISTER_IPC: {
			unsigned int ipcnum = (unsigned int) ioarg;
	
			PRINTK_2(TRACE_MWAVE,
				"mwavedd::mwave_ioctl IOCTL_MW_UNREGISTER_IPC"
				" ipcnum %x\n",
				ipcnum);
			if (ipcnum >= ARRAY_SIZE(pDrvData->IPCs)) {
				PRINTK_ERROR(KERN_ERR_MWAVE
						"mwavedd::mwave_ioctl:"
						" IOCTL_MW_UNREGISTER_IPC:"
						" Error: Invalid ipcnum %x\n",
						ipcnum);
				return -EINVAL;
			}
			ipcnum = array_index_nospec(ipcnum,
						    ARRAY_SIZE(pDrvData->IPCs));
			mutex_lock(&mwave_mutex);
			if (pDrvData->IPCs[ipcnum].bIsEnabled == true) {
				pDrvData->IPCs[ipcnum].bIsEnabled = false;
				if (pDrvData->IPCs[ipcnum].bIsHere == true) {
					wake_up_interruptible(&pDrvData->IPCs[ipcnum].ipc_wait_queue);
				}
			}
			mutex_unlock(&mwave_mutex);
		}
			break;
	
		default:
			return -ENOTTY;
	} /* switch */

	PRINTK_2(TRACE_MWAVE, "mwavedd::mwave_ioctl, exit retval %x\n", retval);

	return retval;
}


static ssize_t mwave_read(struct file *file, char __user *buf, size_t count,
                          loff_t * ppos)
{
	PRINTK_5(TRACE_MWAVE,
		"mwavedd::mwave_read entry file %p, buf %p, count %zx ppos %p\n",
		file, buf, count, ppos);

	return -EINVAL;
}


static ssize_t mwave_write(struct file *file, const char __user *buf,
                           size_t count, loff_t * ppos)
{
	PRINTK_5(TRACE_MWAVE,
		"mwavedd::mwave_write entry file %p, buf %p,"
		" count %zx ppos %p\n",
		file, buf, count, ppos);

	return -EINVAL;
}


static int register_serial_portandirq(unsigned int port, int irq)
{
	struct uart_8250_port uart;
	
	switch ( port ) {
		case 0x3f8:
		case 0x2f8:
		case 0x3e8:
		case 0x2e8:
			/* OK */
			break;
		default:
			PRINTK_ERROR(KERN_ERR_MWAVE
					"mwavedd::register_serial_portandirq:"
					" Error: Illegal port %x\n", port );
			return -1;
	} /* switch */
	/* port is okay */

	switch ( irq ) {
		case 3:
		case 4:
		case 5:
		case 7:
			/* OK */
			break;
		default:
			PRINTK_ERROR(KERN_ERR_MWAVE
					"mwavedd::register_serial_portandirq:"
					" Error: Illegal irq %x\n", irq );
			return -1;
	} /* switch */
	/* irq is okay */

	memset(&uart, 0, sizeof(uart));
	
	uart.port.uartclk =  1843200;
	uart.port.iobase = port;
	uart.port.irq = irq;
	uart.port.iotype = UPIO_PORT;
	uart.port.flags =  UPF_SHARE_IRQ;
	return serial8250_register_8250_port(&uart);
}


static const struct file_operations mwave_fops = {
	.owner		= THIS_MODULE,
	.read		= mwave_read,
	.write		= mwave_write,
	.unlocked_ioctl	= mwave_ioctl,
	.open		= mwave_open,
	.release	= mwave_close,
	.llseek		= default_llseek,
};


static struct miscdevice mwave_misc_dev = { MWAVE_MINOR, "mwave", &mwave_fops };

#if 0 /* totally b0rked */
/*
 * sysfs support <paulsch@us.ibm.com>
 */

struct device mwave_device;

/* Prevent code redundancy, create a macro for mwave_show_* functions. */
#define mwave_show_function(attr_name, format_string, field)		\
static ssize_t mwave_show_##attr_name(struct device *dev, struct device_attribute *attr, char *buf)	\
{									\
	DSP_3780I_CONFIG_SETTINGS *pSettings =				\
		&mwave_s_mdd.rBDData.rDspSettings;			\
        return sprintf(buf, format_string, pSettings->field);		\
}

/* All of our attributes are read attributes. */
#define mwave_dev_rd_attr(attr_name, format_string, field)		\
	mwave_show_function(attr_name, format_string, field)		\
static DEVICE_ATTR(attr_name, S_IRUGO, mwave_show_##attr_name, NULL)

mwave_dev_rd_attr (3780i_dma, "%i\n", usDspDma);
mwave_dev_rd_attr (3780i_irq, "%i\n", usDspIrq);
mwave_dev_rd_attr (3780i_io, "%#.4x\n", usDspBaseIO);
mwave_dev_rd_attr (uart_irq, "%i\n", usUartIrq);
mwave_dev_rd_attr (uart_io, "%#.4x\n", usUartBaseIO);

static struct device_attribute * const mwave_dev_attrs[] = {
	&dev_attr_3780i_dma,
	&dev_attr_3780i_irq,
	&dev_attr_3780i_io,
	&dev_attr_uart_irq,
	&dev_attr_uart_io,
};
#endif

/*
* mwave_init is called on module load
*
* mwave_exit is called on module unload
* mwave_exit is also used to clean up after an aborted mwave_init
*/
static void mwave_exit(void)
{
	pMWAVE_DEVICE_DATA pDrvData = &mwave_s_mdd;

	PRINTK_1(TRACE_MWAVE, "mwavedd::mwave_exit entry\n");

#if 0
	for (i = 0; i < pDrvData->nr_registered_attrs; i++)
		device_remove_file(&mwave_device, mwave_dev_attrs[i]);
	pDrvData->nr_registered_attrs = 0;

	if (pDrvData->device_registered) {
		device_unregister(&mwave_device);
		pDrvData->device_registered = false;
	}
#endif

	if ( pDrvData->sLine >= 0 ) {
		serial8250_unregister_port(pDrvData->sLine);
	}
	if (pDrvData->bMwaveDevRegistered) {
		misc_deregister(&mwave_misc_dev);
	}
	if (pDrvData->bDSPEnabled) {
		tp3780I_DisableDSP(&pDrvData->rBDData);
	}
	if (pDrvData->bResourcesClaimed) {
		tp3780I_ReleaseResources(&pDrvData->rBDData);
	}
	if (pDrvData->bBDInitialized) {
		tp3780I_Cleanup(&pDrvData->rBDData);
	}

	PRINTK_1(TRACE_MWAVE, "mwavedd::mwave_exit exit\n");
}

module_exit(mwave_exit);

static int __init mwave_init(void)
{
	int i;
	int retval = 0;
	pMWAVE_DEVICE_DATA pDrvData = &mwave_s_mdd;

	PRINTK_1(TRACE_MWAVE, "mwavedd::mwave_init entry\n");

	memset(&mwave_s_mdd, 0, sizeof(MWAVE_DEVICE_DATA));

	pDrvData->bBDInitialized = false;
	pDrvData->bResourcesClaimed = false;
	pDrvData->bDSPEnabled = false;
	pDrvData->bDSPReset = false;
	pDrvData->bMwaveDevRegistered = false;
	pDrvData->sLine = -1;

	for (i = 0; i < ARRAY_SIZE(pDrvData->IPCs); i++) {
		pDrvData->IPCs[i].bIsEnabled = false;
		pDrvData->IPCs[i].bIsHere = false;
		pDrvData->IPCs[i].usIntCount = 0;	/* no ints received yet */
		init_waitqueue_head(&pDrvData->IPCs[i].ipc_wait_queue);
	}

	retval = tp3780I_InitializeBoardData(&pDrvData->rBDData);
	PRINTK_2(TRACE_MWAVE,
		"mwavedd::mwave_init, return from tp3780I_InitializeBoardData"
		" retval %x\n",
		retval);
	if (retval) {
		PRINTK_ERROR(KERN_ERR_MWAVE
				"mwavedd::mwave_init: Error:"
				" Failed to initialize board data\n");
		goto cleanup_error;
	}
	pDrvData->bBDInitialized = true;

	retval = tp3780I_CalcResources(&pDrvData->rBDData);
	PRINTK_2(TRACE_MWAVE,
		"mwavedd::mwave_init, return from tp3780I_CalcResources"
		" retval %x\n",
		retval);
	if (retval) {
		PRINTK_ERROR(KERN_ERR_MWAVE
				"mwavedd:mwave_init: Error:"
				" Failed to calculate resources\n");
		goto cleanup_error;
	}

	retval = tp3780I_ClaimResources(&pDrvData->rBDData);
	PRINTK_2(TRACE_MWAVE,
		"mwavedd::mwave_init, return from tp3780I_ClaimResources"
		" retval %x\n",
		retval);
	if (retval) {
		PRINTK_ERROR(KERN_ERR_MWAVE
				"mwavedd:mwave_init: Error:"
				" Failed to claim resources\n");
		goto cleanup_error;
	}
	pDrvData->bResourcesClaimed = true;

	retval = tp3780I_EnableDSP(&pDrvData->rBDData);
	PRINTK_2(TRACE_MWAVE,
		"mwavedd::mwave_init, return from tp3780I_EnableDSP"
		" retval %x\n",
		retval);
	if (retval) {
		PRINTK_ERROR(KERN_ERR_MWAVE
				"mwavedd:mwave_init: Error:"
				" Failed to enable DSP\n");
		goto cleanup_error;
	}
	pDrvData->bDSPEnabled = true;

	if (misc_register(&mwave_misc_dev) < 0) {
		PRINTK_ERROR(KERN_ERR_MWAVE
				"mwavedd:mwave_init: Error:"
				" Failed to register misc device\n");
		goto cleanup_error;
	}
	pDrvData->bMwaveDevRegistered = true;

	pDrvData->sLine = register_serial_portandirq(
		pDrvData->rBDData.rDspSettings.usUartBaseIO,
		pDrvData->rBDData.rDspSettings.usUartIrq
	);
	if (pDrvData->sLine < 0) {
		PRINTK_ERROR(KERN_ERR_MWAVE
				"mwavedd:mwave_init: Error:"
				" Failed to register serial driver\n");
		goto cleanup_error;
	}
	/* uart is registered */

#if 0
	/* sysfs */
	memset(&mwave_device, 0, sizeof (struct device));
	dev_set_name(&mwave_device, "mwave");

	if (device_register(&mwave_device))
		goto cleanup_error;
	pDrvData->device_registered = true;
	for (i = 0; i < ARRAY_SIZE(mwave_dev_attrs); i++) {
		if(device_create_file(&mwave_device, mwave_dev_attrs[i])) {
			PRINTK_ERROR(KERN_ERR_MWAVE
					"mwavedd:mwave_init: Error:"
					" Failed to create sysfs file %s\n",
					mwave_dev_attrs[i]->attr.name);
			goto cleanup_error;
		}
		pDrvData->nr_registered_attrs++;
	}
#endif

	/* SUCCESS! */
	return 0;

cleanup_error:
	PRINTK_ERROR(KERN_ERR_MWAVE
			"mwavedd::mwave_init: Error:"
			" Failed to initialize\n");
	mwave_exit(); /* clean up */

	return -EIO;
}

module_init(mwave_init);

