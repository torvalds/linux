/*
 *      Copyright (C) 1993-1996 Bas Laarhoven,
 *                    1996-1997 Claus-Justus Heine.

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
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/ftape-ctl.c,v $
 * $Revision: 1.4 $
 * $Date: 1997/11/11 14:37:44 $
 *
 *      This file contains the non-read/write ftape functions for the
 *      QIC-40/80/3010/3020 floppy-tape driver "ftape" for Linux.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/mman.h>

#include <linux/ftape.h>
#include <linux/qic117.h>
#include <asm/uaccess.h>
#include <asm/io.h>

/* ease porting between pre-2.4.x and later kernels */
#define vma_get_pgoff(v)      ((v)->vm_pgoff)

#include "../lowlevel/ftape-tracing.h"
#include "../lowlevel/ftape-io.h"
#include "../lowlevel/ftape-ctl.h"
#include "../lowlevel/ftape-write.h"
#include "../lowlevel/ftape-read.h"
#include "../lowlevel/ftape-rw.h"
#include "../lowlevel/ftape-bsm.h"

/*      Global vars.
 */
ftape_info ftape_status = {
/*  vendor information */
	{ 0, },     /* drive type */
/*  data rates */
	500,        /* used data rate */
	500,        /* drive max rate */
	500,        /* fdc max rate   */
/*  drive selection, either FTAPE_SEL_A/B/C/D */
	-1,     /* drive selection */
/*  flags set after decode the drive and tape status   */
	0,          /* formatted */
	1,          /* no tape */
	1,          /* write protected */
	1,          /* new tape */
/*  values of last queried drive/tape status and error */
	{{0,}},     /* last error code */
	{{0,}},     /* drive status, configuration, tape status */
/*  cartridge geometry */
        20,         /* tracks_per_tape */
        102,        /* segments_per_track */
/*  location of header segments, etc. */
	-1,     /* used_header_segment */
	-1,     /* header_segment_1 */
	-1,     /* header_segment_2 */
	-1,     /* first_data_segment */
        -1,     /* last_data_segment */
/*  the format code as stored in the header segment  */
	fmt_normal, /* format code */
/*  the default for the qic std: unknown */
	-1,
/*  is tape running? */
	idle,       /* runner_state */
/*  is tape reading/writing/verifying/formatting/deleting */
	idle,       /* driver state */
/*  flags fatal hardware error */
	1,          /* failure */
/*  history record */
	{ 0, }      /* history record */
};
	
int ftape_segments_per_head     = 1020;
int ftape_segments_per_cylinder = 4;
int ftape_init_drive_needed = 1; /* need to be global for ftape_reset_drive()
				  * in ftape-io.c
				  */

/*      Local vars.
 */
static const vendor_struct vendors[] = QIC117_VENDORS;
static const wakeup_method methods[] = WAKEUP_METHODS;

const ftape_info *ftape_get_status(void)
{
#if defined(STATUS_PARANOYA)
	static ftape_info get_status;

	get_status = ftape_status;
	return &get_status;
#else
	return &ftape_status; /*  maybe return only a copy of it to assure 
			       *  read only access
			       */
#endif
}

static int ftape_not_operational(int status)
{
	/* return true if status indicates tape can not be used.
	 */
	return ((status ^ QIC_STATUS_CARTRIDGE_PRESENT) &
		(QIC_STATUS_ERROR |
		 QIC_STATUS_CARTRIDGE_PRESENT |
		 QIC_STATUS_NEW_CARTRIDGE));
}

int ftape_seek_to_eot(void)
{
	int status;
	TRACE_FUN(ft_t_any);

	TRACE_CATCH(ftape_ready_wait(ftape_timeout.pause, &status),);
	while ((status & QIC_STATUS_AT_EOT) == 0) {
		if (ftape_not_operational(status)) {
			TRACE_EXIT -EIO;
		}
		TRACE_CATCH(ftape_command_wait(QIC_PHYSICAL_FORWARD,
					       ftape_timeout.rewind,&status),);
	}
	TRACE_EXIT 0;
}

