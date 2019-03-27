/*-
 * Copyright (c) 2012-2015 Dag-Erling Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $OpenPAM: openpam_features.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <security/pam_appl.h>

#include "openpam_impl.h"

#define STRUCT_OPENPAM_FEATURE(name, descr, dflt)	\
	[OPENPAM_##name] = {				\
		"OPENPAM_" #name,			\
		descr,					\
		dflt					\
	}

struct openpam_feature openpam_features[OPENPAM_NUM_FEATURES] = {
	STRUCT_OPENPAM_FEATURE(
	    RESTRICT_SERVICE_NAME,
	    "Disallow path separators in service names",
	    1
	),
	STRUCT_OPENPAM_FEATURE(
	    VERIFY_POLICY_FILE,
	    "Verify ownership and permissions of policy files",
	    1
	),
	STRUCT_OPENPAM_FEATURE(
	    RESTRICT_MODULE_NAME,
	    "Disallow path separators in module names",
	    0
	),
	STRUCT_OPENPAM_FEATURE(
	    VERIFY_MODULE_FILE,
	    "Verify ownership and permissions of module files",
	    1
	),
	STRUCT_OPENPAM_FEATURE(
	    FALLBACK_TO_OTHER,
	    "Fall back to \"other\" policy for empty chains",
	    1
	),
};
