/* SPDX-License-Identifier: GPL-2.0 */
#ifndef CIO_DE_H
#define CIO_DE_H

#include <asm/de.h>

/* for use of de feature */
extern de_info_t *cio_de_msg_id;
extern de_info_t *cio_de_trace_id;
extern de_info_t *cio_de_crw_id;

#define CIO_TRACE_EVENT(imp, txt) do {				\
		de_text_event(cio_de_trace_id, imp, txt); \
	} while (0)

#define CIO_MSG_EVENT(imp, args...) do {				\
		de_sprintf_event(cio_de_msg_id, imp , ##args);	\
	} while (0)

#define CIO_CRW_EVENT(imp, args...) do {				\
		de_sprintf_event(cio_de_crw_id, imp , ##args);	\
	} while (0)

static inline void CIO_HEX_EVENT(int level, void *data, int length)
{
	de_event(cio_de_trace_id, level, data, length);
}

#endif