int ftape_seek_to_bot(void)
{
	int status;
	TRACE_FUN(ft_t_any);

	TRACE_CATCH(ftape_ready_wait(ftape_timeout.pause, &status),);
	while ((status & QIC_STATUS_AT_BOT) == 0) {
		if (ftape_not_operational(status)) {
			TRACE_EXIT -EIO;
		}
		TRACE_CATCH(ftape_command_wait(QIC_PHYSICAL_REVERSE,
					       ftape_timeout.rewind,&status),);
	}
	TRACE_EXIT 0;
}

static int ftape_new_cartridge(void)
{
	ft_location.track = -1; /* force seek on first access */
	ftape_zap_read_buffers();
	ftape_zap_write_buffers();
	return 0;
}

int ftape_abort_operation(void)
{
	int result = 0;
	int status;
	TRACE_FUN(ft_t_flow);

	if (ft_runner_status == running) {
		TRACE(ft_t_noise, "aborting runner, waiting");
		
		ft_runner_status = do_abort;
		/* set timeout so that the tape will run to logical EOT
		 * if we missed the last sector and there are no queue pulses.
		 */
		result = ftape_dumb_stop();
	}
	if (ft_runner_status != idle) {
		if (ft_runner_status == do_abort) {
			TRACE(ft_t_noise, "forcing runner abort");
		}
		TRACE(ft_t_noise, "stopping tape");
		result = ftape_stop_tape(&status);
		ft_location.known = 0;
		ft_runner_status  = idle;
	}
	ftape_reset_buffer();
	ftape_zap_read_buffers();
	ftape_set_state(idle);
	TRACE_EXIT result;
}

static int lookup_vendor_id(unsigned int vendor_id)
{
	int i = 0;

	while (vendors[i].vendor_id != vendor_id) {
		if (++i >= NR_ITEMS(vendors)) {
			return -1;
		}
	}
	return i;
}

static void ftape_detach_drive(void)
{
	TRACE_FUN(ft_t_any);

	TRACE(ft_t_flow, "disabling tape drive and fdc");
	ftape_put_drive_to_sleep(ft_drive_type.wake_up);
	fdc_catch_stray_interrupts(1);	/* one always comes */
	fdc_disable();
	fdc_release_irq_and_dma();
	fdc_release_regions();
	TRACE_EXIT;
}

static void clear_history(void)
{
	ft_history.used = 0;
	ft_history.id_am_errors =
		ft_history.id_crc_errors =
		ft_history.data_am_errors =
		ft_history.data_crc_errors =
		ft_history.overrun_errors =
		ft_history.no_data_errors =
		ft_history.retries =
		ft_history.crc_errors =
		ft_history.crc_failures =
		ft_history.ecc_failures =
		ft_history.corrected =
		ft_history.defects =
		ft_history.rewinds = 0;
}

static int ftape_activate_drive(vendor_struct * drive_type)
{
	int result = 0;
	TRACE_FUN(ft_t_flow);

	/* If we already know the drive type, wake it up.
	 * Else try to find out what kind of drive is attached.
	 */
	if (drive_type->wake_up != unknown_wake_up) {
		TRACE(ft_t_flow, "enabling tape drive and fdc");
		result = ftape_wakeup_drive(drive_type->wake_up);
		if (result < 0) {
			TRACE(ft_t_err, "known wakeup method failed");
		}
	} else {
		wake_up_types method;
		const ft_trace_t old_tracing = TRACE_LEVEL;
		if (TRACE_LEVEL < ft_t_flow) {
			SET_TRACE_LEVEL(ft_t_bug);
		}

		/*  Try to awaken the drive using all known methods.
		 *  Lower tracing for a while.
		 */
		for (method=no_wake_up; method < NR_ITEMS(methods); ++method) {
			drive_type->wake_up = method;
#ifdef CONFIG_FT_TWO_DRIVES
			/*  Test setup for dual drive configuration.
			 *  /dev/rft2 uses mountain wakeup
			 *  /dev/rft3 uses colorado wakeup
			 *  Other systems will use the normal scheme.
			 */
			if ((ft_drive_sel < 2)                            ||
			    (ft_drive_sel == 2 && method == FT_WAKE_UP_1) ||
			    (ft_drive_sel == 3 && method == FT_WAKE_UP_2)) {
				result=ftape_wakeup_drive(drive_type->wake_up);
			} else {
				result = -EIO;
			}
#else
			result = ftape_wakeup_drive(drive_type->wake_up);
#endif
			if (result >= 0) {
				TRACE(ft_t_warn, "drive wakeup method: %s",
				      methods[drive_type->wake_up].name);
				break;
			}
		}
		SET_TRACE_LEVEL(old_tracing);

		if (method >= NR_ITEMS(methods)) {
			/* no response at all, cannot open this drive */
			drive_type->wake_up = unknown_wake_up;
			TRACE(ft_t_err, "no tape drive found !");
			result = -ENODEV;
		}
	}
	TRACE_EXIT result;
}

