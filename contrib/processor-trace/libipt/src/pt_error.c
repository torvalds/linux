/*
 * Copyright (c) 2013-2018, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "intel-pt.h"


const char *pt_errstr(enum pt_error_code errcode)
{
	switch (errcode) {
	case pte_ok:
		return "OK";

	case pte_internal:
		return "internal error";

	case pte_invalid:
		return "invalid argument";

	case pte_nosync:
		return "decoder out of sync";

	case pte_bad_opc:
		return "unknown opcode";

	case pte_bad_packet:
		return "unknown packet";

	case pte_bad_context:
		return "unexpected packet context";

	case pte_eos:
		return "reached end of trace stream";

	case pte_bad_query:
		return "trace stream does not match query";

	case pte_nomem:
		return "out of memory";

	case pte_bad_config:
		return "bad configuration";

	case pte_noip:
		return "no ip";

	case pte_ip_suppressed:
		return "ip has been suppressed";

	case pte_nomap:
		return "no memory mapped at this address";

	case pte_bad_insn:
		return "unknown instruction";

	case pte_no_time:
		return "no timing information";

	case pte_no_cbr:
		return "no core:bus ratio";

	case pte_bad_image:
		return "bad image";

	case pte_bad_lock:
		return "locking error";

	case pte_not_supported:
		return "not supported";

	case pte_retstack_empty:
		return "compressed return without call";

	case pte_bad_retcomp:
		return "bad compressed return";

	case pte_bad_status_update:
		return "bad status update";

	case pte_no_enable:
		return "expected tracing enabled event";

	case pte_event_ignored:
		return "event ignored";

	case pte_overflow:
		return "overflow";

	case pte_bad_file:
		return "bad file";

	case pte_bad_cpu:
		return "unknown cpu";
	}

	/* Should not reach here. */
	return "internal error.";
}
