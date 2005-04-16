/*
 *      Copyright (C) 1993-1996 Bas Laarhoven,
 *                (C) 1996      Kai Harrekilde-Petersen,
 *                (C) 1997      Claus-Justus Heine.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/ftape-io.c,v $
 * $Revision: 1.4 $
 * $Date: 1997/11/11 14:02:36 $
 *
 *      This file contains the general control functions for the
 *      QIC-40/80/3010/3020 floppy-tape driver "ftape" for Linux.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/system.h>
#include <linux/ioctl.h>
#include <linux/mtio.h>
#include <linux/delay.h>

#include <linux/ftape.h>
#include <linux/qic117.h>
#include "../lowlevel/ftape-tracing.h"
#include "../lowlevel/fdc-io.h"
#include "../lowlevel/ftape-io.h"
#include "../lowlevel/ftape-ctl.h"
#include "../lowlevel/ftape-rw.h"
#include "../lowlevel/ftape-write.h"
#include "../lowlevel/ftape-read.h"
#include "../lowlevel/ftape-init.h"
#include "../lowlevel/ftape-calibr.h"

/*      Global vars.
 */
/* NOTE: sectors start numbering at 1, all others at 0 ! */
ft_timeout_table ftape_timeout;
unsigned int ftape_tape_len;
volatile qic117_cmd_t ftape_current_command;
const struct qic117_command_table qic117_cmds[] = QIC117_COMMANDS;
int ftape_might_be_off_track;

/*      Local vars.
 */
static int diagnostic_mode;
static unsigned int ftape_udelay_count;
static unsigned int ftape_udelay_time;

void ftape_udelay(unsigned int usecs)
{
	volatile int count = (ftape_udelay_count * usecs +
                              ftape_udelay_count - 1) / ftape_udelay_time;
	volatile int i;

	while (count-- > 0) {
		for (i = 0; i < 20; ++i);
	}
}

void ftape_udelay_calibrate(void)
{
	ftape_calibrate("ftape_udelay",
			ftape_udelay, &ftape_udelay_count, &ftape_udelay_time);
}

/*      Delay (msec) routine.
 */
void ftape_sleep(unsigned int time)
{
	TRACE_FUN(ft_t_any);

	time *= 1000;   /* msecs -> usecs */
	if (time < FT_USPT) {
		/*  Time too small for scheduler, do a busy wait ! */
		ftape_udelay(time);
	} else {
		long timeout;
		unsigned long flags;
		unsigned int ticks = (time + FT_USPT - 1) / FT_USPT;

		TRACE(ft_t_any, "%d msec, %d ticks", time/1000, ticks);
		timeout = ticks;
		save_flags(flags);
		sti();
		msleep_interruptible(jiffies_to_msecs(timeout));
		/*  Mmm. Isn't current->blocked == 0xffffffff ?
		 */
		if (signal_pending(current)) {
			TRACE(ft_t_err, "awoken by non-blocked signal :-(");
		}
		restore_flags(flags);
	}
	TRACE_EXIT;
}

/*  send a command or parameter to the drive
 *  Generates # of step pulses.
 */
static inline int ft_send_to_drive(int arg)
{
	/*  Always wait for a command_timeout period to separate
	 *  individuals commands and/or parameters.
	 */
	ftape_sleep(3 * FT_MILLISECOND);
	/*  Keep cylinder nr within range, step towards home if possible.
	 */
	if (ftape_current_cylinder >= arg) {
		return fdc_seek(ftape_current_cylinder - arg);
	} else {
		return fdc_seek(ftape_current_cylinder + arg);
	}
}

/* forward */ int ftape_report_raw_drive_status(int *status);