static int ftape_get_drive_status(void)
{
	int result;
	int status;
	TRACE_FUN(ft_t_flow);

	ft_no_tape = ft_write_protected = 0;
	/*    Tape drive is activated now.
	 *    First clear error status if present.
	 */
	do {
		result = ftape_ready_wait(ftape_timeout.reset, &status);
		if (result < 0) {
			if (result == -ETIME) {
				TRACE(ft_t_err, "ftape_ready_wait timeout");
			} else if (result == -EINTR) {
				TRACE(ft_t_err, "ftape_ready_wait aborted");
			} else {
				TRACE(ft_t_err, "ftape_ready_wait failed");
			}
			TRACE_EXIT -EIO;
		}
		/*  Clear error condition (drive is ready !)
		 */
		if (status & QIC_STATUS_ERROR) {
			unsigned int error;
			qic117_cmd_t command;

			TRACE(ft_t_err, "error status set");
			result = ftape_report_error(&error, &command, 1);
			if (result < 0) {
				TRACE(ft_t_err,
				      "report_error_code failed: %d", result);
				/* hope it's working next time */
				ftape_reset_drive();
				TRACE_EXIT -EIO;
			} else if (error != 0) {
				TRACE(ft_t_noise, "error code   : %d", error);
				TRACE(ft_t_noise, "error command: %d", command);
			}
		}
		if (status & QIC_STATUS_NEW_CARTRIDGE) {
			unsigned int error;
			qic117_cmd_t command;
			const ft_trace_t old_tracing = TRACE_LEVEL;
			SET_TRACE_LEVEL(ft_t_bug);

			/*  Undocumented feature: Must clear (not present!)
			 *  error here or we'll fail later.
			 */
			ftape_report_error(&error, &command, 1);

			SET_TRACE_LEVEL(old_tracing);
			TRACE(ft_t_info, "status: new cartridge");
			ft_new_tape = 1;
		} else {
			ft_new_tape = 0;
		}
		FT_SIGNAL_EXIT(_DONT_BLOCK);
	} while (status & QIC_STATUS_ERROR);
	
	ft_no_tape = !(status & QIC_STATUS_CARTRIDGE_PRESENT);
	ft_write_protected = (status & QIC_STATUS_WRITE_PROTECT) != 0;
	if (ft_no_tape) {
		TRACE(ft_t_warn, "no cartridge present");
	} else {
		if (ft_write_protected) {
			TRACE(ft_t_noise, "Write protected cartridge");
		}
	}
	TRACE_EXIT 0;
}

