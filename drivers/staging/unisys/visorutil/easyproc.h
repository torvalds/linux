/* easyproc.h
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/** @file *********************************************************************
 *
 *  This describes the interfaces necessary for a simple /proc file
 *  implementation for a driver.
 *
 ******************************************************************************
 */

#ifndef __EASYPROC_H__
#define __EASYPROC_H__

#include "timskmod.h"


struct easyproc_driver_info {
	struct proc_dir_entry *ProcDir;
	struct proc_dir_entry *ProcDriverDir;
	struct proc_dir_entry *ProcDriverDiagFile;
	struct proc_dir_entry *ProcDeviceDir;
	char *ProcId;
	void (*Show_device_info)(struct seq_file *seq, void *p);
	void (*Show_driver_info)(struct seq_file *seq);
	void (*Write_device_info)(char *buf, size_t count,
				  loff_t *ppos, void *p);
	void (*Write_driver_info)(char *buf, size_t count, loff_t *ppos);
};

/* property is a file under /proc/<x>/device/<x>/<property_name> */
struct easyproc_device_property_info {
	char property_name[25];
	struct proc_dir_entry *procEntry;
	struct easyproc_driver_info *pdriver;
	void *devdata;
	void (*show_device_property_info)(struct seq_file *seq, void *p);
};

struct easyproc_device_info {
	struct proc_dir_entry *procDevicexDir;
	struct proc_dir_entry *procDevicexDiagFile;
	struct easyproc_driver_info *pdriver;
	void *devdata;
	int devno;
	/*  allow for a number of custom properties for each device: */
	struct easyproc_device_property_info device_property_info[10];
};

void visor_easyproc_InitDevice(struct easyproc_driver_info *pdriver,
			       struct easyproc_device_info *p, int devno,
			       void *devdata);
void visor_easyproc_DeInitDevice(struct easyproc_driver_info *pdriver,
				 struct easyproc_device_info *p, int devno);
void visor_easyproc_InitDriver(struct easyproc_driver_info *pdriver,
			       char *procId,
			       void (*show_driver_info)(struct seq_file *),
			       void (*show_device_info)(struct seq_file *,
							void *));
void visor_easyproc_InitDriverEx(struct easyproc_driver_info *pdriver,
				 char *procId,
				 void (*show_driver_info)(struct seq_file *),
				 void (*show_device_info)(struct seq_file *,
							  void *),
				 void (*Write_driver_info)(char *buf,
							   size_t count,
							   loff_t *ppos),
				 void (*Write_device_info)(char *buf,
							   size_t count,
							   loff_t *ppos,
							   void *p));
void visor_easyproc_DeInitDriver(struct easyproc_driver_info *pdriver);
void visor_easyproc_CreateDeviceProperty(struct easyproc_device_info *p,
					 void (*show_property_info)
					 (struct seq_file *, void *),
					 char *property_name);

#endif