static int ft_check_cmd_restrictions(qic117_cmd_t command)
{
	int status = -1;
	TRACE_FUN(ft_t_any);
	
	TRACE(ft_t_flow, "%s", qic117_cmds[command].name);
	/* A new motion command during an uninterruptible (motion)
	 *  command requires a ready status before the new command can
	 *  be issued. Otherwise a new motion command needs to be
	 *  checked against required status.
	 */
	if (qic117_cmds[command].cmd_type == motion &&
	    qic117_cmds[ftape_current_command].non_intr) {
		ftape_report_raw_drive_status(&status);
		if ((status & QIC_STATUS_READY) == 0) {
			TRACE(ft_t_noise,
			      "motion cmd (%d) during non-intr cmd (%d)",
			      command, ftape_current_command);
			TRACE(ft_t_noise, "waiting until drive gets ready");
			ftape_ready_wait(ftape_timeout.seek,
					 &status);
		}
	}
	if (qic117_cmds[command].mask != 0) {
		__u8 difference;
		/*  Some commands do require a certain status:
		 */
		if (status == -1) {	/* not yet set */
			ftape_report_raw_drive_status(&status);
		}
		difference = ((status ^ qic117_cmds[command].state) &
			      qic117_cmds[command].mask);
		/*  Wait until the drive gets
		 *  ready. This may last forever if
		 *  the drive never gets ready... 
		 */
		while ((difference & QIC_STATUS_READY) != 0) {
			TRACE(ft_t_noise, "command %d issued while not ready",
			      command);
			TRACE(ft_t_noise, "waiting until drive gets ready");
			if (ftape_ready_wait(ftape_timeout.seek,
					     &status) == -EINTR) {
				/*  Bail out on signal !
				 */
				TRACE_ABORT(-EINTR, ft_t_warn,
				      "interrupted by non-blockable signal");
			}
			difference = ((status ^ qic117_cmds[command].state) &
				      qic117_cmds[command].mask);
		}
		while ((difference & QIC_STATUS_ERROR) != 0) {
			int err;
			qic117_cmd_t cmd;

			TRACE(ft_t_noise,
			      "command %d issued while error pending",
			      command);
			TRACE(ft_t_noise, "clearing error status");
			ftape_report_error(&err, &cmd, 1);
			ftape_report_raw_drive_status(&status);
			difference = ((status ^ qic117_cmds[command].state) &
				      qic117_cmds[command].mask);
			if ((difference & QIC_STATUS_ERROR) != 0) {
				/*  Bail out on fatal signal !
				 */
				FT_SIGNAL_EXIT(_NEVER_BLOCK);
			}
		}
		if (difference) {
			/*  Any remaining difference can't be solved
			 *  here.  
			 */
			if (difference & (QIC_STATUS_CARTRIDGE_PRESENT |
					  QIC_STATUS_NEW_CARTRIDGE |
					  QIC_STATUS_REFERENCED)) {
				TRACE(ft_t_warn,
				      "Fatal: tape removed or reinserted !");
				ft_failure = 1;
			} else {
				TRACE(ft_t_err, "wrong state: 0x%02x should be: 0x%02x",
				      status & qic117_cmds[command].mask,
				      qic117_cmds[command].state);
			}
			TRACE_EXIT -EIO;
		}
		if (~status & QIC_STATUS_READY & qic117_cmds[command].mask) {
			TRACE_ABORT(-EBUSY, ft_t_err, "Bad: still busy!");
		}
	}
	TRACE_EXIT 0;
}

/*      Issue a tape command:
 */
int ftape_command(qic117_cmd_t command)
{
	int result = 0;
	static int level;
	TRACE_FUN(ft_t_any);

	if ((unsigned int)command > NR_ITEMS(qic117_cmds)) {
		/*  This is a bug we'll want to know about too.
		 */
		TRACE_ABORT(-EIO, ft_t_bug, "bug - bad command: %d", command);
	}
	if (++level > 5) { /*  This is a bug we'll want to know about. */
		--level;
		TRACE_ABORT(-EIO, ft_t_bug, "bug - recursion for command: %d",
			    command);
	}
	/*  disable logging and restriction check for some commands,
	 *  check all other commands that have a prescribed starting
	 *  status.
	 */
	if (diagnostic_mode) {
		TRACE(ft_t_flow, "diagnostic command %d", command);
	} else if (command == QIC_REPORT_DRIVE_STATUS ||
		   command == QIC_REPORT_NEXT_BIT) {
		TRACE(ft_t_any, "%s", qic117_cmds[command].name);
	} else {
		TRACE_CATCH(ft_check_cmd_restrictions(command), --level);
	}
	/*  Now all conditions are met or result was < 0.
	 */
	result = ft_send_to_drive((unsigned int)command);
	if (qic117_cmds[command].cmd_type == motion &&
	    command != QIC_LOGICAL_FORWARD && command != QIC_STOP_TAPE) {
		ft_location.known = 0;
	}
	ftape_current_command = command;
	--level;
	TRACE_EXIT result;
}