static void ftape_log_vendor_id(void)
{
	int vendor_index;
	TRACE_FUN(ft_t_flow);

	ftape_report_vendor_id(&ft_drive_type.vendor_id);
	vendor_index = lookup_vendor_id(ft_drive_type.vendor_id);
	if (ft_drive_type.vendor_id == UNKNOWN_VENDOR &&
	    ft_drive_type.wake_up == wake_up_colorado) {
		vendor_index = 0;
		/* hack to get rid of all this mail */
		ft_drive_type.vendor_id = 0;
	}
	if (vendor_index < 0) {
		/* Unknown vendor id, first time opening device.  The
		 * drive_type remains set to type found at wakeup
		 * time, this will probably keep the driver operating
		 * for this new vendor.  
		 */
		TRACE(ft_t_warn, "\n"
		      KERN_INFO "============ unknown vendor id ===========\n"
		      KERN_INFO "A new, yet unsupported tape drive is found\n"
		      KERN_INFO "Please report the following values:\n"
		      KERN_INFO "   Vendor id     : 0x%04x\n"
		      KERN_INFO "   Wakeup method : %s\n"
		      KERN_INFO "And a description of your tape drive\n"
		      KERN_INFO "to "THE_FTAPE_MAINTAINER"\n"
		      KERN_INFO "==========================================",
		      ft_drive_type.vendor_id,
		      methods[ft_drive_type.wake_up].name);
		ft_drive_type.speed = 0;		/* unknown */
	} else {
		ft_drive_type.name  = vendors[vendor_index].name;
		ft_drive_type.speed = vendors[vendor_index].speed;
		TRACE(ft_t_info, "tape drive type: %s", ft_drive_type.name);
		/* scan all methods for this vendor_id in table */
		while(ft_drive_type.wake_up != vendors[vendor_index].wake_up) {
			if (vendor_index < NR_ITEMS(vendors) - 1 &&
			    vendors[vendor_index + 1].vendor_id 
			    == 
			    ft_drive_type.vendor_id) {
				++vendor_index;
			} else {
				break;
			}
		}
		if (ft_drive_type.wake_up != vendors[vendor_index].wake_up) {
			TRACE(ft_t_warn, "\n"
		     KERN_INFO "==========================================\n"
		     KERN_INFO "wakeup type mismatch:\n"
		     KERN_INFO "found: %s, expected: %s\n"
		     KERN_INFO "please report this to "THE_FTAPE_MAINTAINER"\n"
		     KERN_INFO "==========================================",
			      methods[ft_drive_type.wake_up].name,
			      methods[vendors[vendor_index].wake_up].name);
		}
	}
	TRACE_EXIT;
}

