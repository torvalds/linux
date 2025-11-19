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

#define pr_fmt(fmt) "mwavedd: " fmt

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
int mwave_3780i_irq = 0;
int mwave_3780i_io = 0;
int mwave_uart_irq = 0;
int mwave_uart_io = 0;
module_param_hw(mwave_3780i_irq, int, irq, 0);
module_param_hw(mwave_3780i_io, int, ioport, 0);
module_param_hw(mwave_uart_irq, int, irq, 0);
module_param_hw(mwave_uart_io, int, ioport, 0);

struct mwave_device_data mwave_s_mdd;

static long mwave_ioctl(struct file *file, unsigned int iocmd,
							unsigned long ioarg)
{
	unsigned int retval = 0;
	struct mwave_device_data *pDrvData = &mwave_s_mdd;
	void __user *arg = (void __user *)ioarg;

	switch (iocmd) {

		case IOCTL_MW_RESET:
			mutex_lock(&mwave_mutex);
			retval = tp3780I_ResetDSP(&pDrvData->rBDData);
			mutex_unlock(&mwave_mutex);
			break;
	
		case IOCTL_MW_RUN:
			mutex_lock(&mwave_mutex);
			retval = tp3780I_StartDSP(&pDrvData->rBDData);
			mutex_unlock(&mwave_mutex);
			break;
	
		case IOCTL_MW_DSP_ABILITIES: {
			struct mw_abilities rAbilities;
	
			mutex_lock(&mwave_mutex);
			retval = tp3780I_QueryAbilities(&pDrvData->rBDData,
					&rAbilities);
			mutex_unlock(&mwave_mutex);
			if (retval == 0) {
				if (copy_to_user(arg, &rAbilities, sizeof(rAbilities)))
					return -EFAULT;
			}
		}
			break;
	
		case IOCTL_MW_READ_DATA:
		case IOCTL_MW_READCLEAR_DATA: {
			struct mw_readwrite rReadData;
			unsigned short __user *pusBuffer = NULL;
	
			if( copy_from_user(&rReadData, arg,
						sizeof(struct mw_readwrite)) )
				return -EFAULT;
			pusBuffer = (unsigned short __user *) (rReadData.pBuf);
	
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
			struct mw_readwrite rReadData;
			unsigned short __user *pusBuffer = NULL;
	
			if (copy_from_user(&rReadData, arg, sizeof(rReadData)))
				return -EFAULT;
			pusBuffer = (unsigned short __user *) (rReadData.pBuf);
	
			mutex_lock(&mwave_mutex);
			retval = tp3780I_ReadWriteDspDStore(&pDrvData->rBDData,
				iocmd, pusBuffer,
				rReadData.ulDataLength / 2,
				rReadData.usDspAddress);
			mutex_unlock(&mwave_mutex);
		}
			break;
	
		case IOCTL_MW_WRITE_DATA: {
			struct mw_readwrite rWriteData;
			unsigned short __user *pusBuffer = NULL;
	
			if (copy_from_user(&rWriteData, arg, sizeof(rWriteData)))
				return -EFAULT;
			pusBuffer = (unsigned short __user *) (rWriteData.pBuf);
	
			mutex_lock(&mwave_mutex);
			retval = tp3780I_ReadWriteDspDStore(&pDrvData->rBDData,
					iocmd, pusBuffer,
					rWriteData.ulDataLength,
					rWriteData.usDspAddress);
			mutex_unlock(&mwave_mutex);
		}
			break;
	
		case IOCTL_MW_WRITE_INST: {
			struct mw_readwrite rWriteData;
			unsigned short __user *pusBuffer = NULL;
	
			if (copy_from_user(&rWriteData, arg, sizeof(rWriteData)))
				return -EFAULT;
			pusBuffer = (unsigned short __user *)(rWriteData.pBuf);
	
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
				pr_err("%s: IOCTL_MW_REGISTER_IPC: Error: Invalid ipcnum %x\n",
				       __func__, ipcnum);
				return -EINVAL;
			}
			ipcnum = array_index_nospec(ipcnum,
						    ARRAY_SIZE(pDrvData->IPCs));

			mutex_lock(&mwave_mutex);
			pDrvData->IPCs[ipcnum].bIsHere = false;
			pDrvData->IPCs[ipcnum].bIsEnabled = true;
			mutex_unlock(&mwave_mutex);
		}
			break;
	
		case IOCTL_MW_GET_IPC: {
			unsigned int ipcnum = (unsigned int) ioarg;
	
			if (ipcnum >= ARRAY_SIZE(pDrvData->IPCs)) {
				pr_err("%s: IOCTL_MW_GET_IPC: Error: Invalid ipcnum %x\n", __func__,
				       ipcnum);
				return -EINVAL;
			}
			ipcnum = array_index_nospec(ipcnum,
						    ARRAY_SIZE(pDrvData->IPCs));

			mutex_lock(&mwave_mutex);
			if (pDrvData->IPCs[ipcnum].bIsEnabled == true) {
				DECLARE_WAITQUEUE(wait, current);

				add_wait_queue(&pDrvData->IPCs[ipcnum].ipc_wait_queue, &wait);
				pDrvData->IPCs[ipcnum].bIsHere = true;
				set_current_state(TASK_INTERRUPTIBLE);
				/* check whether an event was signalled by */
				/* the interrupt handler while we were gone */
				if (pDrvData->IPCs[ipcnum].usIntCount == 1) {	/* first int has occurred (race condition) */
					pDrvData->IPCs[ipcnum].usIntCount = 2;	/* first int has been handled */
				} else {	/* either 1st int has not yet occurred, or we have already handled the first int */
					schedule();
					if (pDrvData->IPCs[ipcnum].usIntCount == 1) {
						pDrvData->IPCs[ipcnum].usIntCount = 2;
					}
				}
				pDrvData->IPCs[ipcnum].bIsHere = false;
				remove_wait_queue(&pDrvData->IPCs[ipcnum].ipc_wait_queue, &wait);
				set_current_state(TASK_RUNNING);
			}
			mutex_unlock(&mwave_mutex);
		}
			break;
	
		case IOCTL_MW_UNREGISTER_IPC: {
			unsigned int ipcnum = (unsigned int) ioarg;
	
			if (ipcnum >= ARRAY_SIZE(pDrvData->IPCs)) {
				pr_err("%s: IOCTL_MW_UNREGISTER_IPC: Error: Invalid ipcnum %x\n",
				       __func__, ipcnum);
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

	return retval;
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
			pr_err("%s: Error: Illegal port %x\n", __func__, port);
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
			pr_err("%s: Error: Illegal irq %x\n", __func__, irq);
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
	.unlocked_ioctl	= mwave_ioctl,
	.llseek		= default_llseek,
};

static struct miscdevice mwave_misc_dev = { MWAVE_MINOR, "mwave", &mwave_fops };

/*
* mwave_init is called on module load
*
* mwave_exit is called on module unload
* mwave_exit is also used to clean up after an aborted mwave_init
*/
static void mwave_exit(void)
{
	struct mwave_device_data *pDrvData = &mwave_s_mdd;

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
}

module_exit(mwave_exit);

static int __init mwave_init(void)
{
	int i;
	int retval = 0;
	struct mwave_device_data *pDrvData = &mwave_s_mdd;

	memset(&mwave_s_mdd, 0, sizeof(mwave_s_mdd));

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
	if (retval) {
		pr_err("%s: Error: Failed to initialize board data\n", __func__);
		goto cleanup_error;
	}
	pDrvData->bBDInitialized = true;

	retval = tp3780I_CalcResources(&pDrvData->rBDData);
	if (retval) {
		pr_err("%s: Error: Failed to calculate resources\n", __func__);
		goto cleanup_error;
	}

	retval = tp3780I_ClaimResources(&pDrvData->rBDData);
	if (retval) {
		pr_err("%s: Error: Failed to claim resources\n", __func__);
		goto cleanup_error;
	}
	pDrvData->bResourcesClaimed = true;

	retval = tp3780I_EnableDSP(&pDrvData->rBDData);
	if (retval) {
		pr_err("%s: Error: Failed to enable DSP\n", __func__);
		goto cleanup_error;
	}
	pDrvData->bDSPEnabled = true;

	if (misc_register(&mwave_misc_dev) < 0) {
		pr_err("%s: Error: Failed to register misc device\n", __func__);
		goto cleanup_error;
	}
	pDrvData->bMwaveDevRegistered = true;

	pDrvData->sLine = register_serial_portandirq(
		pDrvData->rBDData.rDspSettings.usUartBaseIO,
		pDrvData->rBDData.rDspSettings.usUartIrq
	);
	if (pDrvData->sLine < 0) {
		pr_err("%s: Error: Failed to register serial driver\n", __func__);
		goto cleanup_error;
	}
	/* uart is registered */

	/* SUCCESS! */
	return 0;

cleanup_error:
	pr_err("%s: Error: Failed to initialize\n", __func__);
	mwave_exit(); /* clean up */

	return -EIO;
}

module_init(mwave_init);

