/*
 *  linux/drivers/message/fusion/mptscsih.h
 *      High performance SCSI / Fibre Channel SCSI Host device driver.
 *      For use with PCI chip/adapter(s):
 *          LSIFC9xx/LSI409xx Fibre Channel
 *      running LSI Logic Fusion MPT (Message Passing Technology) firmware.
 *
 *  Credits:
 *      This driver would not exist if not for Alan Cox's development
 *      of the linux i2o driver.
 *
 *      A huge debt of gratitude is owed to David S. Miller (DaveM)
 *      for fixing much of the stupid and broken stuff in the early
 *      driver while porting to sparc64 platform.  THANK YOU!
 *
 *      (see also mptbase.c)
 *
 *  Copyright (c) 1999-2004 LSI Logic Corporation
 *  Originally By: Steven J. Ralston
 *  (mailto:netscape.net)
 *  (mailto:mpt_linux_developer@lsil.com)
 *
 *  $Id: mptscsih.h,v 1.21 2002/12/03 21:26:35 pdelaney Exp $
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

#ifndef SCSIHOST_H_INCLUDED
#define SCSIHOST_H_INCLUDED

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	SCSI Public stuff...
 */

#define MPT_SCSI_CMD_PER_DEV_HIGH	31
#define MPT_SCSI_CMD_PER_DEV_LOW	7

#define MPT_SCSI_CMD_PER_LUN		7

#define MPT_SCSI_MAX_SECTORS    8192

/* To disable domain validation, uncomment the
 * following line. No effect for FC devices.
 * For SCSI devices, driver will negotiate to
 * NVRAM settings (if available) or to maximum adapter
 * capabilities.
 */

#define MPTSCSIH_ENABLE_DOMAIN_VALIDATION


/* SCSI driver setup structure. Settings can be overridden
 * by command line options.
 */
#define MPTSCSIH_DOMAIN_VALIDATION      1
#define MPTSCSIH_MAX_WIDTH              1
#define MPTSCSIH_MIN_SYNC               0x08
#define MPTSCSIH_SAF_TE                 0

#endif
