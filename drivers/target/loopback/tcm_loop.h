#define TCM_LOOP_VERSION		"v2.1-rc1"
#define TL_WWN_ADDR_LEN			256
#define TL_TPGS_PER_HBA			32
/*
 * Defaults for struct scsi_host_template tcm_loop_driver_template
 *
 * We use large can_queue and cmd_per_lun here and let TCM enforce
 * the underlying se_device_t->queue_depth.
 */
#define TL_SCSI_CAN_QUEUE		1024
#define TL_SCSI_CMD_PER_LUN		1024
#define TL_SCSI_MAX_SECTORS		1024
#define TL_SCSI_SG_TABLESIZE		256
/*
 * Used in tcm_loop_driver_probe() for struct Scsi_Host->max_cmd_len
 */
#define TL_SCSI_MAX_CMD_LEN		32

struct tcm_loop_cmd {
	/* State of Linux/SCSI CDB+Data descriptor */
	u32 sc_cmd_state;
	/* Pointer to the CDB+Data descriptor from Linux/SCSI subsystem */
	struct scsi_cmnd *sc;
	struct list_head *tl_cmd_list;
	/* The TCM I/O descriptor that is accessed via container_of() */
	struct se_cmd tl_se_cmd;
	/* Sense buffer that will be mapped into outgoing status */
	unsigned char tl_sense_buf[TRANSPORT_SENSE_BUFFER];
};

struct tcm_loop_tmr {
	atomic_t tmr_complete;
	wait_queue_head_t tl_tmr_wait;
};

struct tcm_loop_nexus {
	int it_nexus_active;
	/*
	 * Pointer to Linux/SCSI HBA from linux/include/scsi_host.h
	 */
	struct scsi_host *sh;
	/*
	 * Pointer to TCM session for I_T Nexus
	 */
	struct se_session *se_sess;
};

struct tcm_loop_nacl {
	struct se_node_acl se_node_acl;
};

struct tcm_loop_tpg {
	unsigned short tl_tpgt;
	atomic_t tl_tpg_port_count;
	struct se_portal_group tl_se_tpg;
	struct tcm_loop_hba *tl_hba;
};

struct tcm_loop_hba {
	u8 tl_proto_id;
	unsigned char tl_wwn_address[TL_WWN_ADDR_LEN];
	struct se_hba_s *se_hba;
	struct se_lun *tl_hba_lun;
	struct se_port *tl_hba_lun_sep;
	struct se_device_s *se_dev_hba_ptr;
	struct tcm_loop_nexus *tl_nexus;
	struct device dev;
	struct Scsi_Host *sh;
	struct tcm_loop_tpg tl_hba_tpgs[TL_TPGS_PER_HBA];
	struct se_wwn tl_hba_wwn;
};
