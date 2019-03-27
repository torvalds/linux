/*
 * Copyright (c) 2006
 *	Hartmut Brandt.
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Begemot: bsnmp/snmp_mibII/mibII_begemot.c,v 1.1 2006/02/14 09:04:19 brandt_h Exp $
 *
 * Private MIB.
 */
#include "mibII.h"
#include "mibII_oid.h"

/*
 * Scalars
 */
int
op_begemot_mibII(struct snmp_context *ctx, struct snmp_value *value,
    u_int sub, u_int idx __unused, enum snmp_op op)
{
	switch (op) {

	  case SNMP_OP_GETNEXT:
		abort();

	  case SNMP_OP_GET:
		goto get;

	  case SNMP_OP_SET:
		switch (value->var.subs[sub - 1]) {

		  case LEAF_begemotIfMaxspeed:
		  case LEAF_begemotIfPoll:
			return (SNMP_ERR_NOT_WRITEABLE);

		  case LEAF_begemotIfForcePoll:
			ctx->scratch->int1 = mibif_force_hc_update_interval;
			mibif_force_hc_update_interval = value->v.uint32;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotIfDataPoll:
			ctx->scratch->int1 = mibII_poll_ticks;
			mibII_poll_ticks = value->v.uint32;
			return (SNMP_ERR_NOERROR);
		}
		abort();

	  case SNMP_OP_ROLLBACK:
		switch (value->var.subs[sub - 1]) {

		  case LEAF_begemotIfForcePoll:
			mibif_force_hc_update_interval = ctx->scratch->int1;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotIfDataPoll:
			mibII_poll_ticks = ctx->scratch->int1;
			return (SNMP_ERR_NOERROR);
		}
		abort();

	  case SNMP_OP_COMMIT:
		switch (value->var.subs[sub - 1]) {

		  case LEAF_begemotIfForcePoll:
			mibif_force_hc_update_interval = ctx->scratch->int1;
			mibif_reset_hc_timer();
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotIfDataPoll:
			mibif_restart_mibII_poll_timer();
			return (SNMP_ERR_NOERROR);
		}
		abort();
	}
	abort();

  get:

	switch (value->var.subs[sub - 1]) {

	  case LEAF_begemotIfMaxspeed:
		value->v.counter64 = mibif_maxspeed;
		return (SNMP_ERR_NOERROR);

	  case LEAF_begemotIfPoll:
		value->v.uint32 = mibif_hc_update_interval;
		return (SNMP_ERR_NOERROR);

	  case LEAF_begemotIfForcePoll:
		value->v.uint32 = mibif_force_hc_update_interval;
		return (SNMP_ERR_NOERROR);

	  case LEAF_begemotIfDataPoll:
		value->v.uint32 = mibII_poll_ticks;
		return (SNMP_ERR_NOERROR);
	}
	abort();
}
