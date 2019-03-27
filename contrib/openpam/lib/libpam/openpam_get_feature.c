/*-
 * Copyright (c) 2012-2017 Dag-Erling Smørgrav
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
 * $OpenPAM: openpam_get_feature.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <security/pam_appl.h>
#include <security/openpam.h>

#include "openpam_impl.h"

/*
 * OpenPAM extension
 *
 * Query the state of an optional feature.
 */

int
openpam_get_feature(int feature, int *onoff)
{

	ENTERF(feature);
	if (feature < 0 || feature >= OPENPAM_NUM_FEATURES)
		RETURNC(PAM_BAD_FEATURE);
	*onoff = openpam_features[feature].onoff;
	RETURNC(PAM_SUCCESS);
}

/*
 * Error codes:
 *
 *	PAM_BAD_FEATURE
 */

/**
 * EXPERIMENTAL
 *
 * The =openpam_get_feature function stores the current state of the
 * specified feature in the variable pointed to by its =onoff argument.
 *
 * The following features are recognized:
 *
 *	=OPENPAM_RESTRICT_SERVICE_NAME:
 *		Disallow path separators in service names.
 *		This feature is enabled by default.
 *		Disabling it allows the application to specify the path to
 *		the desired policy file directly.
 *
 *	=OPENPAM_VERIFY_POLICY_FILE:
 *		Verify the ownership and permissions of the policy file
 *		and the path leading up to it.
 *		This feature is enabled by default.
 *
 *	=OPENPAM_RESTRICT_MODULE_NAME:
 *		Disallow path separators in module names.
 *		This feature is disabled by default.
 *		Enabling it prevents the use of modules in non-standard
 *		locations.
 *
 *	=OPENPAM_VERIFY_MODULE_FILE:
 *		Verify the ownership and permissions of each loadable
 *		module and the path leading up to it.
 *		This feature is enabled by default.
 *
 *
 * >openpam_set_feature
 *
 * AUTHOR DES
 */