/*      Send a tape command parameter:
 *      Generates command # of step pulses.
 *      Skips tape-status call !
 */
int ftape_parameter(unsigned int parameter)
{
	TRACE_FUN(ft_t_any);

	TRACE(ft_t_flow, "called with parameter = %d", parameter);
	TRACE_EXIT ft_send_to_drive(parameter + 2);
}

/*      Wait for the drive to get ready.
 *      timeout time in milli-seconds
 *      Returned status is valid if result != -EIO
 *
 *      Should we allow to be killed by SIGINT?  (^C)
 *      Would be nice at least for large timeouts.
 */
int ftape_ready_wait(unsigned int timeout, int *status)
{
	unsigned long t0;
	unsigned int poll_delay;
	int signal_retries;
	TRACE_FUN(ft_t_any);

	/*  the following ** REALLY ** reduces the system load when
	 *  e.g. one simply rewinds or retensions. The tape is slow 
	 *  anyway. It is really not necessary to detect error 
	 *  conditions with 1/10 seconds granularity
	 *
	 *  On my AMD 133MHZ 486: 100 ms: 23% system load
	 *                        1  sec:  5%
	 *                        5  sec:  0.6%, yeah
	 */
	if (timeout <= FT_SECOND) {
		poll_delay = 100 * FT_MILLISECOND;
		signal_retries = 20; /* two seconds */
	} else if (timeout < 20 * FT_SECOND) {
		TRACE(ft_t_flow, "setting poll delay to 1 second");
		poll_delay = FT_SECOND;
		signal_retries = 2; /* two seconds */
	} else {
		TRACE(ft_t_flow, "setting poll delay to 5 seconds");
		poll_delay = 5 * FT_SECOND;
		signal_retries = 1; /* five seconds */
	}
	for (;;) {
		t0 = jiffies;
		TRACE_CATCH(ftape_report_raw_drive_status(status),);
		if (*status & QIC_STATUS_READY) {
			TRACE_EXIT 0;
		}
		if (!signal_retries--) {
			FT_SIGNAL_EXIT(_NEVER_BLOCK);
		}
		if ((int)timeout >= 0) {
			/* this will fail when jiffies wraps around about
			 * once every year :-)
			 */
			timeout -= ((jiffies - t0) * FT_SECOND) / HZ;
			if (timeout <= 0) {
				TRACE_ABORT(-ETIME, ft_t_err, "timeout");
			}
			ftape_sleep(poll_delay);
			timeout -= poll_delay;
		} else {
			ftape_sleep(poll_delay);
		}
	}
	TRACE_EXIT -ETIME;
}

/*      Issue command and wait up to timeout milli seconds for drive ready
 */
int ftape_command_wait(qic117_cmd_t command, unsigned int timeout, int *status)
{
	int result;

	/* Drive should be ready, issue command
	 */
	result = ftape_command(command);
	if (result >= 0) {
		result = ftape_ready_wait(timeout, status);
	}
	return result;
}

static int ftape_parameter_wait(unsigned int parm, unsigned int timeout, int *status)
{
	int result;

	/* Drive should be ready, issue command
	 */
	result = ftape_parameter(parm);
	if (result >= 0) {
		result = ftape_ready_wait(timeout, status);
	}
	return result;
}

/*--------------------------------------------------------------------------
 *      Report operations
 */

/* Query the drive about its status.  The command is sent and
   result_length bits of status are returned (2 extra bits are read
   for start and stop). */

