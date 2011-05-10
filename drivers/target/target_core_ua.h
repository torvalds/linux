#ifndef TARGET_CORE_UA_H

/*
 * From spc4r17, Table D.1: ASC and ASCQ Assignement
 */
#define ASCQ_29H_POWER_ON_RESET_OR_BUS_DEVICE_RESET_OCCURED	0x00
#define ASCQ_29H_POWER_ON_OCCURRED				0x01
#define ASCQ_29H_SCSI_BUS_RESET_OCCURED				0x02
#define ASCQ_29H_BUS_DEVICE_RESET_FUNCTION_OCCURRED		0x03
#define ASCQ_29H_DEVICE_INTERNAL_RESET				0x04
#define ASCQ_29H_TRANSCEIVER_MODE_CHANGED_TO_SINGLE_ENDED	0x05
#define ASCQ_29H_TRANSCEIVER_MODE_CHANGED_TO_LVD		0x06
#define ASCQ_29H_NEXUS_LOSS_OCCURRED				0x07

#define ASCQ_2AH_PARAMETERS_CHANGED				0x00
#define ASCQ_2AH_MODE_PARAMETERS_CHANGED			0x01
#define ASCQ_2AH_LOG_PARAMETERS_CHANGED				0x02
#define ASCQ_2AH_RESERVATIONS_PREEMPTED				0x03
#define ASCQ_2AH_RESERVATIONS_RELEASED				0x04
#define ASCQ_2AH_REGISTRATIONS_PREEMPTED			0x05
#define ASCQ_2AH_ASYMMETRIC_ACCESS_STATE_CHANGED		0x06
#define ASCQ_2AH_IMPLICT_ASYMMETRIC_ACCESS_STATE_TRANSITION_FAILED 0x07
#define ASCQ_2AH_PRIORITY_CHANGED				0x08

#define ASCQ_2CH_PREVIOUS_RESERVATION_CONFLICT_STATUS		0x09

extern struct kmem_cache *se_ua_cache;

extern int core_scsi3_ua_check(struct se_cmd *, unsigned char *);
extern int core_scsi3_ua_allocate(struct se_node_acl *, u32, u8, u8);
extern void core_scsi3_ua_release_all(struct se_dev_entry *);
extern void core_scsi3_ua_for_check_condition(struct se_cmd *, u8 *, u8 *);
extern int core_scsi3_ua_clear_for_request_sense(struct se_cmd *,
						u8 *, u8 *);

#endif /* TARGET_CORE_UA_H */
