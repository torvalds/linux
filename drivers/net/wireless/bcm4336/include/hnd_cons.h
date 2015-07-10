/*
 * Console support for RTE - for host use only.
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: hnd_cons.h 473343 2014-04-29 01:45:22Z $
 */
#ifndef	_hnd_cons_h_
#define	_hnd_cons_h_

#include <typedefs.h>
#include <siutils.h>

#define CBUF_LEN	(128)

#define LOG_BUF_LEN	1024

#ifdef BOOTLOADER_CONSOLE_OUTPUT
#undef RWL_MAX_DATA_LEN
#undef CBUF_LEN
#undef LOG_BUF_LEN
#define RWL_MAX_DATA_LEN (4 * 1024 + 8)
#define CBUF_LEN	(RWL_MAX_DATA_LEN + 64)
#define LOG_BUF_LEN (16 * 1024)
#endif

typedef struct {
	uint32		buf;		/* Can't be pointer on (64-bit) hosts */
	uint		buf_size;
	uint		idx;
	uint		out_idx;	/* output index */
} hnd_log_t;

typedef struct {
	/* Virtual UART
	 *   When there is no UART (e.g. Quickturn), the host should write a complete
	 *   input line directly into cbuf and then write the length into vcons_in.
	 *   This may also be used when there is a real UART (at risk of conflicting with
	 *   the real UART).  vcons_out is currently unused.
	 */
	volatile uint	vcons_in;
	volatile uint	vcons_out;

	/* Output (logging) buffer
	 *   Console output is written to a ring buffer log_buf at index log_idx.
	 *   The host may read the output when it sees log_idx advance.
	 *   Output will be lost if the output wraps around faster than the host polls.
	 */
	hnd_log_t	log;

	/* Console input line buffer
	 *   Characters are read one at a time into cbuf until <CR> is received, then
	 *   the buffer is processed as a command line.  Also used for virtual UART.
	 */
	uint		cbuf_idx;
	char		cbuf[CBUF_LEN];
} hnd_cons_t;

#endif /* _hnd_cons_h_ */
