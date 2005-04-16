/*
 *      Copyright (C) 1997 Claus-Justus Heine

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
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/ftape-proc.c,v $
 * $Revision: 1.11 $
 * $Date: 1997/10/24 14:47:37 $
 *
 *      This file contains the procfs interface for the
 *      QIC-40/80/3010/3020 floppy-tape driver "ftape" for Linux.

 *	Old code removed, switched to dynamic proc entry.
 */

#include <linux/config.h>

#if defined(CONFIG_PROC_FS) && defined(CONFIG_FT_PROC_FS)

#include <linux/proc_fs.h>

#include <linux/ftape.h>
#include <linux/init.h>
#include <linux/qic117.h>

#include "../lowlevel/ftape-io.h"
#include "../lowlevel/ftape-ctl.h"
#include "../lowlevel/ftape-proc.h"
#include "../lowlevel/ftape-tracing.h"

static size_t get_driver_info(char *buf)
{
	const char *debug_level[] = { "bugs"  ,
				      "errors",
				      "warnings",
				      "informational",
				      "noisy",
				      "program flow",
				      "fdc and dma",
				      "data flow",
				      "anything" };

	return sprintf(buf,
		       "version       : %s\n"
		       "used data rate: %d kbit/sec\n"
		       "dma memory    : %d kb\n"
		       "debug messages: %s\n",
		       FTAPE_VERSION,
		       ft_data_rate,
		       FT_BUFF_SIZE * ft_nr_buffers >> 10,
		       debug_level[TRACE_LEVEL]);
}

static size_t get_tapedrive_info(char *buf)
{ 
	return sprintf(buf,
		       "vendor id : 0x%04x\n"
		       "drive name: %s\n"
		       "wind speed: %d ips\n"
		       "wakeup    : %s\n"
		       "max. rate : %d kbit/sec\n",
		       ft_drive_type.vendor_id,
		       ft_drive_type.name,
		       ft_drive_type.speed,
		       ((ft_drive_type.wake_up == no_wake_up)
			? "No wakeup needed" :
			((ft_drive_type.wake_up == wake_up_colorado)
			 ? "Colorado" :
			 ((ft_drive_type.wake_up == wake_up_mountain)
			  ? "Mountain" :
			  ((ft_drive_type.wake_up == wake_up_insight)
			   ? "Motor on" :
			   "Unknown")))),
		       ft_drive_max_rate);
}

static size_t get_cartridge_info(char *buf)
{
	if (ftape_init_drive_needed) {
		return sprintf(buf, "uninitialized\n");
	}
	if (ft_no_tape) {
		return sprintf(buf, "no cartridge inserted\n");
	}
	return sprintf(buf,
		       "segments  : %5d\n"
		       "tracks    : %5d\n"
		       "length    : %5dft\n"
		       "formatted : %3s\n"
		       "writable  : %3s\n"
		       "QIC spec. : QIC-%s\n"
		       "fmt-code  : %1d\n",
		       ft_segments_per_track,
		       ft_tracks_per_tape,
		       ftape_tape_len,
		       (ft_formatted == 1) ? "yes" : "no",
		       (ft_write_protected == 1) ? "no" : "yes",
		       ((ft_qic_std == QIC_TAPE_QIC40) ? "40" :
			((ft_qic_std == QIC_TAPE_QIC80) ? "80" :
			 ((ft_qic_std == QIC_TAPE_QIC3010) ? "3010" :
			  ((ft_qic_std == QIC_TAPE_QIC3020) ? "3020" :
			   "???")))),
		       ft_format_code);
}

static size_t get_controller_info(char *buf)
{
	const char  *fdc_name[] = { "no fdc",
				    "i8272",
				    "i82077",
				    "i82077AA",
				    "Colorado FC-10 or FC-20",
				    "i82078",
				    "i82078_1" };

	return sprintf(buf,
		       "FDC type  : %s\n"
		       "FDC base  : 0x%03x\n"
		       "FDC irq   : %d\n"
		       "FDC dma   : %d\n"
		       "FDC thr.  : %d\n"
		       "max. rate : %d kbit/sec\n",
		       ft_mach2 ? "Mountain MACH-2" : fdc_name[fdc.type],
		       fdc.sra, fdc.irq, fdc.dma,
		       ft_fdc_threshold, ft_fdc_max_rate);
}

static size_t get_history_info(char *buf)
{
        size_t len;

	len  = sprintf(buf,
		       "\nFDC isr statistics\n"
		       " id_am_errors     : %3d\n"
		       " id_crc_errors    : %3d\n"
		       " data_am_errors   : %3d\n"
		       " data_crc_errors  : %3d\n"
		       " overrun_errors   : %3d\n"
		       " no_data_errors   : %3d\n"
		       " retries          : %3d\n",
		       ft_history.id_am_errors,   ft_history.id_crc_errors,
		       ft_history.data_am_errors, ft_history.data_crc_errors,
		       ft_history.overrun_errors, ft_history.no_data_errors,
		       ft_history.retries);
	len += sprintf(buf + len,
		       "\nECC statistics\n"
		       " crc_errors       : %3d\n"
		       " crc_failures     : %3d\n"
		       " ecc_failures     : %3d\n"
		       " sectors corrected: %3d\n",
		       ft_history.crc_errors,   ft_history.crc_failures,
		       ft_history.ecc_failures, ft_history.corrected);
	len += sprintf(buf + len,
		       "\ntape quality statistics\n"
		       " media defects    : %3d\n",
		       ft_history.defects);
	len += sprintf(buf + len,
		       "\ntape motion statistics\n"
		       " repositions      : %3d\n",
		       ft_history.rewinds);
	return len;
}

static int ftape_read_proc(char *page, char **start, off_t off,
			   int count, int *eof, void *data)
{
	char *ptr = page;
	size_t len;
	
	ptr += sprintf(ptr, "Kernel Driver\n\n");
	ptr += get_driver_info(ptr);
	ptr += sprintf(ptr, "\nTape Drive\n\n");
	ptr += get_tapedrive_info(ptr);
	ptr += sprintf(ptr, "\nFDC Controller\n\n");
	ptr += get_controller_info(ptr);
	ptr += sprintf(ptr, "\nTape Cartridge\n\n");
	ptr += get_cartridge_info(ptr);
	ptr += sprintf(ptr, "\nHistory Record\n\n");
	ptr += get_history_info(ptr);

	len = strlen(page);
	*start = NULL;
	if (off+count >= len) {
		*eof = 1;
	} else {
		*eof = 0;
	}
	return len;
}

int __init ftape_proc_init(void)
{
	return create_proc_read_entry("ftape", 0, &proc_root,
		ftape_read_proc, NULL) != NULL;
}

void ftape_proc_destroy(void)
{
	remove_proc_entry("ftape", &proc_root);
}

#endif /* defined(CONFIG_PROC_FS) && defined(CONFIG_FT_PROC_FS) */