int ftape_report_operation(int *status,
			   qic117_cmd_t command,
			   int result_length)
{
	int i, st3;
	unsigned int t0;
	unsigned int dt;
	TRACE_FUN(ft_t_any);

	TRACE_CATCH(ftape_command(command),);
	t0 = ftape_timestamp();
	i = 0;
	do {
		++i;
		ftape_sleep(3 * FT_MILLISECOND);	/* see remark below */
		TRACE_CATCH(fdc_sense_drive_status(&st3),);
		dt = ftape_timediff(t0, ftape_timestamp());
		/*  Ack should be asserted within Ttimout + Tack = 6 msec.
		 *  Looks like some drives fail to do this so extend this
		 *  period to 300 msec.
		 */
	} while (!(st3 & ST3_TRACK_0) && dt < 300000);
	if (!(st3 & ST3_TRACK_0)) {
		TRACE(ft_t_err,
		      "No acknowledge after %u msec. (%i iter)", dt / 1000, i);
		TRACE_ABORT(-EIO, ft_t_err, "timeout on Acknowledge");
	}
	/*  dt may be larger than expected because of other tasks
	 *  scheduled while we were sleeping.
	 */
	if (i > 1 && dt > 6000) {
		TRACE(ft_t_err, "Acknowledge after %u msec. (%i iter)",
		      dt / 1000, i);
	}
	*status = 0;
	for (i = 0; i < result_length + 1; i++) {
		TRACE_CATCH(ftape_command(QIC_REPORT_NEXT_BIT),);
		TRACE_CATCH(fdc_sense_drive_status(&st3),);
		if (i < result_length) {
			*status |= ((st3 & ST3_TRACK_0) ? 1 : 0) << i;
		} else if ((st3 & ST3_TRACK_0) == 0) {
			TRACE_ABORT(-EIO, ft_t_err, "missing status stop bit");
		}
	}
	/* this command will put track zero and index back into normal state */
	(void)ftape_command(QIC_REPORT_NEXT_BIT);
	TRACE_EXIT 0;
}

/* Report the current drive status. */

int ftape_report_raw_drive_status(int *status)
{
	int result;
	int count = 0;
	TRACE_FUN(ft_t_any);

	do {
		result = ftape_report_operation(status,
						QIC_REPORT_DRIVE_STATUS, 8);
	} while (result < 0 && ++count <= 3);
	if (result < 0) {
		TRACE_ABORT(-EIO, ft_t_err,
			    "report_operation failed after %d trials", count);
	}
	if ((*status & 0xff) == 0xff) {
		TRACE_ABORT(-EIO, ft_t_err,
			    "impossible drive status 0xff");
	}
	if (*status & QIC_STATUS_READY) {
		ftape_current_command = QIC_NO_COMMAND; /* completed */
	}
	ft_last_status.status.drive_status = (__u8)(*status & 0xff);
	TRACE_EXIT 0;
}

int ftape_report_drive_status(int *status)
{
	TRACE_FUN(ft_t_any);

	TRACE_CATCH(ftape_report_raw_drive_status(status),);
	if (*status & QIC_STATUS_NEW_CARTRIDGE ||
	    !(*status & QIC_STATUS_CARTRIDGE_PRESENT)) {
		ft_failure = 1;	/* will inhibit further operations */
		TRACE_EXIT -EIO;
	}
	if (*status & QIC_STATUS_READY && *status & QIC_STATUS_ERROR) {
		/*  Let caller handle all errors */
		TRACE_ABORT(1, ft_t_warn, "warning: error status set!");
	}
	TRACE_EXIT 0;
}

int ftape_report_error(unsigned int *error,
		       qic117_cmd_t *command, int report)
{
	static const ftape_error ftape_errors[] = QIC117_ERRORS;
	int code;
	TRACE_FUN(ft_t_any);

	TRACE_CATCH(ftape_report_operation(&code, QIC_REPORT_ERROR_CODE, 16),);
	*error   = (unsigned int)(code & 0xff);
	*command = (qic117_cmd_t)((code>>8)&0xff);
	/*  remember hardware status, maybe useful for status ioctls
	 */
	ft_last_error.error.command = (__u8)*command;
	ft_last_error.error.error   = (__u8)*error;
	if (!report) {
		TRACE_EXIT 0;
	}
	if (*error == 0) {
		TRACE_ABORT(0, ft_t_info, "No error");
	}
	TRACE(ft_t_info, "errorcode: %d", *error);
	if (*error < NR_ITEMS(ftape_errors)) {
		TRACE(ft_t_noise, "%sFatal ERROR:",
		      (ftape_errors[*error].fatal ? "" : "Non-"));
		TRACE(ft_t_noise, "%s ...", ftape_errors[*error].message);
	} else {
		TRACE(ft_t_noise, "Unknown ERROR !");
	}
	if ((unsigned int)*command < NR_ITEMS(qic117_cmds) &&
	    qic117_cmds[*command].name != NULL) {
		TRACE(ft_t_noise, "... caused by command \'%s\'",
		      qic117_cmds[*command].name);
	} else {
		TRACE(ft_t_noise, "... caused by unknown command %d",
		      *command);
	}
	TRACE_EXIT 0;
}

