/*-
 * Copyright (c) 2011-2017 Dag-Erling Smørgrav
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
 * $OpenPAM: openpam_constants.h 938 2017-04-30 21:34:42Z des $
 */

#ifndef OPENPAM_CONSTANTS_H_INCLUDED
#define OPENPAM_CONSTANTS_H_INCLUDED

extern const char *pam_err_name[PAM_NUM_ERRORS];
extern const char *pam_err_text[PAM_NUM_ERRORS];
extern const char *pam_item_name[PAM_NUM_ITEMS];
extern const char *pam_facility_name[PAM_NUM_FACILITIES];
extern const char *pam_control_flag_name[PAM_NUM_CONTROL_FLAGS];
extern const char *pam_func_name[PAM_NUM_PRIMITIVES];
extern const char *pam_sm_func_name[PAM_NUM_PRIMITIVES];

extern const char *openpam_policy_path[];
extern const char *openpam_module_path[];

#endif
