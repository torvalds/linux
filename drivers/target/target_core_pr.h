/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TARGET_CORE_PR_H
#define TARGET_CORE_PR_H

#include <linux/types.h>
#include <target/target_core_base.h>

/*
 * PERSISTENT_RESERVE_OUT service action codes
 *
 * spc5r04b section 6.15.2 Table 174
 */
#define PRO_REGISTER				0x00
#define PRO_RESERVE				0x01
#define PRO_RELEASE				0x02
#define PRO_CLEAR				0x03
#define PRO_PREEMPT				0x04
#define PRO_PREEMPT_AND_ABORT			0x05
#define PRO_REGISTER_AND_IGNORE_EXISTING_KEY	0x06
#define PRO_REGISTER_AND_MOVE			0x07
#define PRO_REPLACE_LOST_RESERVATION	0x08
/*
 * PERSISTENT_RESERVE_IN service action codes
 *
 * spc5r04b section 6.14.1 Table 162
 */
#define PRI_READ_KEYS				0x00
#define PRI_READ_RESERVATION			0x01
#define PRI_REPORT_CAPABILITIES			0x02
#define PRI_READ_FULL_STATUS			0x03
/*
 * PERSISTENT_RESERVE_ SCOPE field
 *
 * spc5r04b section 6.14.3.2 Table 166
 */
#define PR_SCOPE_LU_SCOPE			0x00
/*
 * PERSISTENT_RESERVE_* TYPE field
 *
 * spc5r04b section 6.14.3.3 Table 167
 */
#define PR_TYPE_WRITE_EXCLUSIVE			0x01
#define PR_TYPE_EXCLUSIVE_ACCESS		0x03
#define PR_TYPE_WRITE_EXCLUSIVE_REGONLY		0x05
#define PR_TYPE_EXCLUSIVE_ACCESS_REGONLY	0x06
#define PR_TYPE_WRITE_EXCLUSIVE_ALLREG		0x07
#define PR_TYPE_EXCLUSIVE_ACCESS_ALLREG		0x08

#define PR_APTPL_MAX_IPORT_LEN			256
#define PR_APTPL_MAX_TPORT_LEN			256

/*
 *  Function defined in target_core_spc.c
 */
void spc_gen_naa_6h_vendor_specific(struct se_device *, unsigned char *);

extern struct kmem_cache *t10_pr_reg_cache;

extern void core_pr_dump_initiator_port(struct t10_pr_registration *,
			char *, u32);
extern void target_release_reservation(struct se_device *dev);
extern sense_reason_t target_scsi2_reservation_release(struct se_cmd *);
extern sense_reason_t target_scsi2_reservation_reserve(struct se_cmd *);
extern int core_scsi3_alloc_aptpl_registration(
			struct t10_reservation *, u64,
			unsigned char *, unsigned char *, u64,
			unsigned char *, u16, u64, int, int, u8);
extern int core_scsi3_check_aptpl_registration(struct se_device *,
			struct se_portal_group *, struct se_lun *,
			struct se_node_acl *, u64);
extern void core_scsi3_free_pr_reg_from_nacl(struct se_device *,
					     struct se_node_acl *);
extern void core_scsi3_free_all_registrations(struct se_device *);
extern unsigned char *core_scsi3_pr_dump_type(int);

extern sense_reason_t target_scsi3_emulate_pr_in(struct se_cmd *);
extern sense_reason_t target_scsi3_emulate_pr_out(struct se_cmd *);
extern sense_reason_t target_check_reservation(struct se_cmd *);

#endif /* TARGET_CORE_PR_H */