int ftape_report_configuration(qic_model *model,
			       unsigned int *rate,
			       int *qic_std,
			       int *tape_len)
{
	int result;
	int config;
	int status;
	static const unsigned int qic_rates[ 4] = { 250, 2000, 500, 1000 };
	TRACE_FUN(ft_t_any);

	result = ftape_report_operation(&config,
					QIC_REPORT_DRIVE_CONFIGURATION, 8);
	if (result < 0) {
		ft_last_status.status.drive_config = (__u8)0x00;
		*model = prehistoric;
		*rate = 500;
		*qic_std = QIC_TAPE_QIC40;
		*tape_len = 205;
		TRACE_EXIT 0;
	} else {
		ft_last_status.status.drive_config = (__u8)(config & 0xff);
	}
	*rate = qic_rates[(config & QIC_CONFIG_RATE_MASK) >> QIC_CONFIG_RATE_SHIFT];
	result = ftape_report_operation(&status, QIC_REPORT_TAPE_STATUS, 8);
	if (result < 0) {
		ft_last_status.status.tape_status = (__u8)0x00;
		/* pre- QIC117 rev C spec. drive, QIC_CONFIG_80 bit is valid.
		 */
		*qic_std = (config & QIC_CONFIG_80) ?
			QIC_TAPE_QIC80 : QIC_TAPE_QIC40;
		/* ?? how's about 425ft tapes? */
		*tape_len = (config & QIC_CONFIG_LONG) ? 307 : 0;
		*model = pre_qic117c;
		result = 0;
	} else {
		ft_last_status.status.tape_status = (__u8)(status & 0xff);
		*model = post_qic117b;
		TRACE(ft_t_any, "report tape status result = %02x", status);
		/* post- QIC117 rev C spec. drive, QIC_CONFIG_80 bit is
		 * invalid. 
		 */
		switch (status & QIC_TAPE_STD_MASK) {
		case QIC_TAPE_QIC40:
		case QIC_TAPE_QIC80:
		case QIC_TAPE_QIC3020:
		case QIC_TAPE_QIC3010:
			*qic_std = status & QIC_TAPE_STD_MASK;
			break;
		default:
			*qic_std = -1;
			break;
		}
		switch (status & QIC_TAPE_LEN_MASK) {
		case QIC_TAPE_205FT:
			/* 205 or 425+ ft 550 Oe tape */
			*tape_len = 0;
			break;
		case QIC_TAPE_307FT:
			/* 307.5 ft 550 Oe Extended Length (XL) tape */
			*tape_len = 307;
			break;
		case QIC_TAPE_VARIABLE:
			/* Variable length 550 Oe tape */
			*tape_len = 0;
			break;
		case QIC_TAPE_1100FT:
			/* 1100 ft 550 Oe tape */
			*tape_len = 1100;
			break;
		case QIC_TAPE_FLEX:
			/* Variable length 900 Oe tape */
			*tape_len = 0;
			break;
		default:
			*tape_len = -1;
			break;
		}
		if (*qic_std == -1 || *tape_len == -1) {
			TRACE(ft_t_any,
			      "post qic-117b spec drive with unknown tape");
		}
		result = *tape_len == -1 ? -EIO : 0;
		if (status & QIC_TAPE_WIDE) {
			switch (*qic_std) {
			case QIC_TAPE_QIC80:
				TRACE(ft_t_info, "TR-1 tape detected");
				break;
			case QIC_TAPE_QIC3010:
				TRACE(ft_t_info, "TR-2 tape detected");
				break;
			case QIC_TAPE_QIC3020:
				TRACE(ft_t_info, "TR-3 tape detected");
				break;
			default:
				TRACE(ft_t_warn,
				      "Unknown Travan tape type detected");
				break;
			}
		}
	}
	TRACE_EXIT (result < 0) ? -EIO : 0;
}

static int ftape_report_rom_version(int *version)
{

	if (ftape_report_operation(version, QIC_REPORT_ROM_VERSION, 8) < 0) {
		return -EIO;
	} else {
		return 0;
	}
}

