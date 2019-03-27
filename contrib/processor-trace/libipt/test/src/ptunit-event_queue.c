/*
 * Copyright (c) 2014-2018, Intel Corporation
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

#include "ptunit.h"

#include "pt_event_queue.h"


/* A test fixture providing an initialized event queue. */
struct evq_fixture {
	/* The event queue. */
	struct pt_event_queue evq;

	/* The test fixture initialization and finalization functions. */
	struct ptunit_result (*init)(struct evq_fixture *);
	struct ptunit_result (*fini)(struct evq_fixture *);
};


static struct ptunit_result efix_init(struct evq_fixture *efix)
{
	pt_evq_init(&efix->evq);

	return ptu_passed();
}

static struct ptunit_result efix_init_pending(struct evq_fixture *efix)
{
	struct pt_event *ev;
	int evb;

	pt_evq_init(&efix->evq);

	for (evb = 0; evb < evb_max; ++evb) {
		ev = pt_evq_enqueue(&efix->evq, (enum pt_event_binding) evb);
		ptu_ptr(ev);
	}

	return ptu_passed();
}

static struct ptunit_result standalone_null(void)
{
	struct pt_event *ev;

	ev = pt_evq_standalone(NULL);
	ptu_null(ev);

	return ptu_passed();
}

static struct ptunit_result standalone(struct evq_fixture *efix)
{
	struct pt_event *ev;

	ev = pt_evq_standalone(&efix->evq);
	ptu_ptr(ev);
	ptu_uint_eq(ev->ip_suppressed, 0ul);
	ptu_uint_eq(ev->status_update, 0ul);

	return ptu_passed();
}

static struct ptunit_result enqueue_null(enum pt_event_binding evb)
{
	struct pt_event *ev;

	ev = pt_evq_enqueue(NULL, evb);
	ptu_null(ev);

	return ptu_passed();
}

static struct ptunit_result dequeue_null(enum pt_event_binding evb)
{
	struct pt_event *ev;

	ev = pt_evq_dequeue(NULL, evb);
	ptu_null(ev);

	return ptu_passed();
}

static struct ptunit_result dequeue_empty(struct evq_fixture *efix,
					  enum pt_event_binding evb)
{
	struct pt_event *ev;

	ev = pt_evq_dequeue(&efix->evq, evb);
	ptu_null(ev);

	return ptu_passed();
}

static struct ptunit_result evq_empty(struct evq_fixture *efix,
				      enum pt_event_binding evb)
{
	int status;

	status = pt_evq_empty(&efix->evq, evb);
	ptu_int_gt(status, 0);

	status = pt_evq_pending(&efix->evq, evb);
	ptu_int_eq(status, 0);

	return ptu_passed();
}

static struct ptunit_result evq_pending(struct evq_fixture *efix,
					enum pt_event_binding evb)
{
	int status;

	status = pt_evq_empty(&efix->evq, evb);
	ptu_int_eq(status, 0);

	status = pt_evq_pending(&efix->evq, evb);
	ptu_int_gt(status, 0);

	return ptu_passed();
}

static struct ptunit_result evq_others_empty(struct evq_fixture *efix,
					     enum pt_event_binding evb)
{
	int other;

	for (other = 0; other < evb_max; ++other) {
		enum pt_event_binding ob;

		ob = (enum pt_event_binding) other;
		if (ob != evb)
			ptu_test(evq_empty, efix, ob);
	}

	return ptu_passed();
}

static struct ptunit_result enqueue_all_dequeue(struct evq_fixture *efix,
						enum pt_event_binding evb,
						size_t num)
{
	struct pt_event *in[evq_max], *out[evq_max];
	size_t idx;

	ptu_uint_le(num, evq_max - 2);

	for (idx = 0; idx < num; ++idx) {
		in[idx] = pt_evq_enqueue(&efix->evq, evb);
		ptu_ptr(in[idx]);
	}

	ptu_test(evq_pending, efix, evb);
	ptu_test(evq_others_empty, efix, evb);

	for (idx = 0; idx < num; ++idx) {
		out[idx] = pt_evq_dequeue(&efix->evq, evb);
		ptu_ptr_eq(out[idx], in[idx]);
	}

	ptu_test(evq_empty, efix, evb);

	return ptu_passed();
}

static struct ptunit_result enqueue_one_dequeue(struct evq_fixture *efix,
						enum pt_event_binding evb,
						size_t num)
{
	size_t idx;

	for (idx = 0; idx < num; ++idx) {
		struct pt_event *in, *out;

		in = pt_evq_enqueue(&efix->evq, evb);
		ptu_ptr(in);

		out = pt_evq_dequeue(&efix->evq, evb);
		ptu_ptr_eq(out, in);
	}

	return ptu_passed();
}

