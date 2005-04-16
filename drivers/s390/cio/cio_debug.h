#ifndef CIO_DEBUG_H
#define CIO_DEBUG_H

#include <asm/debug.h>

#define CIO_TRACE_EVENT(imp, txt) do { \
		debug_text_event(cio_debug_trace_id, imp, txt); \
	} while (0)

#define CIO_MSG_EVENT(imp, args...) do { \
		debug_sprintf_event(cio_debug_msg_id, imp , ##args); \
	} while (0)

#define CIO_CRW_EVENT(imp, args...) do { \
		debug_sprintf_event(cio_debug_crw_id, imp , ##args); \
	} while (0)

#define CIO_HEX_EVENT(imp, args...) do { \
                debug_event(cio_debug_trace_id, imp, ##args); \
        } while (0)

#define CIO_DEBUG(printk_level,event_level,msg...) ({ \
	if (cio_show_msg) printk(printk_level msg); \
	CIO_MSG_EVENT (event_level, msg); \
})

/* for use of debug feature */
extern debug_info_t *cio_debug_msg_id;
extern debug_info_t *cio_debug_trace_id;
extern debug_info_t *cio_debug_crw_id;

#endif
