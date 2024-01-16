/*
 *  linux/drivers/scsi/esas2r/esas2r_log.h
 *      For use with ATTO ExpressSAS R6xx SAS/SATA RAID controllers
 *
 *  Copyright (c) 2001-2013 ATTO Technology, Inc.
 *  (mailto:linuxdrivers@attotech.com)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#ifndef __esas2r_log_h__
#define __esas2r_log_h__

struct device;

enum {
	ESAS2R_LOG_NONE = 0,    /* no events logged */
	ESAS2R_LOG_CRIT = 1,    /* critical events  */
	ESAS2R_LOG_WARN = 2,    /* warning events   */
	ESAS2R_LOG_INFO = 3,    /* info events      */
	ESAS2R_LOG_DEBG = 4,    /* debugging events */
	ESAS2R_LOG_TRCE = 5,    /* tracing events   */

#ifdef ESAS2R_TRACE
	ESAS2R_LOG_DFLT = ESAS2R_LOG_TRCE
#else
	ESAS2R_LOG_DFLT = ESAS2R_LOG_WARN
#endif
};

__printf(2, 3) int esas2r_log(const long level, const char *format, ...);
__printf(3, 4) int esas2r_log_dev(const long level,
		   const struct device *dev,
		   const char *format,
		   ...);
int esas2r_log_hexdump(const long level,
		       const void *buf,
		       size_t len);

/*
 * the following macros are provided specifically for debugging and tracing
 * messages.  esas2r_debug() is provided for generic non-hardware layer
 * debugging and tracing events.  esas2r_hdebug is provided specifically for
 * hardware layer debugging and tracing events.
 */

#ifdef ESAS2R_DEBUG
#define esas2r_debug(f, args ...) esas2r_log(ESAS2R_LOG_DEBG, f, ## args)
#define esas2r_hdebug(f, args ...) esas2r_log(ESAS2R_LOG_DEBG, f, ## args)
#else
#define esas2r_debug(f, args ...)
#define esas2r_hdebug(f, args ...)
#endif  /* ESAS2R_DEBUG */

/*
 * the following macros are provided in order to trace the driver and catch
 * some more serious bugs.  be warned, enabling these macros may *severely*
 * impact performance.
 */

#ifdef ESAS2R_TRACE
#define esas2r_bugon() \
	do { \
		esas2r_log(ESAS2R_LOG_TRCE, "esas2r_bugon() called in %s:%d" \
			   " - dumping stack and stopping kernel", __func__, \
			   __LINE__); \
		dump_stack(); \
		BUG(); \
	} while (0)

#define esas2r_trace_enter() esas2r_log(ESAS2R_LOG_TRCE, "entered %s (%s:%d)", \
					__func__, __FILE__, __LINE__)
#define esas2r_trace_exit() esas2r_log(ESAS2R_LOG_TRCE, "exited %s (%s:%d)", \
				       __func__, __FILE__, __LINE__)
#define esas2r_trace(f, args ...) esas2r_log(ESAS2R_LOG_TRCE, "(%s:%s:%d): " \
					     f, __func__, __FILE__, __LINE__, \
					     ## args)
#else
#define esas2r_bugon()
#define esas2r_trace_enter()
#define esas2r_trace_exit()
#define esas2r_trace(f, args ...)
#endif  /* ESAS2R_TRACE */

#endif  /* __esas2r_log_h__ */
