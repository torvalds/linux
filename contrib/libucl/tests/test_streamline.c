/* Copyright (c) 2013, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include "ucl.h"

int
main (int argc, char **argv)
{
	ucl_object_t *obj, *cur, *ar;
	FILE *out;
	const char *fname_out = NULL;
	struct ucl_emitter_context *ctx;
	struct ucl_emitter_functions *f;
	int ret = 0;

	switch (argc) {
	case 2:
		fname_out = argv[1];
		break;
	}

	if (fname_out != NULL) {
		out = fopen (fname_out, "w");
		if (out == NULL) {
			exit (-errno);
		}
	}
	else {
		out = stdout;
	}

	obj = ucl_object_typed_new (UCL_OBJECT);

	/* Create some strings */
	cur = ucl_object_fromstring_common ("  test string    ", 0, UCL_STRING_TRIM);
	ucl_object_insert_key (obj, cur, "key1", 0, false);
	cur = ucl_object_fromstring_common ("  test \nstring\n    ", 0, UCL_STRING_TRIM | UCL_STRING_ESCAPE);
	ucl_object_insert_key (obj, cur, "key2", 0, false);
	cur = ucl_object_fromstring_common ("  test string    \n", 0, 0);
	ucl_object_insert_key (obj, cur, "key3", 0, false);

	f = ucl_object_emit_file_funcs (out);
	ctx = ucl_object_emit_streamline_new (obj, UCL_EMIT_CONFIG, f);

	assert (ctx != NULL);

	/* Array of numbers */
	ar = ucl_object_typed_new (UCL_ARRAY);
	ar->key = "key4";
	ar->keylen = sizeof ("key4") - 1;

	ucl_object_emit_streamline_start_container (ctx, ar);
	cur = ucl_object_fromint (10);
	ucl_object_emit_streamline_add_object (ctx, cur);
	cur = ucl_object_fromdouble (10.1);
	ucl_object_emit_streamline_add_object (ctx, cur);
	cur = ucl_object_fromdouble (9.999);
	ucl_object_emit_streamline_add_object (ctx, cur);


	ucl_object_emit_streamline_end_container (ctx);
	ucl_object_emit_streamline_finish (ctx);
	ucl_object_emit_funcs_free (f);
	ucl_object_unref (obj);

	fclose (out);

	return ret;
}
