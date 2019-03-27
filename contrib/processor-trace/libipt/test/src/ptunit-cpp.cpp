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

#include "ptunit.h"

#include "intel-pt.h"


static struct ptunit_result init_packet_decoder(void)
{
	uint8_t buf[1];
	struct pt_config config;
	struct pt_packet_decoder *decoder;

	pt_config_init(&config);
	config.begin = buf;
	config.end = buf + sizeof(buf);

	decoder = pt_pkt_alloc_decoder(&config);
	ptu_ptr(decoder);
	pt_pkt_free_decoder(decoder);

	return ptu_passed();
}

static struct ptunit_result init_query_decoder(void)
{
	uint8_t buf[1];
	struct pt_config config;
	struct pt_query_decoder *query_decoder;

	pt_config_init(&config);
	config.begin = buf;
	config.end = buf + sizeof(buf);

	query_decoder = pt_qry_alloc_decoder(&config);
	ptu_ptr(query_decoder);
	pt_qry_free_decoder(query_decoder);

	return ptu_passed();
}

int main(int argc, char **argv)
{
	struct ptunit_suite suite;

	suite = ptunit_mk_suite(argc, argv);

	ptu_run(suite, init_packet_decoder);
	ptu_run(suite, init_query_decoder);

	return ptunit_report(&suite);
}
