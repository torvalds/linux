#include <target/target_core_base.h>
#include <linux/btree.h>

/* length of ASCII WWPNs including pad */
#define TCM_QLA2XXX_NAMELEN	32
/*
 * Number of pre-allocated per-session tags, based upon the worst-case
 * per port number of iocbs
 */
#define TCM_QLA2XXX_DEFAULT_TAGS 2088

#include "qla_target.h"

struct tcm_qla2xxx_nacl {
	struct se_node_acl se_node_acl;

	/* From libfc struct fc_rport->port_id */
	u32 nport_id;
	/* Binary World Wide unique Node Name for remote FC Initiator Nport */
	u64 nport_wwnn;
	/* ASCII formatted WWPN for FC Initiator Nport */
	char nport_name[TCM_QLA2XXX_NAMELEN];
	/* Pointer to qla_tgt_sess */
	struct qla_tgt_sess *qla_tgt_sess;
	/* Pointer to TCM FC nexus */
	struct se_session *nport_nexus;
};

struct tcm_qla2xxx_tpg_attrib {
	int generate_node_acls;
	int cache_dynamic_acls;
	int demo_mode_write_protect;
	int prod_mode_write_protect;
	int demo_mode_login_only;
	int fabric_prot_type;
	int jam_host;
};

struct tcm_qla2xxx_tpg {
	/* FC lport target portal group tag for TCM */
	u16 lport_tpgt;
	/* Atomic bit to determine TPG active status */
	atomic_t lport_tpg_enabled;
	/* Pointer back to tcm_qla2xxx_lport */
	struct tcm_qla2xxx_lport *lport;
	/* Used by tcm_qla2xxx_tpg_attrib_cit */
	struct tcm_qla2xxx_tpg_attrib tpg_attrib;
	/* Returned by tcm_qla2xxx_make_tpg() */
	struct se_portal_group se_tpg;
	/* Items for dealing with configfs_depend_item */
	struct completion tpg_base_comp;
	struct work_struct tpg_base_work;
};

struct tcm_qla2xxx_fc_loopid {
	struct se_node_acl *se_nacl;
};

struct tcm_qla2xxx_lport {
	/* Binary World Wide unique Port Name for FC Target Lport */
	u64 lport_wwpn;
	/* Binary World Wide unique Port Name for FC NPIV Target Lport */
	u64 lport_npiv_wwpn;
	/* Binary World Wide unique Node Name for FC NPIV Target Lport */
	u64 lport_npiv_wwnn;
	/* ASCII formatted WWPN for FC Target Lport */
	char lport_name[TCM_QLA2XXX_NAMELEN];
	/* ASCII formatted naa WWPN for VPD page 83 etc */
	char lport_naa_name[TCM_QLA2XXX_NAMELEN];
	/* map for fc_port pointers in 24-bit FC Port ID space */
	struct btree_head32 lport_fcport_map;
	/* vmalloc-ed memory for fc_port pointers for 16-bit FC loop ID */
	struct tcm_qla2xxx_fc_loopid *lport_loopid_map;
	/* Pointer to struct scsi_qla_host from qla2xxx LLD */
	struct scsi_qla_host *qla_vha;
	/* Pointer to struct qla_tgt pointer */
	struct qla_tgt lport_qla_tgt;
	/* Pointer to TPG=1 for non NPIV mode */
	struct tcm_qla2xxx_tpg *tpg_1;
	/* Returned by tcm_qla2xxx_make_lport() */
	struct se_wwn lport_wwn;
};
