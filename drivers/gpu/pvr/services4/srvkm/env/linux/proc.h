/*************************************************************************/ /*!
@Title          Proc interface definition.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Functions for creating and reading proc filesystem entries.
                Refer to proc.c
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#ifndef __SERVICES_PROC_H__
#define __SERVICES_PROC_H__

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0))
#include <asm/system.h>		// va_list etc
#endif
#include <linux/proc_fs.h>	// read_proc_t etc
#include <linux/seq_file.h> // seq_file

#define END_OF_FILE (off_t) -1

typedef off_t (pvr_read_proc_t)(IMG_CHAR *, size_t, off_t);


#define PVR_PROC_SEQ_START_TOKEN (void*)1
typedef void* (pvr_next_proc_seq_t)(struct seq_file *,void*,loff_t);
typedef void* (pvr_off2element_proc_seq_t)(struct seq_file *, loff_t);
typedef void (pvr_show_proc_seq_t)(struct seq_file *,void*);
typedef void (pvr_startstop_proc_seq_t)(struct seq_file *, IMG_BOOL start);

typedef struct _PVR_PROC_SEQ_HANDLERS_ {
	pvr_next_proc_seq_t *next;	
	pvr_show_proc_seq_t *show;	
	pvr_off2element_proc_seq_t *off2element;
	pvr_startstop_proc_seq_t *startstop;
	IMG_VOID *data;
} PVR_PROC_SEQ_HANDLERS;


/** off2element function for elements with only ONE element (no header) */
void* ProcSeq1ElementOff2Element(struct seq_file *sfile, loff_t off);

/** off2element function for elements with only ONE element (+ header) */
void* ProcSeq1ElementHeaderOff2Element(struct seq_file *sfile, loff_t off);

off_t printAppend(IMG_CHAR * buffer, size_t size, off_t off, const IMG_CHAR * format, ...)
	__attribute__((format(printf, 4, 5)));

IMG_INT CreateProcEntries(IMG_VOID);

IMG_INT CreateProcReadEntry (const IMG_CHAR * name, pvr_read_proc_t handler);

IMG_INT CreateProcEntry(const IMG_CHAR * name, read_proc_t rhandler, write_proc_t whandler, IMG_VOID *data);

IMG_INT CreatePerProcessProcEntry(const IMG_CHAR * name, read_proc_t rhandler, write_proc_t whandler, IMG_VOID *data);

IMG_VOID RemoveProcEntry(const IMG_CHAR * name);

IMG_VOID RemovePerProcessProcEntry(const IMG_CHAR * name);

IMG_VOID RemoveProcEntries(IMG_VOID);

struct proc_dir_entry* CreateProcReadEntrySeq (
								const IMG_CHAR* name, 
								IMG_VOID* data,
								pvr_next_proc_seq_t next_handler, 
								pvr_show_proc_seq_t show_handler,
								pvr_off2element_proc_seq_t off2element_handler,
								pvr_startstop_proc_seq_t startstop_handler
							   );

struct proc_dir_entry* CreateProcEntrySeq (
								const IMG_CHAR* name, 
								IMG_VOID* data,
								pvr_next_proc_seq_t next_handler, 
								pvr_show_proc_seq_t show_handler,
								pvr_off2element_proc_seq_t off2element_handler,
								pvr_startstop_proc_seq_t startstop_handler,
								write_proc_t whandler
							   );

struct proc_dir_entry* CreatePerProcessProcEntrySeq (
								const IMG_CHAR* name, 
								IMG_VOID* data,
								pvr_next_proc_seq_t next_handler, 
								pvr_show_proc_seq_t show_handler,
								pvr_off2element_proc_seq_t off2element_handler,
								pvr_startstop_proc_seq_t startstop_handler,
								write_proc_t whandler
							   );


IMG_VOID RemoveProcEntrySeq(struct proc_dir_entry* proc_entry);
IMG_VOID RemovePerProcessProcEntrySeq(struct proc_dir_entry* proc_entry);

#endif
