/*
 * Console support for hndrte.
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: hndrte_cons.h 383834 2013-02-07 23:21:51Z $
 */
#ifndef	_HNDRTE_CONS_H
#define	_HNDRTE_CONS_H

#include <typedefs.h>

#define CBUF_LEN	(128)

#define LOG_BUF_LEN	1024

typedef struct {
	uint32		buf;		/* Can't be pointer on (64-bit) hosts */
	uint		buf_size;
	uint		idx;
	char		*_buf_compat;	/* redundant pointer for backward compat. */
} hndrte_log_t;

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
	hndrte_log_t	log;

	/* Console input line buffer
	 *   Characters are read one at a time into cbuf until <CR> is received, then
	 *   the buffer is processed as a command line.  Also used for virtual UART.
	 */
	uint		cbuf_idx;
	char		cbuf[CBUF_LEN];
} hndrte_cons_t;

hndrte_cons_t *hndrte_get_active_cons_state(void);

#endif /* _HNDRTE_CONS_H */
