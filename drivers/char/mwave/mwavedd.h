/*
*
* mwavedd.h -- declarations for mwave device driver
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

#ifndef _LINUX_MWAVEDD_H
#define _LINUX_MWAVEDD_H
#include "3780i.h"
#include "tp3780i.h"
#include "smapi.h"
#include "mwavepub.h"
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

extern int mwave_debug;
extern int mwave_3780i_irq;
extern int mwave_3780i_io;
extern int mwave_uart_irq;
extern int mwave_uart_io;

#define PRINTK_ERROR printk
#define KERN_ERR_MWAVE KERN_ERR "mwave: "

#define TRACE_MWAVE     0x0001
#define TRACE_SMAPI     0x0002
#define TRACE_3780I     0x0004
#define TRACE_TP3780I   0x0008

#ifdef MW_TRACE
#define PRINTK_1(f,s)                       \
  if (f & (mwave_debug)) {                  \
    printk(s);                              \
  }

#define PRINTK_2(f,s,v1)                    \
  if (f & (mwave_debug)) {                  \
    printk(s,v1);                           \
  }

#define PRINTK_3(f,s,v1,v2)                 \
  if (f & (mwave_debug)) {                  \
    printk(s,v1,v2);                        \
  }

#define PRINTK_4(f,s,v1,v2,v3)              \
  if (f & (mwave_debug)) {                  \
    printk(s,v1,v2,v3);                     \
  }

#define PRINTK_5(f,s,v1,v2,v3,v4)           \
  if (f & (mwave_debug)) {                  \
    printk(s,v1,v2,v3,v4);                  \
  }

#define PRINTK_6(f,s,v1,v2,v3,v4,v5)        \
  if (f & (mwave_debug)) {                  \
    printk(s,v1,v2,v3,v4,v5);               \
  }

#define PRINTK_7(f,s,v1,v2,v3,v4,v5,v6)     \
  if (f & (mwave_debug)) {                  \
    printk(s,v1,v2,v3,v4,v5,v6);            \
  }

#define PRINTK_8(f,s,v1,v2,v3,v4,v5,v6,v7)  \
  if (f & (mwave_debug)) {                  \
    printk(s,v1,v2,v3,v4,v5,v6,v7);         \
  }

#else
#define PRINTK_1(f,s)
#define PRINTK_2(f,s,v1)
#define PRINTK_3(f,s,v1,v2)
#define PRINTK_4(f,s,v1,v2,v3)
#define PRINTK_5(f,s,v1,v2,v3,v4)
#define PRINTK_6(f,s,v1,v2,v3,v4,v5)
#define PRINTK_7(f,s,v1,v2,v3,v4,v5,v6)
#define PRINTK_8(f,s,v1,v2,v3,v4,v5,v6,v7)
#endif


typedef struct _MWAVE_IPC {
	unsigned short usIntCount;	/* 0=none, 1=first, 2=greater than 1st */
	bool bIsEnabled;
	bool bIsHere;
	/* entry spin lock */
	wait_queue_head_t ipc_wait_queue;
} MWAVE_IPC;

typedef struct _MWAVE_DEVICE_DATA {
	THINKPAD_BD_DATA rBDData;	/* board driver's data area */
	unsigned long ulIPCSource_ISR;	/* IPC source bits for recently processed intr, set during ISR processing */
	unsigned long ulIPCSource_DPC;	/* IPC source bits for recently processed intr, set during DPC processing */
	bool bBDInitialized;
	bool bResourcesClaimed;
	bool bDSPEnabled;
	bool bDSPReset;
	MWAVE_IPC IPCs[16];
	bool bMwaveDevRegistered;
	short sLine;
	int nr_registered_attrs;
	int device_registered;

} MWAVE_DEVICE_DATA, *pMWAVE_DEVICE_DATA;

extern MWAVE_DEVICE_DATA mwave_s_mdd;

#endif
