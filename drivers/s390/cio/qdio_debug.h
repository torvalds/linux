/*
 *  drivers/s390/cio/qdio_debug.h
 *
 *  Copyright IBM Corp. 2008
 *
 *  Author: Jan Glauber (jang@linux.vnet.ibm.com)
 */
#ifndef QDIO_DEBUG_H
#define QDIO_DEBUG_H

#include <asm/debug.h>
#include <asm/qdio.h>
#include "qdio.h"

#define QDIO_DBF_HEX(ex, name, level, addr, len) \
	do { \
	if (ex) \
		debug_exception(qdio_dbf_##name, level, (void *)(addr), len); \
	else \
		debug_event(qdio_dbf_##name, level, (void *)(addr), len); \
	} while (0)
#define QDIO_DBF_TEXT(ex, name, level, text) \
	do { \
	if (ex) \
		debug_text_exception(qdio_dbf_##name, level, text); \
	else \
		debug_text_event(qdio_dbf_##name, level, text); \
	} while (0)

#define QDIO_DBF_HEX0(ex, name, addr, len) QDIO_DBF_HEX(ex, name, 0, addr, len)
#define QDIO_DBF_HEX1(ex, name, addr, len) QDIO_DBF_HEX(ex, name, 1, addr, len)
#define QDIO_DBF_HEX2(ex, name, addr, len) QDIO_DBF_HEX(ex, name, 2, addr, len)

#ifdef CONFIG_QDIO_DEBUG
#define QDIO_DBF_HEX3(ex, name, addr, len) QDIO_DBF_HEX(ex, name, 3, addr, len)
#define QDIO_DBF_HEX4(ex, name, addr, len) QDIO_DBF_HEX(ex, name, 4, addr, len)
#define QDIO_DBF_HEX5(ex, name, addr, len) QDIO_DBF_HEX(ex, name, 5, addr, len)
#define QDIO_DBF_HEX6(ex, name, addr, len) QDIO_DBF_HEX(ex, name, 6, addr, len)
#else
#define QDIO_DBF_HEX3(ex, name, addr, len) do {} while (0)
#define QDIO_DBF_HEX4(ex, name, addr, len) do {} while (0)
#define QDIO_DBF_HEX5(ex, name, addr, len) do {} while (0)
#define QDIO_DBF_HEX6(ex, name, addr, len) do {} while (0)
#endif /* CONFIG_QDIO_DEBUG */

#define QDIO_DBF_TEXT0(ex, name, text) QDIO_DBF_TEXT(ex, name, 0, text)
#define QDIO_DBF_TEXT1(ex, name, text) QDIO_DBF_TEXT(ex, name, 1, text)
#define QDIO_DBF_TEXT2(ex, name, text) QDIO_DBF_TEXT(ex, name, 2, text)

#ifdef CONFIG_QDIO_DEBUG
#define QDIO_DBF_TEXT3(ex, name, text) QDIO_DBF_TEXT(ex, name, 3, text)
#define QDIO_DBF_TEXT4(ex, name, text) QDIO_DBF_TEXT(ex, name, 4, text)
#define QDIO_DBF_TEXT5(ex, name, text) QDIO_DBF_TEXT(ex, name, 5, text)
#define QDIO_DBF_TEXT6(ex, name, text) QDIO_DBF_TEXT(ex, name, 6, text)
#else
#define QDIO_DBF_TEXT3(ex, name, text) do {} while (0)
#define QDIO_DBF_TEXT4(ex, name, text) do {} while (0)
#define QDIO_DBF_TEXT5(ex, name, text) do {} while (0)
#define QDIO_DBF_TEXT6(ex, name, text) do {} while (0)
#endif /* CONFIG_QDIO_DEBUG */

/* s390dbf views */
#define QDIO_DBF_SETUP_LEN		8
#define QDIO_DBF_SETUP_PAGES		8
#define QDIO_DBF_SETUP_NR_AREAS		1

#define QDIO_DBF_TRACE_LEN		8
#define QDIO_DBF_TRACE_NR_AREAS		2

#ifdef CONFIG_QDIO_DEBUG
#define QDIO_DBF_TRACE_PAGES		32
#define QDIO_DBF_SETUP_LEVEL		6
#define QDIO_DBF_TRACE_LEVEL		4
#else /* !CONFIG_QDIO_DEBUG */
#define QDIO_DBF_TRACE_PAGES		8
#define QDIO_DBF_SETUP_LEVEL		2
#define QDIO_DBF_TRACE_LEVEL		2
#endif /* CONFIG_QDIO_DEBUG */

extern debug_info_t *qdio_dbf_setup;
extern debug_info_t *qdio_dbf_trace;

void qdio_allocate_do_dbf(struct qdio_initialize *init_data);
void debug_print_bstat(struct qdio_q *q);
void qdio_setup_debug_entries(struct qdio_irq *irq_ptr,
			      struct ccw_device *cdev);
void qdio_shutdown_debug_entries(struct qdio_irq *irq_ptr,
				 struct ccw_device *cdev);
int qdio_debug_init(void);
void qdio_debug_exit(void);
#endif