void ftape_calc_timeouts(unsigned int qic_std,
			 unsigned int data_rate,
			 unsigned int tape_len)
{
	int speed;		/* deci-ips ! */
	int ff_speed;
	int length;
	TRACE_FUN(ft_t_any);

	/*                           tape transport speed
	 *  data rate:        QIC-40   QIC-80   QIC-3010 QIC-3020
	 *
	 *    250 Kbps        25 ips     n/a      n/a      n/a
	 *    500 Kbps        50 ips   34 ips   22.6 ips   n/a
	 *      1 Mbps          n/a    68 ips   45.2 ips 22.6 ips
	 *      2 Mbps          n/a      n/a      n/a    45.2 ips
	 *
	 *  fast tape transport speed is at least 68 ips.
	 */
	switch (qic_std) {
	case QIC_TAPE_QIC40:
		speed = (data_rate == 250) ? 250 : 500;
		break;
	case QIC_TAPE_QIC80:
		speed = (data_rate == 500) ? 340 : 680;
		break;
	case QIC_TAPE_QIC3010:
		speed = (data_rate == 500) ? 226 : 452;
		break;
	case QIC_TAPE_QIC3020:
		speed = (data_rate == 1000) ? 226 : 452;
		break;
	default:
		TRACE(ft_t_bug, "Unknown qic_std (bug) ?");
		speed = 500;
		break;
	}
	if (ft_drive_type.speed == 0) {
		unsigned long t0;
		static int dt = 0;     /* keep gcc from complaining */
		static int first_time = 1;

		/*  Measure the time it takes to wind to EOT and back to BOT.
		 *  If the tape length is known, calculate the rewind speed.
		 *  Else keep the time value for calculation of the rewind
		 *  speed later on, when the length _is_ known.
		 *  Ask for a report only when length and speed are both known.
		 */
		if (first_time) {
			ftape_seek_to_bot();
			t0 = jiffies;
			ftape_seek_to_eot();
			ftape_seek_to_bot();
			dt = (int) (((jiffies - t0) * FT_USPT) / 1000);
			if (dt < 1) {
				dt = 1;	/* prevent div by zero on failures */
			}
			first_time = 0;
			TRACE(ft_t_info,
			      "trying to determine seek timeout, got %d msec",
			      dt);
		}
		if (tape_len != 0) {
			ft_drive_type.speed = 
				(2 * 12 * tape_len * 1000) / dt;
			TRACE(ft_t_warn, "\n"
		     KERN_INFO "==========================================\n"
		     KERN_INFO "drive type: %s\n"
		     KERN_INFO "delta time = %d ms, length = %d ft\n"
		     KERN_INFO "has a maximum tape speed of %d ips\n"
		     KERN_INFO "please report this to "THE_FTAPE_MAINTAINER"\n"
		     KERN_INFO "==========================================",
			      ft_drive_type.name, dt, tape_len, 
			      ft_drive_type.speed);
		}
	}
	/*  Handle unknown length tapes as very long ones. We'll
	 *  determine the actual length from a header segment later.
	 *  This is normal for all modern (Wide,TR1/2/3) formats.
	 */
	if (tape_len <= 0) {
		TRACE(ft_t_noise,
		      "Unknown tape length, using maximal timeouts");
		length = QIC_TOP_TAPE_LEN;	/* use worst case values */
	} else {
		length = tape_len;		/* use actual values */
	}
	if (ft_drive_type.speed == 0) {
		ff_speed = speed; 
	} else {
		ff_speed = ft_drive_type.speed;
	}
	/*  time to go from bot to eot at normal speed (data rate):
	 *  time = (1+delta) * length (ft) * 12 (inch/ft) / speed (ips)
	 *  delta = 10 % for seek speed, 20 % for rewind speed.
	 */
	ftape_timeout.seek = (length * 132 * FT_SECOND) / speed;
	ftape_timeout.rewind = (length * 144 * FT_SECOND) / (10 * ff_speed);
	ftape_timeout.reset = 20 * FT_SECOND + ftape_timeout.rewind;
	TRACE(ft_t_noise, "timeouts for speed = %d, length = %d\n"
	      KERN_INFO "seek timeout  : %d sec\n"
	      KERN_INFO "rewind timeout: %d sec\n"
	      KERN_INFO "reset timeout : %d sec",
	      speed, length,
	      (ftape_timeout.seek + 500) / 1000,
	      (ftape_timeout.rewind + 500) / 1000,
	      (ftape_timeout.reset + 500) / 1000);
	TRACE_EXIT;
}

/* This function calibrates the datarate (i.e. determines the maximal
 * usable data rate) and sets the global variable ft_qic_std to qic_std
 *
 */
int ftape_calibrate_data_rate(unsigned int qic_std)
{
	int rate = ft_fdc_rate_limit;
	int result;
	TRACE_FUN(ft_t_flow);

	ft_qic_std = qic_std;

	if (ft_qic_std == -1) {
		TRACE_ABORT(-EIO, ft_t_err,
		"Unable to determine data rate if QIC standard is unknown");
	}

	/*  Select highest rate supported by both fdc and drive.
	 *  Start with highest rate supported by the fdc.
	 */
	while (fdc_set_data_rate(rate) < 0 && rate > 250) {
		rate /= 2;
	}
	TRACE(ft_t_info,
	      "Highest FDC supported data rate: %d Kbps", rate);
	ft_fdc_max_rate = rate;
	do {
		result = ftape_set_data_rate(rate, ft_qic_std);
	} while (result == -EINVAL && (rate /= 2) > 250);
	if (result < 0) {
		TRACE_ABORT(-EIO, ft_t_err, "set datarate failed");
	}
	ft_data_rate = rate;
	TRACE_EXIT 0;
}

