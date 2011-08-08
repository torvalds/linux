#ifndef TARGET_CORE_PR_H
#define TARGET_CORE_PR_H
/*
 * PERSISTENT_RESERVE_OUT service action codes
 *
 * spc4r17 section 6.14.2 Table 171
 */
#define PRO_REGISTER				0x00
#define PRO_RESERVE				0x01
#define PRO_RELEASE				0x02
#define PRO_CLEAR				0x03
#define PRO_PREEMPT				0x04
#define PRO_PREEMPT_AND_ABORT			0x05
#define PRO_REGISTER_AND_IGNORE_EXISTING_KEY	0x06
#define PRO_REGISTER_AND_MOVE			0x07
/*
 * PERSISTENT_RESERVE_IN service action codes
 *
 * spc4r17 section 6.13.1 Table 159
 */
#define PRI_READ_KEYS				0x00
#define PRI_READ_RESERVATION			0x01
#define PRI_REPORT_CAPABILITIES			0x02
#define PRI_READ_FULL_STATUS			0x03
/*
 * PERSISTENT_RESERVE_ SCOPE field
 *
 * spc4r17 section 6.13.3.3 Table 163
 */
#define PR_SCOPE_LU_SCOPE			0x00
/*
 * PERSISTENT_RESERVE_* TYPE field
 *
 * spc4r17 section 6.13.3.4 Table 164
 */
#define PR_TYPE_WRITE_EXCLUSIVE			0x01
#define PR_TYPE_EXCLUSIVE_ACCESS		0x03
#define PR_TYPE_WRITE_EXCLUSIVE_REGONLY		0x05
#define PR_TYPE_EXCLUSIVE_ACCESS_REGONLY	0x06
#define PR_TYPE_WRITE_EXCLUSIVE_ALLREG		0x07
#define PR_TYPE_EXCLUSIVE_ACCESS_ALLREG		0x08

#define PR_APTPL_MAX_IPORT_LEN			256
#define PR_APTPL_MAX_TPORT_LEN			256

extern struct kmem_cache *t10_pr_reg_cache;

extern int core_pr_dump_initiator_port(struct t10_pr_registration *,
			char *, u32);
extern int core_scsi2_emulate_crh(struct se_cmd *);
extern int core_scsi3_alloc_aptpl_registration(
			struct t10_reservation *, u64,
			unsigned char *, unsigned char *, u32,
			unsigned char *, u16, u32, int, int, u8);
extern int core_scsi3_check_aptpl_registration(struct se_device *,
			struct se_portal_group *, struct se_lun *,
			struct se_lun_acl *);
extern void core_scsi3_free_pr_reg_from_nacl(struct se_device *,
					     struct se_node_acl *);
extern void core_scsi3_free_all_registrations(struct se_device *);
extern unsigned char *core_scsi3_pr_dump_type(int);
extern int core_scsi3_check_cdb_abort_and_preempt(struct list_head *,
						  struct se_cmd *);
extern int core_scsi3_emulate_pr(struct se_cmd *);
extern int core_setup_reservations(struct se_device *, int);

#endif /* TARGET_CORE_PR_H */
