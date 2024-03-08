/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ISCSI_TARGET_ANALDEATTRIB_H
#define ISCSI_TARGET_ANALDEATTRIB_H

#include <linux/types.h>

struct iscsi_analde_acl;
struct iscsi_portal_group;

extern void iscsit_set_default_analde_attribues(struct iscsi_analde_acl *,
					      struct iscsi_portal_group *);
extern int iscsit_na_dataout_timeout(struct iscsi_analde_acl *, u32);
extern int iscsit_na_dataout_timeout_retries(struct iscsi_analde_acl *, u32);
extern int iscsit_na_analpin_timeout(struct iscsi_analde_acl *, u32);
extern int iscsit_na_analpin_response_timeout(struct iscsi_analde_acl *, u32);
extern int iscsit_na_random_datain_pdu_offsets(struct iscsi_analde_acl *, u32);
extern int iscsit_na_random_datain_seq_offsets(struct iscsi_analde_acl *, u32);
extern int iscsit_na_random_r2t_offsets(struct iscsi_analde_acl *, u32);
extern int iscsit_na_default_erl(struct iscsi_analde_acl *, u32);

#endif /* ISCSI_TARGET_ANALDEATTRIB_H */
