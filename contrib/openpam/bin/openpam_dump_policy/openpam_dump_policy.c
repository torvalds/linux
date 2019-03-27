/*-
 * Copyright (c) 2011-2014 Dag-Erling Smørgrav
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
 * $OpenPAM: openpam_dump_policy.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <security/pam_appl.h>

#include "openpam_impl.h"
#include "openpam_asprintf.h"

static char *
openpam_chain_name(const char *service, pam_facility_t fclt)
{
	const char *facility = pam_facility_name[fclt];
	char *name;

	if (asprintf(&name, "pam_%s_%s", service, facility) == -1)
		return (NULL);
	return (name);
}

static char *
openpam_facility_index_name(pam_facility_t fclt)
{
	const char *facility = pam_facility_name[fclt];
	char *name, *p;

	if (asprintf(&name, "PAM_%s", facility) == -1)
		return (NULL);
	for (p = name + 4; *p; ++p)
		*p = toupper((unsigned char)*p);
	return (name);
}

int
openpam_dump_chain(const char *name, pam_chain_t *chain)
{
	char *modname, **opt, *p;
	int i;

	for (i = 0; chain != NULL; ++i, chain = chain->next) {
		/* declare the module's struct pam_module */
		modname = strrchr(chain->module->path, '/');
		modname = strdup(modname ? modname : chain->module->path);
		if (modname == NULL)
			return (PAM_BUF_ERR);
		for (p = modname; *p && *p != '.'; ++p)
			/* nothing */ ;
		*p = '\0';
		printf("extern struct pam_module %s_pam_module;\n", modname);
		/* module arguments */
		printf("static char *%s_%d_optv[] = {\n", name, i);
		for (opt = chain->optv; *opt; ++opt) {
			printf("\t\"");
			for (p = *opt; *p; ++p) {
				if (isprint((unsigned char)*p) && *p != '"')
					printf("%c", *p);
				else
					printf("\\x%02x", (unsigned char)*p);
			}
			printf("\",\n");
		}
		printf("\tNULL,\n");
		printf("};\n");
		/* next module in chain */
		if (chain->next != NULL)
			printf("static pam_chain_t %s_%d;\n", name, i + 1);
		/* chain entry */
		printf("static pam_chain_t %s_%d = {\n", name, i);
		printf("\t.module = &%s_pam_module,\n", modname);
		printf("\t.flag = 0x%08x,\n", chain->flag);
		printf("\t.optc = %d,\n", chain->optc);
		printf("\t.optv = %s_%d_optv,\n", name, i);
		if (chain->next)
			printf("\t.next = &%s_%d,\n", name, i + 1);
		else
			printf("\t.next = NULL,\n");
		printf("};\n");
		free(modname);
	}
	return (PAM_SUCCESS);
}

int
openpam_dump_policy(const char *service)
{
	pam_handle_t *pamh;
	char *name;
	int fclt, ret;

	if ((pamh = calloc(1, sizeof *pamh)) == NULL)
		return (PAM_BUF_ERR);
	if ((ret = openpam_configure(pamh, service)) != PAM_SUCCESS)
		return (ret);
	for (fclt = 0; fclt < PAM_NUM_FACILITIES; ++fclt) {
		if (pamh->chains[fclt] != NULL) {
			if ((name = openpam_chain_name(service, fclt)) == NULL)
				return (PAM_BUF_ERR);
			ret = openpam_dump_chain(name, pamh->chains[fclt]);
			free(name);
			if (ret != PAM_SUCCESS)
				return (ret);
		}
	}
	printf("static pam_policy_t pam_%s_policy = {\n", service);
	printf("\t.service = \"%s\",\n", service);
	printf("\t.chains = {\n");
	for (fclt = 0; fclt < PAM_NUM_FACILITIES; ++fclt) {
		if ((name = openpam_facility_index_name(fclt)) == NULL)
			return (PAM_BUF_ERR);
		printf("\t\t[%s] = ", name);
		free(name);
		if (pamh->chains[fclt] != NULL) {
			if ((name = openpam_chain_name(service, fclt)) == NULL)
				return (PAM_BUF_ERR);
			printf("&%s_0,\n", name);
			free(name);
		} else {
			printf("NULL,\n");
		}
	}
	printf("\t},\n");
	printf("};\n");
	free(pamh);
	return (PAM_SUCCESS);
}

static void
usage(void)
{

	fprintf(stderr, "usage: openpam_dump_policy [-d] policy ...\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	int i, opt;

	while ((opt = getopt(argc, argv, "d")) != -1)
		switch (opt) {
		case 'd':
			openpam_debug = 1;
			break;
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc < 1)
		usage();

	printf("#include <security/pam_appl.h>\n");
	printf("#include \"openpam_impl.h\"\n");
	for (i = 0; i < argc; ++i)
		openpam_dump_policy(argv[i]);
	printf("pam_policy_t *pam_embedded_policies[] = {\n");
	for (i = 0; i < argc; ++i)
		printf("\t&pam_%s_policy,\n", argv[i]);
	printf("\tNULL,\n");
	printf("};\n");
	exit(0);
}