static int ftape_init_drive(void)
{
	int status;
	qic_model model;
	unsigned int qic_std;
	unsigned int data_rate;
	TRACE_FUN(ft_t_flow);

	ftape_init_drive_needed = 0; /* don't retry if this fails ? */
	TRACE_CATCH(ftape_report_raw_drive_status(&status),);
	if (status & QIC_STATUS_CARTRIDGE_PRESENT) {
		if (!(status & QIC_STATUS_AT_BOT)) {
			/*  Antique drives will get here after a soft reset,
			 *  modern ones only if the driver is loaded when the
			 *  tape wasn't rewound properly.
			 */
			/* Tape should be at bot if new cartridge ! */
			ftape_seek_to_bot();
		}
		if (!(status & QIC_STATUS_REFERENCED)) {
			TRACE(ft_t_flow, "starting seek_load_point");
			TRACE_CATCH(ftape_command_wait(QIC_SEEK_LOAD_POINT,
						       ftape_timeout.reset,
						       &status),);
		}
	}
	ft_formatted = (status & QIC_STATUS_REFERENCED) != 0;
	if (!ft_formatted) {
		TRACE(ft_t_warn, "Warning: tape is not formatted !");
	}

	/*  report configuration aborts when ftape_tape_len == -1
	 *  unknown qic_std is okay if not formatted.
	 */
	TRACE_CATCH(ftape_report_configuration(&model,
					       &data_rate,
					       &qic_std,
					       &ftape_tape_len),);

	/*  Maybe add the following to the /proc entry
	 */
	TRACE(ft_t_info, "%s drive @ %d Kbps",
	      (model == prehistoric) ? "prehistoric" :
	      ((model == pre_qic117c) ? "pre QIC-117C" :
	       ((model == post_qic117b) ? "post QIC-117B" :
		"post QIC-117D")), data_rate);

	if (ft_formatted) {
		/*  initialize ft_used_data_rate to maximum value 
		 *  and set ft_qic_std
		 */
		TRACE_CATCH(ftape_calibrate_data_rate(qic_std),);
		if (ftape_tape_len == 0) {
			TRACE(ft_t_info, "unknown length QIC-%s tape",
			      (ft_qic_std == QIC_TAPE_QIC40) ? "40" :
			      ((ft_qic_std == QIC_TAPE_QIC80) ? "80" :
			       ((ft_qic_std == QIC_TAPE_QIC3010) 
				? "3010" : "3020")));
		} else {
			TRACE(ft_t_info, "%d ft. QIC-%s tape", ftape_tape_len,
			      (ft_qic_std == QIC_TAPE_QIC40) ? "40" :
			      ((ft_qic_std == QIC_TAPE_QIC80) ? "80" :
			       ((ft_qic_std == QIC_TAPE_QIC3010)
				? "3010" : "3020")));
		}
		ftape_calc_timeouts(ft_qic_std, ft_data_rate, ftape_tape_len);
		/* soft write-protect QIC-40/QIC-80 cartridges used with a
		 * Colorado T3000 drive. Buggy hardware!
		 */
		if ((ft_drive_type.vendor_id == 0x011c6) &&
		    ((ft_qic_std == QIC_TAPE_QIC40 ||
		      ft_qic_std == QIC_TAPE_QIC80) &&
		     !ft_write_protected)) {
			TRACE(ft_t_warn, "\n"
	KERN_INFO "The famous Colorado T3000 bug:\n"
	KERN_INFO "%s drives can't write QIC40 and QIC80\n"
	KERN_INFO "cartridges but don't set the write-protect flag!",
			      ft_drive_type.name);
			ft_write_protected = 1;
		}
	} else {
		/*  Doesn't make too much sense to set the data rate
		 *  because we don't know what to use for the write
		 *  precompensation.
		 *  Need to do this again when formatting the cartridge.
		 */
		ft_data_rate = data_rate;
		ftape_calc_timeouts(QIC_TAPE_QIC40,
				    data_rate,
				    ftape_tape_len);
	}
	ftape_new_cartridge();
	TRACE_EXIT 0;
}

static void ftape_munmap(void)
{
	int i;
	TRACE_FUN(ft_t_flow);
	
	for (i = 0; i < ft_nr_buffers; i++) {
		ft_buffer[i]->mmapped = 0;
	}
	TRACE_EXIT;
}

/*   Map the dma buffers into the virtual address range given by vma.
 *   We only check the caller doesn't map non-existent buffers. We
 *   don't check for multiple mappings.
 */
