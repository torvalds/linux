/*-
 * Copyright (c) 2004-2008 Apple Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _BSM_AUDIT_UEVENTS_H_
#define	_BSM_AUDIT_UEVENTS_H_

/*
 * Solaris userspace events.
 */
#define	AUE_at_create		6144
#define	AUE_at_delete		6145
#define	AUE_at_perm		6146
#define	AUE_cron_invoke		6147
#define	AUE_crontab_create	6148
#define	AUE_crontab_delete	6149
#define	AUE_crontab_perm	6150
#define	AUE_inetd_connect	6151
#define	AUE_login		6152
#define	AUE_logout		6153
#define	AUE_telnet		6154
#define	AUE_rlogin		6155
#define	AUE_mountd_mount	6156
#define	AUE_mountd_umount	6157
#define	AUE_rshd		6158
#define	AUE_su			6159
#define	AUE_halt		6160
#define	AUE_reboot		6161
#define	AUE_rexecd		6162
#define	AUE_passwd		6163
#define	AUE_rexd		6164
#define	AUE_ftpd		6165
#define	AUE_init		6166
#define	AUE_uadmin		6167
#define	AUE_shutdown		6168
#define	AUE_poweroff		6169
#define	AUE_crontab_mod		6170
#define	AUE_ftpd_logout		6171
#define	AUE_ssh			6172
#define	AUE_role_login		6173
#define	AUE_prof_cmd		6180
#define	AUE_filesystem_add	6181
#define	AUE_filesystem_delete	6182
#define	AUE_filesystem_modify	6183
#define	AUE_allocate_succ	6200
#define	AUE_allocate_fail	6201
#define	AUE_deallocate_succ	6202
#define	AUE_deallocate_fail	6203
#define	AUE_listdevice_succ	6205
#define	AUE_listdevice_fail	6206
#define	AUE_create_user		6207
#define	AUE_modify_user		6208
#define	AUE_delete_user		6209
#define	AUE_disable_user	6210
#define	AUE_enable_user		6211
#define	AUE_newgrp_login	6212
#define	AUE_admin_authentication	6213
#define	AUE_kadmind_auth	6214
#define	AUE_kadmind_unauth	6215
#define	AUE_krb5kdc_as_req	6216
#define	AUE_krb5kdc_tgs_req	6217
#define	AUE_krb5kdc_tgs_req_2ndtktmm	6218
#define	AUE_krb5kdc_tgs_req_alt_tgt	6219

/*
 * Historic Darwin use of the low event numbering space, which collided with
 * the Solaris event space.  Now obsoleted and new, higher, event numbers
 * assigned to make it easier to interpret Solaris events using the OpenBSM
 * tools.
 */
#define	AUE_DARWIN_audit_startup	6171
#define	AUE_DARWIN_audit_shutdown	6172
#define	AUE_DARWIN_sudo			6300
#define	AUE_DARWIN_modify_password	6501
#define	AUE_DARWIN_create_group		6511
#define	AUE_DARWIN_delete_group		6512
#define	AUE_DARWIN_modify_group		6513
#define	AUE_DARWIN_add_to_group		6514
#define	AUE_DARWIN_remove_from_group	6515
#define	AUE_DARWIN_revoke_obj		6521
#define	AUE_DARWIN_lw_login		6600
#define	AUE_DARWIN_lw_logout		6601
#define	AUE_DARWIN_auth_user		7000
#define	AUE_DARWIN_ssconn		7001
#define	AUE_DARWIN_ssauthorize		7002
#define	AUE_DARWIN_ssauthint		7003

/*
 * Historic/third-party appliation allocations of event idenfiers.
 */
#define	AUE_openssh		32800

/*
 * OpenBSM-managed application event space.
 */
#define	AUE_audit_startup	45000		/* Darwin-specific. */
#define	AUE_audit_shutdown	45001		/* Darwin-specific. */
#define	AUE_modify_password	45014		/* Darwin-specific. */
#define	AUE_create_group	45015		/* Darwin-specific. */
#define	AUE_delete_group	45016		/* Darwin-specific. */
#define	AUE_modify_group	45017		/* Darwin-specific. */
#define	AUE_add_to_group	45018		/* Darwin-specific. */
#define	AUE_remove_from_group	45019		/* Darwin-specific. */
#define	AUE_revoke_obj		45020		/* Darwin-specific. */
#define	AUE_lw_login		45021		/* Darwin-specific. */
#define	AUE_lw_logout		45022		/* Darwin-specific. */
#define	AUE_auth_user		45023		/* Darwin-specific. */
#define	AUE_ssconn		45024		/* Darwin-specific. */
#define	AUE_ssauthorize		45025		/* Darwin-specific. */
#define	AUE_ssauthint		45026		/* Darwin-specific. */
#define	AUE_calife		45027		/* OpenBSM-allocated. */
#define	AUE_sudo		45028		/* OpenBSM-allocated. */
#define	AUE_audit_recovery	45029		/* OpenBSM-allocated. */
#define	AUE_ssauthmech		45030		/* Darwin-specific. */

#endif /* !_BSM_AUDIT_UEVENTS_H_ */
