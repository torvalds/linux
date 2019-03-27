/*-
 * Copyright (c) 2006 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
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
 */

#define	AUDITFILTERD_CONFFILE	"/etc/security/audit_filter"
#define	AUDITFILTERD_PIPEFILE	"/dev/auditpipe"

/*
 * Limit on the number of arguments that can appear in an audit_filterd
 * configuration line.
 */
#define	AUDITFILTERD_CONF_MAXARGS	256

/*
 * Data structure description each instantiated module.
 */
struct auditfilter_module {
	/*
	 * Fields from configuration file and dynamic linker.
	 */
	char						*am_modulename;
	char						*am_arg_buffer;
	int						 am_argc;
	char						**am_argv;
	void						*am_dlhandle;

	/*
	 * Fields provided by or extracted from the module.
	 */
	void						*am_cookie;
	audit_filter_attach_t				 am_attach;
	audit_filter_reinit_t				 am_reinit;
	audit_filter_record_t				 am_record;
	audit_filter_rawrecord_t			 am_rawrecord;
	audit_filter_detach_t				 am_detach;

	/*
	 * Fields for maintaining the list of modules.
	 */
	TAILQ_ENTRY(auditfilter_module)			 am_list;
};
TAILQ_HEAD(auditfilter_module_list, auditfilter_module);

/*
 * List of currently registered modules.
 */
extern struct auditfilter_module_list	filter_list;

/*
 * Function definitions.
 */
int	auditfilterd_conf(const char *filename, FILE *fp);
void	auditfilterd_conf_shutdown(void);
