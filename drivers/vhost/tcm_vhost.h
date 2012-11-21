#define TCM_VHOST_VERSION  "v0.1"
#define TCM_VHOST_NAMELEN 256
#define TCM_VHOST_MAX_CDB_SIZE 32

struct tcm_vhost_cmd {
	/* Descriptor from vhost_get_vq_desc() for virt_queue segment */
	int tvc_vq_desc;
	/* virtio-scsi initiator task attribute */
	int tvc_task_attr;
	/* virtio-scsi initiator data direction */
	enum dma_data_direction tvc_data_direction;
	/* Expected data transfer length from virtio-scsi header */
	u32 tvc_exp_data_len;
	/* The Tag from include/linux/virtio_scsi.h:struct virtio_scsi_cmd_req */
	u64 tvc_tag;
	/* The number of scatterlists associated with this cmd */
	u32 tvc_sgl_count;
	/* Saved unpacked SCSI LUN for tcm_vhost_submission_work() */
	u32 tvc_lun;
	/* Pointer to the SGL formatted memory from virtio-scsi */
	struct scatterlist *tvc_sgl;
	/* Pointer to response */
	struct virtio_scsi_cmd_resp __user *tvc_resp;
	/* Pointer to vhost_scsi for our device */
	struct vhost_scsi *tvc_vhost;
	/* Pointer to vhost nexus memory */
	struct tcm_vhost_nexus *tvc_nexus;
	/* The TCM I/O descriptor that is accessed via container_of() */
	struct se_cmd tvc_se_cmd;
	/* work item used for cmwq dispatch to tcm_vhost_submission_work() */
	struct work_struct work;
	/* Copy of the incoming SCSI command descriptor block (CDB) */
	unsigned char tvc_cdb[TCM_VHOST_MAX_CDB_SIZE];
	/* Sense buffer that will be mapped into outgoing status */
	unsigned char tvc_sense_buf[TRANSPORT_SENSE_BUFFER];
	/* Completed commands list, serviced from vhost worker thread */
	struct list_head tvc_completion_list;
};

struct tcm_vhost_nexus {
	/* Pointer to TCM session for I_T Nexus */
	struct se_session *tvn_se_sess;
};

struct tcm_vhost_nacl {
	/* Binary World Wide unique Port Name for Vhost Initiator port */
	u64 iport_wwpn;
	/* ASCII formatted WWPN for Sas Initiator port */
	char iport_name[TCM_VHOST_NAMELEN];
	/* Returned by tcm_vhost_make_nodeacl() */
	struct se_node_acl se_node_acl;
};

struct tcm_vhost_tpg {
	/* Vhost port target portal group tag for TCM */
	u16 tport_tpgt;
	/* Used to track number of TPG Port/Lun Links wrt to explict I_T Nexus shutdown */
	int tv_tpg_port_count;
	/* Used for vhost_scsi device reference to tpg_nexus, protected by tv_tpg_mutex */
	int tv_tpg_vhost_count;
	/* list for tcm_vhost_list */
	struct list_head tv_tpg_list;
	/* Used to protect access for tpg_nexus */
	struct mutex tv_tpg_mutex;
	/* Pointer to the TCM VHost I_T Nexus for this TPG endpoint */
	struct tcm_vhost_nexus *tpg_nexus;
	/* Pointer back to tcm_vhost_tport */
	struct tcm_vhost_tport *tport;
	/* Returned by tcm_vhost_make_tpg() */
	struct se_portal_group se_tpg;
};

struct tcm_vhost_tport {
	/* SCSI protocol the tport is providing */
	u8 tport_proto_id;
	/* Binary World Wide unique Port Name for Vhost Target port */
	u64 tport_wwpn;
	/* ASCII formatted WWPN for Vhost Target port */
	char tport_name[TCM_VHOST_NAMELEN];
	/* Returned by tcm_vhost_make_tport() */
	struct se_wwn tport_wwn;
};

/*
 * As per request from MST, keep TCM_VHOST related ioctl defines out of
 * linux/vhost.h (user-space) for now..
 */

#include <linux/vhost.h>

/*
 * Used by QEMU userspace to ensure a consistent vhost-scsi ABI.
 *
 * ABI Rev 0: July 2012 version starting point for v3.6-rc merge candidate +
 *            RFC-v2 vhost-scsi userspace.  Add GET_ABI_VERSION ioctl usage
 */

#define VHOST_SCSI_ABI_VERSION	0

struct vhost_scsi_target {
	int abi_version;
	char vhost_wwpn[TRANSPORT_IQN_LEN];
	unsigned short vhost_tpgt;
	unsigned short reserved;
};

/* VHOST_SCSI specific defines */
#define VHOST_SCSI_SET_ENDPOINT _IOW(VHOST_VIRTIO, 0x40, struct vhost_scsi_target)
#define VHOST_SCSI_CLEAR_ENDPOINT _IOW(VHOST_VIRTIO, 0x41, struct vhost_scsi_target)
/* Changing this breaks userspace. */
#define VHOST_SCSI_GET_ABI_VERSION _IOW(VHOST_VIRTIO, 0x42, int)
