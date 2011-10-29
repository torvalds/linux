/*
 * Logging Support for MPT (Message Passing Technology) based controllers
 *
 * This code is based on drivers/scsi/mpt2sas/mpt2_debug.c
 * Copyright (C) 2007-2010  LSI Corporation
 *  (mailto:DL-MPTFusionLinux@lsi.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
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

 * DISCLAIMER OF LIABILITY
 * NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
 * HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#ifndef MPT2SAS_DEBUG_H_INCLUDED
#define MPT2SAS_DEBUG_H_INCLUDED

#define MPT_DEBUG			0x00000001
#define MPT_DEBUG_MSG_FRAME		0x00000002
#define MPT_DEBUG_SG			0x00000004
#define MPT_DEBUG_EVENTS		0x00000008
#define MPT_DEBUG_EVENT_WORK_TASK	0x00000010
#define MPT_DEBUG_INIT			0x00000020
#define MPT_DEBUG_EXIT			0x00000040
#define MPT_DEBUG_FAIL			0x00000080
#define MPT_DEBUG_TM			0x00000100
#define MPT_DEBUG_REPLY			0x00000200
#define MPT_DEBUG_HANDSHAKE		0x00000400
#define MPT_DEBUG_CONFIG		0x00000800
#define MPT_DEBUG_DL			0x00001000
#define MPT_DEBUG_RESET			0x00002000
#define MPT_DEBUG_SCSI			0x00004000
#define MPT_DEBUG_IOCTL			0x00008000
#define MPT_DEBUG_CSMISAS		0x00010000
#define MPT_DEBUG_SAS			0x00020000
#define MPT_DEBUG_TRANSPORT		0x00040000
#define MPT_DEBUG_TASK_SET_FULL		0x00080000

#define MPT_DEBUG_TARGET_MODE		0x00100000


/*
 * CONFIG_SCSI_MPT2SAS_LOGGING - enabled in Kconfig
 */

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
#define MPT_CHECK_LOGGING(IOC, CMD, BITS)			\
{								\
	if (IOC->logging_level & BITS)				\
		CMD;						\
}
#else
#define MPT_CHECK_LOGGING(IOC, CMD, BITS)
#endif /* CONFIG_SCSI_MPT2SAS_LOGGING */


/*
 * debug macros
 */

#define dprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG)

#define dsgprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG_SG)

#define devtprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG_EVENTS)

#define dewtprintk(IOC, CMD)		\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG_EVENT_WORK_TASK)

#define dinitprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG_INIT)

#define dexitprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG_EXIT)

#define dfailprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG_FAIL)

#define dtmprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG_TM)

#define dreplyprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG_REPLY)

#define dhsprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG_HANDSHAKE)

#define dcprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG_CONFIG)

#define ddlprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG_DL)

#define drsprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG_RESET)

#define dsprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG_SCSI)

#define dctlprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG_IOCTL)

#define dcsmisasprintk(IOC, CMD)		\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG_CSMISAS)

#define dsasprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG_SAS)

#define dsastransport(IOC, CMD)		\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG_SAS_WIDE)

#define dmfprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG_MSG_FRAME)

#define dtsfprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG_TASK_SET_FULL)

#define dtransportprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG_TRANSPORT)

#define dTMprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DEBUG_TARGET_MODE)

/* inline functions for dumping debug data*/
#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
/**
 * _debug_dump_mf - print message frame contents
 * @mpi_request: pointer to message frame
 * @sz: number of dwords
 */
static inline void
_debug_dump_mf(void *mpi_request, int sz)
{
	int i;
	__le32 *mfp = (__le32 *)mpi_request;

	printk(KERN_INFO "mf:\n\t");
	for (i = 0; i < sz; i++) {
		if (i && ((i % 8) == 0))
			printk("\n\t");
		printk("%08x ", le32_to_cpu(mfp[i]));
	}
	printk("\n");
}
#else
#define _debug_dump_mf(mpi_request, sz)
#endif /* CONFIG_SCSI_MPT2SAS_LOGGING */

#endif /* MPT2SAS_DEBUG_H_INCLUDED */
