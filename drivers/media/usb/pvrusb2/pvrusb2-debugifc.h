/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 */
#ifndef __PVRUSB2_DEBUGIFC_H
#define __PVRUSB2_DEBUGIFC_H

struct pvr2_hdw;

/* Print general status of driver.  This will also trigger a probe of
   the USB link.  Unlike print_info(), this one synchronizes with the
   driver so the information should be self-consistent (but it will
   hang if the driver is wedged). */
int pvr2_debugifc_print_info(struct pvr2_hdw *,
			     char *buf_ptr, unsigned int buf_size);

/* Non-intrusively print some useful debugging info from inside the
   driver.  This should work even if the driver appears to be
   wedged. */
int pvr2_debugifc_print_status(struct pvr2_hdw *,
			       char *buf_ptr,unsigned int buf_size);

/* Parse a string command into a driver action. */
int pvr2_debugifc_docmd(struct pvr2_hdw *,
			const char *buf_ptr,unsigned int buf_size);

#endif /* __PVRUSB2_DEBUGIFC_H */
