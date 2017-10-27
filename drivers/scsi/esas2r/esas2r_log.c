/*
 *  linux/drivers/scsi/esas2r/esas2r_log.c
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

#include "esas2r.h"

/*
 * this module within the driver is tasked with providing logging functionality.
 * the event_log_level module parameter controls the level of messages that are
 * written to the system log.  the default level of messages that are written
 * are critical and warning messages.  if other types of messages are desired,
 * one simply needs to load the module with the correct value for the
 * event_log_level module parameter.  for example:
 *
 * insmod <module> event_log_level=1
 *
 * will load the module and only critical events will be written by this module
 * to the system log.  if critical, warning, and information-level messages are
 * desired, the correct value for the event_log_level module parameter
 * would be as follows:
 *
 * insmod <module> event_log_level=3
 */

#define EVENT_LOG_BUFF_SIZE 1024

static long event_log_level = ESAS2R_LOG_DFLT;

module_param(event_log_level, long, S_IRUGO | S_IRUSR);
MODULE_PARM_DESC(event_log_level,
		 "Specifies the level of events to report to the system log.  Critical and warning level events are logged by default.");

/* A shared buffer to use for formatting messages. */
static char event_buffer[EVENT_LOG_BUFF_SIZE];

/* A lock to protect the shared buffer used for formatting messages. */
static DEFINE_SPINLOCK(event_buffer_lock);

/**
 * translates an esas2r-defined logging event level to a kernel logging level.
 *
 * @param [in] level the esas2r-defined logging event level to translate
 *
 * @return the corresponding kernel logging level.
 */
static const char *translate_esas2r_event_level_to_kernel(const long level)
{
	switch (level) {
	case ESAS2R_LOG_CRIT:
		return KERN_CRIT;

	case ESAS2R_LOG_WARN:
		return KERN_WARNING;

	case ESAS2R_LOG_INFO:
		return KERN_INFO;

	case ESAS2R_LOG_DEBG:
	case ESAS2R_LOG_TRCE:
	default:
		return KERN_DEBUG;
	}
}

/**
 * the master logging function.  this function will format the message as
 * outlined by the formatting string, the input device information and the
 * substitution arguments and output the resulting string to the system log.
 *
 * @param [in] level  the event log level of the message
 * @param [in] dev    the device information
 * @param [in] format the formatting string for the message
 * @param [in] args   the substition arguments to the formatting string
 *
 * @return 0 on success, or -1 if an error occurred.
 */
static int esas2r_log_master(const long level,
			     const struct device *dev,
			     const char *format,
			     va_list args)
{
	if (level <= event_log_level) {
		unsigned long flags = 0;
		int retval = 0;
		char *buffer = event_buffer;
		size_t buflen = EVENT_LOG_BUFF_SIZE;
		const char *fmt_nodev = "%s%s: ";
		const char *fmt_dev = "%s%s [%s, %s, %s]";
		const char *slevel =
			translate_esas2r_event_level_to_kernel(level);

		spin_lock_irqsave(&event_buffer_lock, flags);

		memset(buffer, 0, buflen);

		/*
		 * format the level onto the beginning of the string and do
		 * some pointer arithmetic to move the pointer to the point
		 * where the actual message can be inserted.
		 */

		if (dev == NULL) {
			snprintf(buffer, buflen, fmt_nodev, slevel,
				 ESAS2R_DRVR_NAME);
		} else {
			snprintf(buffer, buflen, fmt_dev, slevel,
				 ESAS2R_DRVR_NAME,
				 (dev->driver ? dev->driver->name : "unknown"),
				 (dev->bus ? dev->bus->name : "unknown"),
				 dev_name(dev));
		}

		buffer += strlen(event_buffer);
		buflen -= strlen(event_buffer);

		retval = vsnprintf(buffer, buflen, format, args);
		if (retval < 0) {
			spin_unlock_irqrestore(&event_buffer_lock, flags);
			return -1;
		}

		/*
		 * Put a line break at the end of the formatted string so that
		 * we don't wind up with run-on messages.
		 */
		printk("%s\n", event_buffer);

		spin_unlock_irqrestore(&event_buffer_lock, flags);
	}

	return 0;
}

/**
 * formats and logs a message to the system log.
 *
 * @param [in] level  the event level of the message
 * @param [in] format the formating string for the message
 * @param [in] ...    the substitution arguments to the formatting string
 *
 * @return 0 on success, or -1 if an error occurred.
 */
int esas2r_log(const long level, const char *format, ...)
{
	int retval = 0;
	va_list args;

	va_start(args, format);

	retval = esas2r_log_master(level, NULL, format, args);

	va_end(args);

	return retval;
}

/**
 * formats and logs a message to the system log.  this message will include
 * device information.
 *
 * @param [in] level   the event level of the message
 * @param [in] dev     the device information
 * @param [in] format  the formatting string for the message
 * @param [in] ...     the substitution arguments to the formatting string
 *
 * @return 0 on success, or -1 if an error occurred.
 */
int esas2r_log_dev(const long level,
		   const struct device *dev,
		   const char *format,
		   ...)
{
	int retval = 0;
	va_list args;

	va_start(args, format);

	retval = esas2r_log_master(level, dev, format, args);

	va_end(args);

	return retval;
}

/**
 * formats and logs a message to the system log.  this message will include
 * device information.
 *
 * @param [in] level   the event level of the message
 * @param [in] buf
 * @param [in] len
 *
 * @return 0 on success, or -1 if an error occurred.
 */
int esas2r_log_hexdump(const long level,
		       const void *buf,
		       size_t len)
{
	if (level <= event_log_level) {
		print_hex_dump(translate_esas2r_event_level_to_kernel(level),
			       "", DUMP_PREFIX_OFFSET, 16, 1, buf,
			       len, true);
	}

	return 1;
}