static struct ptunit_result overflow(struct evq_fixture *efix,
				     enum pt_event_binding evb,
				     size_t num)
{
	struct pt_event *in[evq_max], *out[evq_max], *ev;
	size_t idx;

	ptu_uint_le(num, evq_max - 2);

	for (idx = 0; idx < (evq_max - 2); ++idx) {
		in[idx] = pt_evq_enqueue(&efix->evq, evb);
		ptu_ptr(in[idx]);
	}

	for (idx = 0; idx < num; ++idx) {
		ev = pt_evq_enqueue(&efix->evq, evb);
		ptu_null(ev);
	}

	for (idx = 0; idx < num; ++idx) {
		out[idx] = pt_evq_dequeue(&efix->evq, evb);
		ptu_ptr_eq(out[idx], in[idx]);
	}

	return ptu_passed();
}

static struct ptunit_result clear_null(enum pt_event_binding evb)
{
	int errcode;

	errcode = pt_evq_clear(NULL, evb);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result clear(struct evq_fixture *efix,
				  enum pt_event_binding evb)
{
	int errcode;

	errcode = pt_evq_clear(&efix->evq, evb);
	ptu_int_eq(errcode, 0);

	ptu_test(evq_empty, efix, evb);

	return ptu_passed();
}

static struct ptunit_result empty_null(enum pt_event_binding evb)
{
	int errcode;

