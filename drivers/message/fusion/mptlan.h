/*
 *  linux/drivers/message/fusion/mptlan.h
 *      IP Over Fibre Channel device driver.
 *      For use with LSI Logic Fibre Channel PCI chip/adapters
 *      running LSI Logic Fusion MPT (Message Passing Technology) firmware.
 *
 *  Copyright (c) 2000-2005 LSI Logic Corporation
 *
 */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    NO WARRANTY
    THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
    CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
    LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
    solely responsible for determining the appropriateness of using and
    distributing the Program and assumes all risks associated with its
    exercise of rights under this Agreement, including but not limited to
    the risks and costs of program errors, damage to or loss of data,
    programs or equipment, and unavailability or interruption of operations.

    DISCLAIMER OF LIABILITY
    NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
    TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
    USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
    HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

/* mptlan.h */

#ifndef LINUX_MPTLAN_H_INCLUDED
#define LINUX_MPTLAN_H_INCLUDED
/*****************************************************************************/

#if !defined(__GENKSYMS__)
#include <linux/module.h>
#endif

#include <linux/netdevice.h>
#include <linux/errno.h>
// #include <linux/etherdevice.h>
#include <linux/fcdevice.h>
// #include <linux/fddidevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
// #include <linux/trdevice.h>

#include <asm/uaccess.h>
#include <asm/io.h>

    /* Override mptbase.h by pre-defining these! */
#define MODULEAUTHOR	"LSI Logic Corporation"

#include "mptbase.h"

/*****************************************************************************/
#define LANAME		"Fusion MPT LAN driver"
#define LANVER		MPT_LINUX_VERSION_COMMON

#ifdef MODULE
MODULE_AUTHOR(MODULEAUTHOR);
MODULE_DESCRIPTION(LANAME);
#endif
/*****************************************************************************/

#define MPT_LAN_MAX_BUCKETS_OUT 256
#define MPT_LAN_BUCKET_THRESH	18 /* 9 buckets in one message */
#define MPT_LAN_BUCKETS_REMAIN_MISMATCH_THRESH 10
#define MPT_LAN_RX_COPYBREAK	200
#define MPT_LAN_TX_TIMEOUT	(1*HZ)
#define MPT_TX_MAX_OUT_LIM      127

#define MPT_LAN_MIN_MTU		96		/* RFC2625 */
#define MPT_LAN_MAX_MTU		65280		/* RFC2625 */
#define MPT_LAN_MTU             13312		/* Max perf range + lower mem
						   usage than 16128 */

#define MPT_LAN_NAA_RFC2625     0x1
#define MPT_LAN_NAA_QLOGIC      0x2

/* MPT LAN Reset and Suspend Resource Flags Defines */

#define MPT_LAN_RESOURCE_FLAG_RETURN_POSTED_BUCKETS    0x01
#define MPT_LAN_RESOURCE_FLAG_RETURN_PEND_TRANSMITS    0x02

/*****************************************************************************/
#ifdef MPT_LAN_IO_DEBUG
#define dioprintk(x)  printk x
#else
#define dioprintk(x)
#endif

#ifdef MPT_LAN_DEBUG
#define dlprintk(x)  printk x
#else
#define dlprintk(x)
#endif

#define NETDEV_TO_LANPRIV_PTR(d)	((struct mpt_lan_priv *)(d)->priv)
#define NETDEV_PTR_TO_IOC_NAME_s(d)	(NETDEV_TO_LANPRIV_PTR(d)->mpt_dev->name)
#define IOC_AND_NETDEV_NAMES_s_s(d)	NETDEV_PTR_TO_IOC_NAME_s(d), (d)->name

/*****************************************************************************/
#endif

