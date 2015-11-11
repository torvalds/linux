#define TCM_LOOP_VERSION		"v2.1-rc2"
#define TL_WWN_ADDR_LEN			256
#define TL_TPGS_PER_HBA			32

struct tcm_loop_cmd {
	/* State of Linux/SCSI CDB+Data descriptor */
	u32 sc_cmd_state;
	/* Tagged command queueing */
	u32 sc_cmd_tag;
	/* Pointer to the CDB+Data descriptor from Linux/SCSI subsystem */
	struct scsi_cmnd *sc;
	/* The TCM I/O descriptor that is accessed via container_of() */
	struct se_cmd tl_se_cmd;
	struct work_struct work;
	/* Sense buffer that will be mapped into outgoing status */
	unsigned char tl_sense_buf[TRANSPORT_SENSE_BUFFER];
};

struct tcm_loop_tmr {
	atomic_t tmr_complete;
	wait_queue_head_t tl_tmr_wait;
};

struct tcm_loop_nexus {
	/*
	 * Pointer to TCM session for I_T Nexus
	 */
	struct se_session *se_sess;
};

#define TCM_TRANSPORT_ONLINE 0
#define TCM_TRANSPORT_OFFLINE 1

struct tcm_loop_tpg {
	unsigned short tl_tpgt;
	unsigned short tl_transport_status;
	enum target_prot_type tl_fabric_prot_type;
	atomic_t tl_tpg_port_count;
	struct se_portal_group tl_se_tpg;
	struct tcm_loop_hba *tl_hba;
	struct tcm_loop_nexus *tl_nexus;
};

struct tcm_loop_hba {
	u8 tl_proto_id;
	unsigned char tl_wwn_address[TL_WWN_ADDR_LEN];
	struct se_hba_s *se_hba;
	struct se_lun *tl_hba_lun;
	struct se_port *tl_hba_lun_sep;
	struct device dev;
	struct Scsi_Host *sh;
	struct tcm_loop_tpg tl_hba_tpgs[TL_TPGS_PER_HBA];
	struct se_wwn tl_hba_wwn;
};