int ftape_mmap(struct vm_area_struct *vma)
{
	int num_buffers;
	int i;
	TRACE_FUN(ft_t_flow);
	
	if (ft_failure) {
		TRACE_EXIT -ENODEV;
	}
	if (!(vma->vm_flags & (VM_READ|VM_WRITE))) {
		TRACE_ABORT(-EINVAL, ft_t_err, "Undefined mmap() access");
	}
	if (vma_get_pgoff(vma) != 0) {
		TRACE_ABORT(-EINVAL, ft_t_err, "page offset must be 0");
	}
	if ((vma->vm_end - vma->vm_start) % FT_BUFF_SIZE != 0) {
		TRACE_ABORT(-EINVAL, ft_t_err,
			    "size = %ld, should be a multiple of %d",
			    vma->vm_end - vma->vm_start,
			    FT_BUFF_SIZE);
	}
	num_buffers = (vma->vm_end - vma->vm_start) / FT_BUFF_SIZE;
	if (num_buffers > ft_nr_buffers) {
		TRACE_ABORT(-EINVAL,
			    ft_t_err, "size = %ld, should be less than %d",
			    vma->vm_end - vma->vm_start,
			    ft_nr_buffers * FT_BUFF_SIZE);
	}
	if (ft_driver_state != idle) {
		/* this also clears the buffer states 
		 */
		ftape_abort_operation();
	} else {
		ftape_reset_buffer();
	}
	for (i = 0; i < num_buffers; i++) {
		unsigned long pfn;

		pfn = virt_to_phys(ft_buffer[i]->address) >> PAGE_SHIFT;
		TRACE_CATCH(remap_pfn_range(vma, vma->vm_start +
					     i * FT_BUFF_SIZE,
					     pfn,
					     FT_BUFF_SIZE,
					     vma->vm_page_prot),
			    _res = -EAGAIN);
		TRACE(ft_t_noise, "remapped dma buffer @ %p to location @ %p",
		      ft_buffer[i]->address,
		      (void *)(vma->vm_start + i * FT_BUFF_SIZE));
	}
	for (i = 0; i < num_buffers; i++) {
		memset(ft_buffer[i]->address, 0xAA, FT_BUFF_SIZE);
		ft_buffer[i]->mmapped++;
	}	
	TRACE_EXIT 0;
}

static void ftape_init_driver(void); /* forward declaration */

/*      OPEN routine called by kernel-interface code
 */
int ftape_enable(int drive_selection)
{
	TRACE_FUN(ft_t_any);

	if (ft_drive_sel == -1 || ft_drive_sel != drive_selection) {
		/* Other selection than last time
		 */
		ftape_init_driver();
	}
	ft_drive_sel = FTAPE_SEL(drive_selection);
	ft_failure = 0;
	TRACE_CATCH(fdc_init(),); /* init & detect fdc */
	TRACE_CATCH(ftape_activate_drive(&ft_drive_type),
		    fdc_disable();
		    fdc_release_irq_and_dma();
		    fdc_release_regions());
	TRACE_CATCH(ftape_get_drive_status(), ftape_detach_drive());
	if (ft_drive_type.vendor_id == UNKNOWN_VENDOR) {
		ftape_log_vendor_id();
	}
	if (ft_new_tape) {
		ftape_init_drive_needed = 1;
	}
	if (!ft_no_tape && ftape_init_drive_needed) {
		TRACE_CATCH(ftape_init_drive(), ftape_detach_drive());
	}
	ftape_munmap(); /* clear the mmap flag */
	clear_history();
	TRACE_EXIT 0;
}

/*   release routine called by the high level interface modules
 *   zftape or sftape.
 */
