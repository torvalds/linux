/*-
 * Copyright (c) 2001-2003 Networks Associates Technology, Inc.
 * Copyright (c) 2004-2017 Dag-Erling Smørgrav
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by ThinkSec AS and
 * Network Associates Laboratories, the Security Research Division of
 * Network Associates, Inc.  under DARPA/SPAWAR contract N66001-01-C-8035
 * ("CBOSS"), as part of the DARPA CHATS research program.
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
 * $OpenPAM: openpam_impl.h 938 2017-04-30 21:34:42Z des $
 */

#ifndef OPENPAM_IMPL_H_INCLUDED
#define OPENPAM_IMPL_H_INCLUDED

#include <security/openpam.h>

extern int openpam_debug;

/*
 * Control flags
 */
typedef enum {
	PAM_BINDING,
	PAM_REQUIRED,
	PAM_REQUISITE,
	PAM_SUFFICIENT,
	PAM_OPTIONAL,
	PAM_NUM_CONTROL_FLAGS
} pam_control_t;

/*
 * Facilities
 */
typedef enum {
	PAM_FACILITY_ANY = -1,
	PAM_AUTH = 0,
	PAM_ACCOUNT,
	PAM_SESSION,
	PAM_PASSWORD,
	PAM_NUM_FACILITIES
} pam_facility_t;

/*
 * Module chains
 */
typedef struct pam_chain pam_chain_t;
struct pam_chain {
	pam_module_t	*module;
	int		 flag;
	int		 optc;
	char	       **optv;
	pam_chain_t	*next;
};

/*
 * Service policies
 */
#if defined(OPENPAM_EMBEDDED)
typedef struct pam_policy pam_policy_t;
struct pam_policy {
	const char	*service;
	pam_chain_t	*chains[PAM_NUM_FACILITIES];
};
extern pam_policy_t *pam_embedded_policies[];
#endif

/*
 * Module-specific data
 */
typedef struct pam_data pam_data_t;
struct pam_data {
	char		*name;
	void		*data;
	void		(*cleanup)(pam_handle_t *, void *, int);
	pam_data_t	*next;
};

/*
 * PAM context
 */
struct pam_handle {
	char		*service;

	/* chains */
	pam_chain_t	*chains[PAM_NUM_FACILITIES];
	pam_chain_t	*current;
	int		 primitive;

	/* items and data */
	void		*item[PAM_NUM_ITEMS];
	pam_data_t	*module_data;

	/* environment list */
	char	       **env;
	int		 env_count;
	int		 env_size;
};

/*
 * Default policy
 */
#define PAM_OTHER	"other"

/*
 * Internal functions
 */
int		 openpam_configure(pam_handle_t *, const char *)
	OPENPAM_NONNULL((1));
int		 openpam_dispatch(pam_handle_t *, int, int)
	OPENPAM_NONNULL((1));
int		 openpam_findenv(pam_handle_t *, const char *, size_t)
	OPENPAM_NONNULL((1,2));
pam_module_t	*openpam_load_module(const char *)
	OPENPAM_NONNULL((1));
void		 openpam_clear_chains(pam_chain_t **)
	OPENPAM_NONNULL((1));

int		 openpam_check_desc_owner_perms(const char *, int)
	OPENPAM_NONNULL((1));
int		 openpam_check_path_owner_perms(const char *)
	OPENPAM_NONNULL((1));

#ifdef OPENPAM_STATIC_MODULES
pam_module_t	*openpam_static(const char *)
	OPENPAM_NONNULL((1));
#endif
pam_module_t	*openpam_dynamic(const char *)
	OPENPAM_NONNULL((1));

#define	FREE(p)					\
	do {					\
		free(p);			\
		(p) = NULL;			\
	} while (0)

#define FREEV(c, v)				\
	do {					\
		if ((v) != NULL) {		\
			while ((c)-- > 0)	\
				FREE((v)[(c)]);	\
			FREE(v);		\
		}				\
	} while (0)

#include "openpam_constants.h"
#include "openpam_debug.h"
#include "openpam_features.h"

#endif
