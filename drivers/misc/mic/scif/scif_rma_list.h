/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * Intel SCIF driver.
 *
 */
#ifndef SCIF_RMA_LIST_H
#define SCIF_RMA_LIST_H

/*
 * struct scif_rma_req - Self Registration list RMA Request query
 *
 * @out_window - Returns the window if found
 * @offset: Starting offset
 * @nr_bytes: number of bytes
 * @prot: protection requested i.e. read or write or both
 * @type: Specify single, partial or multiple windows
 * @head: Head of list on which to search
 * @va_for_temp: VA for searching temporary cached windows
 */
struct scif_rma_req {
	struct scif_window **out_window;
	union {
		s64 offset;
		unsigned long va_for_temp;
	};
	size_t nr_bytes;
	int prot;
	enum scif_window_type type;
	struct list_head *head;
};

/* Insert */
void scif_insert_window(struct scif_window *window, struct list_head *head);
void scif_insert_tcw(struct scif_window *window,
		     struct list_head *head);
/* Query */
int scif_query_window(struct scif_rma_req *request);
int scif_query_tcw(struct scif_endpt *ep, struct scif_rma_req *request);
/* Called from close to unregister all self windows */
int scif_unregister_all_windows(scif_epd_t epd);
void scif_unmap_all_windows(scif_epd_t epd);
/* Traverse list and unregister */
int scif_rma_list_unregister(struct scif_window *window, s64 offset,
			     int nr_pages);
#endif /* SCIF_RMA_LIST_H */