void ftape_report_vendor_id(unsigned int *id)
{
	int result;
	TRACE_FUN(ft_t_any);

	/* We'll try to get a vendor id from the drive.  First
	 * according to the QIC-117 spec, a 16-bit id is requested.
	 * If that fails we'll try an 8-bit version, otherwise we'll
	 * try an undocumented query.
	 */
	result = ftape_report_operation((int *) id, QIC_REPORT_VENDOR_ID, 16);
	if (result < 0) {
		result = ftape_report_operation((int *) id,
						QIC_REPORT_VENDOR_ID, 8);
		if (result < 0) {
			/* The following is an undocumented call found
			 * in the CMS code.
			 */
			result = ftape_report_operation((int *) id, 24, 8);
			if (result < 0) {
				*id = UNKNOWN_VENDOR;
			} else {
				TRACE(ft_t_noise, "got old 8 bit id: %04x",
				      *id);
				*id |= 0x20000;
			}
		} else {
			TRACE(ft_t_noise, "got 8 bit id: %04x", *id);
			*id |= 0x10000;
		}
	} else {
		TRACE(ft_t_noise, "got 16 bit id: %04x", *id);
	}
	if (*id == 0x0047) {
		int version;
		int sign;

		if (ftape_report_rom_version(&version) < 0) {
			TRACE(ft_t_bug, "report rom version failed");
			TRACE_EXIT;
		}
		TRACE(ft_t_noise, "CMS rom version: %d", version);
		ftape_command(QIC_ENTER_DIAGNOSTIC_1);
		ftape_command(QIC_ENTER_DIAGNOSTIC_1);
		diagnostic_mode = 1;
		if (ftape_report_operation(&sign, 9, 8) < 0) {
			unsigned int error;
			qic117_cmd_t command;

			ftape_report_error(&error, &command, 1);
			ftape_command(QIC_ENTER_PRIMARY_MODE);
			diagnostic_mode = 0;
			TRACE_EXIT;	/* failure ! */
		} else {
			TRACE(ft_t_noise, "CMS signature: %02x", sign);
		}
		if (sign == 0xa5) {
			result = ftape_report_operation(&sign, 37, 8);
			if (result < 0) {
				if (version >= 63) {
					*id = 0x8880;
					TRACE(ft_t_noise,
					      "This is an Iomega drive !");
				} else {
					*id = 0x0047;
					TRACE(ft_t_noise,
					      "This is a real CMS drive !");
				}
			} else {
				*id = 0x0047;
				TRACE(ft_t_noise, "CMS status: %d", sign);
			}
		} else {
			*id = UNKNOWN_VENDOR;
		}
		ftape_command(QIC_ENTER_PRIMARY_MODE);
		diagnostic_mode = 0;
	}
	TRACE_EXIT;
}

static int qic_rate_code(unsigned int rate)
{
	switch (rate) {
	case 250:
		return QIC_CONFIG_RATE_250;
	case 500:
		return QIC_CONFIG_RATE_500;
	case 1000:
		return QIC_CONFIG_RATE_1000;
	case 2000:
		return QIC_CONFIG_RATE_2000;
	default:
		return QIC_CONFIG_RATE_500;
	}
}

static int ftape_set_rate_test(unsigned int *max_rate)
{
	unsigned int error;
	qic117_cmd_t command;
	int status;
	int supported = 0;
	TRACE_FUN(ft_t_any);

	/*  Check if the drive does support the select rate command
	 *  by testing all different settings. If any one is accepted
	 *  we assume the command is supported, else not.
	 */
	for (*max_rate = 2000; *max_rate >= 250; *max_rate /= 2) {
		if (ftape_command(QIC_SELECT_RATE) < 0) {
			continue;
		}		
		if (ftape_parameter_wait(qic_rate_code(*max_rate),
					 1 * FT_SECOND, &status) < 0) {
			continue;
		}
		if (status & QIC_STATUS_ERROR) {
			ftape_report_error(&error, &command, 0);
			continue;
		}
		supported = 1; /* did accept a request */
		break;
	}
	TRACE(ft_t_noise, "Select Rate command is%s supported", 
	      supported ? "" : " not");
	TRACE_EXIT supported;
}

