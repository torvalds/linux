/*
 *
 * linux/drivers/s390/net/ctcdbug.c ($Revision: 1.4 $)
 *
 * CTC / ESCON network driver - s390 dbf exploit.
 *
 * Copyright 2000,2003 IBM Corporation
 *
 *    Author(s): Original Code written by
 *			  Peter Tiedemann (ptiedem@de.ibm.com)
 *
 *    $Revision: 1.4 $	 $Date: 2004/08/04 10:11:59 $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "ctcdbug.h"

/**
 * Debug Facility Stuff
 */
debug_info_t *ctc_dbf_setup = NULL;
debug_info_t *ctc_dbf_data = NULL;
debug_info_t *ctc_dbf_trace = NULL;

DEFINE_PER_CPU(char[256], ctc_dbf_txt_buf);

void
ctc_unregister_dbf_views(void)
{
	if (ctc_dbf_setup)
		debug_unregister(ctc_dbf_setup);
	if (ctc_dbf_data)
		debug_unregister(ctc_dbf_data);
	if (ctc_dbf_trace)
		debug_unregister(ctc_dbf_trace);
}
int
ctc_register_dbf_views(void)
{
	ctc_dbf_setup = debug_register(CTC_DBF_SETUP_NAME,
					CTC_DBF_SETUP_INDEX,
					CTC_DBF_SETUP_NR_AREAS,
					CTC_DBF_SETUP_LEN);
	ctc_dbf_data = debug_register(CTC_DBF_DATA_NAME,
				       CTC_DBF_DATA_INDEX,
				       CTC_DBF_DATA_NR_AREAS,
				       CTC_DBF_DATA_LEN);
	ctc_dbf_trace = debug_register(CTC_DBF_TRACE_NAME,
					CTC_DBF_TRACE_INDEX,
					CTC_DBF_TRACE_NR_AREAS,
					CTC_DBF_TRACE_LEN);

	if ((ctc_dbf_setup == NULL) || (ctc_dbf_data == NULL) ||
	    (ctc_dbf_trace == NULL)) {
		ctc_unregister_dbf_views();
		return -ENOMEM;
	}
	debug_register_view(ctc_dbf_setup, &debug_hex_ascii_view);
	debug_set_level(ctc_dbf_setup, CTC_DBF_SETUP_LEVEL);

	debug_register_view(ctc_dbf_data, &debug_hex_ascii_view);
	debug_set_level(ctc_dbf_data, CTC_DBF_DATA_LEVEL);

	debug_register_view(ctc_dbf_trace, &debug_hex_ascii_view);
	debug_set_level(ctc_dbf_trace, CTC_DBF_TRACE_LEVEL);

	return 0;
}