void ftape_disable(void)
{
	int i;
	TRACE_FUN(ft_t_any);

	for (i = 0; i < ft_nr_buffers; i++) {
		if (ft_buffer[i]->mmapped) {
			TRACE(ft_t_noise, "first byte of buffer %d: 0x%02x",
			      i, *ft_buffer[i]->address);
		}
	}
	if (sigtestsetmask(&current->pending.signal, _DONT_BLOCK) && 
	    !(sigtestsetmask(&current->pending.signal, _NEVER_BLOCK)) &&
	    ftape_tape_running) {
		TRACE(ft_t_warn,
		      "Interrupted by fatal signal and tape still running");
		ftape_dumb_stop();
		ftape_abort_operation(); /* it's annoying */
	} else {
		ftape_set_state(idle);
	}
	ftape_detach_drive();
	if (ft_history.used) {
		TRACE(ft_t_info, "== Non-fatal errors this run: ==");
		TRACE(ft_t_info, "fdc isr statistics:\n"
		      KERN_INFO " id_am_errors     : %3d\n"
		      KERN_INFO " id_crc_errors    : %3d\n"
		      KERN_INFO " data_am_errors   : %3d\n"
		      KERN_INFO " data_crc_errors  : %3d\n"
		      KERN_INFO " overrun_errors   : %3d\n"
		      KERN_INFO " no_data_errors   : %3d\n"
		      KERN_INFO " retries          : %3d",
		      ft_history.id_am_errors,   ft_history.id_crc_errors,
		      ft_history.data_am_errors, ft_history.data_crc_errors,
		      ft_history.overrun_errors, ft_history.no_data_errors,
		      ft_history.retries);
		if (ft_history.used & 1) {
			TRACE(ft_t_info, "ecc statistics:\n"
			      KERN_INFO " crc_errors       : %3d\n"
			      KERN_INFO " crc_failures     : %3d\n"
			      KERN_INFO " ecc_failures     : %3d\n"
			      KERN_INFO " sectors corrected: %3d",
			      ft_history.crc_errors,   ft_history.crc_failures,
			      ft_history.ecc_failures, ft_history.corrected);
		}
		if (ft_history.defects > 0) {
			TRACE(ft_t_warn, "Warning: %d media defects!",
			      ft_history.defects);
		}
		if (ft_history.rewinds > 0) {
			TRACE(ft_t_info, "tape motion statistics:\n"
			      KERN_INFO "repositions       : %3d",
			      ft_history.rewinds);
		}
	}
	ft_failure = 1;
	TRACE_EXIT;
}

static void ftape_init_driver(void)
{
	TRACE_FUN(ft_t_flow);

	ft_drive_type.vendor_id = UNKNOWN_VENDOR;
	ft_drive_type.speed     = 0;
	ft_drive_type.wake_up   = unknown_wake_up;
	ft_drive_type.name      = "Unknown";

	ftape_timeout.seek      = 650 * FT_SECOND;
	ftape_timeout.reset     = 670 * FT_SECOND;
	ftape_timeout.rewind    = 650 * FT_SECOND;
	ftape_timeout.head_seek =  15 * FT_SECOND;
	ftape_timeout.stop      =   5 * FT_SECOND;
	ftape_timeout.pause     =  16 * FT_SECOND;

	ft_qic_std             = -1;
	ftape_tape_len         = 0;  /* unknown */
	ftape_current_command  = 0;
	ftape_current_cylinder = -1;

	ft_segments_per_track       = 102;
	ftape_segments_per_head     = 1020;
	ftape_segments_per_cylinder = 4;
	ft_tracks_per_tape          = 20;

	ft_failure = 1;

	ft_formatted       = 0;
	ft_no_tape         = 1;
	ft_write_protected = 1;
	ft_new_tape        = 1;

	ft_driver_state = idle;

	ft_data_rate = 
		ft_fdc_max_rate   = 500;
	ft_drive_max_rate = 0; /* triggers set_rate_test() */

	ftape_init_drive_needed = 1;

	ft_header_segment_1    = -1;
	ft_header_segment_2    = -1;
	ft_used_header_segment = -1;
	ft_first_data_segment  = -1;
	ft_last_data_segment   = -1;

	ft_location.track = -1;
	ft_location.known = 0;

	ftape_tape_running = 0;
	ftape_might_be_off_track = 1;

	ftape_new_cartridge();	/* init some tape related variables */
	ftape_init_bsm();
	TRACE_EXIT;
}
