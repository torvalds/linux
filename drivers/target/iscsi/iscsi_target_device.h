/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ISCSI_TARGET_DEVICE_H
#define ISCSI_TARGET_DEVICE_H

struct iscsit_cmd;
struct iscsit_session;

extern void iscsit_determine_maxcmdsn(struct iscsit_session *);
extern void iscsit_increment_maxcmdsn(struct iscsit_cmd *, struct iscsit_session *);

#endif /* ISCSI_TARGET_DEVICE_H */
