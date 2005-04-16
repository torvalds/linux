/*
*
* mwavepub.h -- PUBLIC declarations for the mwave driver
*               and applications using it
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

#ifndef _LINUX_MWAVEPUB_H
#define _LINUX_MWAVEPUB_H

#include <linux/miscdevice.h>


typedef struct _MW_ABILITIES {
	unsigned long instr_per_sec;
	unsigned long data_size;
	unsigned long inst_size;
	unsigned long bus_dma_bw;
	unsigned short uart_enable;
	short component_count;
	unsigned long component_list[7];
	char mwave_os_name[16];
	char bios_task_name[16];
} MW_ABILITIES, *pMW_ABILITIES;


typedef struct _MW_READWRITE {
	unsigned short usDspAddress;	/* The dsp address */
	unsigned long ulDataLength;	/* The size in bytes of the data or user buffer */
	void *pBuf;		/* Input:variable sized buffer */
} MW_READWRITE, *pMW_READWRITE;

#define IOCTL_MW_RESET           _IO(MWAVE_MINOR,1)
#define IOCTL_MW_RUN             _IO(MWAVE_MINOR,2)
#define IOCTL_MW_DSP_ABILITIES   _IOR(MWAVE_MINOR,3,MW_ABILITIES)
#define IOCTL_MW_READ_DATA       _IOR(MWAVE_MINOR,4,MW_READWRITE)
#define IOCTL_MW_READCLEAR_DATA  _IOR(MWAVE_MINOR,5,MW_READWRITE)
#define IOCTL_MW_READ_INST       _IOR(MWAVE_MINOR,6,MW_READWRITE)
#define IOCTL_MW_WRITE_DATA      _IOW(MWAVE_MINOR,7,MW_READWRITE)
#define IOCTL_MW_WRITE_INST      _IOW(MWAVE_MINOR,8,MW_READWRITE)
#define IOCTL_MW_REGISTER_IPC    _IOW(MWAVE_MINOR,9,int)
#define IOCTL_MW_UNREGISTER_IPC  _IOW(MWAVE_MINOR,10,int)
#define IOCTL_MW_GET_IPC         _IOW(MWAVE_MINOR,11,int)
#define IOCTL_MW_TRACE           _IOR(MWAVE_MINOR,12,MW_READWRITE)


#endif
