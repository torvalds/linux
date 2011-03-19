#ifndef TARGET_CORE_MIB_H
#define TARGET_CORE_MIB_H

typedef enum {
	SCSI_INST_INDEX,
	SCSI_DEVICE_INDEX,
	SCSI_AUTH_INTR_INDEX,
	SCSI_INDEX_TYPE_MAX
} scsi_index_t;

struct scsi_index_table {
	spinlock_t	lock;
	u32 		scsi_mib_index[SCSI_INDEX_TYPE_MAX];
} ____cacheline_aligned;

/* SCSI Port stats */
struct scsi_port_stats {
	u64	cmd_pdus;
	u64	tx_data_octets;
	u64	rx_data_octets;
} ____cacheline_aligned;

extern int init_scsi_target_mib(void);
extern void remove_scsi_target_mib(void);
extern void init_scsi_index_table(void);
extern u32 scsi_get_new_index(scsi_index_t);

#endif   /*** TARGET_CORE_MIB_H ***/