int ftape_set_data_rate(unsigned int new_rate /* Kbps */, unsigned int qic_std)
{
	int status;
	int result = 0;
	unsigned int data_rate = new_rate;
	static int supported;
	int rate_changed = 0;
	qic_model dummy_model;
	unsigned int dummy_qic_std, dummy_tape_len;
	TRACE_FUN(ft_t_any);

	if (ft_drive_max_rate == 0) { /* first time */
		supported = ftape_set_rate_test(&ft_drive_max_rate);
	}
	if (supported) {
		ftape_command(QIC_SELECT_RATE);
		result = ftape_parameter_wait(qic_rate_code(new_rate),
					      1 * FT_SECOND, &status);
		if (result >= 0 && !(status & QIC_STATUS_ERROR)) {
			rate_changed = 1;
		}
	}
	TRACE_CATCH(result = ftape_report_configuration(&dummy_model,
							&data_rate, 
							&dummy_qic_std,
							&dummy_tape_len),);
	if (data_rate != new_rate) {
		if (!supported) {
			TRACE(ft_t_warn, "Rate change not supported!");
		} else if (rate_changed) {
			TRACE(ft_t_warn, "Requested: %d, got %d",
			      new_rate, data_rate);
		} else {
			TRACE(ft_t_warn, "Rate change failed!");
		}
		result = -EINVAL;
	}
	/*
	 *  Set data rate and write precompensation as specified:
	 *
	 *            |  QIC-40/80  | QIC-3010/3020
	 *   rate     |   precomp   |    precomp
	 *  ----------+-------------+--------------
	 *  250 Kbps. |   250 ns.   |     0 ns.
	 *  500 Kbps. |   125 ns.   |     0 ns.
	 *    1 Mbps. |    42 ns.   |     0 ns.
	 *    2 Mbps  |      N/A    |     0 ns.
	 */
	if ((qic_std == QIC_TAPE_QIC40 && data_rate > 500) || 
	    (qic_std == QIC_TAPE_QIC80 && data_rate > 1000)) {
		TRACE_ABORT(-EINVAL,
			    ft_t_warn, "Datarate too high for QIC-mode");
	}
	TRACE_CATCH(fdc_set_data_rate(data_rate),_res = -EINVAL);
	ft_data_rate = data_rate;
	if (qic_std == QIC_TAPE_QIC40 || qic_std == QIC_TAPE_QIC80) {
		switch (data_rate) {
		case 250:
			fdc_set_write_precomp(250);
			break;
		default:
		case 500:
			fdc_set_write_precomp(125);
			break;
		case 1000:
			fdc_set_write_precomp(42);
			break;
		}
	} else {
		fdc_set_write_precomp(0);
	}
	TRACE_EXIT result;
}

/*  The next two functions are used to cope with excessive overrun errors
 */
int ftape_increase_threshold(void)
{
	TRACE_FUN(ft_t_flow);

	if (fdc.type < i82077 || ft_fdc_threshold >= 12) {
		TRACE_ABORT(-EIO, ft_t_err, "cannot increase fifo threshold");
	}
	if (fdc_fifo_threshold(++ft_fdc_threshold, NULL, NULL, NULL) < 0) {
		TRACE(ft_t_err, "cannot increase fifo threshold");
		ft_fdc_threshold --;
		fdc_reset();
	}
	TRACE(ft_t_info, "New FIFO threshold: %d", ft_fdc_threshold);
	TRACE_EXIT 0;
}

int ftape_half_data_rate(void)
{
	if (ft_data_rate < 500) {
		return -1;
	}
	if (ftape_set_data_rate(ft_data_rate / 2, ft_qic_std) < 0) {
		return -EIO;
	}
	ftape_calc_timeouts(ft_qic_std, ft_data_rate, ftape_tape_len);
	return 0;
}

/*      Seek the head to the specified track.
 */
int ftape_seek_head_to_track(unsigned int track)
{
	int status;
	TRACE_FUN(ft_t_any);

	ft_location.track = -1; /* remains set in case of error */
	if (track >= ft_tracks_per_tape) {
		TRACE_ABORT(-EINVAL, ft_t_bug, "track out of bounds");
	}
	TRACE(ft_t_flow, "seeking track %d", track);
	TRACE_CATCH(ftape_command(QIC_SEEK_HEAD_TO_TRACK),);
	TRACE_CATCH(ftape_parameter_wait(track, ftape_timeout.head_seek,
					 &status),);
	ft_location.track = track;
	ftape_might_be_off_track = 0;
	TRACE_EXIT 0;
}

