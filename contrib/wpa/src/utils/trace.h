/*
 * Backtrace debugging
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef TRACE_H
#define TRACE_H

#define WPA_TRACE_LEN 16

#ifdef WPA_TRACE
#include <execinfo.h>

#include "list.h"

#define WPA_TRACE_INFO void *btrace[WPA_TRACE_LEN]; int btrace_num;

struct wpa_trace_ref {
	struct dl_list list;
	const void *addr;
	WPA_TRACE_INFO
};
#define WPA_TRACE_REF(name) struct wpa_trace_ref wpa_trace_ref_##name

#define wpa_trace_dump(title, ptr) \
	wpa_trace_dump_func((title), (ptr)->btrace, (ptr)->btrace_num)
void wpa_trace_dump_func(const char *title, void **btrace, int btrace_num);
#define wpa_trace_record(ptr) \
	(ptr)->btrace_num = backtrace((ptr)->btrace, WPA_TRACE_LEN)
void wpa_trace_show(const char *title);
#define wpa_trace_add_ref(ptr, name, addr) \
	wpa_trace_add_ref_func(&(ptr)->wpa_trace_ref_##name, (addr))
void wpa_trace_add_ref_func(struct wpa_trace_ref *ref, const void *addr);
#define wpa_trace_remove_ref(ptr, name, addr)	\
	do { \
		if ((addr)) \
			dl_list_del(&(ptr)->wpa_trace_ref_##name.list); \
	} while (0)
void wpa_trace_check_ref(const void *addr);
size_t wpa_trace_calling_func(const char *buf[], size_t len);

#else /* WPA_TRACE */

#define WPA_TRACE_INFO
#define WPA_TRACE_REF(n)
#define wpa_trace_dump(title, ptr) do { } while (0)
#define wpa_trace_record(ptr) do { } while (0)
#define wpa_trace_show(title) do { } while (0)
#define wpa_trace_add_ref(ptr, name, addr) do { } while (0)
#define wpa_trace_remove_ref(ptr, name, addr) do { } while (0)
#define wpa_trace_check_ref(addr) do { } while (0)

#endif /* WPA_TRACE */


#ifdef WPA_TRACE_BFD

void wpa_trace_dump_funcname(const char *title, void *pc);

#else /* WPA_TRACE_BFD */

#define wpa_trace_dump_funcname(title, pc) do { } while (0)

#endif /* WPA_TRACE_BFD */

void wpa_trace_deinit(void);

#endif /* TRACE_H */
