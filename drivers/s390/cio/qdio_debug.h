/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright IBM Corp. 2008
 *
 *  Author: Jan Glauber (jang@linux.vnet.ibm.com)
 */
#ifndef QDIO_DE_H
#define QDIO_DE_H

#include <asm/de.h>
#include <asm/qdio.h>
#include "qdio.h"

/* that gives us 15 characters in the text event views */
#define QDIO_DBF_LEN	32

extern de_info_t *qdio_dbf_setup;
extern de_info_t *qdio_dbf_error;

#define DBF_ERR		3	/* error conditions	*/
#define DBF_WARN	4	/* warning conditions	*/
#define DBF_INFO	6	/* informational	*/

#undef DBF_EVENT
#undef DBF_ERROR
#undef DBF_DEV_EVENT

#define DBF_EVENT(text...) \
	do { \
		char de_buffer[QDIO_DBF_LEN]; \
		snprintf(de_buffer, QDIO_DBF_LEN, text); \
		de_text_event(qdio_dbf_setup, DBF_ERR, de_buffer); \
	} while (0)

static inline void DBF_HEX(void *addr, int len)
{
	de_event(qdio_dbf_setup, DBF_ERR, addr, len);
}

#define DBF_ERROR(text...) \
	do { \
		char de_buffer[QDIO_DBF_LEN]; \
		snprintf(de_buffer, QDIO_DBF_LEN, text); \
		de_text_event(qdio_dbf_error, DBF_ERR, de_buffer); \
	} while (0)

static inline void DBF_ERROR_HEX(void *addr, int len)
{
	de_event(qdio_dbf_error, DBF_ERR, addr, len);
}

#define DBF_DEV_EVENT(level, device, text...) \
	do { \
		char de_buffer[QDIO_DBF_LEN]; \
		if (de_level_enabled(device->de_area, level)) { \
			snprintf(de_buffer, QDIO_DBF_LEN, text); \
			de_text_event(device->de_area, level, de_buffer); \
		} \
	} while (0)

static inline void DBF_DEV_HEX(struct qdio_irq *dev, void *addr,
			       int len, int level)
{
	de_event(dev->de_area, level, addr, len);
}

int qdio_allocate_dbf(struct qdio_initialize *init_data,
		       struct qdio_irq *irq_ptr);
void qdio_setup_de_entries(struct qdio_irq *irq_ptr,
			      struct ccw_device *cdev);
void qdio_shutdown_de_entries(struct qdio_irq *irq_ptr);
int qdio_de_init(void);
void qdio_de_exit(void);

#endif