int ftape_wakeup_drive(wake_up_types method)
{
	int status;
	int motor_on = 0;
	TRACE_FUN(ft_t_any);

	switch (method) {
	case wake_up_colorado:
		TRACE_CATCH(ftape_command(QIC_PHANTOM_SELECT),);
		TRACE_CATCH(ftape_parameter(0 /* ft_drive_sel ?? */),);
		break;
	case wake_up_mountain:
		TRACE_CATCH(ftape_command(QIC_SOFT_SELECT),);
		ftape_sleep(FT_MILLISECOND);	/* NEEDED */
		TRACE_CATCH(ftape_parameter(18),);
		break;
	case wake_up_insight:
		ftape_sleep(100 * FT_MILLISECOND);
		motor_on = 1;
		fdc_motor(motor_on);	/* enable is done by motor-on */
	case no_wake_up:
		break;
	default:
		TRACE_EXIT -ENODEV;	/* unknown wakeup method */
		break;
	}
	/*  If wakeup succeeded we shouldn't get an error here..
	 */
	TRACE_CATCH(ftape_report_raw_drive_status(&status),
		    if (motor_on) {
			    fdc_motor(0);
		    });
	TRACE_EXIT 0;
}

int ftape_put_drive_to_sleep(wake_up_types method)
{
	TRACE_FUN(ft_t_any);

	switch (method) {
	case wake_up_colorado:
		TRACE_CATCH(ftape_command(QIC_PHANTOM_DESELECT),);
		break;
	case wake_up_mountain:
		TRACE_CATCH(ftape_command(QIC_SOFT_DESELECT),);
		break;
	case wake_up_insight:
		fdc_motor(0);	/* enable is done by motor-on */
	case no_wake_up:	/* no wakeup / no sleep ! */
		break;
	default:
		TRACE_EXIT -ENODEV;	/* unknown wakeup method */
	}
	TRACE_EXIT 0;
}

int ftape_reset_drive(void)
{
	int result = 0;
	int status;
	unsigned int err_code;
	qic117_cmd_t err_command;
	int i;
	TRACE_FUN(ft_t_any);

	/*  We want to re-establish contact with our drive.  Fire a
	 *  number of reset commands (single step pulses) and pray for
	 *  success.
	 */
	for (i = 0; i < 2; ++i) {
		TRACE(ft_t_flow, "Resetting fdc");
		fdc_reset();
		ftape_sleep(10 * FT_MILLISECOND);
		TRACE(ft_t_flow, "Reset command to drive");
		result = ftape_command(QIC_RESET);
		if (result == 0) {
			ftape_sleep(1 * FT_SECOND); /*  drive not
						     *  accessible
						     *  during 1 second
						     */
			TRACE(ft_t_flow, "Re-selecting drive");

			/* Strange, the QIC-117 specs don't mention
			 * this but the drive gets deselected after a
			 * soft reset !  So we need to enable it
			 * again.
			 */
			if (ftape_wakeup_drive(ft_drive_type.wake_up) < 0) {
				TRACE(ft_t_err, "Wakeup failed !");
			}
			TRACE(ft_t_flow, "Waiting until drive gets ready");
			result= ftape_ready_wait(ftape_timeout.reset, &status);
			if (result == 0 && (status & QIC_STATUS_ERROR)) {
				result = ftape_report_error(&err_code,
							    &err_command, 1);
				if (result == 0 && err_code == 27) {
					/*  Okay, drive saw reset
					 *  command and responded as it
					 *  should
					 */
					break;
				} else {
					result = -EIO;
				}
			} else {
				result = -EIO;
			}
		}
		FT_SIGNAL_EXIT(_DONT_BLOCK);
	}
	if (result != 0) {
		TRACE(ft_t_err, "General failure to reset tape drive");
	} else {
		/*  Restore correct settings: keep original rate 
		 */
		ftape_set_data_rate(ft_data_rate, ft_qic_std);
	}
	ftape_init_drive_needed = 1;
	TRACE_EXIT result;
}