	errcode = pt_evq_empty(NULL, evb);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result pending_null(enum pt_event_binding evb)
{
	int errcode;

	errcode = pt_evq_pending(NULL, evb);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result find_null(enum pt_event_binding evb,
				      enum pt_event_type evt)
{
	struct pt_event *ev;

	ev = pt_evq_find(NULL, evb, evt);
	ptu_null(ev);

	return ptu_passed();
}

static struct ptunit_result find_empty(struct evq_fixture *efix,
				       enum pt_event_binding evb,
				       enum pt_event_type evt)
{
	struct pt_event *ev;

	ev = pt_evq_find(&efix->evq, evb, evt);
	ptu_null(ev);

	return ptu_passed();
}

static struct ptunit_result find_none_evb(struct evq_fixture *efix,
					  enum pt_event_binding evb,
					  enum pt_event_type evt)
{
	struct pt_event *ev;
	size_t other;

	for (other = 0; other < evb_max; ++other) {
		enum pt_event_binding ob;

		ob = (enum pt_event_binding) other;
		if (ob != evb) {
			ev = pt_evq_enqueue(&efix->evq, ob);
			ptu_ptr(ev);

			ev->type = evt;
		}
	}

	ev = pt_evq_find(&efix->evq, evb, evt);
	ptu_null(ev);

	return ptu_passed();
}

static struct ptunit_result evq_enqueue_other(struct evq_fixture *efix,
					      enum pt_event_binding evb,
					      enum pt_event_type evt,
					      size_t num)
{
	enum pt_event_type ot;
	struct pt_event *ev;
	size_t other;

	for (other = 0; other < num; ++other) {
		ot = (enum pt_event_type) other;
		if (ot != evt) {
			ev = pt_evq_enqueue(&efix->evq, evb);
			ptu_ptr(ev);

			ev->type = ot;
		}
	}

	return ptu_passed();
}

static struct ptunit_result find_none_evt(struct evq_fixture *efix,
					  enum pt_event_binding evb,
					  enum pt_event_type evt,
					  size_t num)
{
	struct pt_event *ev;

	ptu_test(evq_enqueue_other, efix, evb, evt, num);

	ev = pt_evq_find(&efix->evq, evb, evt);
	ptu_null(ev);

	return ptu_passed();
}

static struct ptunit_result find(struct evq_fixture *efix,
				 enum pt_event_binding evb,
				 enum pt_event_type evt,
				 size_t before, size_t after)
{
	struct pt_event *in, *out;

	ptu_test(evq_enqueue_other, efix, evb, evt, before);

	in = pt_evq_enqueue(&efix->evq, evb);
	ptu_ptr(in);

	in->type = evt;

	ptu_test(evq_enqueue_other, efix, evb, evt, after);

	out = pt_evq_find(&efix->evq, evb, evt);
	ptu_ptr_eq(out, in);

	return ptu_passed();
}

int main(int argc, char **argv)
{
	struct evq_fixture efix, pfix;
	struct ptunit_suite suite;

	efix.init = efix_init;
	efix.fini = NULL;

	pfix.init = efix_init_pending;
	pfix.fini = NULL;

	suite = ptunit_mk_suite(argc, argv);

	ptu_run(suite, standalone_null);
	ptu_run_f(suite, standalone, efix);

	ptu_run_p(suite, enqueue_null, evb_psbend);
	ptu_run_p(suite, enqueue_null, evb_tip);
	ptu_run_p(suite, enqueue_null, evb_fup);

	ptu_run_p(suite, dequeue_null, evb_psbend);
	ptu_run_p(suite, dequeue_null, evb_tip);
	ptu_run_p(suite, dequeue_null, evb_fup);

	ptu_run_fp(suite, dequeue_empty, efix, evb_psbend);
	ptu_run_fp(suite, dequeue_empty, efix, evb_tip);
	ptu_run_fp(suite, dequeue_empty, efix, evb_fup);

	ptu_run_fp(suite, enqueue_all_dequeue, efix, evb_psbend, 1);
	ptu_run_fp(suite, enqueue_all_dequeue, efix, evb_psbend, 2);
	ptu_run_fp(suite, enqueue_all_dequeue, efix, evb_tip, 1);
	ptu_run_fp(suite, enqueue_all_dequeue, efix, evb_tip, 3);
	ptu_run_fp(suite, enqueue_all_dequeue, efix, evb_fup, 1);
	ptu_run_fp(suite, enqueue_all_dequeue, efix, evb_fup, 4);

	ptu_run_fp(suite, enqueue_one_dequeue, efix, evb_psbend, evb_max * 2);
	ptu_run_fp(suite, enqueue_one_dequeue, efix, evb_tip, evb_max * 2);
	ptu_run_fp(suite, enqueue_one_dequeue, efix, evb_fup, evb_max * 2);

	ptu_run_fp(suite, overflow, efix, evb_psbend, 1);
	ptu_run_fp(suite, overflow, efix, evb_tip, 2);
	ptu_run_fp(suite, overflow, efix, evb_fup, 3);

	ptu_run_p(suite, clear_null, evb_psbend);
	ptu_run_p(suite, clear_null, evb_tip);
	ptu_run_p(suite, clear_null, evb_fup);

	ptu_run_fp(suite, clear, efix, evb_psbend);
	ptu_run_fp(suite, clear, pfix, evb_psbend);
	ptu_run_fp(suite, clear, efix, evb_tip);
	ptu_run_fp(suite, clear, pfix, evb_tip);
	ptu_run_fp(suite, clear, efix, evb_fup);
	ptu_run_fp(suite, clear, pfix, evb_fup);

	ptu_run_p(suite, empty_null, evb_psbend);
	ptu_run_p(suite, empty_null, evb_tip);
	ptu_run_p(suite, empty_null, evb_fup);

	ptu_run_p(suite, pending_null, evb_psbend);
	ptu_run_p(suite, pending_null, evb_tip);
	ptu_run_p(suite, pending_null, evb_fup);

	ptu_run_p(suite, find_null, evb_psbend, ptev_enabled);
	ptu_run_p(suite, find_null, evb_tip, ptev_disabled);
	ptu_run_p(suite, find_null, evb_fup, ptev_paging);

	ptu_run_fp(suite, find_empty, efix, evb_psbend, ptev_enabled);
	ptu_run_fp(suite, find_empty, efix, evb_tip, ptev_disabled);
	ptu_run_fp(suite, find_empty, efix, evb_fup, ptev_paging);

	ptu_run_fp(suite, find_none_evb, efix, evb_psbend, ptev_enabled);
	ptu_run_fp(suite, find_none_evb, efix, evb_tip, ptev_disabled);
	ptu_run_fp(suite, find_none_evb, efix, evb_fup, ptev_paging);

	ptu_run_fp(suite, find_none_evt, efix, evb_psbend, ptev_enabled, 3);
	ptu_run_fp(suite, find_none_evt, efix, evb_tip, ptev_disabled, 4);
	ptu_run_fp(suite, find_none_evt, efix, evb_fup, ptev_paging, 2);

	ptu_run_fp(suite, find, efix, evb_psbend, ptev_enabled, 0, 3);
	ptu_run_fp(suite, find, efix, evb_tip, ptev_disabled, 2, 0);
	ptu_run_fp(suite, find, efix, evb_fup, ptev_paging, 1, 4);

	return ptunit_report(&suite);
}
