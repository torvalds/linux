// SPDX-License-Identifier: GPL-2.0-only
/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2013 QLogic Corporation
 */
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/iscsi_boot_sysfs.h>
#include <linux/inet.h>

#include <scsi/scsi_tcq.h>
#include <scsi/scsicam.h>

#include "ql4_def.h"
#include "ql4_version.h"
#include "ql4_glbl.h"
#include "ql4_dbg.h"
#include "ql4_inline.h"
#include "ql4_83xx.h"

/*
 * Driver version
 */
static char qla4xxx_version_str[40];

/*
 * SRB allocation cache
 */
static struct kmem_cache *srb_cachep;

/*
 * Module parameter information and variables
 */
static int ql4xdisablesysfsboot = 1;
module_param(ql4xdisablesysfsboot, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(ql4xdisablesysfsboot,
		 " Set to disable exporting boot targets to sysfs.\n"
		 "\t\t  0 - Export boot targets\n"
		 "\t\t  1 - Do not export boot targets (Default)");

int ql4xdontresethba;
module_param(ql4xdontresethba, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(ql4xdontresethba,
		 " Don't reset the HBA for driver recovery.\n"
		 "\t\t  0 - It will reset HBA (Default)\n"
		 "\t\t  1 - It will NOT reset HBA");

int ql4xextended_error_logging;
module_param(ql4xextended_error_logging, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(ql4xextended_error_logging,
		 " Option to enable extended error logging.\n"
		 "\t\t  0 - no logging (Default)\n"
		 "\t\t  2 - debug logging");

int ql4xenablemsix = 1;
module_param(ql4xenablemsix, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql4xenablemsix,
		 " Set to enable MSI or MSI-X interrupt mechanism.\n"
		 "\t\t  0 = enable INTx interrupt mechanism.\n"
		 "\t\t  1 = enable MSI-X interrupt mechanism (Default).\n"
		 "\t\t  2 = enable MSI interrupt mechanism.");

#define QL4_DEF_QDEPTH 32
static int ql4xmaxqdepth = QL4_DEF_QDEPTH;
module_param(ql4xmaxqdepth, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(ql4xmaxqdepth,
		 " Maximum queue depth to report for target devices.\n"
		 "\t\t  Default: 32.");

static int ql4xqfulltracking = 1;
module_param(ql4xqfulltracking, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(ql4xqfulltracking,
		 " Enable or disable dynamic tracking and adjustment of\n"
		 "\t\t scsi device queue depth.\n"
		 "\t\t  0 - Disable.\n"
		 "\t\t  1 - Enable. (Default)");

static int ql4xsess_recovery_tmo = QL4_SESS_RECOVERY_TMO;
module_param(ql4xsess_recovery_tmo, int, S_IRUGO);
MODULE_PARM_DESC(ql4xsess_recovery_tmo,
		" Target Session Recovery Timeout.\n"
		"\t\t  Default: 120 sec.");

int ql4xmdcapmask = 0;
module_param(ql4xmdcapmask, int, S_IRUGO);
MODULE_PARM_DESC(ql4xmdcapmask,
		 " Set the Minidump driver capture mask level.\n"
		 "\t\t  Default is 0 (firmware default capture mask)\n"
		 "\t\t  Can be set to 0x3, 0x7, 0xF, 0x1F, 0x3F, 0x7F, 0xFF");

int ql4xenablemd = 1;
module_param(ql4xenablemd, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(ql4xenablemd,
		 " Set to enable minidump.\n"
		 "\t\t  0 - disable minidump\n"
		 "\t\t  1 - enable minidump (Default)");

static int qla4xxx_wait_for_hba_online(struct scsi_qla_host *ha);
/*
 * SCSI host template entry points
 */
static void qla4xxx_config_dma_addressing(struct scsi_qla_host *ha);

/*
 * iSCSI template entry points
 */
static int qla4xxx_session_get_param(struct iscsi_cls_session *cls_sess,
				     enum iscsi_param param, char *buf);
static int qla4xxx_conn_get_param(struct iscsi_cls_conn *conn,
				  enum iscsi_param param, char *buf);
static int qla4xxx_host_get_param(struct Scsi_Host *shost,
				  enum iscsi_host_param param, char *buf);
static int qla4xxx_iface_set_param(struct Scsi_Host *shost, void *data,
				   uint32_t len);
static int qla4xxx_get_iface_param(struct iscsi_iface *iface,
				   enum iscsi_param_type param_type,
				   int param, char *buf);
static enum blk_eh_timer_return qla4xxx_eh_cmd_timed_out(struct scsi_cmnd *sc);
static struct iscsi_endpoint *qla4xxx_ep_connect(struct Scsi_Host *shost,
						 struct sockaddr *dst_addr,
						 int non_blocking);
static int qla4xxx_ep_poll(struct iscsi_endpoint *ep, int timeout_ms);
static void qla4xxx_ep_disconnect(struct iscsi_endpoint *ep);
static int qla4xxx_get_ep_param(struct iscsi_endpoint *ep,
				enum iscsi_param param, char *buf);
static int qla4xxx_conn_start(struct iscsi_cls_conn *conn);
static struct iscsi_cls_conn *
qla4xxx_conn_create(struct iscsi_cls_session *cls_sess, uint32_t conn_idx);
static int qla4xxx_conn_bind(struct iscsi_cls_session *cls_session,
			     struct iscsi_cls_conn *cls_conn,
			     uint64_t transport_fd, int is_leading);
static void qla4xxx_conn_destroy(struct iscsi_cls_conn *conn);
static struct iscsi_cls_session *
qla4xxx_session_create(struct iscsi_endpoint *ep, uint16_t cmds_max,
			uint16_t qdepth, uint32_t initial_cmdsn);
static void qla4xxx_session_destroy(struct iscsi_cls_session *sess);
static void qla4xxx_task_work(struct work_struct *wdata);
static int qla4xxx_alloc_pdu(struct iscsi_task *, uint8_t);
static int qla4xxx_task_xmit(struct iscsi_task *);
static void qla4xxx_task_cleanup(struct iscsi_task *);
static void qla4xxx_fail_session(struct iscsi_cls_session *cls_session);
static void qla4xxx_conn_get_stats(struct iscsi_cls_conn *cls_conn,
				   struct iscsi_stats *stats);
static int qla4xxx_send_ping(struct Scsi_Host *shost, uint32_t iface_num,
			     uint32_t iface_type, uint32_t payload_size,
			     uint32_t pid, struct sockaddr *dst_addr);
static int qla4xxx_get_chap_list(struct Scsi_Host *shost, uint16_t chap_tbl_idx,
				 uint32_t *num_entries, char *buf);
static int qla4xxx_delete_chap(struct Scsi_Host *shost, uint16_t chap_tbl_idx);
static int qla4xxx_set_chap_entry(struct Scsi_Host *shost, void  *data,
				  int len);
static int qla4xxx_get_host_stats(struct Scsi_Host *shost, char *buf, int len);

/*
 * SCSI host template entry points
 */
static int qla4xxx_queuecommand(struct Scsi_Host *h, struct scsi_cmnd *cmd);
static int qla4xxx_eh_abort(struct scsi_cmnd *cmd);
static int qla4xxx_eh_device_reset(struct scsi_cmnd *cmd);
static int qla4xxx_eh_target_reset(struct scsi_cmnd *cmd);
static int qla4xxx_eh_host_reset(struct scsi_cmnd *cmd);
static int qla4xxx_slave_alloc(struct scsi_device *device);
static umode_t qla4_attr_is_visible(int param_type, int param);
static int qla4xxx_host_reset(struct Scsi_Host *shost, int reset_type);

/*
 * iSCSI Flash DDB sysfs entry points
 */
static int
qla4xxx_sysfs_ddb_set_param(struct iscsi_bus_flash_session *fnode_sess,
			    struct iscsi_bus_flash_conn *fnode_conn,
			    void *data, int len);
static int
qla4xxx_sysfs_ddb_get_param(struct iscsi_bus_flash_session *fnode_sess,
			    int param, char *buf);
static int qla4xxx_sysfs_ddb_add(struct Scsi_Host *shost, const char *buf,
				 int len);
static int
qla4xxx_sysfs_ddb_delete(struct iscsi_bus_flash_session *fnode_sess);
static int qla4xxx_sysfs_ddb_login(struct iscsi_bus_flash_session *fnode_sess,
				   struct iscsi_bus_flash_conn *fnode_conn);
static int qla4xxx_sysfs_ddb_logout(struct iscsi_bus_flash_session *fnode_sess,
				    struct iscsi_bus_flash_conn *fnode_conn);
static int qla4xxx_sysfs_ddb_logout_sid(struct iscsi_cls_session *cls_sess);

static struct qla4_8xxx_legacy_intr_set legacy_intr[] =
    QLA82XX_LEGACY_INTR_CONFIG;

static const uint32_t qla4_82xx_reg_tbl[] = {
	QLA82XX_PEG_HALT_STATUS1,
	QLA82XX_PEG_HALT_STATUS2,
	QLA82XX_PEG_ALIVE_COUNTER,
	QLA82XX_CRB_DRV_ACTIVE,
	QLA82XX_CRB_DEV_STATE,
	QLA82XX_CRB_DRV_STATE,
	QLA82XX_CRB_DRV_SCRATCH,
	QLA82XX_CRB_DEV_PART_INFO,
	QLA82XX_CRB_DRV_IDC_VERSION,
	QLA82XX_FW_VERSION_MAJOR,
	QLA82XX_FW_VERSION_MINOR,
	QLA82XX_FW_VERSION_SUB,
	CRB_CMDPEG_STATE,
	CRB_TEMP_STATE,
};

static const uint32_t qla4_83xx_reg_tbl[] = {
	QLA83XX_PEG_HALT_STATUS1,
	QLA83XX_PEG_HALT_STATUS2,
	QLA83XX_PEG_ALIVE_COUNTER,
	QLA83XX_CRB_DRV_ACTIVE,
	QLA83XX_CRB_DEV_STATE,
	QLA83XX_CRB_DRV_STATE,
	QLA83XX_CRB_DRV_SCRATCH,
	QLA83XX_CRB_DEV_PART_INFO1,
	QLA83XX_CRB_IDC_VER_MAJOR,
	QLA83XX_FW_VER_MAJOR,
	QLA83XX_FW_VER_MINOR,
	QLA83XX_FW_VER_SUB,
	QLA83XX_CMDPEG_STATE,
	QLA83XX_ASIC_TEMP,
};

static struct scsi_host_template qla4xxx_driver_template = {
	.module			= THIS_MODULE,
	.name			= DRIVER_NAME,
	.proc_name		= DRIVER_NAME,
	.queuecommand		= qla4xxx_queuecommand,
	.cmd_size		= sizeof(struct qla4xxx_cmd_priv),

	.eh_abort_handler	= qla4xxx_eh_abort,
	.eh_device_reset_handler = qla4xxx_eh_device_reset,
	.eh_target_reset_handler = qla4xxx_eh_target_reset,
	.eh_host_reset_handler	= qla4xxx_eh_host_reset,
	.eh_timed_out		= qla4xxx_eh_cmd_timed_out,

	.slave_alloc		= qla4xxx_slave_alloc,
	.change_queue_depth	= scsi_change_queue_depth,

	.this_id		= -1,
	.cmd_per_lun		= 3,
	.sg_tablesize		= SG_ALL,

	.max_sectors		= 0xFFFF,
	.shost_groups		= qla4xxx_host_groups,
	.host_reset		= qla4xxx_host_reset,
	.vendor_id		= SCSI_NL_VID_TYPE_PCI | PCI_VENDOR_ID_QLOGIC,
};

static struct iscsi_transport qla4xxx_iscsi_transport = {
	.owner			= THIS_MODULE,
	.name			= DRIVER_NAME,
	.caps			= CAP_TEXT_NEGO |
				  CAP_DATA_PATH_OFFLOAD | CAP_HDRDGST |
				  CAP_DATADGST | CAP_LOGIN_OFFLOAD |
				  CAP_MULTI_R2T,
	.attr_is_visible	= qla4_attr_is_visible,
	.create_session         = qla4xxx_session_create,
	.destroy_session        = qla4xxx_session_destroy,
	.start_conn             = qla4xxx_conn_start,
	.create_conn            = qla4xxx_conn_create,
	.bind_conn              = qla4xxx_conn_bind,
	.unbind_conn		= iscsi_conn_unbind,
	.stop_conn              = iscsi_conn_stop,
	.destroy_conn           = qla4xxx_conn_destroy,
	.set_param              = iscsi_set_param,
	.get_conn_param		= qla4xxx_conn_get_param,
	.get_session_param	= qla4xxx_session_get_param,
	.get_ep_param           = qla4xxx_get_ep_param,
	.ep_connect		= qla4xxx_ep_connect,
	.ep_poll		= qla4xxx_ep_poll,
	.ep_disconnect		= qla4xxx_ep_disconnect,
	.get_stats		= qla4xxx_conn_get_stats,
	.send_pdu		= iscsi_conn_send_pdu,
	.xmit_task		= qla4xxx_task_xmit,
	.cleanup_task		= qla4xxx_task_cleanup,
	.alloc_pdu		= qla4xxx_alloc_pdu,

	.get_host_param		= qla4xxx_host_get_param,
	.set_iface_param	= qla4xxx_iface_set_param,
	.get_iface_param	= qla4xxx_get_iface_param,
	.bsg_request		= qla4xxx_bsg_request,
	.send_ping		= qla4xxx_send_ping,
	.get_chap		= qla4xxx_get_chap_list,
	.delete_chap		= qla4xxx_delete_chap,
	.set_chap		= qla4xxx_set_chap_entry,
	.get_flashnode_param	= qla4xxx_sysfs_ddb_get_param,
	.set_flashnode_param	= qla4xxx_sysfs_ddb_set_param,
	.new_flashnode		= qla4xxx_sysfs_ddb_add,
	.del_flashnode		= qla4xxx_sysfs_ddb_delete,
	.login_flashnode	= qla4xxx_sysfs_ddb_login,
	.logout_flashnode	= qla4xxx_sysfs_ddb_logout,
	.logout_flashnode_sid	= qla4xxx_sysfs_ddb_logout_sid,
	.get_host_stats		= qla4xxx_get_host_stats,
};

static struct scsi_transport_template *qla4xxx_scsi_transport;

static int qla4xxx_isp_check_reg(struct scsi_qla_host *ha)
{
	u32 reg_val = 0;
	int rval = QLA_SUCCESS;

	if (is_qla8022(ha))
		reg_val = readl(&ha->qla4_82xx_reg->host_status);
	else if (is_qla8032(ha) || is_qla8042(ha))
		reg_val = qla4_8xxx_rd_direct(ha, QLA8XXX_PEG_ALIVE_COUNTER);
	else
		reg_val = readw(&ha->reg->ctrl_status);

	if (reg_val == QL4_ISP_REG_DISCONNECT)
		rval = QLA_ERROR;

	return rval;
}

static int qla4xxx_send_ping(struct Scsi_Host *shost, uint32_t iface_num,
			     uint32_t iface_type, uint32_t payload_size,
			     uint32_t pid, struct sockaddr *dst_addr)
{
	struct scsi_qla_host *ha = to_qla_host(shost);
	struct sockaddr_in *addr;
	struct sockaddr_in6 *addr6;
	uint32_t options = 0;
	uint8_t ipaddr[IPv6_ADDR_LEN];
	int rval;

	memset(ipaddr, 0, IPv6_ADDR_LEN);
	/* IPv4 to IPv4 */
	if ((iface_type == ISCSI_IFACE_TYPE_IPV4) &&
	    (dst_addr->sa_family == AF_INET)) {
		addr = (struct sockaddr_in *)dst_addr;
		memcpy(ipaddr, &addr->sin_addr.s_addr, IP_ADDR_LEN);
		DEBUG2(ql4_printk(KERN_INFO, ha, "%s: IPv4 Ping src: %pI4 "
				  "dest: %pI4\n", __func__,
				  &ha->ip_config.ip_address, ipaddr));
		rval = qla4xxx_ping_iocb(ha, options, payload_size, pid,
					 ipaddr);
		if (rval)
			rval = -EINVAL;
	} else if ((iface_type == ISCSI_IFACE_TYPE_IPV6) &&
		   (dst_addr->sa_family == AF_INET6)) {
		/* IPv6 to IPv6 */
		addr6 = (struct sockaddr_in6 *)dst_addr;
		memcpy(ipaddr, &addr6->sin6_addr.in6_u.u6_addr8, IPv6_ADDR_LEN);

		options |= PING_IPV6_PROTOCOL_ENABLE;

		/* Ping using LinkLocal address */
		if ((iface_num == 0) || (iface_num == 1)) {
			DEBUG2(ql4_printk(KERN_INFO, ha, "%s: LinkLocal Ping "
					  "src: %pI6 dest: %pI6\n", __func__,
					  &ha->ip_config.ipv6_link_local_addr,
					  ipaddr));
			options |= PING_IPV6_LINKLOCAL_ADDR;
			rval = qla4xxx_ping_iocb(ha, options, payload_size,
						 pid, ipaddr);
		} else {
			ql4_printk(KERN_WARNING, ha, "%s: iface num = %d "
				   "not supported\n", __func__, iface_num);
			rval = -ENOSYS;
			goto exit_send_ping;
		}

		/*
		 * If ping using LinkLocal address fails, try ping using
		 * IPv6 address
		 */
		if (rval != QLA_SUCCESS) {
			options &= ~PING_IPV6_LINKLOCAL_ADDR;
			if (iface_num == 0) {
				options |= PING_IPV6_ADDR0;
				DEBUG2(ql4_printk(KERN_INFO, ha, "%s: IPv6 "
						  "Ping src: %pI6 "
						  "dest: %pI6\n", __func__,
						  &ha->ip_config.ipv6_addr0,
						  ipaddr));
			} else if (iface_num == 1) {
				options |= PING_IPV6_ADDR1;
				DEBUG2(ql4_printk(KERN_INFO, ha, "%s: IPv6 "
						  "Ping src: %pI6 "
						  "dest: %pI6\n", __func__,
						  &ha->ip_config.ipv6_addr1,
						  ipaddr));
			}
			rval = qla4xxx_ping_iocb(ha, options, payload_size,
						 pid, ipaddr);
			if (rval)
				rval = -EINVAL;
		}
	} else
		rval = -ENOSYS;
exit_send_ping:
	return rval;
}

static umode_t qla4_attr_is_visible(int param_type, int param)
{
	switch (param_type) {
	case ISCSI_HOST_PARAM:
		switch (param) {
		case ISCSI_HOST_PARAM_HWADDRESS:
		case ISCSI_HOST_PARAM_IPADDRESS:
		case ISCSI_HOST_PARAM_INITIATOR_NAME:
		case ISCSI_HOST_PARAM_PORT_STATE:
		case ISCSI_HOST_PARAM_PORT_SPEED:
			return S_IRUGO;
		default:
			return 0;
		}
	case ISCSI_PARAM:
		switch (param) {
		case ISCSI_PARAM_PERSISTENT_ADDRESS:
		case ISCSI_PARAM_PERSISTENT_PORT:
		case ISCSI_PARAM_CONN_ADDRESS:
		case ISCSI_PARAM_CONN_PORT:
		case ISCSI_PARAM_TARGET_NAME:
		case ISCSI_PARAM_TPGT:
		case ISCSI_PARAM_TARGET_ALIAS:
		case ISCSI_PARAM_MAX_BURST:
		case ISCSI_PARAM_MAX_R2T:
		case ISCSI_PARAM_FIRST_BURST:
		case ISCSI_PARAM_MAX_RECV_DLENGTH:
		case ISCSI_PARAM_MAX_XMIT_DLENGTH:
		case ISCSI_PARAM_IFACE_NAME:
		case ISCSI_PARAM_CHAP_OUT_IDX:
		case ISCSI_PARAM_CHAP_IN_IDX:
		case ISCSI_PARAM_USERNAME:
		case ISCSI_PARAM_PASSWORD:
		case ISCSI_PARAM_USERNAME_IN:
		case ISCSI_PARAM_PASSWORD_IN:
		case ISCSI_PARAM_AUTO_SND_TGT_DISABLE:
		case ISCSI_PARAM_DISCOVERY_SESS:
		case ISCSI_PARAM_PORTAL_TYPE:
		case ISCSI_PARAM_CHAP_AUTH_EN:
		case ISCSI_PARAM_DISCOVERY_LOGOUT_EN:
		case ISCSI_PARAM_BIDI_CHAP_EN:
		case ISCSI_PARAM_DISCOVERY_AUTH_OPTIONAL:
		case ISCSI_PARAM_DEF_TIME2WAIT:
		case ISCSI_PARAM_DEF_TIME2RETAIN:
		case ISCSI_PARAM_HDRDGST_EN:
		case ISCSI_PARAM_DATADGST_EN:
		case ISCSI_PARAM_INITIAL_R2T_EN:
		case ISCSI_PARAM_IMM_DATA_EN:
		case ISCSI_PARAM_PDU_INORDER_EN:
		case ISCSI_PARAM_DATASEQ_INORDER_EN:
		case ISCSI_PARAM_MAX_SEGMENT_SIZE:
		case ISCSI_PARAM_TCP_TIMESTAMP_STAT:
		case ISCSI_PARAM_TCP_WSF_DISABLE:
		case ISCSI_PARAM_TCP_NAGLE_DISABLE:
		case ISCSI_PARAM_TCP_TIMER_SCALE:
		case ISCSI_PARAM_TCP_TIMESTAMP_EN:
		case ISCSI_PARAM_TCP_XMIT_WSF:
		case ISCSI_PARAM_TCP_RECV_WSF:
		case ISCSI_PARAM_IP_FRAGMENT_DISABLE:
		case ISCSI_PARAM_IPV4_TOS:
		case ISCSI_PARAM_IPV6_TC:
		case ISCSI_PARAM_IPV6_FLOW_LABEL:
		case ISCSI_PARAM_IS_FW_ASSIGNED_IPV6:
		case ISCSI_PARAM_KEEPALIVE_TMO:
		case ISCSI_PARAM_LOCAL_PORT:
		case ISCSI_PARAM_ISID:
		case ISCSI_PARAM_TSID:
		case ISCSI_PARAM_DEF_TASKMGMT_TMO:
		case ISCSI_PARAM_ERL:
		case ISCSI_PARAM_STATSN:
		case ISCSI_PARAM_EXP_STATSN:
		case ISCSI_PARAM_DISCOVERY_PARENT_IDX:
		case ISCSI_PARAM_DISCOVERY_PARENT_TYPE:
		case ISCSI_PARAM_LOCAL_IPADDR:
			return S_IRUGO;
		default:
			return 0;
		}
	case ISCSI_NET_PARAM:
		switch (param) {
		case ISCSI_NET_PARAM_IPV4_ADDR:
		case ISCSI_NET_PARAM_IPV4_SUBNET:
		case ISCSI_NET_PARAM_IPV4_GW:
		case ISCSI_NET_PARAM_IPV4_BOOTPROTO:
		case ISCSI_NET_PARAM_IFACE_ENABLE:
		case ISCSI_NET_PARAM_IPV6_LINKLOCAL:
		case ISCSI_NET_PARAM_IPV6_ADDR:
		case ISCSI_NET_PARAM_IPV6_ROUTER:
		case ISCSI_NET_PARAM_IPV6_ADDR_AUTOCFG:
		case ISCSI_NET_PARAM_IPV6_LINKLOCAL_AUTOCFG:
		case ISCSI_NET_PARAM_VLAN_ID:
		case ISCSI_NET_PARAM_VLAN_PRIORITY:
		case ISCSI_NET_PARAM_VLAN_ENABLED:
		case ISCSI_NET_PARAM_MTU:
		case ISCSI_NET_PARAM_PORT:
		case ISCSI_NET_PARAM_IPADDR_STATE:
		case ISCSI_NET_PARAM_IPV6_LINKLOCAL_STATE:
		case ISCSI_NET_PARAM_IPV6_ROUTER_STATE:
		case ISCSI_NET_PARAM_DELAYED_ACK_EN:
		case ISCSI_NET_PARAM_TCP_NAGLE_DISABLE:
		case ISCSI_NET_PARAM_TCP_WSF_DISABLE:
		case ISCSI_NET_PARAM_TCP_WSF:
		case ISCSI_NET_PARAM_TCP_TIMER_SCALE:
		case ISCSI_NET_PARAM_TCP_TIMESTAMP_EN:
		case ISCSI_NET_PARAM_CACHE_ID:
		case ISCSI_NET_PARAM_IPV4_DHCP_DNS_ADDR_EN:
		case ISCSI_NET_PARAM_IPV4_DHCP_SLP_DA_EN:
		case ISCSI_NET_PARAM_IPV4_TOS_EN:
		case ISCSI_NET_PARAM_IPV4_TOS:
		case ISCSI_NET_PARAM_IPV4_GRAT_ARP_EN:
		case ISCSI_NET_PARAM_IPV4_DHCP_ALT_CLIENT_ID_EN:
		case ISCSI_NET_PARAM_IPV4_DHCP_ALT_CLIENT_ID:
		case ISCSI_NET_PARAM_IPV4_DHCP_REQ_VENDOR_ID_EN:
		case ISCSI_NET_PARAM_IPV4_DHCP_USE_VENDOR_ID_EN:
		case ISCSI_NET_PARAM_IPV4_DHCP_VENDOR_ID:
		case ISCSI_NET_PARAM_IPV4_DHCP_LEARN_IQN_EN:
		case ISCSI_NET_PARAM_IPV4_FRAGMENT_DISABLE:
		case ISCSI_NET_PARAM_IPV4_IN_FORWARD_EN:
		case ISCSI_NET_PARAM_REDIRECT_EN:
		case ISCSI_NET_PARAM_IPV4_TTL:
		case ISCSI_NET_PARAM_IPV6_GRAT_NEIGHBOR_ADV_EN:
		case ISCSI_NET_PARAM_IPV6_MLD_EN:
		case ISCSI_NET_PARAM_IPV6_FLOW_LABEL:
		case ISCSI_NET_PARAM_IPV6_TRAFFIC_CLASS:
		case ISCSI_NET_PARAM_IPV6_HOP_LIMIT:
		case ISCSI_NET_PARAM_IPV6_ND_REACHABLE_TMO:
		case ISCSI_NET_PARAM_IPV6_ND_REXMIT_TIME:
		case ISCSI_NET_PARAM_IPV6_ND_STALE_TMO:
		case ISCSI_NET_PARAM_IPV6_DUP_ADDR_DETECT_CNT:
		case ISCSI_NET_PARAM_IPV6_RTR_ADV_LINK_MTU:
			return S_IRUGO;
		default:
			return 0;
		}
	case ISCSI_IFACE_PARAM:
		switch (param) {
		case ISCSI_IFACE_PARAM_DEF_TASKMGMT_TMO:
		case ISCSI_IFACE_PARAM_HDRDGST_EN:
		case ISCSI_IFACE_PARAM_DATADGST_EN:
		case ISCSI_IFACE_PARAM_IMM_DATA_EN:
		case ISCSI_IFACE_PARAM_INITIAL_R2T_EN:
		case ISCSI_IFACE_PARAM_DATASEQ_INORDER_EN:
		case ISCSI_IFACE_PARAM_PDU_INORDER_EN:
		case ISCSI_IFACE_PARAM_ERL:
		case ISCSI_IFACE_PARAM_MAX_RECV_DLENGTH:
		case ISCSI_IFACE_PARAM_FIRST_BURST:
		case ISCSI_IFACE_PARAM_MAX_R2T:
		case ISCSI_IFACE_PARAM_MAX_BURST:
		case ISCSI_IFACE_PARAM_CHAP_AUTH_EN:
		case ISCSI_IFACE_PARAM_BIDI_CHAP_EN:
		case ISCSI_IFACE_PARAM_DISCOVERY_AUTH_OPTIONAL:
		case ISCSI_IFACE_PARAM_DISCOVERY_LOGOUT_EN:
		case ISCSI_IFACE_PARAM_STRICT_LOGIN_COMP_EN:
		case ISCSI_IFACE_PARAM_INITIATOR_NAME:
			return S_IRUGO;
		default:
			return 0;
		}
	case ISCSI_FLASHNODE_PARAM:
		switch (param) {
		case ISCSI_FLASHNODE_IS_FW_ASSIGNED_IPV6:
		case ISCSI_FLASHNODE_PORTAL_TYPE:
		case ISCSI_FLASHNODE_AUTO_SND_TGT_DISABLE:
		case ISCSI_FLASHNODE_DISCOVERY_SESS:
		case ISCSI_FLASHNODE_ENTRY_EN:
		case ISCSI_FLASHNODE_HDR_DGST_EN:
		case ISCSI_FLASHNODE_DATA_DGST_EN:
		case ISCSI_FLASHNODE_IMM_DATA_EN:
		case ISCSI_FLASHNODE_INITIAL_R2T_EN:
		case ISCSI_FLASHNODE_DATASEQ_INORDER:
		case ISCSI_FLASHNODE_PDU_INORDER:
		case ISCSI_FLASHNODE_CHAP_AUTH_EN:
		case ISCSI_FLASHNODE_SNACK_REQ_EN:
		case ISCSI_FLASHNODE_DISCOVERY_LOGOUT_EN:
		case ISCSI_FLASHNODE_BIDI_CHAP_EN:
		case ISCSI_FLASHNODE_DISCOVERY_AUTH_OPTIONAL:
		case ISCSI_FLASHNODE_ERL:
		case ISCSI_FLASHNODE_TCP_TIMESTAMP_STAT:
		case ISCSI_FLASHNODE_TCP_NAGLE_DISABLE:
		case ISCSI_FLASHNODE_TCP_WSF_DISABLE:
		case ISCSI_FLASHNODE_TCP_TIMER_SCALE:
		case ISCSI_FLASHNODE_TCP_TIMESTAMP_EN:
		case ISCSI_FLASHNODE_IP_FRAG_DISABLE:
		case ISCSI_FLASHNODE_MAX_RECV_DLENGTH:
		case ISCSI_FLASHNODE_MAX_XMIT_DLENGTH:
		case ISCSI_FLASHNODE_FIRST_BURST:
		case ISCSI_FLASHNODE_DEF_TIME2WAIT:
		case ISCSI_FLASHNODE_DEF_TIME2RETAIN:
		case ISCSI_FLASHNODE_MAX_R2T:
		case ISCSI_FLASHNODE_KEEPALIVE_TMO:
		case ISCSI_FLASHNODE_ISID:
		case ISCSI_FLASHNODE_TSID:
		case ISCSI_FLASHNODE_PORT:
		case ISCSI_FLASHNODE_MAX_BURST:
		case ISCSI_FLASHNODE_DEF_TASKMGMT_TMO:
		case ISCSI_FLASHNODE_IPADDR:
		case ISCSI_FLASHNODE_ALIAS:
		case ISCSI_FLASHNODE_REDIRECT_IPADDR:
		case ISCSI_FLASHNODE_MAX_SEGMENT_SIZE:
		case ISCSI_FLASHNODE_LOCAL_PORT:
		case ISCSI_FLASHNODE_IPV4_TOS:
		case ISCSI_FLASHNODE_IPV6_TC:
		case ISCSI_FLASHNODE_IPV6_FLOW_LABEL:
		case ISCSI_FLASHNODE_NAME:
		case ISCSI_FLASHNODE_TPGT:
		case ISCSI_FLASHNODE_LINK_LOCAL_IPV6:
		case ISCSI_FLASHNODE_DISCOVERY_PARENT_IDX:
		case ISCSI_FLASHNODE_DISCOVERY_PARENT_TYPE:
		case ISCSI_FLASHNODE_TCP_XMIT_WSF:
		case ISCSI_FLASHNODE_TCP_RECV_WSF:
		case ISCSI_FLASHNODE_CHAP_OUT_IDX:
		case ISCSI_FLASHNODE_USERNAME:
		case ISCSI_FLASHNODE_PASSWORD:
		case ISCSI_FLASHNODE_STATSN:
		case ISCSI_FLASHNODE_EXP_STATSN:
		case ISCSI_FLASHNODE_IS_BOOT_TGT:
			return S_IRUGO;
		default:
			return 0;
		}
	}

	return 0;
}

/**
 * qla4xxx_create_chap_list - Create CHAP list from FLASH
 * @ha: pointer to adapter structure
 *
 * Read flash and make a list of CHAP entries, during login when a CHAP entry
 * is received, it will be checked in this list. If entry exist then the CHAP
 * entry index is set in the DDB. If CHAP entry does not exist in this list
 * then a new entry is added in FLASH in CHAP table and the index obtained is
 * used in the DDB.
 **/
static void qla4xxx_create_chap_list(struct scsi_qla_host *ha)
{
	int rval = 0;
	uint8_t *chap_flash_data = NULL;
	uint32_t offset;
	dma_addr_t chap_dma;
	uint32_t chap_size = 0;

	if (is_qla40XX(ha))
		chap_size = MAX_CHAP_ENTRIES_40XX *
			    sizeof(struct ql4_chap_table);
	else	/* Single region contains CHAP info for both
		 * ports which is divided into half for each port.
		 */
		chap_size = ha->hw.flt_chap_size / 2;

	chap_flash_data = dma_alloc_coherent(&ha->pdev->dev, chap_size,
					     &chap_dma, GFP_KERNEL);
	if (!chap_flash_data) {
		ql4_printk(KERN_ERR, ha, "No memory for chap_flash_data\n");
		return;
	}

	if (is_qla40XX(ha)) {
		offset = FLASH_CHAP_OFFSET;
	} else {
		offset = FLASH_RAW_ACCESS_ADDR + (ha->hw.flt_region_chap << 2);
		if (ha->port_num == 1)
			offset += chap_size;
	}

	rval = qla4xxx_get_flash(ha, chap_dma, offset, chap_size);
	if (rval != QLA_SUCCESS)
		goto exit_chap_list;

	if (ha->chap_list == NULL)
		ha->chap_list = vmalloc(chap_size);
	if (ha->chap_list == NULL) {
		ql4_printk(KERN_ERR, ha, "No memory for ha->chap_list\n");
		goto exit_chap_list;
	}

	memcpy(ha->chap_list, chap_flash_data, chap_size);

exit_chap_list:
	dma_free_coherent(&ha->pdev->dev, chap_size, chap_flash_data, chap_dma);
}

static int qla4xxx_get_chap_by_index(struct scsi_qla_host *ha,
				     int16_t chap_index,
				     struct ql4_chap_table **chap_entry)
{
	int rval = QLA_ERROR;
	int max_chap_entries;

	if (!ha->chap_list) {
		ql4_printk(KERN_ERR, ha, "CHAP table cache is empty!\n");
		goto exit_get_chap;
	}

	if (is_qla80XX(ha))
		max_chap_entries = (ha->hw.flt_chap_size / 2) /
				   sizeof(struct ql4_chap_table);
	else
		max_chap_entries = MAX_CHAP_ENTRIES_40XX;

	if (chap_index > max_chap_entries) {
		ql4_printk(KERN_ERR, ha, "Invalid Chap index\n");
		goto exit_get_chap;
	}

	*chap_entry = (struct ql4_chap_table *)ha->chap_list + chap_index;
	if ((*chap_entry)->cookie !=
	     cpu_to_le16(CHAP_VALID_COOKIE)) {
		*chap_entry = NULL;
	} else {
		rval = QLA_SUCCESS;
	}

exit_get_chap:
	return rval;
}

/**
 * qla4xxx_find_free_chap_index - Find the first free chap index
 * @ha: pointer to adapter structure
 * @chap_index: CHAP index to be returned
 *
 * Find the first free chap index available in the chap table
 *
 * Note: Caller should acquire the chap lock before getting here.
 **/
static int qla4xxx_find_free_chap_index(struct scsi_qla_host *ha,
					uint16_t *chap_index)
{
	int i, rval;
	int free_index = -1;
	int max_chap_entries = 0;
	struct ql4_chap_table *chap_table;

	if (is_qla80XX(ha))
		max_chap_entries = (ha->hw.flt_chap_size / 2) /
						sizeof(struct ql4_chap_table);
	else
		max_chap_entries = MAX_CHAP_ENTRIES_40XX;

	if (!ha->chap_list) {
		ql4_printk(KERN_ERR, ha, "CHAP table cache is empty!\n");
		rval = QLA_ERROR;
		goto exit_find_chap;
	}

	for (i = 0; i < max_chap_entries; i++) {
		chap_table = (struct ql4_chap_table *)ha->chap_list + i;

		if ((chap_table->cookie !=
		    cpu_to_le16(CHAP_VALID_COOKIE)) &&
		   (i > MAX_RESRV_CHAP_IDX)) {
				free_index = i;
				break;
		}
	}

	if (free_index != -1) {
		*chap_index = free_index;
		rval = QLA_SUCCESS;
	} else {
		rval = QLA_ERROR;
	}

exit_find_chap:
	return rval;
}

static int qla4xxx_get_chap_list(struct Scsi_Host *shost, uint16_t chap_tbl_idx,
				  uint32_t *num_entries, char *buf)
{
	struct scsi_qla_host *ha = to_qla_host(shost);
	struct ql4_chap_table *chap_table;
	struct iscsi_chap_rec *chap_rec;
	int max_chap_entries = 0;
	int valid_chap_entries = 0;
	int ret = 0, i;

	if (is_qla80XX(ha))
		max_chap_entries = (ha->hw.flt_chap_size / 2) /
					sizeof(struct ql4_chap_table);
	else
		max_chap_entries = MAX_CHAP_ENTRIES_40XX;

	ql4_printk(KERN_INFO, ha, "%s: num_entries = %d, CHAP idx = %d\n",
			__func__, *num_entries, chap_tbl_idx);

	if (!buf) {
		ret = -ENOMEM;
		goto exit_get_chap_list;
	}

	qla4xxx_create_chap_list(ha);

	chap_rec = (struct iscsi_chap_rec *) buf;
	mutex_lock(&ha->chap_sem);
	for (i = chap_tbl_idx; i < max_chap_entries; i++) {
		chap_table = (struct ql4_chap_table *)ha->chap_list + i;
		if (chap_table->cookie !=
		    cpu_to_le16(CHAP_VALID_COOKIE))
			continue;

		chap_rec->chap_tbl_idx = i;
		strlcpy(chap_rec->username, chap_table->name,
			ISCSI_CHAP_AUTH_NAME_MAX_LEN);
		strlcpy(chap_rec->password, chap_table->secret,
			QL4_CHAP_MAX_SECRET_LEN);
		chap_rec->password_length = chap_table->secret_len;

		if (chap_table->flags & BIT_7) /* local */
			chap_rec->chap_type = CHAP_TYPE_OUT;

		if (chap_table->flags & BIT_6) /* peer */
			chap_rec->chap_type = CHAP_TYPE_IN;

		chap_rec++;

		valid_chap_entries++;
		if (valid_chap_entries == *num_entries)
			break;
	}
	mutex_unlock(&ha->chap_sem);

exit_get_chap_list:
	ql4_printk(KERN_INFO, ha, "%s: Valid CHAP Entries = %d\n",
			__func__,  valid_chap_entries);
	*num_entries = valid_chap_entries;
	return ret;
}

static int __qla4xxx_is_chap_active(struct device *dev, void *data)
{
	int ret = 0;
	uint16_t *chap_tbl_idx = (uint16_t *) data;
	struct iscsi_cls_session *cls_session;
	struct iscsi_session *sess;
	struct ddb_entry *ddb_entry;

	if (!iscsi_is_session_dev(dev))
		goto exit_is_chap_active;

	cls_session = iscsi_dev_to_session(dev);
	sess = cls_session->dd_data;
	ddb_entry = sess->dd_data;

	if (iscsi_is_session_online(cls_session))
		goto exit_is_chap_active;

	if (ddb_entry->chap_tbl_idx == *chap_tbl_idx)
		ret = 1;

exit_is_chap_active:
	return ret;
}

static int qla4xxx_is_chap_active(struct Scsi_Host *shost,
				  uint16_t chap_tbl_idx)
{
	int ret = 0;

	ret = device_for_each_child(&shost->shost_gendev, &chap_tbl_idx,
				    __qla4xxx_is_chap_active);

	return ret;
}

static int qla4xxx_delete_chap(struct Scsi_Host *shost, uint16_t chap_tbl_idx)
{
	struct scsi_qla_host *ha = to_qla_host(shost);
	struct ql4_chap_table *chap_table;
	dma_addr_t chap_dma;
	int max_chap_entries = 0;
	uint32_t offset = 0;
	uint32_t chap_size;
	int ret = 0;

	chap_table = dma_pool_zalloc(ha->chap_dma_pool, GFP_KERNEL, &chap_dma);
	if (chap_table == NULL)
		return -ENOMEM;

	if (is_qla80XX(ha))
		max_chap_entries = (ha->hw.flt_chap_size / 2) /
				   sizeof(struct ql4_chap_table);
	else
		max_chap_entries = MAX_CHAP_ENTRIES_40XX;

	if (chap_tbl_idx > max_chap_entries) {
		ret = -EINVAL;
		goto exit_delete_chap;
	}

	/* Check if chap index is in use.
	 * If chap is in use don't delet chap entry */
	ret = qla4xxx_is_chap_active(shost, chap_tbl_idx);
	if (ret) {
		ql4_printk(KERN_INFO, ha, "CHAP entry %d is in use, cannot "
			   "delete from flash\n", chap_tbl_idx);
		ret = -EBUSY;
		goto exit_delete_chap;
	}

	chap_size = sizeof(struct ql4_chap_table);
	if (is_qla40XX(ha))
		offset = FLASH_CHAP_OFFSET | (chap_tbl_idx * chap_size);
	else {
		offset = FLASH_RAW_ACCESS_ADDR + (ha->hw.flt_region_chap << 2);
		/* flt_chap_size is CHAP table size for both ports
		 * so divide it by 2 to calculate the offset for second port
		 */
		if (ha->port_num == 1)
			offset += (ha->hw.flt_chap_size / 2);
		offset += (chap_tbl_idx * chap_size);
	}

	ret = qla4xxx_get_flash(ha, chap_dma, offset, chap_size);
	if (ret != QLA_SUCCESS) {
		ret = -EINVAL;
		goto exit_delete_chap;
	}

	DEBUG2(ql4_printk(KERN_INFO, ha, "Chap Cookie: x%x\n",
			  __le16_to_cpu(chap_table->cookie)));

	if (__le16_to_cpu(chap_table->cookie) != CHAP_VALID_COOKIE) {
		ql4_printk(KERN_ERR, ha, "No valid chap entry found\n");
		goto exit_delete_chap;
	}

	chap_table->cookie = cpu_to_le16(0xFFFF);

	offset = FLASH_CHAP_OFFSET |
			(chap_tbl_idx * sizeof(struct ql4_chap_table));
	ret = qla4xxx_set_flash(ha, chap_dma, offset, chap_size,
				FLASH_OPT_RMW_COMMIT);
	if (ret == QLA_SUCCESS && ha->chap_list) {
		mutex_lock(&ha->chap_sem);
		/* Update ha chap_list cache */
		memcpy((struct ql4_chap_table *)ha->chap_list + chap_tbl_idx,
			chap_table, sizeof(struct ql4_chap_table));
		mutex_unlock(&ha->chap_sem);
	}
	if (ret != QLA_SUCCESS)
		ret =  -EINVAL;

exit_delete_chap:
	dma_pool_free(ha->chap_dma_pool, chap_table, chap_dma);
	return ret;
}

/**
 * qla4xxx_set_chap_entry - Make chap entry with given information
 * @shost: pointer to host
 * @data: chap info - credentials, index and type to make chap entry
 * @len: length of data
 *
 * Add or update chap entry with the given information
 **/
static int qla4xxx_set_chap_entry(struct Scsi_Host *shost, void *data, int len)
{
	struct scsi_qla_host *ha = to_qla_host(shost);
	struct iscsi_chap_rec chap_rec;
	struct ql4_chap_table *chap_entry = NULL;
	struct iscsi_param_info *param_info;
	struct nlattr *attr;
	int max_chap_entries = 0;
	int type;
	int rem = len;
	int rc = 0;
	int size;

	memset(&chap_rec, 0, sizeof(chap_rec));

	nla_for_each_attr(attr, data, len, rem) {
		param_info = nla_data(attr);

		switch (param_info->param) {
		case ISCSI_CHAP_PARAM_INDEX:
			chap_rec.chap_tbl_idx = *(uint16_t *)param_info->value;
			break;
		case ISCSI_CHAP_PARAM_CHAP_TYPE:
			chap_rec.chap_type = param_info->value[0];
			break;
		case ISCSI_CHAP_PARAM_USERNAME:
			size = min_t(size_t, sizeof(chap_rec.username),
				     param_info->len);
			memcpy(chap_rec.username, param_info->value, size);
			break;
		case ISCSI_CHAP_PARAM_PASSWORD:
			size = min_t(size_t, sizeof(chap_rec.password),
				     param_info->len);
			memcpy(chap_rec.password, param_info->value, size);
			break;
		case ISCSI_CHAP_PARAM_PASSWORD_LEN:
			chap_rec.password_length = param_info->value[0];
			break;
		default:
			ql4_printk(KERN_ERR, ha,
				   "%s: No such sysfs attribute\n", __func__);
			rc = -ENOSYS;
			goto exit_set_chap;
		}
	}

	if (chap_rec.chap_type == CHAP_TYPE_IN)
		type = BIDI_CHAP;
	else
		type = LOCAL_CHAP;

	if (is_qla80XX(ha))
		max_chap_entries = (ha->hw.flt_chap_size / 2) /
				   sizeof(struct ql4_chap_table);
	else
		max_chap_entries = MAX_CHAP_ENTRIES_40XX;

	mutex_lock(&ha->chap_sem);
	if (chap_rec.chap_tbl_idx < max_chap_entries) {
		rc = qla4xxx_get_chap_by_index(ha, chap_rec.chap_tbl_idx,
					       &chap_entry);
		if (!rc) {
			if (!(type == qla4xxx_get_chap_type(chap_entry))) {
				ql4_printk(KERN_INFO, ha,
					   "Type mismatch for CHAP entry %d\n",
					   chap_rec.chap_tbl_idx);
				rc = -EINVAL;
				goto exit_unlock_chap;
			}

			/* If chap index is in use then don't modify it */
			rc = qla4xxx_is_chap_active(shost,
						    chap_rec.chap_tbl_idx);
			if (rc) {
				ql4_printk(KERN_INFO, ha,
					   "CHAP entry %d is in use\n",
					   chap_rec.chap_tbl_idx);
				rc = -EBUSY;
				goto exit_unlock_chap;
			}
		}
	} else {
		rc = qla4xxx_find_free_chap_index(ha, &chap_rec.chap_tbl_idx);
		if (rc) {
			ql4_printk(KERN_INFO, ha, "CHAP entry not available\n");
			rc = -EBUSY;
			goto exit_unlock_chap;
		}
	}

	rc = qla4xxx_set_chap(ha, chap_rec.username, chap_rec.password,
			      chap_rec.chap_tbl_idx, type);

exit_unlock_chap:
	mutex_unlock(&ha->chap_sem);

exit_set_chap:
	return rc;
}


static int qla4xxx_get_host_stats(struct Scsi_Host *shost, char *buf, int len)
{
	struct scsi_qla_host *ha = to_qla_host(shost);
	struct iscsi_offload_host_stats *host_stats = NULL;
	int host_stats_size;
	int ret = 0;
	int ddb_idx = 0;
	struct ql_iscsi_stats *ql_iscsi_stats = NULL;
	int stats_size;
	dma_addr_t iscsi_stats_dma;

	DEBUG2(ql4_printk(KERN_INFO, ha, "Func: %s\n", __func__));

	host_stats_size = sizeof(struct iscsi_offload_host_stats);

	if (host_stats_size != len) {
		ql4_printk(KERN_INFO, ha, "%s: host_stats size mismatch expected = %d, is = %d\n",
			   __func__, len, host_stats_size);
		ret = -EINVAL;
		goto exit_host_stats;
	}
	host_stats = (struct iscsi_offload_host_stats *)buf;

	if (!buf) {
		ret = -ENOMEM;
		goto exit_host_stats;
	}

	stats_size = PAGE_ALIGN(sizeof(struct ql_iscsi_stats));

	ql_iscsi_stats = dma_alloc_coherent(&ha->pdev->dev, stats_size,
					    &iscsi_stats_dma, GFP_KERNEL);
	if (!ql_iscsi_stats) {
		ql4_printk(KERN_ERR, ha,
			   "Unable to allocate memory for iscsi stats\n");
		ret = -ENOMEM;
		goto exit_host_stats;
	}

	ret =  qla4xxx_get_mgmt_data(ha, ddb_idx, stats_size,
				     iscsi_stats_dma);
	if (ret != QLA_SUCCESS) {
		ql4_printk(KERN_ERR, ha,
			   "Unable to retrieve iscsi stats\n");
		ret = -EIO;
		goto exit_host_stats;
	}
	host_stats->mactx_frames = le64_to_cpu(ql_iscsi_stats->mac_tx_frames);
	host_stats->mactx_bytes = le64_to_cpu(ql_iscsi_stats->mac_tx_bytes);
	host_stats->mactx_multicast_frames =
			le64_to_cpu(ql_iscsi_stats->mac_tx_multicast_frames);
	host_stats->mactx_broadcast_frames =
			le64_to_cpu(ql_iscsi_stats->mac_tx_broadcast_frames);
	host_stats->mactx_pause_frames =
			le64_to_cpu(ql_iscsi_stats->mac_tx_pause_frames);
	host_stats->mactx_control_frames =
			le64_to_cpu(ql_iscsi_stats->mac_tx_control_frames);
	host_stats->mactx_deferral =
			le64_to_cpu(ql_iscsi_stats->mac_tx_deferral);
	host_stats->mactx_excess_deferral =
			le64_to_cpu(ql_iscsi_stats->mac_tx_excess_deferral);
	host_stats->mactx_late_collision =
			le64_to_cpu(ql_iscsi_stats->mac_tx_late_collision);
	host_stats->mactx_abort	= le64_to_cpu(ql_iscsi_stats->mac_tx_abort);
	host_stats->mactx_single_collision =
			le64_to_cpu(ql_iscsi_stats->mac_tx_single_collision);
	host_stats->mactx_multiple_collision =
			le64_to_cpu(ql_iscsi_stats->mac_tx_multiple_collision);
	host_stats->mactx_collision =
			le64_to_cpu(ql_iscsi_stats->mac_tx_collision);
	host_stats->mactx_frames_dropped =
			le64_to_cpu(ql_iscsi_stats->mac_tx_frames_dropped);
	host_stats->mactx_jumbo_frames =
			le64_to_cpu(ql_iscsi_stats->mac_tx_jumbo_frames);
	host_stats->macrx_frames = le64_to_cpu(ql_iscsi_stats->mac_rx_frames);
	host_stats->macrx_bytes = le64_to_cpu(ql_iscsi_stats->mac_rx_bytes);
	host_stats->macrx_unknown_control_frames =
		le64_to_cpu(ql_iscsi_stats->mac_rx_unknown_control_frames);
	host_stats->macrx_pause_frames =
			le64_to_cpu(ql_iscsi_stats->mac_rx_pause_frames);
	host_stats->macrx_control_frames =
			le64_to_cpu(ql_iscsi_stats->mac_rx_control_frames);
	host_stats->macrx_dribble =
			le64_to_cpu(ql_iscsi_stats->mac_rx_dribble);
	host_stats->macrx_frame_length_error =
			le64_to_cpu(ql_iscsi_stats->mac_rx_frame_length_error);
	host_stats->macrx_jabber = le64_to_cpu(ql_iscsi_stats->mac_rx_jabber);
	host_stats->macrx_carrier_sense_error =
		le64_to_cpu(ql_iscsi_stats->mac_rx_carrier_sense_error);
	host_stats->macrx_frame_discarded =
			le64_to_cpu(ql_iscsi_stats->mac_rx_frame_discarded);
	host_stats->macrx_frames_dropped =
			le64_to_cpu(ql_iscsi_stats->mac_rx_frames_dropped);
	host_stats->mac_crc_error = le64_to_cpu(ql_iscsi_stats->mac_crc_error);
	host_stats->mac_encoding_error =
			le64_to_cpu(ql_iscsi_stats->mac_encoding_error);
	host_stats->macrx_length_error_large =
			le64_to_cpu(ql_iscsi_stats->mac_rx_length_error_large);
	host_stats->macrx_length_error_small =
			le64_to_cpu(ql_iscsi_stats->mac_rx_length_error_small);
	host_stats->macrx_multicast_frames =
			le64_to_cpu(ql_iscsi_stats->mac_rx_multicast_frames);
	host_stats->macrx_broadcast_frames =
			le64_to_cpu(ql_iscsi_stats->mac_rx_broadcast_frames);
	host_stats->iptx_packets = le64_to_cpu(ql_iscsi_stats->ip_tx_packets);
	host_stats->iptx_bytes = le64_to_cpu(ql_iscsi_stats->ip_tx_bytes);
	host_stats->iptx_fragments =
			le64_to_cpu(ql_iscsi_stats->ip_tx_fragments);
	host_stats->iprx_packets = le64_to_cpu(ql_iscsi_stats->ip_rx_packets);
	host_stats->iprx_bytes = le64_to_cpu(ql_iscsi_stats->ip_rx_bytes);
	host_stats->iprx_fragments =
			le64_to_cpu(ql_iscsi_stats->ip_rx_fragments);
	host_stats->ip_datagram_reassembly =
			le64_to_cpu(ql_iscsi_stats->ip_datagram_reassembly);
	host_stats->ip_invalid_address_error =
			le64_to_cpu(ql_iscsi_stats->ip_invalid_address_error);
	host_stats->ip_error_packets =
			le64_to_cpu(ql_iscsi_stats->ip_error_packets);
	host_stats->ip_fragrx_overlap =
			le64_to_cpu(ql_iscsi_stats->ip_fragrx_overlap);
	host_stats->ip_fragrx_outoforder =
			le64_to_cpu(ql_iscsi_stats->ip_fragrx_outoforder);
	host_stats->ip_datagram_reassembly_timeout =
		le64_to_cpu(ql_iscsi_stats->ip_datagram_reassembly_timeout);
	host_stats->ipv6tx_packets =
			le64_to_cpu(ql_iscsi_stats->ipv6_tx_packets);
	host_stats->ipv6tx_bytes = le64_to_cpu(ql_iscsi_stats->ipv6_tx_bytes);
	host_stats->ipv6tx_fragments =
			le64_to_cpu(ql_iscsi_stats->ipv6_tx_fragments);
	host_stats->ipv6rx_packets =
			le64_to_cpu(ql_iscsi_stats->ipv6_rx_packets);
	host_stats->ipv6rx_bytes = le64_to_cpu(ql_iscsi_stats->ipv6_rx_bytes);
	host_stats->ipv6rx_fragments =
			le64_to_cpu(ql_iscsi_stats->ipv6_rx_fragments);
	host_stats->ipv6_datagram_reassembly =
			le64_to_cpu(ql_iscsi_stats->ipv6_datagram_reassembly);
	host_stats->ipv6_invalid_address_error =
		le64_to_cpu(ql_iscsi_stats->ipv6_invalid_address_error);
	host_stats->ipv6_error_packets =
			le64_to_cpu(ql_iscsi_stats->ipv6_error_packets);
	host_stats->ipv6_fragrx_overlap =
			le64_to_cpu(ql_iscsi_stats->ipv6_fragrx_overlap);
	host_stats->ipv6_fragrx_outoforder =
			le64_to_cpu(ql_iscsi_stats->ipv6_fragrx_outoforder);
	host_stats->ipv6_datagram_reassembly_timeout =
		le64_to_cpu(ql_iscsi_stats->ipv6_datagram_reassembly_timeout);
	host_stats->tcptx_segments =
			le64_to_cpu(ql_iscsi_stats->tcp_tx_segments);
	host_stats->tcptx_bytes	= le64_to_cpu(ql_iscsi_stats->tcp_tx_bytes);
	host_stats->tcprx_segments =
			le64_to_cpu(ql_iscsi_stats->tcp_rx_segments);
	host_stats->tcprx_byte = le64_to_cpu(ql_iscsi_stats->tcp_rx_byte);
	host_stats->tcp_duplicate_ack_retx =
			le64_to_cpu(ql_iscsi_stats->tcp_duplicate_ack_retx);
	host_stats->tcp_retx_timer_expired =
			le64_to_cpu(ql_iscsi_stats->tcp_retx_timer_expired);
	host_stats->tcprx_duplicate_ack	=
			le64_to_cpu(ql_iscsi_stats->tcp_rx_duplicate_ack);
	host_stats->tcprx_pure_ackr =
			le64_to_cpu(ql_iscsi_stats->tcp_rx_pure_ackr);
	host_stats->tcptx_delayed_ack =
			le64_to_cpu(ql_iscsi_stats->tcp_tx_delayed_ack);
	host_stats->tcptx_pure_ack =
			le64_to_cpu(ql_iscsi_stats->tcp_tx_pure_ack);
	host_stats->tcprx_segment_error =
			le64_to_cpu(ql_iscsi_stats->tcp_rx_segment_error);
	host_stats->tcprx_segment_outoforder =
			le64_to_cpu(ql_iscsi_stats->tcp_rx_segment_outoforder);
	host_stats->tcprx_window_probe =
			le64_to_cpu(ql_iscsi_stats->tcp_rx_window_probe);
	host_stats->tcprx_window_update =
			le64_to_cpu(ql_iscsi_stats->tcp_rx_window_update);
	host_stats->tcptx_window_probe_persist =
		le64_to_cpu(ql_iscsi_stats->tcp_tx_window_probe_persist);
	host_stats->ecc_error_correction =
			le64_to_cpu(ql_iscsi_stats->ecc_error_correction);
	host_stats->iscsi_pdu_tx = le64_to_cpu(ql_iscsi_stats->iscsi_pdu_tx);
	host_stats->iscsi_data_bytes_tx =
			le64_to_cpu(ql_iscsi_stats->iscsi_data_bytes_tx);
	host_stats->iscsi_pdu_rx = le64_to_cpu(ql_iscsi_stats->iscsi_pdu_rx);
	host_stats->iscsi_data_bytes_rx	=
			le64_to_cpu(ql_iscsi_stats->iscsi_data_bytes_rx);
	host_stats->iscsi_io_completed =
			le64_to_cpu(ql_iscsi_stats->iscsi_io_completed);
	host_stats->iscsi_unexpected_io_rx =
			le64_to_cpu(ql_iscsi_stats->iscsi_unexpected_io_rx);
	host_stats->iscsi_format_error =
			le64_to_cpu(ql_iscsi_stats->iscsi_format_error);
	host_stats->iscsi_hdr_digest_error =
			le64_to_cpu(ql_iscsi_stats->iscsi_hdr_digest_error);
	host_stats->iscsi_data_digest_error =
			le64_to_cpu(ql_iscsi_stats->iscsi_data_digest_error);
	host_stats->iscsi_sequence_error =
			le64_to_cpu(ql_iscsi_stats->iscsi_sequence_error);
exit_host_stats:
	if (ql_iscsi_stats)
		dma_free_coherent(&ha->pdev->dev, stats_size,
				  ql_iscsi_stats, iscsi_stats_dma);

	ql4_printk(KERN_INFO, ha, "%s: Get host stats done\n",
		   __func__);
	return ret;
}

static int qla4xxx_get_iface_param(struct iscsi_iface *iface,
				   enum iscsi_param_type param_type,
				   int param, char *buf)
{
	struct Scsi_Host *shost = iscsi_iface_to_shost(iface);
	struct scsi_qla_host *ha = to_qla_host(shost);
	int ival;
	char *pval = NULL;
	int len = -ENOSYS;

	if (param_type == ISCSI_NET_PARAM) {
		switch (param) {
		case ISCSI_NET_PARAM_IPV4_ADDR:
			len = sprintf(buf, "%pI4\n", &ha->ip_config.ip_address);
			break;
		case ISCSI_NET_PARAM_IPV4_SUBNET:
			len = sprintf(buf, "%pI4\n",
				      &ha->ip_config.subnet_mask);
			break;
		case ISCSI_NET_PARAM_IPV4_GW:
			len = sprintf(buf, "%pI4\n", &ha->ip_config.gateway);
			break;
		case ISCSI_NET_PARAM_IFACE_ENABLE:
			if (iface->iface_type == ISCSI_IFACE_TYPE_IPV4) {
				OP_STATE(ha->ip_config.ipv4_options,
					 IPOPT_IPV4_PROTOCOL_ENABLE, pval);
			} else {
				OP_STATE(ha->ip_config.ipv6_options,
					 IPV6_OPT_IPV6_PROTOCOL_ENABLE, pval);
			}

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_IPV4_BOOTPROTO:
			len = sprintf(buf, "%s\n",
				      (ha->ip_config.tcp_options &
				       TCPOPT_DHCP_ENABLE) ?
				      "dhcp" : "static");
			break;
		case ISCSI_NET_PARAM_IPV6_ADDR:
			if (iface->iface_num == 0)
				len = sprintf(buf, "%pI6\n",
					      &ha->ip_config.ipv6_addr0);
			if (iface->iface_num == 1)
				len = sprintf(buf, "%pI6\n",
					      &ha->ip_config.ipv6_addr1);
			break;
		case ISCSI_NET_PARAM_IPV6_LINKLOCAL:
			len = sprintf(buf, "%pI6\n",
				      &ha->ip_config.ipv6_link_local_addr);
			break;
		case ISCSI_NET_PARAM_IPV6_ROUTER:
			len = sprintf(buf, "%pI6\n",
				      &ha->ip_config.ipv6_default_router_addr);
			break;
		case ISCSI_NET_PARAM_IPV6_ADDR_AUTOCFG:
			pval = (ha->ip_config.ipv6_addl_options &
				IPV6_ADDOPT_NEIGHBOR_DISCOVERY_ADDR_ENABLE) ?
				"nd" : "static";

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_IPV6_LINKLOCAL_AUTOCFG:
			pval = (ha->ip_config.ipv6_addl_options &
				IPV6_ADDOPT_AUTOCONFIG_LINK_LOCAL_ADDR) ?
				"auto" : "static";

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_VLAN_ID:
			if (iface->iface_type == ISCSI_IFACE_TYPE_IPV4)
				ival = ha->ip_config.ipv4_vlan_tag &
				       ISCSI_MAX_VLAN_ID;
			else
				ival = ha->ip_config.ipv6_vlan_tag &
				       ISCSI_MAX_VLAN_ID;

			len = sprintf(buf, "%d\n", ival);
			break;
		case ISCSI_NET_PARAM_VLAN_PRIORITY:
			if (iface->iface_type == ISCSI_IFACE_TYPE_IPV4)
				ival = (ha->ip_config.ipv4_vlan_tag >> 13) &
				       ISCSI_MAX_VLAN_PRIORITY;
			else
				ival = (ha->ip_config.ipv6_vlan_tag >> 13) &
				       ISCSI_MAX_VLAN_PRIORITY;

			len = sprintf(buf, "%d\n", ival);
			break;
		case ISCSI_NET_PARAM_VLAN_ENABLED:
			if (iface->iface_type == ISCSI_IFACE_TYPE_IPV4) {
				OP_STATE(ha->ip_config.ipv4_options,
					 IPOPT_VLAN_TAGGING_ENABLE, pval);
			} else {
				OP_STATE(ha->ip_config.ipv6_options,
					 IPV6_OPT_VLAN_TAGGING_ENABLE, pval);
			}
			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_MTU:
			len = sprintf(buf, "%d\n", ha->ip_config.eth_mtu_size);
			break;
		case ISCSI_NET_PARAM_PORT:
			if (iface->iface_type == ISCSI_IFACE_TYPE_IPV4)
				len = sprintf(buf, "%d\n",
					      ha->ip_config.ipv4_port);
			else
				len = sprintf(buf, "%d\n",
					      ha->ip_config.ipv6_port);
			break;
		case ISCSI_NET_PARAM_IPADDR_STATE:
			if (iface->iface_type == ISCSI_IFACE_TYPE_IPV4) {
				pval = iscsi_get_ipaddress_state_name(
						ha->ip_config.ipv4_addr_state);
			} else {
				if (iface->iface_num == 0)
					pval = iscsi_get_ipaddress_state_name(
						ha->ip_config.ipv6_addr0_state);
				else if (iface->iface_num == 1)
					pval = iscsi_get_ipaddress_state_name(
						ha->ip_config.ipv6_addr1_state);
			}

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_IPV6_LINKLOCAL_STATE:
			pval = iscsi_get_ipaddress_state_name(
					ha->ip_config.ipv6_link_local_state);
			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_IPV6_ROUTER_STATE:
			pval = iscsi_get_router_state_name(
				      ha->ip_config.ipv6_default_router_state);
			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_DELAYED_ACK_EN:
			if (iface->iface_type == ISCSI_IFACE_TYPE_IPV4) {
				OP_STATE(~ha->ip_config.tcp_options,
					 TCPOPT_DELAYED_ACK_DISABLE, pval);
			} else {
				OP_STATE(~ha->ip_config.ipv6_tcp_options,
					 IPV6_TCPOPT_DELAYED_ACK_DISABLE, pval);
			}
			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_TCP_NAGLE_DISABLE:
			if (iface->iface_type == ISCSI_IFACE_TYPE_IPV4) {
				OP_STATE(~ha->ip_config.tcp_options,
					 TCPOPT_NAGLE_ALGO_DISABLE, pval);
			} else {
				OP_STATE(~ha->ip_config.ipv6_tcp_options,
					 IPV6_TCPOPT_NAGLE_ALGO_DISABLE, pval);
			}
			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_TCP_WSF_DISABLE:
			if (iface->iface_type == ISCSI_IFACE_TYPE_IPV4) {
				OP_STATE(~ha->ip_config.tcp_options,
					 TCPOPT_WINDOW_SCALE_DISABLE, pval);
			} else {
				OP_STATE(~ha->ip_config.ipv6_tcp_options,
					 IPV6_TCPOPT_WINDOW_SCALE_DISABLE,
					 pval);
			}
			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_TCP_WSF:
			if (iface->iface_type == ISCSI_IFACE_TYPE_IPV4)
				len = sprintf(buf, "%d\n",
					      ha->ip_config.tcp_wsf);
			else
				len = sprintf(buf, "%d\n",
					      ha->ip_config.ipv6_tcp_wsf);
			break;
		case ISCSI_NET_PARAM_TCP_TIMER_SCALE:
			if (iface->iface_type == ISCSI_IFACE_TYPE_IPV4)
				ival = (ha->ip_config.tcp_options &
					TCPOPT_TIMER_SCALE) >> 1;
			else
				ival = (ha->ip_config.ipv6_tcp_options &
					IPV6_TCPOPT_TIMER_SCALE) >> 1;

			len = sprintf(buf, "%d\n", ival);
			break;
		case ISCSI_NET_PARAM_TCP_TIMESTAMP_EN:
			if (iface->iface_type == ISCSI_IFACE_TYPE_IPV4) {
				OP_STATE(ha->ip_config.tcp_options,
					 TCPOPT_TIMESTAMP_ENABLE, pval);
			} else {
				OP_STATE(ha->ip_config.ipv6_tcp_options,
					 IPV6_TCPOPT_TIMESTAMP_EN, pval);
			}
			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_CACHE_ID:
			if (iface->iface_type == ISCSI_IFACE_TYPE_IPV4)
				len = sprintf(buf, "%d\n",
					      ha->ip_config.ipv4_cache_id);
			else
				len = sprintf(buf, "%d\n",
					      ha->ip_config.ipv6_cache_id);
			break;
		case ISCSI_NET_PARAM_IPV4_DHCP_DNS_ADDR_EN:
			OP_STATE(ha->ip_config.tcp_options,
				 TCPOPT_DNS_SERVER_IP_EN, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_IPV4_DHCP_SLP_DA_EN:
			OP_STATE(ha->ip_config.tcp_options,
				 TCPOPT_SLP_DA_INFO_EN, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_IPV4_TOS_EN:
			OP_STATE(ha->ip_config.ipv4_options,
				 IPOPT_IPV4_TOS_EN, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_IPV4_TOS:
			len = sprintf(buf, "%d\n", ha->ip_config.ipv4_tos);
			break;
		case ISCSI_NET_PARAM_IPV4_GRAT_ARP_EN:
			OP_STATE(ha->ip_config.ipv4_options,
				 IPOPT_GRAT_ARP_EN, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_IPV4_DHCP_ALT_CLIENT_ID_EN:
			OP_STATE(ha->ip_config.ipv4_options, IPOPT_ALT_CID_EN,
				 pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_IPV4_DHCP_ALT_CLIENT_ID:
			pval = (ha->ip_config.ipv4_alt_cid_len) ?
			       (char *)ha->ip_config.ipv4_alt_cid : "";

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_IPV4_DHCP_REQ_VENDOR_ID_EN:
			OP_STATE(ha->ip_config.ipv4_options,
				 IPOPT_REQ_VID_EN, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_IPV4_DHCP_USE_VENDOR_ID_EN:
			OP_STATE(ha->ip_config.ipv4_options,
				 IPOPT_USE_VID_EN, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_IPV4_DHCP_VENDOR_ID:
			pval = (ha->ip_config.ipv4_vid_len) ?
			       (char *)ha->ip_config.ipv4_vid : "";

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_IPV4_DHCP_LEARN_IQN_EN:
			OP_STATE(ha->ip_config.ipv4_options,
				 IPOPT_LEARN_IQN_EN, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_IPV4_FRAGMENT_DISABLE:
			OP_STATE(~ha->ip_config.ipv4_options,
				 IPOPT_FRAGMENTATION_DISABLE, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_IPV4_IN_FORWARD_EN:
			OP_STATE(ha->ip_config.ipv4_options,
				 IPOPT_IN_FORWARD_EN, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_REDIRECT_EN:
			if (iface->iface_type == ISCSI_IFACE_TYPE_IPV4) {
				OP_STATE(ha->ip_config.ipv4_options,
					 IPOPT_ARP_REDIRECT_EN, pval);
			} else {
				OP_STATE(ha->ip_config.ipv6_options,
					 IPV6_OPT_REDIRECT_EN, pval);
			}
			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_IPV4_TTL:
			len = sprintf(buf, "%d\n", ha->ip_config.ipv4_ttl);
			break;
		case ISCSI_NET_PARAM_IPV6_GRAT_NEIGHBOR_ADV_EN:
			OP_STATE(ha->ip_config.ipv6_options,
				 IPV6_OPT_GRAT_NEIGHBOR_ADV_EN, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_IPV6_MLD_EN:
			OP_STATE(ha->ip_config.ipv6_addl_options,
				 IPV6_ADDOPT_MLD_EN, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_NET_PARAM_IPV6_FLOW_LABEL:
			len = sprintf(buf, "%u\n", ha->ip_config.ipv6_flow_lbl);
			break;
		case ISCSI_NET_PARAM_IPV6_TRAFFIC_CLASS:
			len = sprintf(buf, "%d\n",
				      ha->ip_config.ipv6_traffic_class);
			break;
		case ISCSI_NET_PARAM_IPV6_HOP_LIMIT:
			len = sprintf(buf, "%d\n",
				      ha->ip_config.ipv6_hop_limit);
			break;
		case ISCSI_NET_PARAM_IPV6_ND_REACHABLE_TMO:
			len = sprintf(buf, "%d\n",
				      ha->ip_config.ipv6_nd_reach_time);
			break;
		case ISCSI_NET_PARAM_IPV6_ND_REXMIT_TIME:
			len = sprintf(buf, "%d\n",
				      ha->ip_config.ipv6_nd_rexmit_timer);
			break;
		case ISCSI_NET_PARAM_IPV6_ND_STALE_TMO:
			len = sprintf(buf, "%d\n",
				      ha->ip_config.ipv6_nd_stale_timeout);
			break;
		case ISCSI_NET_PARAM_IPV6_DUP_ADDR_DETECT_CNT:
			len = sprintf(buf, "%d\n",
				      ha->ip_config.ipv6_dup_addr_detect_count);
			break;
		case ISCSI_NET_PARAM_IPV6_RTR_ADV_LINK_MTU:
			len = sprintf(buf, "%d\n",
				      ha->ip_config.ipv6_gw_advrt_mtu);
			break;
		default:
			len = -ENOSYS;
		}
	} else if (param_type == ISCSI_IFACE_PARAM) {
		switch (param) {
		case ISCSI_IFACE_PARAM_DEF_TASKMGMT_TMO:
			len = sprintf(buf, "%d\n", ha->ip_config.def_timeout);
			break;
		case ISCSI_IFACE_PARAM_HDRDGST_EN:
			OP_STATE(ha->ip_config.iscsi_options,
				 ISCSIOPTS_HEADER_DIGEST_EN, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_IFACE_PARAM_DATADGST_EN:
			OP_STATE(ha->ip_config.iscsi_options,
				 ISCSIOPTS_DATA_DIGEST_EN, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_IFACE_PARAM_IMM_DATA_EN:
			OP_STATE(ha->ip_config.iscsi_options,
				 ISCSIOPTS_IMMEDIATE_DATA_EN, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_IFACE_PARAM_INITIAL_R2T_EN:
			OP_STATE(ha->ip_config.iscsi_options,
				 ISCSIOPTS_INITIAL_R2T_EN, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_IFACE_PARAM_DATASEQ_INORDER_EN:
			OP_STATE(ha->ip_config.iscsi_options,
				 ISCSIOPTS_DATA_SEQ_INORDER_EN, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_IFACE_PARAM_PDU_INORDER_EN:
			OP_STATE(ha->ip_config.iscsi_options,
				 ISCSIOPTS_DATA_PDU_INORDER_EN, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_IFACE_PARAM_ERL:
			len = sprintf(buf, "%d\n",
				      (ha->ip_config.iscsi_options &
				       ISCSIOPTS_ERL));
			break;
		case ISCSI_IFACE_PARAM_MAX_RECV_DLENGTH:
			len = sprintf(buf, "%u\n",
				      ha->ip_config.iscsi_max_pdu_size *
				      BYTE_UNITS);
			break;
		case ISCSI_IFACE_PARAM_FIRST_BURST:
			len = sprintf(buf, "%u\n",
				      ha->ip_config.iscsi_first_burst_len *
				      BYTE_UNITS);
			break;
		case ISCSI_IFACE_PARAM_MAX_R2T:
			len = sprintf(buf, "%d\n",
				      ha->ip_config.iscsi_max_outstnd_r2t);
			break;
		case ISCSI_IFACE_PARAM_MAX_BURST:
			len = sprintf(buf, "%u\n",
				      ha->ip_config.iscsi_max_burst_len *
				      BYTE_UNITS);
			break;
		case ISCSI_IFACE_PARAM_CHAP_AUTH_EN:
			OP_STATE(ha->ip_config.iscsi_options,
				 ISCSIOPTS_CHAP_AUTH_EN, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_IFACE_PARAM_BIDI_CHAP_EN:
			OP_STATE(ha->ip_config.iscsi_options,
				 ISCSIOPTS_BIDI_CHAP_EN, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_IFACE_PARAM_DISCOVERY_AUTH_OPTIONAL:
			OP_STATE(ha->ip_config.iscsi_options,
				 ISCSIOPTS_DISCOVERY_AUTH_EN, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_IFACE_PARAM_DISCOVERY_LOGOUT_EN:
			OP_STATE(ha->ip_config.iscsi_options,
				 ISCSIOPTS_DISCOVERY_LOGOUT_EN, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_IFACE_PARAM_STRICT_LOGIN_COMP_EN:
			OP_STATE(ha->ip_config.iscsi_options,
				 ISCSIOPTS_STRICT_LOGIN_COMP_EN, pval);

			len = sprintf(buf, "%s\n", pval);
			break;
		case ISCSI_IFACE_PARAM_INITIATOR_NAME:
			len = sprintf(buf, "%s\n", ha->ip_config.iscsi_name);
			break;
		default:
			len = -ENOSYS;
		}
	}

	return len;
}

static struct iscsi_endpoint *
qla4xxx_ep_connect(struct Scsi_Host *shost, struct sockaddr *dst_addr,
		   int non_blocking)
{
	int ret;
	struct iscsi_endpoint *ep;
	struct qla_endpoint *qla_ep;
	struct scsi_qla_host *ha;
	struct sockaddr_in *addr;
	struct sockaddr_in6 *addr6;

	if (!shost) {
		ret = -ENXIO;
		pr_err("%s: shost is NULL\n", __func__);
		return ERR_PTR(ret);
	}

	ha = iscsi_host_priv(shost);
	ep = iscsi_create_endpoint(sizeof(struct qla_endpoint));
	if (!ep) {
		ret = -ENOMEM;
		return ERR_PTR(ret);
	}

	qla_ep = ep->dd_data;
	memset(qla_ep, 0, sizeof(struct qla_endpoint));
	if (dst_addr->sa_family == AF_INET) {
		memcpy(&qla_ep->dst_addr, dst_addr, sizeof(struct sockaddr_in));
		addr = (struct sockaddr_in *)&qla_ep->dst_addr;
		DEBUG2(ql4_printk(KERN_INFO, ha, "%s: %pI4\n", __func__,
				  (char *)&addr->sin_addr));
	} else if (dst_addr->sa_family == AF_INET6) {
		memcpy(&qla_ep->dst_addr, dst_addr,
		       sizeof(struct sockaddr_in6));
		addr6 = (struct sockaddr_in6 *)&qla_ep->dst_addr;
		DEBUG2(ql4_printk(KERN_INFO, ha, "%s: %pI6\n", __func__,
				  (char *)&addr6->sin6_addr));
	} else {
		ql4_printk(KERN_WARNING, ha, "%s: Invalid endpoint\n",
			   __func__);
	}

	qla_ep->host = shost;

	return ep;
}

static int qla4xxx_ep_poll(struct iscsi_endpoint *ep, int timeout_ms)
{
	struct qla_endpoint *qla_ep;
	struct scsi_qla_host *ha;
	int ret = 0;

	qla_ep = ep->dd_data;
	ha = to_qla_host(qla_ep->host);
	DEBUG2(pr_info_ratelimited("%s: host: %ld\n", __func__, ha->host_no));

	if (adapter_up(ha) && !test_bit(AF_BUILD_DDB_LIST, &ha->flags))
		ret = 1;

	return ret;
}

static void qla4xxx_ep_disconnect(struct iscsi_endpoint *ep)
{
	struct qla_endpoint *qla_ep;
	struct scsi_qla_host *ha;

	qla_ep = ep->dd_data;
	ha = to_qla_host(qla_ep->host);
	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: host: %ld\n", __func__,
			  ha->host_no));
	iscsi_destroy_endpoint(ep);
}

static int qla4xxx_get_ep_param(struct iscsi_endpoint *ep,
				enum iscsi_param param,
				char *buf)
{
	struct qla_endpoint *qla_ep = ep->dd_data;
	struct sockaddr *dst_addr;
	struct scsi_qla_host *ha;

	if (!qla_ep)
		return -ENOTCONN;

	ha = to_qla_host(qla_ep->host);
	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: host: %ld\n", __func__,
			  ha->host_no));

	switch (param) {
	case ISCSI_PARAM_CONN_PORT:
	case ISCSI_PARAM_CONN_ADDRESS:
		dst_addr = (struct sockaddr *)&qla_ep->dst_addr;
		if (!dst_addr)
			return -ENOTCONN;

		return iscsi_conn_get_addr_param((struct sockaddr_storage *)
						 &qla_ep->dst_addr, param, buf);
	default:
		return -ENOSYS;
	}
}

static void qla4xxx_conn_get_stats(struct iscsi_cls_conn *cls_conn,
				   struct iscsi_stats *stats)
{
	struct iscsi_session *sess;
	struct iscsi_cls_session *cls_sess;
	struct ddb_entry *ddb_entry;
	struct scsi_qla_host *ha;
	struct ql_iscsi_stats *ql_iscsi_stats;
	int stats_size;
	int ret;
	dma_addr_t iscsi_stats_dma;

	cls_sess = iscsi_conn_to_session(cls_conn);
	sess = cls_sess->dd_data;
	ddb_entry = sess->dd_data;
	ha = ddb_entry->ha;

	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: host: %ld\n", __func__,
			  ha->host_no));
	stats_size = PAGE_ALIGN(sizeof(struct ql_iscsi_stats));
	/* Allocate memory */
	ql_iscsi_stats = dma_alloc_coherent(&ha->pdev->dev, stats_size,
					    &iscsi_stats_dma, GFP_KERNEL);
	if (!ql_iscsi_stats) {
		ql4_printk(KERN_ERR, ha,
			   "Unable to allocate memory for iscsi stats\n");
		goto exit_get_stats;
	}

	ret =  qla4xxx_get_mgmt_data(ha, ddb_entry->fw_ddb_index, stats_size,
				     iscsi_stats_dma);
	if (ret != QLA_SUCCESS) {
		ql4_printk(KERN_ERR, ha,
			   "Unable to retrieve iscsi stats\n");
		goto free_stats;
	}

	/* octets */
	stats->txdata_octets = le64_to_cpu(ql_iscsi_stats->tx_data_octets);
	stats->rxdata_octets = le64_to_cpu(ql_iscsi_stats->rx_data_octets);
	/* xmit pdus */
	stats->noptx_pdus = le32_to_cpu(ql_iscsi_stats->tx_nopout_pdus);
	stats->scsicmd_pdus = le32_to_cpu(ql_iscsi_stats->tx_scsi_cmd_pdus);
	stats->tmfcmd_pdus = le32_to_cpu(ql_iscsi_stats->tx_tmf_cmd_pdus);
	stats->login_pdus = le32_to_cpu(ql_iscsi_stats->tx_login_cmd_pdus);
	stats->text_pdus = le32_to_cpu(ql_iscsi_stats->tx_text_cmd_pdus);
	stats->dataout_pdus = le32_to_cpu(ql_iscsi_stats->tx_scsi_write_pdus);
	stats->logout_pdus = le32_to_cpu(ql_iscsi_stats->tx_logout_cmd_pdus);
	stats->snack_pdus = le32_to_cpu(ql_iscsi_stats->tx_snack_req_pdus);
	/* recv pdus */
	stats->noprx_pdus = le32_to_cpu(ql_iscsi_stats->rx_nopin_pdus);
	stats->scsirsp_pdus = le32_to_cpu(ql_iscsi_stats->rx_scsi_resp_pdus);
	stats->tmfrsp_pdus = le32_to_cpu(ql_iscsi_stats->rx_tmf_resp_pdus);
	stats->textrsp_pdus = le32_to_cpu(ql_iscsi_stats->rx_text_resp_pdus);
	stats->datain_pdus = le32_to_cpu(ql_iscsi_stats->rx_scsi_read_pdus);
	stats->logoutrsp_pdus =
			le32_to_cpu(ql_iscsi_stats->rx_logout_resp_pdus);
	stats->r2t_pdus = le32_to_cpu(ql_iscsi_stats->rx_r2t_pdus);
	stats->async_pdus = le32_to_cpu(ql_iscsi_stats->rx_async_pdus);
	stats->rjt_pdus = le32_to_cpu(ql_iscsi_stats->rx_reject_pdus);

free_stats:
	dma_free_coherent(&ha->pdev->dev, stats_size, ql_iscsi_stats,
			  iscsi_stats_dma);
exit_get_stats:
	return;
}

static enum blk_eh_timer_return qla4xxx_eh_cmd_timed_out(struct scsi_cmnd *sc)
{
	struct iscsi_cls_session *session;
	unsigned long flags;
	enum blk_eh_timer_return ret = BLK_EH_DONE;

	session = starget_to_session(scsi_target(sc->device));

	spin_lock_irqsave(&session->lock, flags);
	if (session->state == ISCSI_SESSION_FAILED)
		ret = BLK_EH_RESET_TIMER;
	spin_unlock_irqrestore(&session->lock, flags);

	return ret;
}

static void qla4xxx_set_port_speed(struct Scsi_Host *shost)
{
	struct scsi_qla_host *ha = to_qla_host(shost);
	struct iscsi_cls_host *ihost = shost->shost_data;
	uint32_t speed = ISCSI_PORT_SPEED_UNKNOWN;

	qla4xxx_get_firmware_state(ha);

	switch (ha->addl_fw_state & 0x0F00) {
	case FW_ADDSTATE_LINK_SPEED_10MBPS:
		speed = ISCSI_PORT_SPEED_10MBPS;
		break;
	case FW_ADDSTATE_LINK_SPEED_100MBPS:
		speed = ISCSI_PORT_SPEED_100MBPS;
		break;
	case FW_ADDSTATE_LINK_SPEED_1GBPS:
		speed = ISCSI_PORT_SPEED_1GBPS;
		break;
	case FW_ADDSTATE_LINK_SPEED_10GBPS:
		speed = ISCSI_PORT_SPEED_10GBPS;
		break;
	}
	ihost->port_speed = speed;
}

static void qla4xxx_set_port_state(struct Scsi_Host *shost)
{
	struct scsi_qla_host *ha = to_qla_host(shost);
	struct iscsi_cls_host *ihost = shost->shost_data;
	uint32_t state = ISCSI_PORT_STATE_DOWN;

	if (test_bit(AF_LINK_UP, &ha->flags))
		state = ISCSI_PORT_STATE_UP;

	ihost->port_state = state;
}

static int qla4xxx_host_get_param(struct Scsi_Host *shost,
				  enum iscsi_host_param param, char *buf)
{
	struct scsi_qla_host *ha = to_qla_host(shost);
	int len;

	switch (param) {
	case ISCSI_HOST_PARAM_HWADDRESS:
		len = sysfs_format_mac(buf, ha->my_mac, MAC_ADDR_LEN);
		break;
	case ISCSI_HOST_PARAM_IPADDRESS:
		len = sprintf(buf, "%pI4\n", &ha->ip_config.ip_address);
		break;
	case ISCSI_HOST_PARAM_INITIATOR_NAME:
		len = sprintf(buf, "%s\n", ha->name_string);
		break;
	case ISCSI_HOST_PARAM_PORT_STATE:
		qla4xxx_set_port_state(shost);
		len = sprintf(buf, "%s\n", iscsi_get_port_state_name(shost));
		break;
	case ISCSI_HOST_PARAM_PORT_SPEED:
		qla4xxx_set_port_speed(shost);
		len = sprintf(buf, "%s\n", iscsi_get_port_speed_name(shost));
		break;
	default:
		return -ENOSYS;
	}

	return len;
}

static void qla4xxx_create_ipv4_iface(struct scsi_qla_host *ha)
{
	if (ha->iface_ipv4)
		return;

	/* IPv4 */
	ha->iface_ipv4 = iscsi_create_iface(ha->host,
					    &qla4xxx_iscsi_transport,
					    ISCSI_IFACE_TYPE_IPV4, 0, 0);
	if (!ha->iface_ipv4)
		ql4_printk(KERN_ERR, ha, "Could not create IPv4 iSCSI "
			   "iface0.\n");
}

static void qla4xxx_create_ipv6_iface(struct scsi_qla_host *ha)
{
	if (!ha->iface_ipv6_0)
		/* IPv6 iface-0 */
		ha->iface_ipv6_0 = iscsi_create_iface(ha->host,
						      &qla4xxx_iscsi_transport,
						      ISCSI_IFACE_TYPE_IPV6, 0,
						      0);
	if (!ha->iface_ipv6_0)
		ql4_printk(KERN_ERR, ha, "Could not create IPv6 iSCSI "
			   "iface0.\n");

	if (!ha->iface_ipv6_1)
		/* IPv6 iface-1 */
		ha->iface_ipv6_1 = iscsi_create_iface(ha->host,
						      &qla4xxx_iscsi_transport,
						      ISCSI_IFACE_TYPE_IPV6, 1,
						      0);
	if (!ha->iface_ipv6_1)
		ql4_printk(KERN_ERR, ha, "Could not create IPv6 iSCSI "
			   "iface1.\n");
}

static void qla4xxx_create_ifaces(struct scsi_qla_host *ha)
{
	if (ha->ip_config.ipv4_options & IPOPT_IPV4_PROTOCOL_ENABLE)
		qla4xxx_create_ipv4_iface(ha);

	if (ha->ip_config.ipv6_options & IPV6_OPT_IPV6_PROTOCOL_ENABLE)
		qla4xxx_create_ipv6_iface(ha);
}

static void qla4xxx_destroy_ipv4_iface(struct scsi_qla_host *ha)
{
	if (ha->iface_ipv4) {
		iscsi_destroy_iface(ha->iface_ipv4);
		ha->iface_ipv4 = NULL;
	}
}

static void qla4xxx_destroy_ipv6_iface(struct scsi_qla_host *ha)
{
	if (ha->iface_ipv6_0) {
		iscsi_destroy_iface(ha->iface_ipv6_0);
		ha->iface_ipv6_0 = NULL;
	}
	if (ha->iface_ipv6_1) {
		iscsi_destroy_iface(ha->iface_ipv6_1);
		ha->iface_ipv6_1 = NULL;
	}
}

static void qla4xxx_destroy_ifaces(struct scsi_qla_host *ha)
{
	qla4xxx_destroy_ipv4_iface(ha);
	qla4xxx_destroy_ipv6_iface(ha);
}

static void qla4xxx_set_ipv6(struct scsi_qla_host *ha,
			     struct iscsi_iface_param_info *iface_param,
			     struct addr_ctrl_blk *init_fw_cb)
{
	/*
	 * iface_num 0 is valid for IPv6 Addr, linklocal, router, autocfg.
	 * iface_num 1 is valid only for IPv6 Addr.
	 */
	switch (iface_param->param) {
	case ISCSI_NET_PARAM_IPV6_ADDR:
		if (iface_param->iface_num & 0x1)
			/* IPv6 Addr 1 */
			memcpy(init_fw_cb->ipv6_addr1, iface_param->value,
			       sizeof(init_fw_cb->ipv6_addr1));
		else
			/* IPv6 Addr 0 */
			memcpy(init_fw_cb->ipv6_addr0, iface_param->value,
			       sizeof(init_fw_cb->ipv6_addr0));
		break;
	case ISCSI_NET_PARAM_IPV6_LINKLOCAL:
		if (iface_param->iface_num & 0x1)
			break;
		memcpy(init_fw_cb->ipv6_if_id, &iface_param->value[8],
		       sizeof(init_fw_cb->ipv6_if_id));
		break;
	case ISCSI_NET_PARAM_IPV6_ROUTER:
		if (iface_param->iface_num & 0x1)
			break;
		memcpy(init_fw_cb->ipv6_dflt_rtr_addr, iface_param->value,
		       sizeof(init_fw_cb->ipv6_dflt_rtr_addr));
		break;
	case ISCSI_NET_PARAM_IPV6_ADDR_AUTOCFG:
		/* Autocfg applies to even interface */
		if (iface_param->iface_num & 0x1)
			break;

		if (iface_param->value[0] == ISCSI_IPV6_AUTOCFG_DISABLE)
			init_fw_cb->ipv6_addtl_opts &=
				cpu_to_le16(
				  ~IPV6_ADDOPT_NEIGHBOR_DISCOVERY_ADDR_ENABLE);
		else if (iface_param->value[0] == ISCSI_IPV6_AUTOCFG_ND_ENABLE)
			init_fw_cb->ipv6_addtl_opts |=
				cpu_to_le16(
				  IPV6_ADDOPT_NEIGHBOR_DISCOVERY_ADDR_ENABLE);
		else
			ql4_printk(KERN_ERR, ha,
				   "Invalid autocfg setting for IPv6 addr\n");
		break;
	case ISCSI_NET_PARAM_IPV6_LINKLOCAL_AUTOCFG:
		/* Autocfg applies to even interface */
		if (iface_param->iface_num & 0x1)
			break;

		if (iface_param->value[0] ==
		    ISCSI_IPV6_LINKLOCAL_AUTOCFG_ENABLE)
			init_fw_cb->ipv6_addtl_opts |= cpu_to_le16(
					IPV6_ADDOPT_AUTOCONFIG_LINK_LOCAL_ADDR);
		else if (iface_param->value[0] ==
			 ISCSI_IPV6_LINKLOCAL_AUTOCFG_DISABLE)
			init_fw_cb->ipv6_addtl_opts &= cpu_to_le16(
				       ~IPV6_ADDOPT_AUTOCONFIG_LINK_LOCAL_ADDR);
		else
			ql4_printk(KERN_ERR, ha,
				   "Invalid autocfg setting for IPv6 linklocal addr\n");
		break;
	case ISCSI_NET_PARAM_IPV6_ROUTER_AUTOCFG:
		/* Autocfg applies to even interface */
		if (iface_param->iface_num & 0x1)
			break;

		if (iface_param->value[0] == ISCSI_IPV6_ROUTER_AUTOCFG_ENABLE)
			memset(init_fw_cb->ipv6_dflt_rtr_addr, 0,
			       sizeof(init_fw_cb->ipv6_dflt_rtr_addr));
		break;
	case ISCSI_NET_PARAM_IFACE_ENABLE:
		if (iface_param->value[0] == ISCSI_IFACE_ENABLE) {
			init_fw_cb->ipv6_opts |=
				cpu_to_le16(IPV6_OPT_IPV6_PROTOCOL_ENABLE);
			qla4xxx_create_ipv6_iface(ha);
		} else {
			init_fw_cb->ipv6_opts &=
				cpu_to_le16(~IPV6_OPT_IPV6_PROTOCOL_ENABLE &
					    0xFFFF);
			qla4xxx_destroy_ipv6_iface(ha);
		}
		break;
	case ISCSI_NET_PARAM_VLAN_TAG:
		if (iface_param->len != sizeof(init_fw_cb->ipv6_vlan_tag))
			break;
		init_fw_cb->ipv6_vlan_tag =
				cpu_to_be16(*(uint16_t *)iface_param->value);
		break;
	case ISCSI_NET_PARAM_VLAN_ENABLED:
		if (iface_param->value[0] == ISCSI_VLAN_ENABLE)
			init_fw_cb->ipv6_opts |=
				cpu_to_le16(IPV6_OPT_VLAN_TAGGING_ENABLE);
		else
			init_fw_cb->ipv6_opts &=
				cpu_to_le16(~IPV6_OPT_VLAN_TAGGING_ENABLE);
		break;
	case ISCSI_NET_PARAM_MTU:
		init_fw_cb->eth_mtu_size =
				cpu_to_le16(*(uint16_t *)iface_param->value);
		break;
	case ISCSI_NET_PARAM_PORT:
		/* Autocfg applies to even interface */
		if (iface_param->iface_num & 0x1)
			break;

		init_fw_cb->ipv6_port =
				cpu_to_le16(*(uint16_t *)iface_param->value);
		break;
	case ISCSI_NET_PARAM_DELAYED_ACK_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_DISABLE)
			init_fw_cb->ipv6_tcp_opts |=
				cpu_to_le16(IPV6_TCPOPT_DELAYED_ACK_DISABLE);
		else
			init_fw_cb->ipv6_tcp_opts &=
				cpu_to_le16(~IPV6_TCPOPT_DELAYED_ACK_DISABLE &
					    0xFFFF);
		break;
	case ISCSI_NET_PARAM_TCP_NAGLE_DISABLE:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_DISABLE)
			init_fw_cb->ipv6_tcp_opts |=
				cpu_to_le16(IPV6_TCPOPT_NAGLE_ALGO_DISABLE);
		else
			init_fw_cb->ipv6_tcp_opts &=
				cpu_to_le16(~IPV6_TCPOPT_NAGLE_ALGO_DISABLE);
		break;
	case ISCSI_NET_PARAM_TCP_WSF_DISABLE:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_DISABLE)
			init_fw_cb->ipv6_tcp_opts |=
				cpu_to_le16(IPV6_TCPOPT_WINDOW_SCALE_DISABLE);
		else
			init_fw_cb->ipv6_tcp_opts &=
				cpu_to_le16(~IPV6_TCPOPT_WINDOW_SCALE_DISABLE);
		break;
	case ISCSI_NET_PARAM_TCP_WSF:
		if (iface_param->iface_num & 0x1)
			break;
		init_fw_cb->ipv6_tcp_wsf = iface_param->value[0];
		break;
	case ISCSI_NET_PARAM_TCP_TIMER_SCALE:
		if (iface_param->iface_num & 0x1)
			break;
		init_fw_cb->ipv6_tcp_opts &=
					cpu_to_le16(~IPV6_TCPOPT_TIMER_SCALE);
		init_fw_cb->ipv6_tcp_opts |=
				cpu_to_le16((iface_param->value[0] << 1) &
					    IPV6_TCPOPT_TIMER_SCALE);
		break;
	case ISCSI_NET_PARAM_TCP_TIMESTAMP_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->ipv6_tcp_opts |=
				cpu_to_le16(IPV6_TCPOPT_TIMESTAMP_EN);
		else
			init_fw_cb->ipv6_tcp_opts &=
				cpu_to_le16(~IPV6_TCPOPT_TIMESTAMP_EN);
		break;
	case ISCSI_NET_PARAM_IPV6_GRAT_NEIGHBOR_ADV_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->ipv6_opts |=
				cpu_to_le16(IPV6_OPT_GRAT_NEIGHBOR_ADV_EN);
		else
			init_fw_cb->ipv6_opts &=
				cpu_to_le16(~IPV6_OPT_GRAT_NEIGHBOR_ADV_EN);
		break;
	case ISCSI_NET_PARAM_REDIRECT_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->ipv6_opts |=
				cpu_to_le16(IPV6_OPT_REDIRECT_EN);
		else
			init_fw_cb->ipv6_opts &=
				cpu_to_le16(~IPV6_OPT_REDIRECT_EN);
		break;
	case ISCSI_NET_PARAM_IPV6_MLD_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->ipv6_addtl_opts |=
				cpu_to_le16(IPV6_ADDOPT_MLD_EN);
		else
			init_fw_cb->ipv6_addtl_opts &=
				cpu_to_le16(~IPV6_ADDOPT_MLD_EN);
		break;
	case ISCSI_NET_PARAM_IPV6_FLOW_LABEL:
		if (iface_param->iface_num & 0x1)
			break;
		init_fw_cb->ipv6_flow_lbl =
				cpu_to_le16(*(uint16_t *)iface_param->value);
		break;
	case ISCSI_NET_PARAM_IPV6_TRAFFIC_CLASS:
		if (iface_param->iface_num & 0x1)
			break;
		init_fw_cb->ipv6_traffic_class = iface_param->value[0];
		break;
	case ISCSI_NET_PARAM_IPV6_HOP_LIMIT:
		if (iface_param->iface_num & 0x1)
			break;
		init_fw_cb->ipv6_hop_limit = iface_param->value[0];
		break;
	case ISCSI_NET_PARAM_IPV6_ND_REACHABLE_TMO:
		if (iface_param->iface_num & 0x1)
			break;
		init_fw_cb->ipv6_nd_reach_time =
				cpu_to_le32(*(uint32_t *)iface_param->value);
		break;
	case ISCSI_NET_PARAM_IPV6_ND_REXMIT_TIME:
		if (iface_param->iface_num & 0x1)
			break;
		init_fw_cb->ipv6_nd_rexmit_timer =
				cpu_to_le32(*(uint32_t *)iface_param->value);
		break;
	case ISCSI_NET_PARAM_IPV6_ND_STALE_TMO:
		if (iface_param->iface_num & 0x1)
			break;
		init_fw_cb->ipv6_nd_stale_timeout =
				cpu_to_le32(*(uint32_t *)iface_param->value);
		break;
	case ISCSI_NET_PARAM_IPV6_DUP_ADDR_DETECT_CNT:
		if (iface_param->iface_num & 0x1)
			break;
		init_fw_cb->ipv6_dup_addr_detect_count = iface_param->value[0];
		break;
	case ISCSI_NET_PARAM_IPV6_RTR_ADV_LINK_MTU:
		if (iface_param->iface_num & 0x1)
			break;
		init_fw_cb->ipv6_gw_advrt_mtu =
				cpu_to_le32(*(uint32_t *)iface_param->value);
		break;
	default:
		ql4_printk(KERN_ERR, ha, "Unknown IPv6 param = %d\n",
			   iface_param->param);
		break;
	}
}

static void qla4xxx_set_ipv4(struct scsi_qla_host *ha,
			     struct iscsi_iface_param_info *iface_param,
			     struct addr_ctrl_blk *init_fw_cb)
{
	switch (iface_param->param) {
	case ISCSI_NET_PARAM_IPV4_ADDR:
		memcpy(init_fw_cb->ipv4_addr, iface_param->value,
		       sizeof(init_fw_cb->ipv4_addr));
		break;
	case ISCSI_NET_PARAM_IPV4_SUBNET:
		memcpy(init_fw_cb->ipv4_subnet,	iface_param->value,
		       sizeof(init_fw_cb->ipv4_subnet));
		break;
	case ISCSI_NET_PARAM_IPV4_GW:
		memcpy(init_fw_cb->ipv4_gw_addr, iface_param->value,
		       sizeof(init_fw_cb->ipv4_gw_addr));
		break;
	case ISCSI_NET_PARAM_IPV4_BOOTPROTO:
		if (iface_param->value[0] == ISCSI_BOOTPROTO_DHCP)
			init_fw_cb->ipv4_tcp_opts |=
					cpu_to_le16(TCPOPT_DHCP_ENABLE);
		else if (iface_param->value[0] == ISCSI_BOOTPROTO_STATIC)
			init_fw_cb->ipv4_tcp_opts &=
					cpu_to_le16(~TCPOPT_DHCP_ENABLE);
		else
			ql4_printk(KERN_ERR, ha, "Invalid IPv4 bootproto\n");
		break;
	case ISCSI_NET_PARAM_IFACE_ENABLE:
		if (iface_param->value[0] == ISCSI_IFACE_ENABLE) {
			init_fw_cb->ipv4_ip_opts |=
				cpu_to_le16(IPOPT_IPV4_PROTOCOL_ENABLE);
			qla4xxx_create_ipv4_iface(ha);
		} else {
			init_fw_cb->ipv4_ip_opts &=
				cpu_to_le16(~IPOPT_IPV4_PROTOCOL_ENABLE &
					    0xFFFF);
			qla4xxx_destroy_ipv4_iface(ha);
		}
		break;
	case ISCSI_NET_PARAM_VLAN_TAG:
		if (iface_param->len != sizeof(init_fw_cb->ipv4_vlan_tag))
			break;
		init_fw_cb->ipv4_vlan_tag =
				cpu_to_be16(*(uint16_t *)iface_param->value);
		break;
	case ISCSI_NET_PARAM_VLAN_ENABLED:
		if (iface_param->value[0] == ISCSI_VLAN_ENABLE)
			init_fw_cb->ipv4_ip_opts |=
					cpu_to_le16(IPOPT_VLAN_TAGGING_ENABLE);
		else
			init_fw_cb->ipv4_ip_opts &=
					cpu_to_le16(~IPOPT_VLAN_TAGGING_ENABLE);
		break;
	case ISCSI_NET_PARAM_MTU:
		init_fw_cb->eth_mtu_size =
				cpu_to_le16(*(uint16_t *)iface_param->value);
		break;
	case ISCSI_NET_PARAM_PORT:
		init_fw_cb->ipv4_port =
				cpu_to_le16(*(uint16_t *)iface_param->value);
		break;
	case ISCSI_NET_PARAM_DELAYED_ACK_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_DISABLE)
			init_fw_cb->ipv4_tcp_opts |=
				cpu_to_le16(TCPOPT_DELAYED_ACK_DISABLE);
		else
			init_fw_cb->ipv4_tcp_opts &=
				cpu_to_le16(~TCPOPT_DELAYED_ACK_DISABLE &
					    0xFFFF);
		break;
	case ISCSI_NET_PARAM_TCP_NAGLE_DISABLE:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_DISABLE)
			init_fw_cb->ipv4_tcp_opts |=
				cpu_to_le16(TCPOPT_NAGLE_ALGO_DISABLE);
		else
			init_fw_cb->ipv4_tcp_opts &=
				cpu_to_le16(~TCPOPT_NAGLE_ALGO_DISABLE);
		break;
	case ISCSI_NET_PARAM_TCP_WSF_DISABLE:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_DISABLE)
			init_fw_cb->ipv4_tcp_opts |=
				cpu_to_le16(TCPOPT_WINDOW_SCALE_DISABLE);
		else
			init_fw_cb->ipv4_tcp_opts &=
				cpu_to_le16(~TCPOPT_WINDOW_SCALE_DISABLE);
		break;
	case ISCSI_NET_PARAM_TCP_WSF:
		if (iface_param->iface_num & 0x1)
			break;
		init_fw_cb->ipv4_tcp_wsf = iface_param->value[0];
		break;
	case ISCSI_NET_PARAM_TCP_TIMER_SCALE:
		if (iface_param->iface_num & 0x1)
			break;
		init_fw_cb->ipv4_tcp_opts &= cpu_to_le16(~TCPOPT_TIMER_SCALE);
		init_fw_cb->ipv4_tcp_opts |=
				cpu_to_le16((iface_param->value[0] << 1) &
					    TCPOPT_TIMER_SCALE);
		break;
	case ISCSI_NET_PARAM_TCP_TIMESTAMP_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->ipv4_tcp_opts |=
				cpu_to_le16(TCPOPT_TIMESTAMP_ENABLE);
		else
			init_fw_cb->ipv4_tcp_opts &=
				cpu_to_le16(~TCPOPT_TIMESTAMP_ENABLE);
		break;
	case ISCSI_NET_PARAM_IPV4_DHCP_DNS_ADDR_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->ipv4_tcp_opts |=
				cpu_to_le16(TCPOPT_DNS_SERVER_IP_EN);
		else
			init_fw_cb->ipv4_tcp_opts &=
				cpu_to_le16(~TCPOPT_DNS_SERVER_IP_EN);
		break;
	case ISCSI_NET_PARAM_IPV4_DHCP_SLP_DA_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->ipv4_tcp_opts |=
				cpu_to_le16(TCPOPT_SLP_DA_INFO_EN);
		else
			init_fw_cb->ipv4_tcp_opts &=
				cpu_to_le16(~TCPOPT_SLP_DA_INFO_EN);
		break;
	case ISCSI_NET_PARAM_IPV4_TOS_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->ipv4_ip_opts |=
				cpu_to_le16(IPOPT_IPV4_TOS_EN);
		else
			init_fw_cb->ipv4_ip_opts &=
				cpu_to_le16(~IPOPT_IPV4_TOS_EN);
		break;
	case ISCSI_NET_PARAM_IPV4_TOS:
		if (iface_param->iface_num & 0x1)
			break;
		init_fw_cb->ipv4_tos = iface_param->value[0];
		break;
	case ISCSI_NET_PARAM_IPV4_GRAT_ARP_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->ipv4_ip_opts |=
					cpu_to_le16(IPOPT_GRAT_ARP_EN);
		else
			init_fw_cb->ipv4_ip_opts &=
					cpu_to_le16(~IPOPT_GRAT_ARP_EN);
		break;
	case ISCSI_NET_PARAM_IPV4_DHCP_ALT_CLIENT_ID_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->ipv4_ip_opts |=
				cpu_to_le16(IPOPT_ALT_CID_EN);
		else
			init_fw_cb->ipv4_ip_opts &=
				cpu_to_le16(~IPOPT_ALT_CID_EN);
		break;
	case ISCSI_NET_PARAM_IPV4_DHCP_ALT_CLIENT_ID:
		if (iface_param->iface_num & 0x1)
			break;
		memcpy(init_fw_cb->ipv4_dhcp_alt_cid, iface_param->value,
		       (sizeof(init_fw_cb->ipv4_dhcp_alt_cid) - 1));
		init_fw_cb->ipv4_dhcp_alt_cid_len =
					strlen(init_fw_cb->ipv4_dhcp_alt_cid);
		break;
	case ISCSI_NET_PARAM_IPV4_DHCP_REQ_VENDOR_ID_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->ipv4_ip_opts |=
					cpu_to_le16(IPOPT_REQ_VID_EN);
		else
			init_fw_cb->ipv4_ip_opts &=
					cpu_to_le16(~IPOPT_REQ_VID_EN);
		break;
	case ISCSI_NET_PARAM_IPV4_DHCP_USE_VENDOR_ID_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->ipv4_ip_opts |=
					cpu_to_le16(IPOPT_USE_VID_EN);
		else
			init_fw_cb->ipv4_ip_opts &=
					cpu_to_le16(~IPOPT_USE_VID_EN);
		break;
	case ISCSI_NET_PARAM_IPV4_DHCP_VENDOR_ID:
		if (iface_param->iface_num & 0x1)
			break;
		memcpy(init_fw_cb->ipv4_dhcp_vid, iface_param->value,
		       (sizeof(init_fw_cb->ipv4_dhcp_vid) - 1));
		init_fw_cb->ipv4_dhcp_vid_len =
					strlen(init_fw_cb->ipv4_dhcp_vid);
		break;
	case ISCSI_NET_PARAM_IPV4_DHCP_LEARN_IQN_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->ipv4_ip_opts |=
					cpu_to_le16(IPOPT_LEARN_IQN_EN);
		else
			init_fw_cb->ipv4_ip_opts &=
					cpu_to_le16(~IPOPT_LEARN_IQN_EN);
		break;
	case ISCSI_NET_PARAM_IPV4_FRAGMENT_DISABLE:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_DISABLE)
			init_fw_cb->ipv4_ip_opts |=
				cpu_to_le16(IPOPT_FRAGMENTATION_DISABLE);
		else
			init_fw_cb->ipv4_ip_opts &=
				cpu_to_le16(~IPOPT_FRAGMENTATION_DISABLE);
		break;
	case ISCSI_NET_PARAM_IPV4_IN_FORWARD_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->ipv4_ip_opts |=
				cpu_to_le16(IPOPT_IN_FORWARD_EN);
		else
			init_fw_cb->ipv4_ip_opts &=
				cpu_to_le16(~IPOPT_IN_FORWARD_EN);
		break;
	case ISCSI_NET_PARAM_REDIRECT_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->ipv4_ip_opts |=
				cpu_to_le16(IPOPT_ARP_REDIRECT_EN);
		else
			init_fw_cb->ipv4_ip_opts &=
				cpu_to_le16(~IPOPT_ARP_REDIRECT_EN);
		break;
	case ISCSI_NET_PARAM_IPV4_TTL:
		if (iface_param->iface_num & 0x1)
			break;
		init_fw_cb->ipv4_ttl = iface_param->value[0];
		break;
	default:
		ql4_printk(KERN_ERR, ha, "Unknown IPv4 param = %d\n",
			   iface_param->param);
		break;
	}
}

static void qla4xxx_set_iscsi_param(struct scsi_qla_host *ha,
				    struct iscsi_iface_param_info *iface_param,
				    struct addr_ctrl_blk *init_fw_cb)
{
	switch (iface_param->param) {
	case ISCSI_IFACE_PARAM_DEF_TASKMGMT_TMO:
		if (iface_param->iface_num & 0x1)
			break;
		init_fw_cb->def_timeout =
				cpu_to_le16(*(uint16_t *)iface_param->value);
		break;
	case ISCSI_IFACE_PARAM_HDRDGST_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->iscsi_opts |=
				cpu_to_le16(ISCSIOPTS_HEADER_DIGEST_EN);
		else
			init_fw_cb->iscsi_opts &=
				cpu_to_le16(~ISCSIOPTS_HEADER_DIGEST_EN);
		break;
	case ISCSI_IFACE_PARAM_DATADGST_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->iscsi_opts |=
				cpu_to_le16(ISCSIOPTS_DATA_DIGEST_EN);
		else
			init_fw_cb->iscsi_opts &=
				cpu_to_le16(~ISCSIOPTS_DATA_DIGEST_EN);
		break;
	case ISCSI_IFACE_PARAM_IMM_DATA_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->iscsi_opts |=
				cpu_to_le16(ISCSIOPTS_IMMEDIATE_DATA_EN);
		else
			init_fw_cb->iscsi_opts &=
				cpu_to_le16(~ISCSIOPTS_IMMEDIATE_DATA_EN);
		break;
	case ISCSI_IFACE_PARAM_INITIAL_R2T_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->iscsi_opts |=
				cpu_to_le16(ISCSIOPTS_INITIAL_R2T_EN);
		else
			init_fw_cb->iscsi_opts &=
				cpu_to_le16(~ISCSIOPTS_INITIAL_R2T_EN);
		break;
	case ISCSI_IFACE_PARAM_DATASEQ_INORDER_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->iscsi_opts |=
				cpu_to_le16(ISCSIOPTS_DATA_SEQ_INORDER_EN);
		else
			init_fw_cb->iscsi_opts &=
				cpu_to_le16(~ISCSIOPTS_DATA_SEQ_INORDER_EN);
		break;
	case ISCSI_IFACE_PARAM_PDU_INORDER_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->iscsi_opts |=
				cpu_to_le16(ISCSIOPTS_DATA_PDU_INORDER_EN);
		else
			init_fw_cb->iscsi_opts &=
				cpu_to_le16(~ISCSIOPTS_DATA_PDU_INORDER_EN);
		break;
	case ISCSI_IFACE_PARAM_ERL:
		if (iface_param->iface_num & 0x1)
			break;
		init_fw_cb->iscsi_opts &= cpu_to_le16(~ISCSIOPTS_ERL);
		init_fw_cb->iscsi_opts |= cpu_to_le16(iface_param->value[0] &
						      ISCSIOPTS_ERL);
		break;
	case ISCSI_IFACE_PARAM_MAX_RECV_DLENGTH:
		if (iface_param->iface_num & 0x1)
			break;
		init_fw_cb->iscsi_max_pdu_size =
				cpu_to_le32(*(uint32_t *)iface_param->value) /
				BYTE_UNITS;
		break;
	case ISCSI_IFACE_PARAM_FIRST_BURST:
		if (iface_param->iface_num & 0x1)
			break;
		init_fw_cb->iscsi_fburst_len =
				cpu_to_le32(*(uint32_t *)iface_param->value) /
				BYTE_UNITS;
		break;
	case ISCSI_IFACE_PARAM_MAX_R2T:
		if (iface_param->iface_num & 0x1)
			break;
		init_fw_cb->iscsi_max_outstnd_r2t =
				cpu_to_le16(*(uint16_t *)iface_param->value);
		break;
	case ISCSI_IFACE_PARAM_MAX_BURST:
		if (iface_param->iface_num & 0x1)
			break;
		init_fw_cb->iscsi_max_burst_len =
				cpu_to_le32(*(uint32_t *)iface_param->value) /
				BYTE_UNITS;
		break;
	case ISCSI_IFACE_PARAM_CHAP_AUTH_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->iscsi_opts |=
				cpu_to_le16(ISCSIOPTS_CHAP_AUTH_EN);
		else
			init_fw_cb->iscsi_opts &=
				cpu_to_le16(~ISCSIOPTS_CHAP_AUTH_EN);
		break;
	case ISCSI_IFACE_PARAM_BIDI_CHAP_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->iscsi_opts |=
				cpu_to_le16(ISCSIOPTS_BIDI_CHAP_EN);
		else
			init_fw_cb->iscsi_opts &=
				cpu_to_le16(~ISCSIOPTS_BIDI_CHAP_EN);
		break;
	case ISCSI_IFACE_PARAM_DISCOVERY_AUTH_OPTIONAL:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->iscsi_opts |=
				cpu_to_le16(ISCSIOPTS_DISCOVERY_AUTH_EN);
		else
			init_fw_cb->iscsi_opts &=
				cpu_to_le16(~ISCSIOPTS_DISCOVERY_AUTH_EN);
		break;
	case ISCSI_IFACE_PARAM_DISCOVERY_LOGOUT_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->iscsi_opts |=
				cpu_to_le16(ISCSIOPTS_DISCOVERY_LOGOUT_EN);
		else
			init_fw_cb->iscsi_opts &=
				cpu_to_le16(~ISCSIOPTS_DISCOVERY_LOGOUT_EN);
		break;
	case ISCSI_IFACE_PARAM_STRICT_LOGIN_COMP_EN:
		if (iface_param->iface_num & 0x1)
			break;
		if (iface_param->value[0] == ISCSI_NET_PARAM_ENABLE)
			init_fw_cb->iscsi_opts |=
				cpu_to_le16(ISCSIOPTS_STRICT_LOGIN_COMP_EN);
		else
			init_fw_cb->iscsi_opts &=
				cpu_to_le16(~ISCSIOPTS_STRICT_LOGIN_COMP_EN);
		break;
	default:
		ql4_printk(KERN_ERR, ha, "Unknown iscsi param = %d\n",
			   iface_param->param);
		break;
	}
}

static void
qla4xxx_initcb_to_acb(struct addr_ctrl_blk *init_fw_cb)
{
	struct addr_ctrl_blk_def *acb;
	acb = (struct addr_ctrl_blk_def *)init_fw_cb;
	memset(acb->reserved1, 0, sizeof(acb->reserved1));
	memset(acb->reserved2, 0, sizeof(acb->reserved2));
	memset(acb->reserved3, 0, sizeof(acb->reserved3));
	memset(acb->reserved4, 0, sizeof(acb->reserved4));
	memset(acb->reserved5, 0, sizeof(acb->reserved5));
	memset(acb->reserved6, 0, sizeof(acb->reserved6));
	memset(acb->reserved7, 0, sizeof(acb->reserved7));
	memset(acb->reserved8, 0, sizeof(acb->reserved8));
	memset(acb->reserved9, 0, sizeof(acb->reserved9));
	memset(acb->reserved10, 0, sizeof(acb->reserved10));
	memset(acb->reserved11, 0, sizeof(acb->reserved11));
	memset(acb->reserved12, 0, sizeof(acb->reserved12));
	memset(acb->reserved13, 0, sizeof(acb->reserved13));
	memset(acb->reserved14, 0, sizeof(acb->reserved14));
	memset(acb->reserved15, 0, sizeof(acb->reserved15));
}

static int
qla4xxx_iface_set_param(struct Scsi_Host *shost, void *data, uint32_t len)
{
	struct scsi_qla_host *ha = to_qla_host(shost);
	int rval = 0;
	struct iscsi_iface_param_info *iface_param = NULL;
	struct addr_ctrl_blk *init_fw_cb = NULL;
	dma_addr_t init_fw_cb_dma;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	uint32_t rem = len;
	struct nlattr *attr;

	init_fw_cb = dma_alloc_coherent(&ha->pdev->dev,
					sizeof(struct addr_ctrl_blk),
					&init_fw_cb_dma, GFP_KERNEL);
	if (!init_fw_cb) {
		ql4_printk(KERN_ERR, ha, "%s: Unable to alloc init_cb\n",
			   __func__);
		return -ENOMEM;
	}

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	if (qla4xxx_get_ifcb(ha, &mbox_cmd[0], &mbox_sts[0], init_fw_cb_dma)) {
		ql4_printk(KERN_ERR, ha, "%s: get ifcb failed\n", __func__);
		rval = -EIO;
		goto exit_init_fw_cb;
	}

	nla_for_each_attr(attr, data, len, rem) {
		iface_param = nla_data(attr);

		if (iface_param->param_type == ISCSI_NET_PARAM) {
			switch (iface_param->iface_type) {
			case ISCSI_IFACE_TYPE_IPV4:
				switch (iface_param->iface_num) {
				case 0:
					qla4xxx_set_ipv4(ha, iface_param,
							 init_fw_cb);
					break;
				default:
				/* Cannot have more than one IPv4 interface */
					ql4_printk(KERN_ERR, ha,
						   "Invalid IPv4 iface number = %d\n",
						   iface_param->iface_num);
					break;
				}
				break;
			case ISCSI_IFACE_TYPE_IPV6:
				switch (iface_param->iface_num) {
				case 0:
				case 1:
					qla4xxx_set_ipv6(ha, iface_param,
							 init_fw_cb);
					break;
				default:
				/* Cannot have more than two IPv6 interface */
					ql4_printk(KERN_ERR, ha,
						   "Invalid IPv6 iface number = %d\n",
						   iface_param->iface_num);
					break;
				}
				break;
			default:
				ql4_printk(KERN_ERR, ha,
					   "Invalid iface type\n");
				break;
			}
		} else if (iface_param->param_type == ISCSI_IFACE_PARAM) {
				qla4xxx_set_iscsi_param(ha, iface_param,
							init_fw_cb);
		} else {
			continue;
		}
	}

	init_fw_cb->cookie = cpu_to_le32(0x11BEAD5A);

	rval = qla4xxx_set_flash(ha, init_fw_cb_dma, FLASH_SEGMENT_IFCB,
				 sizeof(struct addr_ctrl_blk),
				 FLASH_OPT_RMW_COMMIT);
	if (rval != QLA_SUCCESS) {
		ql4_printk(KERN_ERR, ha, "%s: set flash mbx failed\n",
			   __func__);
		rval = -EIO;
		goto exit_init_fw_cb;
	}

	rval = qla4xxx_disable_acb(ha);
	if (rval != QLA_SUCCESS) {
		ql4_printk(KERN_ERR, ha, "%s: disable acb mbx failed\n",
			   __func__);
		rval = -EIO;
		goto exit_init_fw_cb;
	}

	wait_for_completion_timeout(&ha->disable_acb_comp,
				    DISABLE_ACB_TOV * HZ);

	qla4xxx_initcb_to_acb(init_fw_cb);

	rval = qla4xxx_set_acb(ha, &mbox_cmd[0], &mbox_sts[0], init_fw_cb_dma);
	if (rval != QLA_SUCCESS) {
		ql4_printk(KERN_ERR, ha, "%s: set acb mbx failed\n",
			   __func__);
		rval = -EIO;
		goto exit_init_fw_cb;
	}

	memset(init_fw_cb, 0, sizeof(struct addr_ctrl_blk));
	qla4xxx_update_local_ifcb(ha, &mbox_cmd[0], &mbox_sts[0], init_fw_cb,
				  init_fw_cb_dma);

exit_init_fw_cb:
	dma_free_coherent(&ha->pdev->dev, sizeof(struct addr_ctrl_blk),
			  init_fw_cb, init_fw_cb_dma);

	return rval;
}

static int qla4xxx_session_get_param(struct iscsi_cls_session *cls_sess,
				     enum iscsi_param param, char *buf)
{
	struct iscsi_session *sess = cls_sess->dd_data;
	struct ddb_entry *ddb_entry = sess->dd_data;
	struct scsi_qla_host *ha = ddb_entry->ha;
	struct iscsi_cls_conn *cls_conn = ddb_entry->conn;
	struct ql4_chap_table chap_tbl;
	int rval, len;
	uint16_t idx;

	memset(&chap_tbl, 0, sizeof(chap_tbl));
	switch (param) {
	case ISCSI_PARAM_CHAP_IN_IDX:
		rval = qla4xxx_get_chap_index(ha, sess->username_in,
					      sess->password_in, BIDI_CHAP,
					      &idx);
		if (rval)
			len = sprintf(buf, "\n");
		else
			len = sprintf(buf, "%hu\n", idx);
		break;
	case ISCSI_PARAM_CHAP_OUT_IDX:
		if (ddb_entry->ddb_type == FLASH_DDB) {
			if (ddb_entry->chap_tbl_idx != INVALID_ENTRY) {
				idx = ddb_entry->chap_tbl_idx;
				rval = QLA_SUCCESS;
			} else {
				rval = QLA_ERROR;
			}
		} else {
			rval = qla4xxx_get_chap_index(ha, sess->username,
						      sess->password,
						      LOCAL_CHAP, &idx);
		}
		if (rval)
			len = sprintf(buf, "\n");
		else
			len = sprintf(buf, "%hu\n", idx);
		break;
	case ISCSI_PARAM_USERNAME:
	case ISCSI_PARAM_PASSWORD:
		/* First, populate session username and password for FLASH DDB,
		 * if not already done. This happens when session login fails
		 * for a FLASH DDB.
		 */
		if (ddb_entry->ddb_type == FLASH_DDB &&
		    ddb_entry->chap_tbl_idx != INVALID_ENTRY &&
		    !sess->username && !sess->password) {
			idx = ddb_entry->chap_tbl_idx;
			rval = qla4xxx_get_uni_chap_at_index(ha, chap_tbl.name,
							    chap_tbl.secret,
							    idx);
			if (!rval) {
				iscsi_set_param(cls_conn, ISCSI_PARAM_USERNAME,
						(char *)chap_tbl.name,
						strlen((char *)chap_tbl.name));
				iscsi_set_param(cls_conn, ISCSI_PARAM_PASSWORD,
						(char *)chap_tbl.secret,
						chap_tbl.secret_len);
			}
		}
		fallthrough;
	default:
		return iscsi_session_get_param(cls_sess, param, buf);
	}

	return len;
}

static int qla4xxx_conn_get_param(struct iscsi_cls_conn *cls_conn,
				  enum iscsi_param param, char *buf)
{
	struct iscsi_conn *conn;
	struct qla_conn *qla_conn;
	struct sockaddr *dst_addr;

	conn = cls_conn->dd_data;
	qla_conn = conn->dd_data;
	dst_addr = (struct sockaddr *)&qla_conn->qla_ep->dst_addr;

	switch (param) {
	case ISCSI_PARAM_CONN_PORT:
	case ISCSI_PARAM_CONN_ADDRESS:
		return iscsi_conn_get_addr_param((struct sockaddr_storage *)
						 dst_addr, param, buf);
	default:
		return iscsi_conn_get_param(cls_conn, param, buf);
	}
}

int qla4xxx_get_ddb_index(struct scsi_qla_host *ha, uint16_t *ddb_index)
{
	uint32_t mbx_sts = 0;
	uint16_t tmp_ddb_index;
	int ret;

get_ddb_index:
	tmp_ddb_index = find_first_zero_bit(ha->ddb_idx_map, MAX_DDB_ENTRIES);

	if (tmp_ddb_index >= MAX_DDB_ENTRIES) {
		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "Free DDB index not available\n"));
		ret = QLA_ERROR;
		goto exit_get_ddb_index;
	}

	if (test_and_set_bit(tmp_ddb_index, ha->ddb_idx_map))
		goto get_ddb_index;

	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "Found a free DDB index at %d\n", tmp_ddb_index));
	ret = qla4xxx_req_ddb_entry(ha, tmp_ddb_index, &mbx_sts);
	if (ret == QLA_ERROR) {
		if (mbx_sts == MBOX_STS_COMMAND_ERROR) {
			ql4_printk(KERN_INFO, ha,
				   "DDB index = %d not available trying next\n",
				   tmp_ddb_index);
			goto get_ddb_index;
		}
		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "Free FW DDB not available\n"));
	}

	*ddb_index = tmp_ddb_index;

exit_get_ddb_index:
	return ret;
}

static int qla4xxx_match_ipaddress(struct scsi_qla_host *ha,
				   struct ddb_entry *ddb_entry,
				   char *existing_ipaddr,
				   char *user_ipaddr)
{
	uint8_t dst_ipaddr[IPv6_ADDR_LEN];
	char formatted_ipaddr[DDB_IPADDR_LEN];
	int status = QLA_SUCCESS, ret = 0;

	if (ddb_entry->fw_ddb_entry.options & DDB_OPT_IPV6_DEVICE) {
		ret = in6_pton(user_ipaddr, strlen(user_ipaddr), dst_ipaddr,
			       '\0', NULL);
		if (ret == 0) {
			status = QLA_ERROR;
			goto out_match;
		}
		ret = sprintf(formatted_ipaddr, "%pI6", dst_ipaddr);
	} else {
		ret = in4_pton(user_ipaddr, strlen(user_ipaddr), dst_ipaddr,
			       '\0', NULL);
		if (ret == 0) {
			status = QLA_ERROR;
			goto out_match;
		}
		ret = sprintf(formatted_ipaddr, "%pI4", dst_ipaddr);
	}

	if (strcmp(existing_ipaddr, formatted_ipaddr))
		status = QLA_ERROR;

out_match:
	return status;
}

static int qla4xxx_match_fwdb_session(struct scsi_qla_host *ha,
				      struct iscsi_cls_conn *cls_conn)
{
	int idx = 0, max_ddbs, rval;
	struct iscsi_cls_session *cls_sess = iscsi_conn_to_session(cls_conn);
	struct iscsi_session *sess, *existing_sess;
	struct iscsi_conn *conn, *existing_conn;
	struct ddb_entry *ddb_entry;

	sess = cls_sess->dd_data;
	conn = cls_conn->dd_data;

	if (sess->targetname == NULL ||
	    conn->persistent_address == NULL ||
	    conn->persistent_port == 0)
		return QLA_ERROR;

	max_ddbs =  is_qla40XX(ha) ? MAX_DEV_DB_ENTRIES_40XX :
				     MAX_DEV_DB_ENTRIES;

	for (idx = 0; idx < max_ddbs; idx++) {
		ddb_entry = qla4xxx_lookup_ddb_by_fw_index(ha, idx);
		if (ddb_entry == NULL)
			continue;

		if (ddb_entry->ddb_type != FLASH_DDB)
			continue;

		existing_sess = ddb_entry->sess->dd_data;
		existing_conn = ddb_entry->conn->dd_data;

		if (existing_sess->targetname == NULL ||
		    existing_conn->persistent_address == NULL ||
		    existing_conn->persistent_port == 0)
			continue;

		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "IQN = %s User IQN = %s\n",
				  existing_sess->targetname,
				  sess->targetname));

		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "IP = %s User IP = %s\n",
				  existing_conn->persistent_address,
				  conn->persistent_address));

		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "Port = %d User Port = %d\n",
				  existing_conn->persistent_port,
				  conn->persistent_port));

		if (strcmp(existing_sess->targetname, sess->targetname))
			continue;
		rval = qla4xxx_match_ipaddress(ha, ddb_entry,
					existing_conn->persistent_address,
					conn->persistent_address);
		if (rval == QLA_ERROR)
			continue;
		if (existing_conn->persistent_port != conn->persistent_port)
			continue;
		break;
	}

	if (idx == max_ddbs)
		return QLA_ERROR;

	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "Match found in fwdb sessions\n"));
	return QLA_SUCCESS;
}

static struct iscsi_cls_session *
qla4xxx_session_create(struct iscsi_endpoint *ep,
			uint16_t cmds_max, uint16_t qdepth,
			uint32_t initial_cmdsn)
{
	struct iscsi_cls_session *cls_sess;
	struct scsi_qla_host *ha;
	struct qla_endpoint *qla_ep;
	struct ddb_entry *ddb_entry;
	uint16_t ddb_index;
	struct iscsi_session *sess;
	int ret;

	if (!ep) {
		printk(KERN_ERR "qla4xxx: missing ep.\n");
		return NULL;
	}

	qla_ep = ep->dd_data;
	ha = to_qla_host(qla_ep->host);
	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: host: %ld\n", __func__,
			  ha->host_no));

	ret = qla4xxx_get_ddb_index(ha, &ddb_index);
	if (ret == QLA_ERROR)
		return NULL;

	cls_sess = iscsi_session_setup(&qla4xxx_iscsi_transport, qla_ep->host,
				       cmds_max, sizeof(struct ddb_entry),
				       sizeof(struct ql4_task_data),
				       initial_cmdsn, ddb_index);
	if (!cls_sess)
		return NULL;

	sess = cls_sess->dd_data;
	ddb_entry = sess->dd_data;
	ddb_entry->fw_ddb_index = ddb_index;
	ddb_entry->fw_ddb_device_state = DDB_DS_NO_CONNECTION_ACTIVE;
	ddb_entry->ha = ha;
	ddb_entry->sess = cls_sess;
	ddb_entry->unblock_sess = qla4xxx_unblock_ddb;
	ddb_entry->ddb_change = qla4xxx_ddb_change;
	clear_bit(DDB_CONN_CLOSE_FAILURE, &ddb_entry->flags);
	cls_sess->recovery_tmo = ql4xsess_recovery_tmo;
	ha->fw_ddb_index_map[ddb_entry->fw_ddb_index] = ddb_entry;
	ha->tot_ddbs++;

	return cls_sess;
}

static void qla4xxx_session_destroy(struct iscsi_cls_session *cls_sess)
{
	struct iscsi_session *sess;
	struct ddb_entry *ddb_entry;
	struct scsi_qla_host *ha;
	unsigned long flags, wtime;
	struct dev_db_entry *fw_ddb_entry = NULL;
	dma_addr_t fw_ddb_entry_dma;
	uint32_t ddb_state;
	int ret;

	sess = cls_sess->dd_data;
	ddb_entry = sess->dd_data;
	ha = ddb_entry->ha;
	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: host: %ld\n", __func__,
			  ha->host_no));

	fw_ddb_entry = dma_alloc_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
					  &fw_ddb_entry_dma, GFP_KERNEL);
	if (!fw_ddb_entry) {
		ql4_printk(KERN_ERR, ha,
			   "%s: Unable to allocate dma buffer\n", __func__);
		goto destroy_session;
	}

	wtime = jiffies + (HZ * LOGOUT_TOV);
	do {
		ret = qla4xxx_get_fwddb_entry(ha, ddb_entry->fw_ddb_index,
					      fw_ddb_entry, fw_ddb_entry_dma,
					      NULL, NULL, &ddb_state, NULL,
					      NULL, NULL);
		if (ret == QLA_ERROR)
			goto destroy_session;

		if ((ddb_state == DDB_DS_NO_CONNECTION_ACTIVE) ||
		    (ddb_state == DDB_DS_SESSION_FAILED))
			goto destroy_session;

		schedule_timeout_uninterruptible(HZ);
	} while ((time_after(wtime, jiffies)));

destroy_session:
	qla4xxx_clear_ddb_entry(ha, ddb_entry->fw_ddb_index);
	if (test_and_clear_bit(DDB_CONN_CLOSE_FAILURE, &ddb_entry->flags))
		clear_bit(ddb_entry->fw_ddb_index, ha->ddb_idx_map);
	spin_lock_irqsave(&ha->hardware_lock, flags);
	qla4xxx_free_ddb(ha, ddb_entry);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	iscsi_session_teardown(cls_sess);

	if (fw_ddb_entry)
		dma_free_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
				  fw_ddb_entry, fw_ddb_entry_dma);
}

static struct iscsi_cls_conn *
qla4xxx_conn_create(struct iscsi_cls_session *cls_sess, uint32_t conn_idx)
{
	struct iscsi_cls_conn *cls_conn;
	struct iscsi_session *sess;
	struct ddb_entry *ddb_entry;
	struct scsi_qla_host *ha;

	cls_conn = iscsi_conn_setup(cls_sess, sizeof(struct qla_conn),
				    conn_idx);
	if (!cls_conn) {
		pr_info("%s: Can not create connection for conn_idx = %u\n",
			__func__, conn_idx);
		return NULL;
	}

	sess = cls_sess->dd_data;
	ddb_entry = sess->dd_data;
	ddb_entry->conn = cls_conn;

	ha = ddb_entry->ha;
	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: conn_idx = %u\n", __func__,
			  conn_idx));
	return cls_conn;
}

static int qla4xxx_conn_bind(struct iscsi_cls_session *cls_session,
			     struct iscsi_cls_conn *cls_conn,
			     uint64_t transport_fd, int is_leading)
{
	struct iscsi_conn *conn;
	struct qla_conn *qla_conn;
	struct iscsi_endpoint *ep;
	struct ddb_entry *ddb_entry;
	struct scsi_qla_host *ha;
	struct iscsi_session *sess;

	sess = cls_session->dd_data;
	ddb_entry = sess->dd_data;
	ha = ddb_entry->ha;

	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: sid = %d, cid = %d\n", __func__,
			  cls_session->sid, cls_conn->cid));

	if (iscsi_conn_bind(cls_session, cls_conn, is_leading))
		return -EINVAL;
	ep = iscsi_lookup_endpoint(transport_fd);
	if (!ep)
		return -EINVAL;
	conn = cls_conn->dd_data;
	qla_conn = conn->dd_data;
	qla_conn->qla_ep = ep->dd_data;
	iscsi_put_endpoint(ep);
	return 0;
}

static int qla4xxx_conn_start(struct iscsi_cls_conn *cls_conn)
{
	struct iscsi_cls_session *cls_sess = iscsi_conn_to_session(cls_conn);
	struct iscsi_session *sess;
	struct ddb_entry *ddb_entry;
	struct scsi_qla_host *ha;
	struct dev_db_entry *fw_ddb_entry = NULL;
	dma_addr_t fw_ddb_entry_dma;
	uint32_t mbx_sts = 0;
	int ret = 0;
	int status = QLA_SUCCESS;

	sess = cls_sess->dd_data;
	ddb_entry = sess->dd_data;
	ha = ddb_entry->ha;
	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: sid = %d, cid = %d\n", __func__,
			  cls_sess->sid, cls_conn->cid));

	/* Check if we have  matching FW DDB, if yes then do not
	 * login to this target. This could cause target to logout previous
	 * connection
	 */
	ret = qla4xxx_match_fwdb_session(ha, cls_conn);
	if (ret == QLA_SUCCESS) {
		ql4_printk(KERN_INFO, ha,
			   "Session already exist in FW.\n");
		ret = -EEXIST;
		goto exit_conn_start;
	}

	fw_ddb_entry = dma_alloc_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
					  &fw_ddb_entry_dma, GFP_KERNEL);
	if (!fw_ddb_entry) {
		ql4_printk(KERN_ERR, ha,
			   "%s: Unable to allocate dma buffer\n", __func__);
		ret = -ENOMEM;
		goto exit_conn_start;
	}

	ret = qla4xxx_set_param_ddbentry(ha, ddb_entry, cls_conn, &mbx_sts);
	if (ret) {
		/* If iscsid is stopped and started then no need to do
		* set param again since ddb state will be already
		* active and FW does not allow set ddb to an
		* active session.
		*/
		if (mbx_sts)
			if (ddb_entry->fw_ddb_device_state ==
						DDB_DS_SESSION_ACTIVE) {
				ddb_entry->unblock_sess(ddb_entry->sess);
				goto exit_set_param;
			}

		ql4_printk(KERN_ERR, ha, "%s: Failed set param for index[%d]\n",
			   __func__, ddb_entry->fw_ddb_index);
		goto exit_conn_start;
	}

	status = qla4xxx_conn_open(ha, ddb_entry->fw_ddb_index);
	if (status == QLA_ERROR) {
		ql4_printk(KERN_ERR, ha, "%s: Login failed: %s\n", __func__,
			   sess->targetname);
		ret = -EINVAL;
		goto exit_conn_start;
	}

	if (ddb_entry->fw_ddb_device_state == DDB_DS_NO_CONNECTION_ACTIVE)
		ddb_entry->fw_ddb_device_state = DDB_DS_LOGIN_IN_PROCESS;

	DEBUG2(printk(KERN_INFO "%s: DDB state [%d]\n", __func__,
		      ddb_entry->fw_ddb_device_state));

exit_set_param:
	ret = 0;

exit_conn_start:
	if (fw_ddb_entry)
		dma_free_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
				  fw_ddb_entry, fw_ddb_entry_dma);
	return ret;
}

static void qla4xxx_conn_destroy(struct iscsi_cls_conn *cls_conn)
{
	struct iscsi_cls_session *cls_sess = iscsi_conn_to_session(cls_conn);
	struct iscsi_session *sess;
	struct scsi_qla_host *ha;
	struct ddb_entry *ddb_entry;
	int options;

	sess = cls_sess->dd_data;
	ddb_entry = sess->dd_data;
	ha = ddb_entry->ha;
	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: cid = %d\n", __func__,
			  cls_conn->cid));

	options = LOGOUT_OPTION_CLOSE_SESSION;
	if (qla4xxx_session_logout_ddb(ha, ddb_entry, options) == QLA_ERROR)
		ql4_printk(KERN_ERR, ha, "%s: Logout failed\n", __func__);
}

static void qla4xxx_task_work(struct work_struct *wdata)
{
	struct ql4_task_data *task_data;
	struct scsi_qla_host *ha;
	struct passthru_status *sts;
	struct iscsi_task *task;
	struct iscsi_hdr *hdr;
	uint8_t *data;
	uint32_t data_len;
	struct iscsi_conn *conn;
	int hdr_len;
	itt_t itt;

	task_data = container_of(wdata, struct ql4_task_data, task_work);
	ha = task_data->ha;
	task = task_data->task;
	sts = &task_data->sts;
	hdr_len = sizeof(struct iscsi_hdr);

	DEBUG3(printk(KERN_INFO "Status returned\n"));
	DEBUG3(qla4xxx_dump_buffer(sts, 64));
	DEBUG3(printk(KERN_INFO "Response buffer"));
	DEBUG3(qla4xxx_dump_buffer(task_data->resp_buffer, 64));

	conn = task->conn;

	switch (sts->completionStatus) {
	case PASSTHRU_STATUS_COMPLETE:
		hdr = (struct iscsi_hdr *)task_data->resp_buffer;
		/* Assign back the itt in hdr, until we use the PREASSIGN_TAG */
		itt = sts->handle;
		hdr->itt = itt;
		data = task_data->resp_buffer + hdr_len;
		data_len = task_data->resp_len - hdr_len;
		iscsi_complete_pdu(conn, hdr, data, data_len);
		break;
	default:
		ql4_printk(KERN_ERR, ha, "Passthru failed status = 0x%x\n",
			   sts->completionStatus);
		break;
	}
	return;
}

static int qla4xxx_alloc_pdu(struct iscsi_task *task, uint8_t opcode)
{
	struct ql4_task_data *task_data;
	struct iscsi_session *sess;
	struct ddb_entry *ddb_entry;
	struct scsi_qla_host *ha;
	int hdr_len;

	sess = task->conn->session;
	ddb_entry = sess->dd_data;
	ha = ddb_entry->ha;
	task_data = task->dd_data;
	memset(task_data, 0, sizeof(struct ql4_task_data));

	if (task->sc) {
		ql4_printk(KERN_INFO, ha,
			   "%s: SCSI Commands not implemented\n", __func__);
		return -EINVAL;
	}

	hdr_len = sizeof(struct iscsi_hdr);
	task_data->ha = ha;
	task_data->task = task;

	if (task->data_count) {
		task_data->data_dma = dma_map_single(&ha->pdev->dev, task->data,
						     task->data_count,
						     DMA_TO_DEVICE);
	}

	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: MaxRecvLen %u, iscsi hrd %d\n",
		      __func__, task->conn->max_recv_dlength, hdr_len));

	task_data->resp_len = task->conn->max_recv_dlength + hdr_len;
	task_data->resp_buffer = dma_alloc_coherent(&ha->pdev->dev,
						    task_data->resp_len,
						    &task_data->resp_dma,
						    GFP_ATOMIC);
	if (!task_data->resp_buffer)
		goto exit_alloc_pdu;

	task_data->req_len = task->data_count + hdr_len;
	task_data->req_buffer = dma_alloc_coherent(&ha->pdev->dev,
						   task_data->req_len,
						   &task_data->req_dma,
						   GFP_ATOMIC);
	if (!task_data->req_buffer)
		goto exit_alloc_pdu;

	task->hdr = task_data->req_buffer;

	INIT_WORK(&task_data->task_work, qla4xxx_task_work);

	return 0;

exit_alloc_pdu:
	if (task_data->resp_buffer)
		dma_free_coherent(&ha->pdev->dev, task_data->resp_len,
				  task_data->resp_buffer, task_data->resp_dma);

	if (task_data->req_buffer)
		dma_free_coherent(&ha->pdev->dev, task_data->req_len,
				  task_data->req_buffer, task_data->req_dma);
	return -ENOMEM;
}

static void qla4xxx_task_cleanup(struct iscsi_task *task)
{
	struct ql4_task_data *task_data;
	struct iscsi_session *sess;
	struct ddb_entry *ddb_entry;
	struct scsi_qla_host *ha;
	int hdr_len;

	hdr_len = sizeof(struct iscsi_hdr);
	sess = task->conn->session;
	ddb_entry = sess->dd_data;
	ha = ddb_entry->ha;
	task_data = task->dd_data;

	if (task->data_count) {
		dma_unmap_single(&ha->pdev->dev, task_data->data_dma,
				 task->data_count, DMA_TO_DEVICE);
	}

	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: MaxRecvLen %u, iscsi hrd %d\n",
		      __func__, task->conn->max_recv_dlength, hdr_len));

	dma_free_coherent(&ha->pdev->dev, task_data->resp_len,
			  task_data->resp_buffer, task_data->resp_dma);
	dma_free_coherent(&ha->pdev->dev, task_data->req_len,
			  task_data->req_buffer, task_data->req_dma);
	return;
}

static int qla4xxx_task_xmit(struct iscsi_task *task)
{
	struct scsi_cmnd *sc = task->sc;
	struct iscsi_session *sess = task->conn->session;
	struct ddb_entry *ddb_entry = sess->dd_data;
	struct scsi_qla_host *ha = ddb_entry->ha;

	if (!sc)
		return qla4xxx_send_passthru0(task);

	ql4_printk(KERN_INFO, ha, "%s: scsi cmd xmit not implemented\n",
		   __func__);
	return -ENOSYS;
}

static int qla4xxx_copy_from_fwddb_param(struct iscsi_bus_flash_session *sess,
					 struct iscsi_bus_flash_conn *conn,
					 struct dev_db_entry *fw_ddb_entry)
{
	unsigned long options = 0;
	int rc = 0;

	options = le16_to_cpu(fw_ddb_entry->options);
	conn->is_fw_assigned_ipv6 = test_bit(OPT_IS_FW_ASSIGNED_IPV6, &options);
	if (test_bit(OPT_IPV6_DEVICE, &options)) {
		rc = iscsi_switch_str_param(&sess->portal_type,
					    PORTAL_TYPE_IPV6);
		if (rc)
			goto exit_copy;
	} else {
		rc = iscsi_switch_str_param(&sess->portal_type,
					    PORTAL_TYPE_IPV4);
		if (rc)
			goto exit_copy;
	}

	sess->auto_snd_tgt_disable = test_bit(OPT_AUTO_SENDTGTS_DISABLE,
					      &options);
	sess->discovery_sess = test_bit(OPT_DISC_SESSION, &options);
	sess->entry_state = test_bit(OPT_ENTRY_STATE, &options);

	options = le16_to_cpu(fw_ddb_entry->iscsi_options);
	conn->hdrdgst_en = test_bit(ISCSIOPT_HEADER_DIGEST_EN, &options);
	conn->datadgst_en = test_bit(ISCSIOPT_DATA_DIGEST_EN, &options);
	sess->imm_data_en = test_bit(ISCSIOPT_IMMEDIATE_DATA_EN, &options);
	sess->initial_r2t_en = test_bit(ISCSIOPT_INITIAL_R2T_EN, &options);
	sess->dataseq_inorder_en = test_bit(ISCSIOPT_DATA_SEQ_IN_ORDER,
					    &options);
	sess->pdu_inorder_en = test_bit(ISCSIOPT_DATA_PDU_IN_ORDER, &options);
	sess->chap_auth_en = test_bit(ISCSIOPT_CHAP_AUTH_EN, &options);
	conn->snack_req_en = test_bit(ISCSIOPT_SNACK_REQ_EN, &options);
	sess->discovery_logout_en = test_bit(ISCSIOPT_DISCOVERY_LOGOUT_EN,
					     &options);
	sess->bidi_chap_en = test_bit(ISCSIOPT_BIDI_CHAP_EN, &options);
	sess->discovery_auth_optional =
			test_bit(ISCSIOPT_DISCOVERY_AUTH_OPTIONAL, &options);
	if (test_bit(ISCSIOPT_ERL1, &options))
		sess->erl |= BIT_1;
	if (test_bit(ISCSIOPT_ERL0, &options))
		sess->erl |= BIT_0;

	options = le16_to_cpu(fw_ddb_entry->tcp_options);
	conn->tcp_timestamp_stat = test_bit(TCPOPT_TIMESTAMP_STAT, &options);
	conn->tcp_nagle_disable = test_bit(TCPOPT_NAGLE_DISABLE, &options);
	conn->tcp_wsf_disable = test_bit(TCPOPT_WSF_DISABLE, &options);
	if (test_bit(TCPOPT_TIMER_SCALE3, &options))
		conn->tcp_timer_scale |= BIT_3;
	if (test_bit(TCPOPT_TIMER_SCALE2, &options))
		conn->tcp_timer_scale |= BIT_2;
	if (test_bit(TCPOPT_TIMER_SCALE1, &options))
		conn->tcp_timer_scale |= BIT_1;

	conn->tcp_timer_scale >>= 1;
	conn->tcp_timestamp_en = test_bit(TCPOPT_TIMESTAMP_EN, &options);

	options = le16_to_cpu(fw_ddb_entry->ip_options);
	conn->fragment_disable = test_bit(IPOPT_FRAGMENT_DISABLE, &options);

	conn->max_recv_dlength = BYTE_UNITS *
			  le16_to_cpu(fw_ddb_entry->iscsi_max_rcv_data_seg_len);
	conn->max_xmit_dlength = BYTE_UNITS *
			  le16_to_cpu(fw_ddb_entry->iscsi_max_snd_data_seg_len);
	sess->first_burst = BYTE_UNITS *
			       le16_to_cpu(fw_ddb_entry->iscsi_first_burst_len);
	sess->max_burst = BYTE_UNITS *
				 le16_to_cpu(fw_ddb_entry->iscsi_max_burst_len);
	sess->max_r2t = le16_to_cpu(fw_ddb_entry->iscsi_max_outsnd_r2t);
	sess->time2wait = le16_to_cpu(fw_ddb_entry->iscsi_def_time2wait);
	sess->time2retain = le16_to_cpu(fw_ddb_entry->iscsi_def_time2retain);
	sess->tpgt = le32_to_cpu(fw_ddb_entry->tgt_portal_grp);
	conn->max_segment_size = le16_to_cpu(fw_ddb_entry->mss);
	conn->tcp_xmit_wsf = fw_ddb_entry->tcp_xmt_wsf;
	conn->tcp_recv_wsf = fw_ddb_entry->tcp_rcv_wsf;
	conn->ipv6_flow_label = le16_to_cpu(fw_ddb_entry->ipv6_flow_lbl);
	conn->keepalive_timeout = le16_to_cpu(fw_ddb_entry->ka_timeout);
	conn->local_port = le16_to_cpu(fw_ddb_entry->lcl_port);
	conn->statsn = le32_to_cpu(fw_ddb_entry->stat_sn);
	conn->exp_statsn = le32_to_cpu(fw_ddb_entry->exp_stat_sn);
	sess->discovery_parent_idx = le16_to_cpu(fw_ddb_entry->ddb_link);
	sess->discovery_parent_type = le16_to_cpu(fw_ddb_entry->ddb_link);
	sess->chap_out_idx = le16_to_cpu(fw_ddb_entry->chap_tbl_idx);
	sess->tsid = le16_to_cpu(fw_ddb_entry->tsid);

	sess->default_taskmgmt_timeout =
				le16_to_cpu(fw_ddb_entry->def_timeout);
	conn->port = le16_to_cpu(fw_ddb_entry->port);

	options = le16_to_cpu(fw_ddb_entry->options);
	conn->ipaddress = kzalloc(IPv6_ADDR_LEN, GFP_KERNEL);
	if (!conn->ipaddress) {
		rc = -ENOMEM;
		goto exit_copy;
	}

	conn->redirect_ipaddr = kzalloc(IPv6_ADDR_LEN, GFP_KERNEL);
	if (!conn->redirect_ipaddr) {
		rc = -ENOMEM;
		goto exit_copy;
	}

	memcpy(conn->ipaddress, fw_ddb_entry->ip_addr, IPv6_ADDR_LEN);
	memcpy(conn->redirect_ipaddr, fw_ddb_entry->tgt_addr, IPv6_ADDR_LEN);

	if (test_bit(OPT_IPV6_DEVICE, &options)) {
		conn->ipv6_traffic_class = fw_ddb_entry->ipv4_tos;

		conn->link_local_ipv6_addr = kmemdup(
					fw_ddb_entry->link_local_ipv6_addr,
					IPv6_ADDR_LEN, GFP_KERNEL);
		if (!conn->link_local_ipv6_addr) {
			rc = -ENOMEM;
			goto exit_copy;
		}
	} else {
		conn->ipv4_tos = fw_ddb_entry->ipv4_tos;
	}

	if (fw_ddb_entry->iscsi_name[0]) {
		rc = iscsi_switch_str_param(&sess->targetname,
					    (char *)fw_ddb_entry->iscsi_name);
		if (rc)
			goto exit_copy;
	}

	if (fw_ddb_entry->iscsi_alias[0]) {
		rc = iscsi_switch_str_param(&sess->targetalias,
					    (char *)fw_ddb_entry->iscsi_alias);
		if (rc)
			goto exit_copy;
	}

	COPY_ISID(sess->isid, fw_ddb_entry->isid);

exit_copy:
	return rc;
}

static int qla4xxx_copy_to_fwddb_param(struct iscsi_bus_flash_session *sess,
				       struct iscsi_bus_flash_conn *conn,
				       struct dev_db_entry *fw_ddb_entry)
{
	uint16_t options;

	options = le16_to_cpu(fw_ddb_entry->options);
	SET_BITVAL(conn->is_fw_assigned_ipv6,  options, BIT_11);
	if (!strncmp(sess->portal_type, PORTAL_TYPE_IPV6, 4))
		options |= BIT_8;
	else
		options &= ~BIT_8;

	SET_BITVAL(sess->auto_snd_tgt_disable, options, BIT_6);
	SET_BITVAL(sess->discovery_sess, options, BIT_4);
	SET_BITVAL(sess->entry_state, options, BIT_3);
	fw_ddb_entry->options = cpu_to_le16(options);

	options = le16_to_cpu(fw_ddb_entry->iscsi_options);
	SET_BITVAL(conn->hdrdgst_en, options, BIT_13);
	SET_BITVAL(conn->datadgst_en, options, BIT_12);
	SET_BITVAL(sess->imm_data_en, options, BIT_11);
	SET_BITVAL(sess->initial_r2t_en, options, BIT_10);
	SET_BITVAL(sess->dataseq_inorder_en, options, BIT_9);
	SET_BITVAL(sess->pdu_inorder_en, options, BIT_8);
	SET_BITVAL(sess->chap_auth_en, options, BIT_7);
	SET_BITVAL(conn->snack_req_en, options, BIT_6);
	SET_BITVAL(sess->discovery_logout_en, options, BIT_5);
	SET_BITVAL(sess->bidi_chap_en, options, BIT_4);
	SET_BITVAL(sess->discovery_auth_optional, options, BIT_3);
	SET_BITVAL(sess->erl & BIT_1, options, BIT_1);
	SET_BITVAL(sess->erl & BIT_0, options, BIT_0);
	fw_ddb_entry->iscsi_options = cpu_to_le16(options);

	options = le16_to_cpu(fw_ddb_entry->tcp_options);
	SET_BITVAL(conn->tcp_timestamp_stat, options, BIT_6);
	SET_BITVAL(conn->tcp_nagle_disable, options, BIT_5);
	SET_BITVAL(conn->tcp_wsf_disable, options, BIT_4);
	SET_BITVAL(conn->tcp_timer_scale & BIT_2, options, BIT_3);
	SET_BITVAL(conn->tcp_timer_scale & BIT_1, options, BIT_2);
	SET_BITVAL(conn->tcp_timer_scale & BIT_0, options, BIT_1);
	SET_BITVAL(conn->tcp_timestamp_en, options, BIT_0);
	fw_ddb_entry->tcp_options = cpu_to_le16(options);

	options = le16_to_cpu(fw_ddb_entry->ip_options);
	SET_BITVAL(conn->fragment_disable, options, BIT_4);
	fw_ddb_entry->ip_options = cpu_to_le16(options);

	fw_ddb_entry->iscsi_max_outsnd_r2t = cpu_to_le16(sess->max_r2t);
	fw_ddb_entry->iscsi_max_rcv_data_seg_len =
			       cpu_to_le16(conn->max_recv_dlength / BYTE_UNITS);
	fw_ddb_entry->iscsi_max_snd_data_seg_len =
			       cpu_to_le16(conn->max_xmit_dlength / BYTE_UNITS);
	fw_ddb_entry->iscsi_first_burst_len =
				cpu_to_le16(sess->first_burst / BYTE_UNITS);
	fw_ddb_entry->iscsi_max_burst_len = cpu_to_le16(sess->max_burst /
					    BYTE_UNITS);
	fw_ddb_entry->iscsi_def_time2wait = cpu_to_le16(sess->time2wait);
	fw_ddb_entry->iscsi_def_time2retain = cpu_to_le16(sess->time2retain);
	fw_ddb_entry->tgt_portal_grp = cpu_to_le16(sess->tpgt);
	fw_ddb_entry->mss = cpu_to_le16(conn->max_segment_size);
	fw_ddb_entry->tcp_xmt_wsf = (uint8_t) cpu_to_le32(conn->tcp_xmit_wsf);
	fw_ddb_entry->tcp_rcv_wsf = (uint8_t) cpu_to_le32(conn->tcp_recv_wsf);
	fw_ddb_entry->ipv6_flow_lbl = cpu_to_le16(conn->ipv6_flow_label);
	fw_ddb_entry->ka_timeout = cpu_to_le16(conn->keepalive_timeout);
	fw_ddb_entry->lcl_port = cpu_to_le16(conn->local_port);
	fw_ddb_entry->stat_sn = cpu_to_le32(conn->statsn);
	fw_ddb_entry->exp_stat_sn = cpu_to_le32(conn->exp_statsn);
	fw_ddb_entry->ddb_link = cpu_to_le16(sess->discovery_parent_idx);
	fw_ddb_entry->chap_tbl_idx = cpu_to_le16(sess->chap_out_idx);
	fw_ddb_entry->tsid = cpu_to_le16(sess->tsid);
	fw_ddb_entry->port = cpu_to_le16(conn->port);
	fw_ddb_entry->def_timeout =
				cpu_to_le16(sess->default_taskmgmt_timeout);

	if (!strncmp(sess->portal_type, PORTAL_TYPE_IPV6, 4))
		fw_ddb_entry->ipv4_tos = conn->ipv6_traffic_class;
	else
		fw_ddb_entry->ipv4_tos = conn->ipv4_tos;

	if (conn->ipaddress)
		memcpy(fw_ddb_entry->ip_addr, conn->ipaddress,
		       sizeof(fw_ddb_entry->ip_addr));

	if (conn->redirect_ipaddr)
		memcpy(fw_ddb_entry->tgt_addr, conn->redirect_ipaddr,
		       sizeof(fw_ddb_entry->tgt_addr));

	if (conn->link_local_ipv6_addr)
		memcpy(fw_ddb_entry->link_local_ipv6_addr,
		       conn->link_local_ipv6_addr,
		       sizeof(fw_ddb_entry->link_local_ipv6_addr));

	if (sess->targetname)
		memcpy(fw_ddb_entry->iscsi_name, sess->targetname,
		       sizeof(fw_ddb_entry->iscsi_name));

	if (sess->targetalias)
		memcpy(fw_ddb_entry->iscsi_alias, sess->targetalias,
		       sizeof(fw_ddb_entry->iscsi_alias));

	COPY_ISID(fw_ddb_entry->isid, sess->isid);

	return 0;
}

static void qla4xxx_copy_to_sess_conn_params(struct iscsi_conn *conn,
					     struct iscsi_session *sess,
					     struct dev_db_entry *fw_ddb_entry)
{
	unsigned long options = 0;
	uint16_t ddb_link;
	uint16_t disc_parent;
	char ip_addr[DDB_IPADDR_LEN];

	options = le16_to_cpu(fw_ddb_entry->options);
	conn->is_fw_assigned_ipv6 = test_bit(OPT_IS_FW_ASSIGNED_IPV6, &options);
	sess->auto_snd_tgt_disable = test_bit(OPT_AUTO_SENDTGTS_DISABLE,
					      &options);
	sess->discovery_sess = test_bit(OPT_DISC_SESSION, &options);

	options = le16_to_cpu(fw_ddb_entry->iscsi_options);
	conn->hdrdgst_en = test_bit(ISCSIOPT_HEADER_DIGEST_EN, &options);
	conn->datadgst_en = test_bit(ISCSIOPT_DATA_DIGEST_EN, &options);
	sess->imm_data_en = test_bit(ISCSIOPT_IMMEDIATE_DATA_EN, &options);
	sess->initial_r2t_en = test_bit(ISCSIOPT_INITIAL_R2T_EN, &options);
	sess->dataseq_inorder_en = test_bit(ISCSIOPT_DATA_SEQ_IN_ORDER,
					    &options);
	sess->pdu_inorder_en = test_bit(ISCSIOPT_DATA_PDU_IN_ORDER, &options);
	sess->chap_auth_en = test_bit(ISCSIOPT_CHAP_AUTH_EN, &options);
	sess->discovery_logout_en = test_bit(ISCSIOPT_DISCOVERY_LOGOUT_EN,
					     &options);
	sess->bidi_chap_en = test_bit(ISCSIOPT_BIDI_CHAP_EN, &options);
	sess->discovery_auth_optional =
			test_bit(ISCSIOPT_DISCOVERY_AUTH_OPTIONAL, &options);
	if (test_bit(ISCSIOPT_ERL1, &options))
		sess->erl |= BIT_1;
	if (test_bit(ISCSIOPT_ERL0, &options))
		sess->erl |= BIT_0;

	options = le16_to_cpu(fw_ddb_entry->tcp_options);
	conn->tcp_timestamp_stat = test_bit(TCPOPT_TIMESTAMP_STAT, &options);
	conn->tcp_nagle_disable = test_bit(TCPOPT_NAGLE_DISABLE, &options);
	conn->tcp_wsf_disable = test_bit(TCPOPT_WSF_DISABLE, &options);
	if (test_bit(TCPOPT_TIMER_SCALE3, &options))
		conn->tcp_timer_scale |= BIT_3;
	if (test_bit(TCPOPT_TIMER_SCALE2, &options))
		conn->tcp_timer_scale |= BIT_2;
	if (test_bit(TCPOPT_TIMER_SCALE1, &options))
		conn->tcp_timer_scale |= BIT_1;

	conn->tcp_timer_scale >>= 1;
	conn->tcp_timestamp_en = test_bit(TCPOPT_TIMESTAMP_EN, &options);

	options = le16_to_cpu(fw_ddb_entry->ip_options);
	conn->fragment_disable = test_bit(IPOPT_FRAGMENT_DISABLE, &options);

	conn->max_recv_dlength = BYTE_UNITS *
			  le16_to_cpu(fw_ddb_entry->iscsi_max_rcv_data_seg_len);
	conn->max_xmit_dlength = BYTE_UNITS *
			  le16_to_cpu(fw_ddb_entry->iscsi_max_snd_data_seg_len);
	sess->max_r2t = le16_to_cpu(fw_ddb_entry->iscsi_max_outsnd_r2t);
	sess->first_burst = BYTE_UNITS *
			       le16_to_cpu(fw_ddb_entry->iscsi_first_burst_len);
	sess->max_burst = BYTE_UNITS *
				 le16_to_cpu(fw_ddb_entry->iscsi_max_burst_len);
	sess->time2wait = le16_to_cpu(fw_ddb_entry->iscsi_def_time2wait);
	sess->time2retain = le16_to_cpu(fw_ddb_entry->iscsi_def_time2retain);
	sess->tpgt = le32_to_cpu(fw_ddb_entry->tgt_portal_grp);
	conn->max_segment_size = le16_to_cpu(fw_ddb_entry->mss);
	conn->tcp_xmit_wsf = fw_ddb_entry->tcp_xmt_wsf;
	conn->tcp_recv_wsf = fw_ddb_entry->tcp_rcv_wsf;
	conn->ipv4_tos = fw_ddb_entry->ipv4_tos;
	conn->keepalive_tmo = le16_to_cpu(fw_ddb_entry->ka_timeout);
	conn->local_port = le16_to_cpu(fw_ddb_entry->lcl_port);
	conn->statsn = le32_to_cpu(fw_ddb_entry->stat_sn);
	conn->exp_statsn = le32_to_cpu(fw_ddb_entry->exp_stat_sn);
	sess->tsid = le16_to_cpu(fw_ddb_entry->tsid);
	COPY_ISID(sess->isid, fw_ddb_entry->isid);

	ddb_link = le16_to_cpu(fw_ddb_entry->ddb_link);
	if (ddb_link == DDB_ISNS)
		disc_parent = ISCSI_DISC_PARENT_ISNS;
	else if (ddb_link == DDB_NO_LINK)
		disc_parent = ISCSI_DISC_PARENT_UNKNOWN;
	else if (ddb_link < MAX_DDB_ENTRIES)
		disc_parent = ISCSI_DISC_PARENT_SENDTGT;
	else
		disc_parent = ISCSI_DISC_PARENT_UNKNOWN;

	iscsi_set_param(conn->cls_conn, ISCSI_PARAM_DISCOVERY_PARENT_TYPE,
			iscsi_get_discovery_parent_name(disc_parent), 0);

	iscsi_set_param(conn->cls_conn, ISCSI_PARAM_TARGET_ALIAS,
			(char *)fw_ddb_entry->iscsi_alias, 0);

	options = le16_to_cpu(fw_ddb_entry->options);
	if (options & DDB_OPT_IPV6_DEVICE) {
		memset(ip_addr, 0, sizeof(ip_addr));
		sprintf(ip_addr, "%pI6", fw_ddb_entry->link_local_ipv6_addr);
		iscsi_set_param(conn->cls_conn, ISCSI_PARAM_LOCAL_IPADDR,
				(char *)ip_addr, 0);
	}
}

static void qla4xxx_copy_fwddb_param(struct scsi_qla_host *ha,
				     struct dev_db_entry *fw_ddb_entry,
				     struct iscsi_cls_session *cls_sess,
				     struct iscsi_cls_conn *cls_conn)
{
	int buflen = 0;
	struct iscsi_session *sess;
	struct ddb_entry *ddb_entry;
	struct ql4_chap_table chap_tbl;
	struct iscsi_conn *conn;
	char ip_addr[DDB_IPADDR_LEN];
	uint16_t options = 0;

	sess = cls_sess->dd_data;
	ddb_entry = sess->dd_data;
	conn = cls_conn->dd_data;
	memset(&chap_tbl, 0, sizeof(chap_tbl));

	ddb_entry->chap_tbl_idx = le16_to_cpu(fw_ddb_entry->chap_tbl_idx);

	qla4xxx_copy_to_sess_conn_params(conn, sess, fw_ddb_entry);

	sess->def_taskmgmt_tmo = le16_to_cpu(fw_ddb_entry->def_timeout);
	conn->persistent_port = le16_to_cpu(fw_ddb_entry->port);

	memset(ip_addr, 0, sizeof(ip_addr));
	options = le16_to_cpu(fw_ddb_entry->options);
	if (options & DDB_OPT_IPV6_DEVICE) {
		iscsi_set_param(cls_conn, ISCSI_PARAM_PORTAL_TYPE, "ipv6", 4);

		memset(ip_addr, 0, sizeof(ip_addr));
		sprintf(ip_addr, "%pI6", fw_ddb_entry->ip_addr);
	} else {
		iscsi_set_param(cls_conn, ISCSI_PARAM_PORTAL_TYPE, "ipv4", 4);
		sprintf(ip_addr, "%pI4", fw_ddb_entry->ip_addr);
	}

	iscsi_set_param(cls_conn, ISCSI_PARAM_PERSISTENT_ADDRESS,
			(char *)ip_addr, buflen);
	iscsi_set_param(cls_conn, ISCSI_PARAM_TARGET_NAME,
			(char *)fw_ddb_entry->iscsi_name, buflen);
	iscsi_set_param(cls_conn, ISCSI_PARAM_INITIATOR_NAME,
			(char *)ha->name_string, buflen);

	if (ddb_entry->chap_tbl_idx != INVALID_ENTRY) {
		if (!qla4xxx_get_uni_chap_at_index(ha, chap_tbl.name,
						   chap_tbl.secret,
						   ddb_entry->chap_tbl_idx)) {
			iscsi_set_param(cls_conn, ISCSI_PARAM_USERNAME,
					(char *)chap_tbl.name,
					strlen((char *)chap_tbl.name));
			iscsi_set_param(cls_conn, ISCSI_PARAM_PASSWORD,
					(char *)chap_tbl.secret,
					chap_tbl.secret_len);
		}
	}
}

void qla4xxx_update_session_conn_fwddb_param(struct scsi_qla_host *ha,
					     struct ddb_entry *ddb_entry)
{
	struct iscsi_cls_session *cls_sess;
	struct iscsi_cls_conn *cls_conn;
	uint32_t ddb_state;
	dma_addr_t fw_ddb_entry_dma;
	struct dev_db_entry *fw_ddb_entry;

	fw_ddb_entry = dma_alloc_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
					  &fw_ddb_entry_dma, GFP_KERNEL);
	if (!fw_ddb_entry) {
		ql4_printk(KERN_ERR, ha,
			   "%s: Unable to allocate dma buffer\n", __func__);
		goto exit_session_conn_fwddb_param;
	}

	if (qla4xxx_get_fwddb_entry(ha, ddb_entry->fw_ddb_index, fw_ddb_entry,
				    fw_ddb_entry_dma, NULL, NULL, &ddb_state,
				    NULL, NULL, NULL) == QLA_ERROR) {
		DEBUG2(ql4_printk(KERN_ERR, ha, "scsi%ld: %s: failed "
				  "get_ddb_entry for fw_ddb_index %d\n",
				  ha->host_no, __func__,
				  ddb_entry->fw_ddb_index));
		goto exit_session_conn_fwddb_param;
	}

	cls_sess = ddb_entry->sess;

	cls_conn = ddb_entry->conn;

	/* Update params */
	qla4xxx_copy_fwddb_param(ha, fw_ddb_entry, cls_sess, cls_conn);

exit_session_conn_fwddb_param:
	if (fw_ddb_entry)
		dma_free_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
				  fw_ddb_entry, fw_ddb_entry_dma);
}

void qla4xxx_update_session_conn_param(struct scsi_qla_host *ha,
				       struct ddb_entry *ddb_entry)
{
	struct iscsi_cls_session *cls_sess;
	struct iscsi_cls_conn *cls_conn;
	struct iscsi_session *sess;
	struct iscsi_conn *conn;
	uint32_t ddb_state;
	dma_addr_t fw_ddb_entry_dma;
	struct dev_db_entry *fw_ddb_entry;

	fw_ddb_entry = dma_alloc_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
					  &fw_ddb_entry_dma, GFP_KERNEL);
	if (!fw_ddb_entry) {
		ql4_printk(KERN_ERR, ha,
			   "%s: Unable to allocate dma buffer\n", __func__);
		goto exit_session_conn_param;
	}

	if (qla4xxx_get_fwddb_entry(ha, ddb_entry->fw_ddb_index, fw_ddb_entry,
				    fw_ddb_entry_dma, NULL, NULL, &ddb_state,
				    NULL, NULL, NULL) == QLA_ERROR) {
		DEBUG2(ql4_printk(KERN_ERR, ha, "scsi%ld: %s: failed "
				  "get_ddb_entry for fw_ddb_index %d\n",
				  ha->host_no, __func__,
				  ddb_entry->fw_ddb_index));
		goto exit_session_conn_param;
	}

	cls_sess = ddb_entry->sess;
	sess = cls_sess->dd_data;

	cls_conn = ddb_entry->conn;
	conn = cls_conn->dd_data;

	/* Update timers after login */
	ddb_entry->default_relogin_timeout =
		(le16_to_cpu(fw_ddb_entry->def_timeout) > LOGIN_TOV) &&
		 (le16_to_cpu(fw_ddb_entry->def_timeout) < LOGIN_TOV * 10) ?
		 le16_to_cpu(fw_ddb_entry->def_timeout) : LOGIN_TOV;
	ddb_entry->default_time2wait =
				le16_to_cpu(fw_ddb_entry->iscsi_def_time2wait);

	/* Update params */
	ddb_entry->chap_tbl_idx = le16_to_cpu(fw_ddb_entry->chap_tbl_idx);
	qla4xxx_copy_to_sess_conn_params(conn, sess, fw_ddb_entry);

	memcpy(sess->initiatorname, ha->name_string,
	       min(sizeof(ha->name_string), sizeof(sess->initiatorname)));

exit_session_conn_param:
	if (fw_ddb_entry)
		dma_free_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
				  fw_ddb_entry, fw_ddb_entry_dma);
}

/*
 * Timer routines
 */
static void qla4xxx_timer(struct timer_list *t);

static void qla4xxx_start_timer(struct scsi_qla_host *ha,
				unsigned long interval)
{
	DEBUG(printk("scsi: %s: Starting timer thread for adapter %d\n",
		     __func__, ha->host->host_no));
	timer_setup(&ha->timer, qla4xxx_timer, 0);
	ha->timer.expires = jiffies + interval * HZ;
	add_timer(&ha->timer);
	ha->timer_active = 1;
}

static void qla4xxx_stop_timer(struct scsi_qla_host *ha)
{
	del_timer_sync(&ha->timer);
	ha->timer_active = 0;
}

/***
 * qla4xxx_mark_device_missing - blocks the session
 * @cls_session: Pointer to the session to be blocked
 * @ddb_entry: Pointer to device database entry
 *
 * This routine marks a device missing and close connection.
 **/
void qla4xxx_mark_device_missing(struct iscsi_cls_session *cls_session)
{
	iscsi_block_session(cls_session);
}

/**
 * qla4xxx_mark_all_devices_missing - mark all devices as missing.
 * @ha: Pointer to host adapter structure.
 *
 * This routine marks a device missing and resets the relogin retry count.
 **/
void qla4xxx_mark_all_devices_missing(struct scsi_qla_host *ha)
{
	iscsi_host_for_each_session(ha->host, qla4xxx_mark_device_missing);
}

static struct srb* qla4xxx_get_new_srb(struct scsi_qla_host *ha,
				       struct ddb_entry *ddb_entry,
				       struct scsi_cmnd *cmd)
{
	struct srb *srb;

	srb = mempool_alloc(ha->srb_mempool, GFP_ATOMIC);
	if (!srb)
		return srb;

	kref_init(&srb->srb_ref);
	srb->ha = ha;
	srb->ddb = ddb_entry;
	srb->cmd = cmd;
	srb->flags = 0;
	qla4xxx_cmd_priv(cmd)->srb = srb;

	return srb;
}

static void qla4xxx_srb_free_dma(struct scsi_qla_host *ha, struct srb *srb)
{
	struct scsi_cmnd *cmd = srb->cmd;

	if (srb->flags & SRB_DMA_VALID) {
		scsi_dma_unmap(cmd);
		srb->flags &= ~SRB_DMA_VALID;
	}
	qla4xxx_cmd_priv(cmd)->srb = NULL;
}

void qla4xxx_srb_compl(struct kref *ref)
{
	struct srb *srb = container_of(ref, struct srb, srb_ref);
	struct scsi_cmnd *cmd = srb->cmd;
	struct scsi_qla_host *ha = srb->ha;

	qla4xxx_srb_free_dma(ha, srb);

	mempool_free(srb, ha->srb_mempool);

	scsi_done(cmd);
}

/**
 * qla4xxx_queuecommand - scsi layer issues scsi command to driver.
 * @host: scsi host
 * @cmd: Pointer to Linux's SCSI command structure
 *
 * Remarks:
 * This routine is invoked by Linux to send a SCSI command to the driver.
 * The mid-level driver tries to ensure that queuecommand never gets
 * invoked concurrently with itself or the interrupt handler (although
 * the interrupt handler may call this routine as part of request-
 * completion handling).   Unfortunely, it sometimes calls the scheduler
 * in interrupt context which is a big NO! NO!.
 **/
static int qla4xxx_queuecommand(struct Scsi_Host *host, struct scsi_cmnd *cmd)
{
	struct scsi_qla_host *ha = to_qla_host(host);
	struct ddb_entry *ddb_entry = cmd->device->hostdata;
	struct iscsi_cls_session *sess = ddb_entry->sess;
	struct srb *srb;
	int rval;

	if (test_bit(AF_EEH_BUSY, &ha->flags)) {
		if (test_bit(AF_PCI_CHANNEL_IO_PERM_FAILURE, &ha->flags))
			cmd->result = DID_NO_CONNECT << 16;
		else
			cmd->result = DID_REQUEUE << 16;
		goto qc_fail_command;
	}

	if (!sess) {
		cmd->result = DID_IMM_RETRY << 16;
		goto qc_fail_command;
	}

	rval = iscsi_session_chkready(sess);
	if (rval) {
		cmd->result = rval;
		goto qc_fail_command;
	}

	if (test_bit(DPC_RESET_HA_INTR, &ha->dpc_flags) ||
	    test_bit(DPC_RESET_ACTIVE, &ha->dpc_flags) ||
	    test_bit(DPC_RESET_HA, &ha->dpc_flags) ||
	    test_bit(DPC_HA_UNRECOVERABLE, &ha->dpc_flags) ||
	    test_bit(DPC_HA_NEED_QUIESCENT, &ha->dpc_flags) ||
	    !test_bit(AF_ONLINE, &ha->flags) ||
	    !test_bit(AF_LINK_UP, &ha->flags) ||
	    test_bit(AF_LOOPBACK, &ha->flags) ||
	    test_bit(DPC_POST_IDC_ACK, &ha->dpc_flags) ||
	    test_bit(DPC_RESTORE_ACB, &ha->dpc_flags) ||
	    test_bit(DPC_RESET_HA_FW_CONTEXT, &ha->dpc_flags))
		goto qc_host_busy;

	srb = qla4xxx_get_new_srb(ha, ddb_entry, cmd);
	if (!srb)
		goto qc_host_busy;

	rval = qla4xxx_send_command_to_isp(ha, srb);
	if (rval != QLA_SUCCESS)
		goto qc_host_busy_free_sp;

	return 0;

qc_host_busy_free_sp:
	qla4xxx_srb_free_dma(ha, srb);
	mempool_free(srb, ha->srb_mempool);

qc_host_busy:
	return SCSI_MLQUEUE_HOST_BUSY;

qc_fail_command:
	scsi_done(cmd);

	return 0;
}

/**
 * qla4xxx_mem_free - frees memory allocated to adapter
 * @ha: Pointer to host adapter structure.
 *
 * Frees memory previously allocated by qla4xxx_mem_alloc
 **/
static void qla4xxx_mem_free(struct scsi_qla_host *ha)
{
	if (ha->queues)
		dma_free_coherent(&ha->pdev->dev, ha->queues_len, ha->queues,
				  ha->queues_dma);

	vfree(ha->fw_dump);

	ha->queues_len = 0;
	ha->queues = NULL;
	ha->queues_dma = 0;
	ha->request_ring = NULL;
	ha->request_dma = 0;
	ha->response_ring = NULL;
	ha->response_dma = 0;
	ha->shadow_regs = NULL;
	ha->shadow_regs_dma = 0;
	ha->fw_dump = NULL;
	ha->fw_dump_size = 0;

	/* Free srb pool. */
	mempool_destroy(ha->srb_mempool);
	ha->srb_mempool = NULL;

	dma_pool_destroy(ha->chap_dma_pool);

	vfree(ha->chap_list);
	ha->chap_list = NULL;

	dma_pool_destroy(ha->fw_ddb_dma_pool);

	/* release io space registers  */
	if (is_qla8022(ha)) {
		if (ha->nx_pcibase)
			iounmap(
			    (struct device_reg_82xx __iomem *)ha->nx_pcibase);
	} else if (is_qla8032(ha) || is_qla8042(ha)) {
		if (ha->nx_pcibase)
			iounmap(
			    (struct device_reg_83xx __iomem *)ha->nx_pcibase);
	} else if (ha->reg) {
		iounmap(ha->reg);
	}

	vfree(ha->reset_tmplt.buff);

	pci_release_regions(ha->pdev);
}

/**
 * qla4xxx_mem_alloc - allocates memory for use by adapter.
 * @ha: Pointer to host adapter structure
 *
 * Allocates DMA memory for request and response queues. Also allocates memory
 * for srbs.
 **/
static int qla4xxx_mem_alloc(struct scsi_qla_host *ha)
{
	unsigned long align;

	/* Allocate contiguous block of DMA memory for queues. */
	ha->queues_len = ((REQUEST_QUEUE_DEPTH * QUEUE_SIZE) +
			  (RESPONSE_QUEUE_DEPTH * QUEUE_SIZE) +
			  sizeof(struct shadow_regs) +
			  MEM_ALIGN_VALUE +
			  (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);
	ha->queues = dma_alloc_coherent(&ha->pdev->dev, ha->queues_len,
					&ha->queues_dma, GFP_KERNEL);
	if (ha->queues == NULL) {
		ql4_printk(KERN_WARNING, ha,
		    "Memory Allocation failed - queues.\n");

		goto mem_alloc_error_exit;
	}

	/*
	 * As per RISC alignment requirements -- the bus-address must be a
	 * multiple of the request-ring size (in bytes).
	 */
	align = 0;
	if ((unsigned long)ha->queues_dma & (MEM_ALIGN_VALUE - 1))
		align = MEM_ALIGN_VALUE - ((unsigned long)ha->queues_dma &
					   (MEM_ALIGN_VALUE - 1));

	/* Update request and response queue pointers. */
	ha->request_dma = ha->queues_dma + align;
	ha->request_ring = (struct queue_entry *) (ha->queues + align);
	ha->response_dma = ha->queues_dma + align +
		(REQUEST_QUEUE_DEPTH * QUEUE_SIZE);
	ha->response_ring = (struct queue_entry *) (ha->queues + align +
						    (REQUEST_QUEUE_DEPTH *
						     QUEUE_SIZE));
	ha->shadow_regs_dma = ha->queues_dma + align +
		(REQUEST_QUEUE_DEPTH * QUEUE_SIZE) +
		(RESPONSE_QUEUE_DEPTH * QUEUE_SIZE);
	ha->shadow_regs = (struct shadow_regs *) (ha->queues + align +
						  (REQUEST_QUEUE_DEPTH *
						   QUEUE_SIZE) +
						  (RESPONSE_QUEUE_DEPTH *
						   QUEUE_SIZE));

	/* Allocate memory for srb pool. */
	ha->srb_mempool = mempool_create(SRB_MIN_REQ, mempool_alloc_slab,
					 mempool_free_slab, srb_cachep);
	if (ha->srb_mempool == NULL) {
		ql4_printk(KERN_WARNING, ha,
		    "Memory Allocation failed - SRB Pool.\n");

		goto mem_alloc_error_exit;
	}

	ha->chap_dma_pool = dma_pool_create("ql4_chap", &ha->pdev->dev,
					    CHAP_DMA_BLOCK_SIZE, 8, 0);

	if (ha->chap_dma_pool == NULL) {
		ql4_printk(KERN_WARNING, ha,
		    "%s: chap_dma_pool allocation failed..\n", __func__);
		goto mem_alloc_error_exit;
	}

	ha->fw_ddb_dma_pool = dma_pool_create("ql4_fw_ddb", &ha->pdev->dev,
					      DDB_DMA_BLOCK_SIZE, 8, 0);

	if (ha->fw_ddb_dma_pool == NULL) {
		ql4_printk(KERN_WARNING, ha,
			   "%s: fw_ddb_dma_pool allocation failed..\n",
			   __func__);
		goto mem_alloc_error_exit;
	}

	return QLA_SUCCESS;

mem_alloc_error_exit:
	return QLA_ERROR;
}

/**
 * qla4_8xxx_check_temp - Check the ISP82XX temperature.
 * @ha: adapter block pointer.
 *
 * Note: The caller should not hold the idc lock.
 **/
static int qla4_8xxx_check_temp(struct scsi_qla_host *ha)
{
	uint32_t temp, temp_state, temp_val;
	int status = QLA_SUCCESS;

	temp = qla4_8xxx_rd_direct(ha, QLA8XXX_CRB_TEMP_STATE);

	temp_state = qla82xx_get_temp_state(temp);
	temp_val = qla82xx_get_temp_val(temp);

	if (temp_state == QLA82XX_TEMP_PANIC) {
		ql4_printk(KERN_WARNING, ha, "Device temperature %d degrees C"
			   " exceeds maximum allowed. Hardware has been shut"
			   " down.\n", temp_val);
		status = QLA_ERROR;
	} else if (temp_state == QLA82XX_TEMP_WARN) {
		if (ha->temperature == QLA82XX_TEMP_NORMAL)
			ql4_printk(KERN_WARNING, ha, "Device temperature %d"
				   " degrees C exceeds operating range."
				   " Immediate action needed.\n", temp_val);
	} else {
		if (ha->temperature == QLA82XX_TEMP_WARN)
			ql4_printk(KERN_INFO, ha, "Device temperature is"
				   " now %d degrees C in normal range.\n",
				   temp_val);
	}
	ha->temperature = temp_state;
	return status;
}

/**
 * qla4_8xxx_check_fw_alive  - Check firmware health
 * @ha: Pointer to host adapter structure.
 *
 * Context: Interrupt
 **/
static int qla4_8xxx_check_fw_alive(struct scsi_qla_host *ha)
{
	uint32_t fw_heartbeat_counter;
	int status = QLA_SUCCESS;

	fw_heartbeat_counter = qla4_8xxx_rd_direct(ha,
						   QLA8XXX_PEG_ALIVE_COUNTER);
	/* If PEG_ALIVE_COUNTER is 0xffffffff, AER/EEH is in progress, ignore */
	if (fw_heartbeat_counter == 0xffffffff) {
		DEBUG2(printk(KERN_WARNING "scsi%ld: %s: Device in frozen "
		    "state, QLA82XX_PEG_ALIVE_COUNTER is 0xffffffff\n",
		    ha->host_no, __func__));
		return status;
	}

	if (ha->fw_heartbeat_counter == fw_heartbeat_counter) {
		ha->seconds_since_last_heartbeat++;
		/* FW not alive after 2 seconds */
		if (ha->seconds_since_last_heartbeat == 2) {
			ha->seconds_since_last_heartbeat = 0;
			qla4_8xxx_dump_peg_reg(ha);
			status = QLA_ERROR;
		}
	} else
		ha->seconds_since_last_heartbeat = 0;

	ha->fw_heartbeat_counter = fw_heartbeat_counter;
	return status;
}

static void qla4_8xxx_process_fw_error(struct scsi_qla_host *ha)
{
	uint32_t halt_status;
	int halt_status_unrecoverable = 0;

	halt_status = qla4_8xxx_rd_direct(ha, QLA8XXX_PEG_HALT_STATUS1);

	if (is_qla8022(ha)) {
		ql4_printk(KERN_INFO, ha, "%s: disabling pause transmit on port 0 & 1.\n",
			   __func__);
		qla4_82xx_wr_32(ha, QLA82XX_CRB_NIU + 0x98,
				CRB_NIU_XG_PAUSE_CTL_P0 |
				CRB_NIU_XG_PAUSE_CTL_P1);

		if (QLA82XX_FWERROR_CODE(halt_status) == 0x67)
			ql4_printk(KERN_ERR, ha, "%s: Firmware aborted with error code 0x00006700. Device is being reset\n",
				   __func__);
		if (halt_status & HALT_STATUS_UNRECOVERABLE)
			halt_status_unrecoverable = 1;
	} else if (is_qla8032(ha) || is_qla8042(ha)) {
		if (halt_status & QLA83XX_HALT_STATUS_FW_RESET)
			ql4_printk(KERN_ERR, ha, "%s: Firmware error detected device is being reset\n",
				   __func__);
		else if (halt_status & QLA83XX_HALT_STATUS_UNRECOVERABLE)
			halt_status_unrecoverable = 1;
	}

	/*
	 * Since we cannot change dev_state in interrupt context,
	 * set appropriate DPC flag then wakeup DPC
	 */
	if (halt_status_unrecoverable) {
		set_bit(DPC_HA_UNRECOVERABLE, &ha->dpc_flags);
	} else {
		ql4_printk(KERN_INFO, ha, "%s: detect abort needed!\n",
			   __func__);
		set_bit(DPC_RESET_HA, &ha->dpc_flags);
	}
	qla4xxx_mailbox_premature_completion(ha);
	qla4xxx_wake_dpc(ha);
}

/**
 * qla4_8xxx_watchdog - Poll dev state
 * @ha: Pointer to host adapter structure.
 *
 * Context: Interrupt
 **/
void qla4_8xxx_watchdog(struct scsi_qla_host *ha)
{
	uint32_t dev_state;
	uint32_t idc_ctrl;

	if (is_qla8032(ha) &&
	    (qla4_83xx_is_detached(ha) == QLA_SUCCESS))
		WARN_ONCE(1, "%s: iSCSI function %d marked invisible\n",
			  __func__, ha->func_num);

	/* don't poll if reset is going on */
	if (!(test_bit(DPC_RESET_ACTIVE, &ha->dpc_flags) ||
	    test_bit(DPC_RESET_HA, &ha->dpc_flags) ||
	    test_bit(DPC_RETRY_RESET_HA, &ha->dpc_flags))) {
		dev_state = qla4_8xxx_rd_direct(ha, QLA8XXX_CRB_DEV_STATE);

		if (qla4_8xxx_check_temp(ha)) {
			if (is_qla8022(ha)) {
				ql4_printk(KERN_INFO, ha, "disabling pause transmit on port 0 & 1.\n");
				qla4_82xx_wr_32(ha, QLA82XX_CRB_NIU + 0x98,
						CRB_NIU_XG_PAUSE_CTL_P0 |
						CRB_NIU_XG_PAUSE_CTL_P1);
			}
			set_bit(DPC_HA_UNRECOVERABLE, &ha->dpc_flags);
			qla4xxx_wake_dpc(ha);
		} else if (dev_state == QLA8XXX_DEV_NEED_RESET &&
			   !test_bit(DPC_RESET_HA, &ha->dpc_flags)) {

			ql4_printk(KERN_INFO, ha, "%s: HW State: NEED RESET!\n",
				   __func__);

			if (is_qla8032(ha) || is_qla8042(ha)) {
				idc_ctrl = qla4_83xx_rd_reg(ha,
							QLA83XX_IDC_DRV_CTRL);
				if (!(idc_ctrl & GRACEFUL_RESET_BIT1)) {
					ql4_printk(KERN_INFO, ha, "%s: Graceful reset bit is not set\n",
						   __func__);
					qla4xxx_mailbox_premature_completion(
									    ha);
				}
			}

			if ((is_qla8032(ha) || is_qla8042(ha)) ||
			    (is_qla8022(ha) && !ql4xdontresethba)) {
				set_bit(DPC_RESET_HA, &ha->dpc_flags);
				qla4xxx_wake_dpc(ha);
			}
		} else if (dev_state == QLA8XXX_DEV_NEED_QUIESCENT &&
		    !test_bit(DPC_HA_NEED_QUIESCENT, &ha->dpc_flags)) {
			ql4_printk(KERN_INFO, ha, "%s: HW State: NEED QUIES!\n",
			    __func__);
			set_bit(DPC_HA_NEED_QUIESCENT, &ha->dpc_flags);
			qla4xxx_wake_dpc(ha);
		} else  {
			/* Check firmware health */
			if (qla4_8xxx_check_fw_alive(ha))
				qla4_8xxx_process_fw_error(ha);
		}
	}
}

static void qla4xxx_check_relogin_flash_ddb(struct iscsi_cls_session *cls_sess)
{
	struct iscsi_session *sess;
	struct ddb_entry *ddb_entry;
	struct scsi_qla_host *ha;

	sess = cls_sess->dd_data;
	ddb_entry = sess->dd_data;
	ha = ddb_entry->ha;

	if (!(ddb_entry->ddb_type == FLASH_DDB))
		return;

	if (adapter_up(ha) && !test_bit(DF_RELOGIN, &ddb_entry->flags) &&
	    !iscsi_is_session_online(cls_sess)) {
		if (atomic_read(&ddb_entry->retry_relogin_timer) !=
		    INVALID_ENTRY) {
			if (atomic_read(&ddb_entry->retry_relogin_timer) ==
					0) {
				atomic_set(&ddb_entry->retry_relogin_timer,
					   INVALID_ENTRY);
				set_bit(DPC_RELOGIN_DEVICE, &ha->dpc_flags);
				set_bit(DF_RELOGIN, &ddb_entry->flags);
				DEBUG2(ql4_printk(KERN_INFO, ha,
				       "%s: index [%d] login device\n",
					__func__, ddb_entry->fw_ddb_index));
			} else
				atomic_dec(&ddb_entry->retry_relogin_timer);
		}
	}

	/* Wait for relogin to timeout */
	if (atomic_read(&ddb_entry->relogin_timer) &&
	    (atomic_dec_and_test(&ddb_entry->relogin_timer) != 0)) {
		/*
		 * If the relogin times out and the device is
		 * still NOT ONLINE then try and relogin again.
		 */
		if (!iscsi_is_session_online(cls_sess)) {
			/* Reset retry relogin timer */
			atomic_inc(&ddb_entry->relogin_retry_count);
			DEBUG2(ql4_printk(KERN_INFO, ha,
				"%s: index[%d] relogin timed out-retrying"
				" relogin (%d), retry (%d)\n", __func__,
				ddb_entry->fw_ddb_index,
				atomic_read(&ddb_entry->relogin_retry_count),
				ddb_entry->default_time2wait + 4));
			set_bit(DPC_RELOGIN_DEVICE, &ha->dpc_flags);
			atomic_set(&ddb_entry->retry_relogin_timer,
				   ddb_entry->default_time2wait + 4);
		}
	}
}

/**
 * qla4xxx_timer - checks every second for work to do.
 * @t: Context to obtain pointer to host adapter structure.
 **/
static void qla4xxx_timer(struct timer_list *t)
{
	struct scsi_qla_host *ha = from_timer(ha, t, timer);
	int start_dpc = 0;
	uint16_t w;

	iscsi_host_for_each_session(ha->host, qla4xxx_check_relogin_flash_ddb);

	/* If we are in the middle of AER/EEH processing
	 * skip any processing and reschedule the timer
	 */
	if (test_bit(AF_EEH_BUSY, &ha->flags)) {
		mod_timer(&ha->timer, jiffies + HZ);
		return;
	}

	/* Hardware read to trigger an EEH error during mailbox waits. */
	if (!pci_channel_offline(ha->pdev))
		pci_read_config_word(ha->pdev, PCI_VENDOR_ID, &w);

	if (is_qla80XX(ha))
		qla4_8xxx_watchdog(ha);

	if (is_qla40XX(ha)) {
		/* Check for heartbeat interval. */
		if (ha->firmware_options & FWOPT_HEARTBEAT_ENABLE &&
		    ha->heartbeat_interval != 0) {
			ha->seconds_since_last_heartbeat++;
			if (ha->seconds_since_last_heartbeat >
			    ha->heartbeat_interval + 2)
				set_bit(DPC_RESET_HA, &ha->dpc_flags);
		}
	}

	/* Process any deferred work. */
	if (!list_empty(&ha->work_list))
		start_dpc++;

	/* Wakeup the dpc routine for this adapter, if needed. */
	if (start_dpc ||
	     test_bit(DPC_RESET_HA, &ha->dpc_flags) ||
	     test_bit(DPC_RETRY_RESET_HA, &ha->dpc_flags) ||
	     test_bit(DPC_RELOGIN_DEVICE, &ha->dpc_flags) ||
	     test_bit(DPC_RESET_HA_FW_CONTEXT, &ha->dpc_flags) ||
	     test_bit(DPC_RESET_HA_INTR, &ha->dpc_flags) ||
	     test_bit(DPC_GET_DHCP_IP_ADDR, &ha->dpc_flags) ||
	     test_bit(DPC_LINK_CHANGED, &ha->dpc_flags) ||
	     test_bit(DPC_HA_UNRECOVERABLE, &ha->dpc_flags) ||
	     test_bit(DPC_HA_NEED_QUIESCENT, &ha->dpc_flags) ||
	     test_bit(DPC_SYSFS_DDB_EXPORT, &ha->dpc_flags) ||
	     test_bit(DPC_AEN, &ha->dpc_flags)) {
		DEBUG2(printk("scsi%ld: %s: scheduling dpc routine"
			      " - dpc flags = 0x%lx\n",
			      ha->host_no, __func__, ha->dpc_flags));
		qla4xxx_wake_dpc(ha);
	}

	/* Reschedule timer thread to call us back in one second */
	mod_timer(&ha->timer, jiffies + HZ);

	DEBUG2(ha->seconds_since_last_intr++);
}

/**
 * qla4xxx_cmd_wait - waits for all outstanding commands to complete
 * @ha: Pointer to host adapter structure.
 *
 * This routine stalls the driver until all outstanding commands are returned.
 * Caller must release the Hardware Lock prior to calling this routine.
 **/
static int qla4xxx_cmd_wait(struct scsi_qla_host *ha)
{
	uint32_t index = 0;
	unsigned long flags;
	struct scsi_cmnd *cmd;
	unsigned long wtime;
	uint32_t wtmo;

	if (is_qla40XX(ha))
		wtmo = WAIT_CMD_TOV;
	else
		wtmo = ha->nx_reset_timeout / 2;

	wtime = jiffies + (wtmo * HZ);

	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "Wait up to %u seconds for cmds to complete\n",
			  wtmo));

	while (!time_after_eq(jiffies, wtime)) {
		spin_lock_irqsave(&ha->hardware_lock, flags);
		/* Find a command that hasn't completed. */
		for (index = 0; index < ha->host->can_queue; index++) {
			cmd = scsi_host_find_tag(ha->host, index);
			/*
			 * We cannot just check if the index is valid,
			 * becase if we are run from the scsi eh, then
			 * the scsi/block layer is going to prevent
			 * the tag from being released.
			 */
			if (cmd != NULL && qla4xxx_cmd_priv(cmd)->srb)
				break;
		}
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		/* If No Commands are pending, wait is complete */
		if (index == ha->host->can_queue)
			return QLA_SUCCESS;

		msleep(1000);
	}
	/* If we timed out on waiting for commands to come back
	 * return ERROR. */
	return QLA_ERROR;
}

int qla4xxx_hw_reset(struct scsi_qla_host *ha)
{
	uint32_t ctrl_status;
	unsigned long flags = 0;

	DEBUG2(printk(KERN_ERR "scsi%ld: %s\n", ha->host_no, __func__));

	if (ql4xxx_lock_drvr_wait(ha) != QLA_SUCCESS)
		return QLA_ERROR;

	spin_lock_irqsave(&ha->hardware_lock, flags);

	/*
	 * If the SCSI Reset Interrupt bit is set, clear it.
	 * Otherwise, the Soft Reset won't work.
	 */
	ctrl_status = readw(&ha->reg->ctrl_status);
	if ((ctrl_status & CSR_SCSI_RESET_INTR) != 0)
		writel(set_rmask(CSR_SCSI_RESET_INTR), &ha->reg->ctrl_status);

	/* Issue Soft Reset */
	writel(set_rmask(CSR_SOFT_RESET), &ha->reg->ctrl_status);
	readl(&ha->reg->ctrl_status);

	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return QLA_SUCCESS;
}

/**
 * qla4xxx_soft_reset - performs soft reset.
 * @ha: Pointer to host adapter structure.
 **/
int qla4xxx_soft_reset(struct scsi_qla_host *ha)
{
	uint32_t max_wait_time;
	unsigned long flags = 0;
	int status;
	uint32_t ctrl_status;

	status = qla4xxx_hw_reset(ha);
	if (status != QLA_SUCCESS)
		return status;

	status = QLA_ERROR;
	/* Wait until the Network Reset Intr bit is cleared */
	max_wait_time = RESET_INTR_TOV;
	do {
		spin_lock_irqsave(&ha->hardware_lock, flags);
		ctrl_status = readw(&ha->reg->ctrl_status);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		if ((ctrl_status & CSR_NET_RESET_INTR) == 0)
			break;

		msleep(1000);
	} while ((--max_wait_time));

	if ((ctrl_status & CSR_NET_RESET_INTR) != 0) {
		DEBUG2(printk(KERN_WARNING
			      "scsi%ld: Network Reset Intr not cleared by "
			      "Network function, clearing it now!\n",
			      ha->host_no));
		spin_lock_irqsave(&ha->hardware_lock, flags);
		writel(set_rmask(CSR_NET_RESET_INTR), &ha->reg->ctrl_status);
		readl(&ha->reg->ctrl_status);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
	}

	/* Wait until the firmware tells us the Soft Reset is done */
	max_wait_time = SOFT_RESET_TOV;
	do {
		spin_lock_irqsave(&ha->hardware_lock, flags);
		ctrl_status = readw(&ha->reg->ctrl_status);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		if ((ctrl_status & CSR_SOFT_RESET) == 0) {
			status = QLA_SUCCESS;
			break;
		}

		msleep(1000);
	} while ((--max_wait_time));

	/*
	 * Also, make sure that the SCSI Reset Interrupt bit has been cleared
	 * after the soft reset has taken place.
	 */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	ctrl_status = readw(&ha->reg->ctrl_status);
	if ((ctrl_status & CSR_SCSI_RESET_INTR) != 0) {
		writel(set_rmask(CSR_SCSI_RESET_INTR), &ha->reg->ctrl_status);
		readl(&ha->reg->ctrl_status);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	/* If soft reset fails then most probably the bios on other
	 * function is also enabled.
	 * Since the initialization is sequential the other fn
	 * wont be able to acknowledge the soft reset.
	 * Issue a force soft reset to workaround this scenario.
	 */
	if (max_wait_time == 0) {
		/* Issue Force Soft Reset */
		spin_lock_irqsave(&ha->hardware_lock, flags);
		writel(set_rmask(CSR_FORCE_SOFT_RESET), &ha->reg->ctrl_status);
		readl(&ha->reg->ctrl_status);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		/* Wait until the firmware tells us the Soft Reset is done */
		max_wait_time = SOFT_RESET_TOV;
		do {
			spin_lock_irqsave(&ha->hardware_lock, flags);
			ctrl_status = readw(&ha->reg->ctrl_status);
			spin_unlock_irqrestore(&ha->hardware_lock, flags);

			if ((ctrl_status & CSR_FORCE_SOFT_RESET) == 0) {
				status = QLA_SUCCESS;
				break;
			}

			msleep(1000);
		} while ((--max_wait_time));
	}

	return status;
}

/**
 * qla4xxx_abort_active_cmds - returns all outstanding i/o requests to O.S.
 * @ha: Pointer to host adapter structure.
 * @res: returned scsi status
 *
 * This routine is called just prior to a HARD RESET to return all
 * outstanding commands back to the Operating System.
 * Caller should make sure that the following locks are released
 * before this calling routine: Hardware lock, and io_request_lock.
 **/
static void qla4xxx_abort_active_cmds(struct scsi_qla_host *ha, int res)
{
	struct srb *srb;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (i = 0; i < ha->host->can_queue; i++) {
		srb = qla4xxx_del_from_active_array(ha, i);
		if (srb != NULL) {
			srb->cmd->result = res;
			kref_put(&srb->srb_ref, qla4xxx_srb_compl);
		}
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

void qla4xxx_dead_adapter_cleanup(struct scsi_qla_host *ha)
{
	clear_bit(AF_ONLINE, &ha->flags);

	/* Disable the board */
	ql4_printk(KERN_INFO, ha, "Disabling the board\n");

	qla4xxx_abort_active_cmds(ha, DID_NO_CONNECT << 16);
	qla4xxx_mark_all_devices_missing(ha);
	clear_bit(AF_INIT_DONE, &ha->flags);
}

static void qla4xxx_fail_session(struct iscsi_cls_session *cls_session)
{
	struct iscsi_session *sess;
	struct ddb_entry *ddb_entry;

	sess = cls_session->dd_data;
	ddb_entry = sess->dd_data;
	ddb_entry->fw_ddb_device_state = DDB_DS_SESSION_FAILED;

	if (ddb_entry->ddb_type == FLASH_DDB)
		iscsi_block_session(ddb_entry->sess);
	else
		iscsi_session_failure(cls_session->dd_data,
				      ISCSI_ERR_CONN_FAILED);
}

/**
 * qla4xxx_recover_adapter - recovers adapter after a fatal error
 * @ha: Pointer to host adapter structure.
 **/
static int qla4xxx_recover_adapter(struct scsi_qla_host *ha)
{
	int status = QLA_ERROR;
	uint8_t reset_chip = 0;
	uint32_t dev_state;
	unsigned long wait;

	/* Stall incoming I/O until we are done */
	scsi_block_requests(ha->host);
	clear_bit(AF_ONLINE, &ha->flags);
	clear_bit(AF_LINK_UP, &ha->flags);

	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: adapter OFFLINE\n", __func__));

	set_bit(DPC_RESET_ACTIVE, &ha->dpc_flags);

	if ((is_qla8032(ha) || is_qla8042(ha)) &&
	    !test_bit(DPC_RESET_HA_FW_CONTEXT, &ha->dpc_flags)) {
		ql4_printk(KERN_INFO, ha, "%s: disabling pause transmit on port 0 & 1.\n",
			   __func__);
		/* disable pause frame for ISP83xx */
		qla4_83xx_disable_pause(ha);
	}

	iscsi_host_for_each_session(ha->host, qla4xxx_fail_session);

	if (test_bit(DPC_RESET_HA, &ha->dpc_flags))
		reset_chip = 1;

	/* For the DPC_RESET_HA_INTR case (ISP-4xxx specific)
	 * do not reset adapter, jump to initialize_adapter */
	if (test_bit(DPC_RESET_HA_INTR, &ha->dpc_flags)) {
		status = QLA_SUCCESS;
		goto recover_ha_init_adapter;
	}

	/* For the ISP-8xxx adapter, issue a stop_firmware if invoked
	 * from eh_host_reset or ioctl module */
	if (is_qla80XX(ha) && !reset_chip &&
	    test_bit(DPC_RESET_HA_FW_CONTEXT, &ha->dpc_flags)) {

		DEBUG2(ql4_printk(KERN_INFO, ha,
		    "scsi%ld: %s - Performing stop_firmware...\n",
		    ha->host_no, __func__));
		status = ha->isp_ops->reset_firmware(ha);
		if (status == QLA_SUCCESS) {
			ha->isp_ops->disable_intrs(ha);
			qla4xxx_process_aen(ha, FLUSH_DDB_CHANGED_AENS);
			qla4xxx_abort_active_cmds(ha, DID_RESET << 16);
		} else {
			/* If the stop_firmware fails then
			 * reset the entire chip */
			reset_chip = 1;
			clear_bit(DPC_RESET_HA_FW_CONTEXT, &ha->dpc_flags);
			set_bit(DPC_RESET_HA, &ha->dpc_flags);
		}
	}

	/* Issue full chip reset if recovering from a catastrophic error,
	 * or if stop_firmware fails for ISP-8xxx.
	 * This is the default case for ISP-4xxx */
	if (is_qla40XX(ha) || reset_chip) {
		if (is_qla40XX(ha))
			goto chip_reset;

		/* Check if 8XXX firmware is alive or not
		 * We may have arrived here from NEED_RESET
		 * detection only */
		if (test_bit(AF_FW_RECOVERY, &ha->flags))
			goto chip_reset;

		wait = jiffies + (FW_ALIVE_WAIT_TOV * HZ);
		while (time_before(jiffies, wait)) {
			if (qla4_8xxx_check_fw_alive(ha)) {
				qla4xxx_mailbox_premature_completion(ha);
				break;
			}

			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ);
		}
chip_reset:
		if (!test_bit(AF_FW_RECOVERY, &ha->flags))
			qla4xxx_cmd_wait(ha);

		qla4xxx_process_aen(ha, FLUSH_DDB_CHANGED_AENS);
		DEBUG2(ql4_printk(KERN_INFO, ha,
		    "scsi%ld: %s - Performing chip reset..\n",
		    ha->host_no, __func__));
		status = ha->isp_ops->reset_chip(ha);
		qla4xxx_abort_active_cmds(ha, DID_RESET << 16);
	}

	/* Flush any pending ddb changed AENs */
	qla4xxx_process_aen(ha, FLUSH_DDB_CHANGED_AENS);

recover_ha_init_adapter:
	/* Upon successful firmware/chip reset, re-initialize the adapter */
	if (status == QLA_SUCCESS) {
		/* For ISP-4xxx, force function 1 to always initialize
		 * before function 3 to prevent both funcions from
		 * stepping on top of the other */
		if (is_qla40XX(ha) && (ha->mac_index == 3))
			ssleep(6);

		/* NOTE: AF_ONLINE flag set upon successful completion of
		 * qla4xxx_initialize_adapter */
		status = qla4xxx_initialize_adapter(ha, RESET_ADAPTER);
		if (is_qla80XX(ha) && (status == QLA_ERROR)) {
			status = qla4_8xxx_check_init_adapter_retry(ha);
			if (status == QLA_ERROR) {
				ql4_printk(KERN_INFO, ha, "scsi%ld: %s: Don't retry recover adapter\n",
					   ha->host_no, __func__);
				qla4xxx_dead_adapter_cleanup(ha);
				clear_bit(DPC_RETRY_RESET_HA, &ha->dpc_flags);
				clear_bit(DPC_RESET_HA, &ha->dpc_flags);
				clear_bit(DPC_RESET_HA_FW_CONTEXT,
					  &ha->dpc_flags);
				goto exit_recover;
			}
		}
	}

	/* Retry failed adapter initialization, if necessary
	 * Do not retry initialize_adapter for RESET_HA_INTR (ISP-4xxx specific)
	 * case to prevent ping-pong resets between functions */
	if (!test_bit(AF_ONLINE, &ha->flags) &&
	    !test_bit(DPC_RESET_HA_INTR, &ha->dpc_flags)) {
		/* Adapter initialization failed, see if we can retry
		 * resetting the ha.
		 * Since we don't want to block the DPC for too long
		 * with multiple resets in the same thread,
		 * utilize DPC to retry */
		if (is_qla80XX(ha)) {
			ha->isp_ops->idc_lock(ha);
			dev_state = qla4_8xxx_rd_direct(ha,
							QLA8XXX_CRB_DEV_STATE);
			ha->isp_ops->idc_unlock(ha);
			if (dev_state == QLA8XXX_DEV_FAILED) {
				ql4_printk(KERN_INFO, ha, "%s: don't retry "
					   "recover adapter. H/W is in Failed "
					   "state\n", __func__);
				qla4xxx_dead_adapter_cleanup(ha);
				clear_bit(DPC_RETRY_RESET_HA, &ha->dpc_flags);
				clear_bit(DPC_RESET_HA, &ha->dpc_flags);
				clear_bit(DPC_RESET_HA_FW_CONTEXT,
						&ha->dpc_flags);
				status = QLA_ERROR;

				goto exit_recover;
			}
		}

		if (!test_bit(DPC_RETRY_RESET_HA, &ha->dpc_flags)) {
			ha->retry_reset_ha_cnt = MAX_RESET_HA_RETRIES;
			DEBUG2(printk("scsi%ld: recover adapter - retrying "
				      "(%d) more times\n", ha->host_no,
				      ha->retry_reset_ha_cnt));
			set_bit(DPC_RETRY_RESET_HA, &ha->dpc_flags);
			status = QLA_ERROR;
		} else {
			if (ha->retry_reset_ha_cnt > 0) {
				/* Schedule another Reset HA--DPC will retry */
				ha->retry_reset_ha_cnt--;
				DEBUG2(printk("scsi%ld: recover adapter - "
					      "retry remaining %d\n",
					      ha->host_no,
					      ha->retry_reset_ha_cnt));
				status = QLA_ERROR;
			}

			if (ha->retry_reset_ha_cnt == 0) {
				/* Recover adapter retries have been exhausted.
				 * Adapter DEAD */
				DEBUG2(printk("scsi%ld: recover adapter "
					      "failed - board disabled\n",
					      ha->host_no));
				qla4xxx_dead_adapter_cleanup(ha);
				clear_bit(DPC_RETRY_RESET_HA, &ha->dpc_flags);
				clear_bit(DPC_RESET_HA, &ha->dpc_flags);
				clear_bit(DPC_RESET_HA_FW_CONTEXT,
					  &ha->dpc_flags);
				status = QLA_ERROR;
			}
		}
	} else {
		clear_bit(DPC_RESET_HA, &ha->dpc_flags);
		clear_bit(DPC_RESET_HA_FW_CONTEXT, &ha->dpc_flags);
		clear_bit(DPC_RETRY_RESET_HA, &ha->dpc_flags);
	}

exit_recover:
	ha->adapter_error_count++;

	if (test_bit(AF_ONLINE, &ha->flags))
		ha->isp_ops->enable_intrs(ha);

	scsi_unblock_requests(ha->host);

	clear_bit(DPC_RESET_ACTIVE, &ha->dpc_flags);
	DEBUG2(printk("scsi%ld: recover adapter: %s\n", ha->host_no,
	    status == QLA_ERROR ? "FAILED" : "SUCCEEDED"));

	return status;
}

static void qla4xxx_relogin_devices(struct iscsi_cls_session *cls_session)
{
	struct iscsi_session *sess;
	struct ddb_entry *ddb_entry;
	struct scsi_qla_host *ha;

	sess = cls_session->dd_data;
	ddb_entry = sess->dd_data;
	ha = ddb_entry->ha;
	if (!iscsi_is_session_online(cls_session)) {
		if (ddb_entry->fw_ddb_device_state == DDB_DS_SESSION_ACTIVE) {
			ql4_printk(KERN_INFO, ha, "scsi%ld: %s: ddb[%d]"
				   " unblock session\n", ha->host_no, __func__,
				   ddb_entry->fw_ddb_index);
			iscsi_unblock_session(ddb_entry->sess);
		} else {
			/* Trigger relogin */
			if (ddb_entry->ddb_type == FLASH_DDB) {
				if (!(test_bit(DF_RELOGIN, &ddb_entry->flags) ||
				      test_bit(DF_DISABLE_RELOGIN,
					       &ddb_entry->flags)))
					qla4xxx_arm_relogin_timer(ddb_entry);
			} else
				iscsi_session_failure(cls_session->dd_data,
						      ISCSI_ERR_CONN_FAILED);
		}
	}
}

int qla4xxx_unblock_flash_ddb(struct iscsi_cls_session *cls_session)
{
	struct iscsi_session *sess;
	struct ddb_entry *ddb_entry;
	struct scsi_qla_host *ha;

	sess = cls_session->dd_data;
	ddb_entry = sess->dd_data;
	ha = ddb_entry->ha;
	ql4_printk(KERN_INFO, ha, "scsi%ld: %s: ddb[%d]"
		   " unblock session\n", ha->host_no, __func__,
		   ddb_entry->fw_ddb_index);

	iscsi_unblock_session(ddb_entry->sess);

	/* Start scan target */
	if (test_bit(AF_ONLINE, &ha->flags)) {
		ql4_printk(KERN_INFO, ha, "scsi%ld: %s: ddb[%d]"
			   " start scan\n", ha->host_no, __func__,
			   ddb_entry->fw_ddb_index);
		queue_work(ddb_entry->sess->workq, &ddb_entry->sess->scan_work);
	}
	return QLA_SUCCESS;
}

int qla4xxx_unblock_ddb(struct iscsi_cls_session *cls_session)
{
	struct iscsi_session *sess;
	struct ddb_entry *ddb_entry;
	struct scsi_qla_host *ha;
	int status = QLA_SUCCESS;

	sess = cls_session->dd_data;
	ddb_entry = sess->dd_data;
	ha = ddb_entry->ha;
	ql4_printk(KERN_INFO, ha, "scsi%ld: %s: ddb[%d]"
		   " unblock user space session\n", ha->host_no, __func__,
		   ddb_entry->fw_ddb_index);

	if (!iscsi_is_session_online(cls_session)) {
		iscsi_conn_start(ddb_entry->conn);
		iscsi_conn_login_event(ddb_entry->conn,
				       ISCSI_CONN_STATE_LOGGED_IN);
	} else {
		ql4_printk(KERN_INFO, ha,
			   "scsi%ld: %s: ddb[%d] session [%d] already logged in\n",
			   ha->host_no, __func__, ddb_entry->fw_ddb_index,
			   cls_session->sid);
		status = QLA_ERROR;
	}

	return status;
}

static void qla4xxx_relogin_all_devices(struct scsi_qla_host *ha)
{
	iscsi_host_for_each_session(ha->host, qla4xxx_relogin_devices);
}

static void qla4xxx_relogin_flash_ddb(struct iscsi_cls_session *cls_sess)
{
	uint16_t relogin_timer;
	struct iscsi_session *sess;
	struct ddb_entry *ddb_entry;
	struct scsi_qla_host *ha;

	sess = cls_sess->dd_data;
	ddb_entry = sess->dd_data;
	ha = ddb_entry->ha;

	relogin_timer = max(ddb_entry->default_relogin_timeout,
			    (uint16_t)RELOGIN_TOV);
	atomic_set(&ddb_entry->relogin_timer, relogin_timer);

	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "scsi%ld: Relogin index [%d]. TOV=%d\n", ha->host_no,
			  ddb_entry->fw_ddb_index, relogin_timer));

	qla4xxx_login_flash_ddb(cls_sess);
}

static void qla4xxx_dpc_relogin(struct iscsi_cls_session *cls_sess)
{
	struct iscsi_session *sess;
	struct ddb_entry *ddb_entry;
	struct scsi_qla_host *ha;

	sess = cls_sess->dd_data;
	ddb_entry = sess->dd_data;
	ha = ddb_entry->ha;

	if (!(ddb_entry->ddb_type == FLASH_DDB))
		return;

	if (test_bit(DF_DISABLE_RELOGIN, &ddb_entry->flags))
		return;

	if (test_and_clear_bit(DF_RELOGIN, &ddb_entry->flags) &&
	    !iscsi_is_session_online(cls_sess)) {
		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "relogin issued\n"));
		qla4xxx_relogin_flash_ddb(cls_sess);
	}
}

void qla4xxx_wake_dpc(struct scsi_qla_host *ha)
{
	if (ha->dpc_thread)
		queue_work(ha->dpc_thread, &ha->dpc_work);
}

static struct qla4_work_evt *
qla4xxx_alloc_work(struct scsi_qla_host *ha, uint32_t data_size,
		   enum qla4_work_type type)
{
	struct qla4_work_evt *e;
	uint32_t size = sizeof(struct qla4_work_evt) + data_size;

	e = kzalloc(size, GFP_ATOMIC);
	if (!e)
		return NULL;

	INIT_LIST_HEAD(&e->list);
	e->type = type;
	return e;
}

static void qla4xxx_post_work(struct scsi_qla_host *ha,
			     struct qla4_work_evt *e)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->work_lock, flags);
	list_add_tail(&e->list, &ha->work_list);
	spin_unlock_irqrestore(&ha->work_lock, flags);
	qla4xxx_wake_dpc(ha);
}

int qla4xxx_post_aen_work(struct scsi_qla_host *ha,
			  enum iscsi_host_event_code aen_code,
			  uint32_t data_size, uint8_t *data)
{
	struct qla4_work_evt *e;

	e = qla4xxx_alloc_work(ha, data_size, QLA4_EVENT_AEN);
	if (!e)
		return QLA_ERROR;

	e->u.aen.code = aen_code;
	e->u.aen.data_size = data_size;
	memcpy(e->u.aen.data, data, data_size);

	qla4xxx_post_work(ha, e);

	return QLA_SUCCESS;
}

int qla4xxx_post_ping_evt_work(struct scsi_qla_host *ha,
			       uint32_t status, uint32_t pid,
			       uint32_t data_size, uint8_t *data)
{
	struct qla4_work_evt *e;

	e = qla4xxx_alloc_work(ha, data_size, QLA4_EVENT_PING_STATUS);
	if (!e)
		return QLA_ERROR;

	e->u.ping.status = status;
	e->u.ping.pid = pid;
	e->u.ping.data_size = data_size;
	memcpy(e->u.ping.data, data, data_size);

	qla4xxx_post_work(ha, e);

	return QLA_SUCCESS;
}

static void qla4xxx_do_work(struct scsi_qla_host *ha)
{
	struct qla4_work_evt *e, *tmp;
	unsigned long flags;
	LIST_HEAD(work);

	spin_lock_irqsave(&ha->work_lock, flags);
	list_splice_init(&ha->work_list, &work);
	spin_unlock_irqrestore(&ha->work_lock, flags);

	list_for_each_entry_safe(e, tmp, &work, list) {
		list_del_init(&e->list);

		switch (e->type) {
		case QLA4_EVENT_AEN:
			iscsi_post_host_event(ha->host_no,
					      &qla4xxx_iscsi_transport,
					      e->u.aen.code,
					      e->u.aen.data_size,
					      e->u.aen.data);
			break;
		case QLA4_EVENT_PING_STATUS:
			iscsi_ping_comp_event(ha->host_no,
					      &qla4xxx_iscsi_transport,
					      e->u.ping.status,
					      e->u.ping.pid,
					      e->u.ping.data_size,
					      e->u.ping.data);
			break;
		default:
			ql4_printk(KERN_WARNING, ha, "event type: 0x%x not "
				   "supported", e->type);
		}
		kfree(e);
	}
}

/**
 * qla4xxx_do_dpc - dpc routine
 * @work: Context to obtain pointer to host adapter structure.
 *
 * This routine is a task that is schedule by the interrupt handler
 * to perform the background processing for interrupts.  We put it
 * on a task queue that is consumed whenever the scheduler runs; that's
 * so you can do anything (i.e. put the process to sleep etc).  In fact,
 * the mid-level tries to sleep when it reaches the driver threshold
 * "host->can_queue". This can cause a panic if we were in our interrupt code.
 **/
static void qla4xxx_do_dpc(struct work_struct *work)
{
	struct scsi_qla_host *ha =
		container_of(work, struct scsi_qla_host, dpc_work);
	int status = QLA_ERROR;

	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "scsi%ld: %s: DPC handler waking up. flags = 0x%08lx, dpc_flags = 0x%08lx\n",
			  ha->host_no, __func__, ha->flags, ha->dpc_flags));

	/* Initialization not yet finished. Don't do anything yet. */
	if (!test_bit(AF_INIT_DONE, &ha->flags))
		return;

	if (test_bit(AF_EEH_BUSY, &ha->flags)) {
		DEBUG2(printk(KERN_INFO "scsi%ld: %s: flags = %lx\n",
		    ha->host_no, __func__, ha->flags));
		return;
	}

	/* post events to application */
	qla4xxx_do_work(ha);

	if (is_qla80XX(ha)) {
		if (test_bit(DPC_HA_UNRECOVERABLE, &ha->dpc_flags)) {
			if (is_qla8032(ha) || is_qla8042(ha)) {
				ql4_printk(KERN_INFO, ha, "%s: disabling pause transmit on port 0 & 1.\n",
					   __func__);
				/* disable pause frame for ISP83xx */
				qla4_83xx_disable_pause(ha);
			}

			ha->isp_ops->idc_lock(ha);
			qla4_8xxx_wr_direct(ha, QLA8XXX_CRB_DEV_STATE,
					    QLA8XXX_DEV_FAILED);
			ha->isp_ops->idc_unlock(ha);
			ql4_printk(KERN_INFO, ha, "HW State: FAILED\n");
			qla4_8xxx_device_state_handler(ha);
		}

		if (test_bit(DPC_POST_IDC_ACK, &ha->dpc_flags)) {
			if (is_qla8042(ha)) {
				if (ha->idc_info.info2 &
				    ENABLE_INTERNAL_LOOPBACK) {
					ql4_printk(KERN_INFO, ha, "%s: Disabling ACB\n",
						   __func__);
					status = qla4_84xx_config_acb(ha,
							    ACB_CONFIG_DISABLE);
					if (status != QLA_SUCCESS) {
						ql4_printk(KERN_INFO, ha, "%s: ACB config failed\n",
							   __func__);
					}
				}
			}
			qla4_83xx_post_idc_ack(ha);
			clear_bit(DPC_POST_IDC_ACK, &ha->dpc_flags);
		}

		if (is_qla8042(ha) &&
		    test_bit(DPC_RESTORE_ACB, &ha->dpc_flags)) {
			ql4_printk(KERN_INFO, ha, "%s: Restoring ACB\n",
				   __func__);
			if (qla4_84xx_config_acb(ha, ACB_CONFIG_SET) !=
			    QLA_SUCCESS) {
				ql4_printk(KERN_INFO, ha, "%s: ACB config failed ",
					   __func__);
			}
			clear_bit(DPC_RESTORE_ACB, &ha->dpc_flags);
		}

		if (test_and_clear_bit(DPC_HA_NEED_QUIESCENT, &ha->dpc_flags)) {
			qla4_8xxx_need_qsnt_handler(ha);
		}
	}

	if (!test_bit(DPC_RESET_ACTIVE, &ha->dpc_flags) &&
	    (test_bit(DPC_RESET_HA, &ha->dpc_flags) ||
	    test_bit(DPC_RESET_HA_INTR, &ha->dpc_flags) ||
	    test_bit(DPC_RESET_HA_FW_CONTEXT, &ha->dpc_flags))) {
		if ((is_qla8022(ha) && ql4xdontresethba) ||
		    ((is_qla8032(ha) || is_qla8042(ha)) &&
		     qla4_83xx_idc_dontreset(ha))) {
			DEBUG2(printk("scsi%ld: %s: Don't Reset HBA\n",
			    ha->host_no, __func__));
			clear_bit(DPC_RESET_HA, &ha->dpc_flags);
			clear_bit(DPC_RESET_HA_INTR, &ha->dpc_flags);
			clear_bit(DPC_RESET_HA_FW_CONTEXT, &ha->dpc_flags);
			goto dpc_post_reset_ha;
		}
		if (test_bit(DPC_RESET_HA_FW_CONTEXT, &ha->dpc_flags) ||
		    test_bit(DPC_RESET_HA, &ha->dpc_flags))
			qla4xxx_recover_adapter(ha);

		if (test_bit(DPC_RESET_HA_INTR, &ha->dpc_flags)) {
			uint8_t wait_time = RESET_INTR_TOV;

			while ((readw(&ha->reg->ctrl_status) &
				(CSR_SOFT_RESET | CSR_FORCE_SOFT_RESET)) != 0) {
				if (--wait_time == 0)
					break;
				msleep(1000);
			}
			if (wait_time == 0)
				DEBUG2(printk("scsi%ld: %s: SR|FSR "
					      "bit not cleared-- resetting\n",
					      ha->host_no, __func__));
			qla4xxx_abort_active_cmds(ha, DID_RESET << 16);
			if (ql4xxx_lock_drvr_wait(ha) == QLA_SUCCESS) {
				qla4xxx_process_aen(ha, FLUSH_DDB_CHANGED_AENS);
				status = qla4xxx_recover_adapter(ha);
			}
			clear_bit(DPC_RESET_HA_INTR, &ha->dpc_flags);
			if (status == QLA_SUCCESS)
				ha->isp_ops->enable_intrs(ha);
		}
	}

dpc_post_reset_ha:
	/* ---- process AEN? --- */
	if (test_and_clear_bit(DPC_AEN, &ha->dpc_flags))
		qla4xxx_process_aen(ha, PROCESS_ALL_AENS);

	/* ---- Get DHCP IP Address? --- */
	if (test_and_clear_bit(DPC_GET_DHCP_IP_ADDR, &ha->dpc_flags))
		qla4xxx_get_dhcp_ip_address(ha);

	/* ---- relogin device? --- */
	if (adapter_up(ha) &&
	    test_and_clear_bit(DPC_RELOGIN_DEVICE, &ha->dpc_flags)) {
		iscsi_host_for_each_session(ha->host, qla4xxx_dpc_relogin);
	}

	/* ---- link change? --- */
	if (!test_bit(AF_LOOPBACK, &ha->flags) &&
	    test_and_clear_bit(DPC_LINK_CHANGED, &ha->dpc_flags)) {
		if (!test_bit(AF_LINK_UP, &ha->flags)) {
			/* ---- link down? --- */
			qla4xxx_mark_all_devices_missing(ha);
		} else {
			/* ---- link up? --- *
			 * F/W will auto login to all devices ONLY ONCE after
			 * link up during driver initialization and runtime
			 * fatal error recovery.  Therefore, the driver must
			 * manually relogin to devices when recovering from
			 * connection failures, logouts, expired KATO, etc. */
			if (test_and_clear_bit(AF_BUILD_DDB_LIST, &ha->flags)) {
				qla4xxx_build_ddb_list(ha, ha->is_reset);
				iscsi_host_for_each_session(ha->host,
						qla4xxx_login_flash_ddb);
			} else
				qla4xxx_relogin_all_devices(ha);
		}
	}
	if (test_and_clear_bit(DPC_SYSFS_DDB_EXPORT, &ha->dpc_flags)) {
		if (qla4xxx_sysfs_ddb_export(ha))
			ql4_printk(KERN_ERR, ha, "%s: Error exporting ddb to sysfs\n",
				   __func__);
	}
}

/**
 * qla4xxx_free_adapter - release the adapter
 * @ha: pointer to adapter structure
 **/
static void qla4xxx_free_adapter(struct scsi_qla_host *ha)
{
	qla4xxx_abort_active_cmds(ha, DID_NO_CONNECT << 16);

	/* Turn-off interrupts on the card. */
	ha->isp_ops->disable_intrs(ha);

	if (is_qla40XX(ha)) {
		writel(set_rmask(CSR_SCSI_PROCESSOR_INTR),
		       &ha->reg->ctrl_status);
		readl(&ha->reg->ctrl_status);
	} else if (is_qla8022(ha)) {
		writel(0, &ha->qla4_82xx_reg->host_int);
		readl(&ha->qla4_82xx_reg->host_int);
	} else if (is_qla8032(ha) || is_qla8042(ha)) {
		writel(0, &ha->qla4_83xx_reg->risc_intr);
		readl(&ha->qla4_83xx_reg->risc_intr);
	}

	/* Remove timer thread, if present */
	if (ha->timer_active)
		qla4xxx_stop_timer(ha);

	/* Kill the kernel thread for this host */
	if (ha->dpc_thread)
		destroy_workqueue(ha->dpc_thread);

	/* Kill the kernel thread for this host */
	if (ha->task_wq)
		destroy_workqueue(ha->task_wq);

	/* Put firmware in known state */
	ha->isp_ops->reset_firmware(ha);

	if (is_qla80XX(ha)) {
		ha->isp_ops->idc_lock(ha);
		qla4_8xxx_clear_drv_active(ha);
		ha->isp_ops->idc_unlock(ha);
	}

	/* Detach interrupts */
	qla4xxx_free_irqs(ha);

	/* free extra memory */
	qla4xxx_mem_free(ha);
}

int qla4_8xxx_iospace_config(struct scsi_qla_host *ha)
{
	int status = 0;
	unsigned long mem_base, mem_len;
	struct pci_dev *pdev = ha->pdev;

	status = pci_request_regions(pdev, DRIVER_NAME);
	if (status) {
		printk(KERN_WARNING
		    "scsi(%ld) Failed to reserve PIO regions (%s) "
		    "status=%d\n", ha->host_no, pci_name(pdev), status);
		goto iospace_error_exit;
	}

	DEBUG2(printk(KERN_INFO "%s: revision-id=%d\n",
	    __func__, pdev->revision));
	ha->revision_id = pdev->revision;

	/* remap phys address */
	mem_base = pci_resource_start(pdev, 0); /* 0 is for BAR 0 */
	mem_len = pci_resource_len(pdev, 0);
	DEBUG2(printk(KERN_INFO "%s: ioremap from %lx a size of %lx\n",
	    __func__, mem_base, mem_len));

	/* mapping of pcibase pointer */
	ha->nx_pcibase = (unsigned long)ioremap(mem_base, mem_len);
	if (!ha->nx_pcibase) {
		printk(KERN_ERR
		    "cannot remap MMIO (%s), aborting\n", pci_name(pdev));
		pci_release_regions(ha->pdev);
		goto iospace_error_exit;
	}

	/* Mapping of IO base pointer, door bell read and write pointer */

	/* mapping of IO base pointer */
	if (is_qla8022(ha)) {
		ha->qla4_82xx_reg = (struct device_reg_82xx  __iomem *)
				    ((uint8_t *)ha->nx_pcibase + 0xbc000 +
				     (ha->pdev->devfn << 11));
		ha->nx_db_wr_ptr = (ha->pdev->devfn == 4 ? QLA82XX_CAM_RAM_DB1 :
				    QLA82XX_CAM_RAM_DB2);
	} else if (is_qla8032(ha) || is_qla8042(ha)) {
		ha->qla4_83xx_reg = (struct device_reg_83xx __iomem *)
				    ((uint8_t *)ha->nx_pcibase);
	}

	return 0;
iospace_error_exit:
	return -ENOMEM;
}

/***
 * qla4xxx_iospace_config - maps registers
 * @ha: pointer to adapter structure
 *
 * This routines maps HBA's registers from the pci address space
 * into the kernel virtual address space for memory mapped i/o.
 **/
int qla4xxx_iospace_config(struct scsi_qla_host *ha)
{
	unsigned long pio, pio_len, pio_flags;
	unsigned long mmio, mmio_len, mmio_flags;

	pio = pci_resource_start(ha->pdev, 0);
	pio_len = pci_resource_len(ha->pdev, 0);
	pio_flags = pci_resource_flags(ha->pdev, 0);
	if (pio_flags & IORESOURCE_IO) {
		if (pio_len < MIN_IOBASE_LEN) {
			ql4_printk(KERN_WARNING, ha,
				"Invalid PCI I/O region size\n");
			pio = 0;
		}
	} else {
		ql4_printk(KERN_WARNING, ha, "region #0 not a PIO resource\n");
		pio = 0;
	}

	/* Use MMIO operations for all accesses. */
	mmio = pci_resource_start(ha->pdev, 1);
	mmio_len = pci_resource_len(ha->pdev, 1);
	mmio_flags = pci_resource_flags(ha->pdev, 1);

	if (!(mmio_flags & IORESOURCE_MEM)) {
		ql4_printk(KERN_ERR, ha,
		    "region #0 not an MMIO resource, aborting\n");

		goto iospace_error_exit;
	}

	if (mmio_len < MIN_IOBASE_LEN) {
		ql4_printk(KERN_ERR, ha,
		    "Invalid PCI mem region size, aborting\n");
		goto iospace_error_exit;
	}

	if (pci_request_regions(ha->pdev, DRIVER_NAME)) {
		ql4_printk(KERN_WARNING, ha,
		    "Failed to reserve PIO/MMIO regions\n");

		goto iospace_error_exit;
	}

	ha->pio_address = pio;
	ha->pio_length = pio_len;
	ha->reg = ioremap(mmio, MIN_IOBASE_LEN);
	if (!ha->reg) {
		ql4_printk(KERN_ERR, ha,
		    "cannot remap MMIO, aborting\n");

		goto iospace_error_exit;
	}

	return 0;

iospace_error_exit:
	return -ENOMEM;
}

static struct isp_operations qla4xxx_isp_ops = {
	.iospace_config         = qla4xxx_iospace_config,
	.pci_config             = qla4xxx_pci_config,
	.disable_intrs          = qla4xxx_disable_intrs,
	.enable_intrs           = qla4xxx_enable_intrs,
	.start_firmware         = qla4xxx_start_firmware,
	.intr_handler           = qla4xxx_intr_handler,
	.interrupt_service_routine = qla4xxx_interrupt_service_routine,
	.reset_chip             = qla4xxx_soft_reset,
	.reset_firmware         = qla4xxx_hw_reset,
	.queue_iocb             = qla4xxx_queue_iocb,
	.complete_iocb          = qla4xxx_complete_iocb,
	.rd_shdw_req_q_out      = qla4xxx_rd_shdw_req_q_out,
	.rd_shdw_rsp_q_in       = qla4xxx_rd_shdw_rsp_q_in,
	.get_sys_info           = qla4xxx_get_sys_info,
	.queue_mailbox_command	= qla4xxx_queue_mbox_cmd,
	.process_mailbox_interrupt = qla4xxx_process_mbox_intr,
};

static struct isp_operations qla4_82xx_isp_ops = {
	.iospace_config         = qla4_8xxx_iospace_config,
	.pci_config             = qla4_8xxx_pci_config,
	.disable_intrs          = qla4_82xx_disable_intrs,
	.enable_intrs           = qla4_82xx_enable_intrs,
	.start_firmware         = qla4_8xxx_load_risc,
	.restart_firmware	= qla4_82xx_try_start_fw,
	.intr_handler           = qla4_82xx_intr_handler,
	.interrupt_service_routine = qla4_82xx_interrupt_service_routine,
	.need_reset		= qla4_8xxx_need_reset,
	.reset_chip             = qla4_82xx_isp_reset,
	.reset_firmware         = qla4_8xxx_stop_firmware,
	.queue_iocb             = qla4_82xx_queue_iocb,
	.complete_iocb          = qla4_82xx_complete_iocb,
	.rd_shdw_req_q_out      = qla4_82xx_rd_shdw_req_q_out,
	.rd_shdw_rsp_q_in       = qla4_82xx_rd_shdw_rsp_q_in,
	.get_sys_info           = qla4_8xxx_get_sys_info,
	.rd_reg_direct		= qla4_82xx_rd_32,
	.wr_reg_direct		= qla4_82xx_wr_32,
	.rd_reg_indirect	= qla4_82xx_md_rd_32,
	.wr_reg_indirect	= qla4_82xx_md_wr_32,
	.idc_lock		= qla4_82xx_idc_lock,
	.idc_unlock		= qla4_82xx_idc_unlock,
	.rom_lock_recovery	= qla4_82xx_rom_lock_recovery,
	.queue_mailbox_command	= qla4_82xx_queue_mbox_cmd,
	.process_mailbox_interrupt = qla4_82xx_process_mbox_intr,
};

static struct isp_operations qla4_83xx_isp_ops = {
	.iospace_config		= qla4_8xxx_iospace_config,
	.pci_config		= qla4_8xxx_pci_config,
	.disable_intrs		= qla4_83xx_disable_intrs,
	.enable_intrs		= qla4_83xx_enable_intrs,
	.start_firmware		= qla4_8xxx_load_risc,
	.restart_firmware	= qla4_83xx_start_firmware,
	.intr_handler		= qla4_83xx_intr_handler,
	.interrupt_service_routine = qla4_83xx_interrupt_service_routine,
	.need_reset		= qla4_8xxx_need_reset,
	.reset_chip		= qla4_83xx_isp_reset,
	.reset_firmware		= qla4_8xxx_stop_firmware,
	.queue_iocb		= qla4_83xx_queue_iocb,
	.complete_iocb		= qla4_83xx_complete_iocb,
	.rd_shdw_req_q_out	= qla4xxx_rd_shdw_req_q_out,
	.rd_shdw_rsp_q_in	= qla4xxx_rd_shdw_rsp_q_in,
	.get_sys_info		= qla4_8xxx_get_sys_info,
	.rd_reg_direct		= qla4_83xx_rd_reg,
	.wr_reg_direct		= qla4_83xx_wr_reg,
	.rd_reg_indirect	= qla4_83xx_rd_reg_indirect,
	.wr_reg_indirect	= qla4_83xx_wr_reg_indirect,
	.idc_lock		= qla4_83xx_drv_lock,
	.idc_unlock		= qla4_83xx_drv_unlock,
	.rom_lock_recovery	= qla4_83xx_rom_lock_recovery,
	.queue_mailbox_command	= qla4_83xx_queue_mbox_cmd,
	.process_mailbox_interrupt = qla4_83xx_process_mbox_intr,
};

uint16_t qla4xxx_rd_shdw_req_q_out(struct scsi_qla_host *ha)
{
	return (uint16_t)le32_to_cpu(ha->shadow_regs->req_q_out);
}

uint16_t qla4_82xx_rd_shdw_req_q_out(struct scsi_qla_host *ha)
{
	return (uint16_t)le32_to_cpu(readl(&ha->qla4_82xx_reg->req_q_out));
}

uint16_t qla4xxx_rd_shdw_rsp_q_in(struct scsi_qla_host *ha)
{
	return (uint16_t)le32_to_cpu(ha->shadow_regs->rsp_q_in);
}

uint16_t qla4_82xx_rd_shdw_rsp_q_in(struct scsi_qla_host *ha)
{
	return (uint16_t)le32_to_cpu(readl(&ha->qla4_82xx_reg->rsp_q_in));
}

static ssize_t qla4xxx_show_boot_eth_info(void *data, int type, char *buf)
{
	struct scsi_qla_host *ha = data;
	char *str = buf;
	int rc;

	switch (type) {
	case ISCSI_BOOT_ETH_FLAGS:
		rc = sprintf(str, "%d\n", (char)SYSFS_FLAG_FW_SEL_BOOT);
		break;
	case ISCSI_BOOT_ETH_INDEX:
		rc = sprintf(str, "0\n");
		break;
	case ISCSI_BOOT_ETH_MAC:
		rc = sysfs_format_mac(str, ha->my_mac,
				      MAC_ADDR_LEN);
		break;
	default:
		rc = -ENOSYS;
		break;
	}
	return rc;
}

static umode_t qla4xxx_eth_get_attr_visibility(void *data, int type)
{
	int rc;

	switch (type) {
	case ISCSI_BOOT_ETH_FLAGS:
	case ISCSI_BOOT_ETH_MAC:
	case ISCSI_BOOT_ETH_INDEX:
		rc = S_IRUGO;
		break;
	default:
		rc = 0;
		break;
	}
	return rc;
}

static ssize_t qla4xxx_show_boot_ini_info(void *data, int type, char *buf)
{
	struct scsi_qla_host *ha = data;
	char *str = buf;
	int rc;

	switch (type) {
	case ISCSI_BOOT_INI_INITIATOR_NAME:
		rc = sprintf(str, "%s\n", ha->name_string);
		break;
	default:
		rc = -ENOSYS;
		break;
	}
	return rc;
}

static umode_t qla4xxx_ini_get_attr_visibility(void *data, int type)
{
	int rc;

	switch (type) {
	case ISCSI_BOOT_INI_INITIATOR_NAME:
		rc = S_IRUGO;
		break;
	default:
		rc = 0;
		break;
	}
	return rc;
}

static ssize_t
qla4xxx_show_boot_tgt_info(struct ql4_boot_session_info *boot_sess, int type,
			   char *buf)
{
	struct ql4_conn_info *boot_conn = &boot_sess->conn_list[0];
	char *str = buf;
	int rc;

	switch (type) {
	case ISCSI_BOOT_TGT_NAME:
		rc = sprintf(buf, "%s\n", (char *)&boot_sess->target_name);
		break;
	case ISCSI_BOOT_TGT_IP_ADDR:
		if (boot_sess->conn_list[0].dest_ipaddr.ip_type == 0x1)
			rc = sprintf(buf, "%pI4\n",
				     &boot_conn->dest_ipaddr.ip_address);
		else
			rc = sprintf(str, "%pI6\n",
				     &boot_conn->dest_ipaddr.ip_address);
		break;
	case ISCSI_BOOT_TGT_PORT:
			rc = sprintf(str, "%d\n", boot_conn->dest_port);
		break;
	case ISCSI_BOOT_TGT_CHAP_NAME:
		rc = sprintf(str,  "%.*s\n",
			     boot_conn->chap.target_chap_name_length,
			     (char *)&boot_conn->chap.target_chap_name);
		break;
	case ISCSI_BOOT_TGT_CHAP_SECRET:
		rc = sprintf(str,  "%.*s\n",
			     boot_conn->chap.target_secret_length,
			     (char *)&boot_conn->chap.target_secret);
		break;
	case ISCSI_BOOT_TGT_REV_CHAP_NAME:
		rc = sprintf(str,  "%.*s\n",
			     boot_conn->chap.intr_chap_name_length,
			     (char *)&boot_conn->chap.intr_chap_name);
		break;
	case ISCSI_BOOT_TGT_REV_CHAP_SECRET:
		rc = sprintf(str,  "%.*s\n",
			     boot_conn->chap.intr_secret_length,
			     (char *)&boot_conn->chap.intr_secret);
		break;
	case ISCSI_BOOT_TGT_FLAGS:
		rc = sprintf(str, "%d\n", (char)SYSFS_FLAG_FW_SEL_BOOT);
		break;
	case ISCSI_BOOT_TGT_NIC_ASSOC:
		rc = sprintf(str, "0\n");
		break;
	default:
		rc = -ENOSYS;
		break;
	}
	return rc;
}

static ssize_t qla4xxx_show_boot_tgt_pri_info(void *data, int type, char *buf)
{
	struct scsi_qla_host *ha = data;
	struct ql4_boot_session_info *boot_sess = &(ha->boot_tgt.boot_pri_sess);

	return qla4xxx_show_boot_tgt_info(boot_sess, type, buf);
}

static ssize_t qla4xxx_show_boot_tgt_sec_info(void *data, int type, char *buf)
{
	struct scsi_qla_host *ha = data;
	struct ql4_boot_session_info *boot_sess = &(ha->boot_tgt.boot_sec_sess);

	return qla4xxx_show_boot_tgt_info(boot_sess, type, buf);
}

static umode_t qla4xxx_tgt_get_attr_visibility(void *data, int type)
{
	int rc;

	switch (type) {
	case ISCSI_BOOT_TGT_NAME:
	case ISCSI_BOOT_TGT_IP_ADDR:
	case ISCSI_BOOT_TGT_PORT:
	case ISCSI_BOOT_TGT_CHAP_NAME:
	case ISCSI_BOOT_TGT_CHAP_SECRET:
	case ISCSI_BOOT_TGT_REV_CHAP_NAME:
	case ISCSI_BOOT_TGT_REV_CHAP_SECRET:
	case ISCSI_BOOT_TGT_NIC_ASSOC:
	case ISCSI_BOOT_TGT_FLAGS:
		rc = S_IRUGO;
		break;
	default:
		rc = 0;
		break;
	}
	return rc;
}

static void qla4xxx_boot_release(void *data)
{
	struct scsi_qla_host *ha = data;

	scsi_host_put(ha->host);
}

static int get_fw_boot_info(struct scsi_qla_host *ha, uint16_t ddb_index[])
{
	dma_addr_t buf_dma;
	uint32_t addr, pri_addr, sec_addr;
	uint32_t offset;
	uint16_t func_num;
	uint8_t val;
	uint8_t *buf = NULL;
	size_t size = 13 * sizeof(uint8_t);
	int ret = QLA_SUCCESS;

	func_num = PCI_FUNC(ha->pdev->devfn);

	ql4_printk(KERN_INFO, ha, "%s: Get FW boot info for 0x%x func %d\n",
		   __func__, ha->pdev->device, func_num);

	if (is_qla40XX(ha)) {
		if (func_num == 1) {
			addr = NVRAM_PORT0_BOOT_MODE;
			pri_addr = NVRAM_PORT0_BOOT_PRI_TGT;
			sec_addr = NVRAM_PORT0_BOOT_SEC_TGT;
		} else if (func_num == 3) {
			addr = NVRAM_PORT1_BOOT_MODE;
			pri_addr = NVRAM_PORT1_BOOT_PRI_TGT;
			sec_addr = NVRAM_PORT1_BOOT_SEC_TGT;
		} else {
			ret = QLA_ERROR;
			goto exit_boot_info;
		}

		/* Check Boot Mode */
		val = rd_nvram_byte(ha, addr);
		if (!(val & 0x07)) {
			DEBUG2(ql4_printk(KERN_INFO, ha, "%s: Adapter boot "
					  "options : 0x%x\n", __func__, val));
			ret = QLA_ERROR;
			goto exit_boot_info;
		}

		/* get primary valid target index */
		val = rd_nvram_byte(ha, pri_addr);
		if (val & BIT_7)
			ddb_index[0] = (val & 0x7f);

		/* get secondary valid target index */
		val = rd_nvram_byte(ha, sec_addr);
		if (val & BIT_7)
			ddb_index[1] = (val & 0x7f);
		goto exit_boot_info;
	} else if (is_qla80XX(ha)) {
		buf = dma_alloc_coherent(&ha->pdev->dev, size,
					 &buf_dma, GFP_KERNEL);
		if (!buf) {
			DEBUG2(ql4_printk(KERN_ERR, ha,
					  "%s: Unable to allocate dma buffer\n",
					   __func__));
			ret = QLA_ERROR;
			goto exit_boot_info;
		}

		if (ha->port_num == 0)
			offset = BOOT_PARAM_OFFSET_PORT0;
		else if (ha->port_num == 1)
			offset = BOOT_PARAM_OFFSET_PORT1;
		else {
			ret = QLA_ERROR;
			goto exit_boot_info_free;
		}
		addr = FLASH_RAW_ACCESS_ADDR + (ha->hw.flt_iscsi_param * 4) +
		       offset;
		if (qla4xxx_get_flash(ha, buf_dma, addr,
				      13 * sizeof(uint8_t)) != QLA_SUCCESS) {
			DEBUG2(ql4_printk(KERN_ERR, ha, "scsi%ld: %s: Get Flash"
					  " failed\n", ha->host_no, __func__));
			ret = QLA_ERROR;
			goto exit_boot_info_free;
		}
		/* Check Boot Mode */
		if (!(buf[1] & 0x07)) {
			DEBUG2(ql4_printk(KERN_INFO, ha, "Firmware boot options"
					  " : 0x%x\n", buf[1]));
			ret = QLA_ERROR;
			goto exit_boot_info_free;
		}

		/* get primary valid target index */
		if (buf[2] & BIT_7)
			ddb_index[0] = buf[2] & 0x7f;

		/* get secondary valid target index */
		if (buf[11] & BIT_7)
			ddb_index[1] = buf[11] & 0x7f;
	} else {
		ret = QLA_ERROR;
		goto exit_boot_info;
	}

	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: Primary target ID %d, Secondary"
			  " target ID %d\n", __func__, ddb_index[0],
			  ddb_index[1]));

exit_boot_info_free:
	dma_free_coherent(&ha->pdev->dev, size, buf, buf_dma);
exit_boot_info:
	ha->pri_ddb_idx = ddb_index[0];
	ha->sec_ddb_idx = ddb_index[1];
	return ret;
}

/**
 * qla4xxx_get_bidi_chap - Get a BIDI CHAP user and password
 * @ha: pointer to adapter structure
 * @username: CHAP username to be returned
 * @password: CHAP password to be returned
 *
 * If a boot entry has BIDI CHAP enabled then we need to set the BIDI CHAP
 * user and password in the sysfs entry in /sys/firmware/iscsi_boot#/.
 * So from the CHAP cache find the first BIDI CHAP entry and set it
 * to the boot record in sysfs.
 **/
static int qla4xxx_get_bidi_chap(struct scsi_qla_host *ha, char *username,
			    char *password)
{
	int i, ret = -EINVAL;
	int max_chap_entries = 0;
	struct ql4_chap_table *chap_table;

	if (is_qla80XX(ha))
		max_chap_entries = (ha->hw.flt_chap_size / 2) /
						sizeof(struct ql4_chap_table);
	else
		max_chap_entries = MAX_CHAP_ENTRIES_40XX;

	if (!ha->chap_list) {
		ql4_printk(KERN_ERR, ha, "Do not have CHAP table cache\n");
		return ret;
	}

	mutex_lock(&ha->chap_sem);
	for (i = 0; i < max_chap_entries; i++) {
		chap_table = (struct ql4_chap_table *)ha->chap_list + i;
		if (chap_table->cookie !=
		    cpu_to_le16(CHAP_VALID_COOKIE)) {
			continue;
		}

		if (chap_table->flags & BIT_7) /* local */
			continue;

		if (!(chap_table->flags & BIT_6)) /* Not BIDI */
			continue;

		strlcpy(password, chap_table->secret, QL4_CHAP_MAX_SECRET_LEN);
		strlcpy(username, chap_table->name, QL4_CHAP_MAX_NAME_LEN);
		ret = 0;
		break;
	}
	mutex_unlock(&ha->chap_sem);

	return ret;
}


static int qla4xxx_get_boot_target(struct scsi_qla_host *ha,
				   struct ql4_boot_session_info *boot_sess,
				   uint16_t ddb_index)
{
	struct ql4_conn_info *boot_conn = &boot_sess->conn_list[0];
	struct dev_db_entry *fw_ddb_entry;
	dma_addr_t fw_ddb_entry_dma;
	uint16_t idx;
	uint16_t options;
	int ret = QLA_SUCCESS;

	fw_ddb_entry = dma_alloc_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
					  &fw_ddb_entry_dma, GFP_KERNEL);
	if (!fw_ddb_entry) {
		DEBUG2(ql4_printk(KERN_ERR, ha,
				  "%s: Unable to allocate dma buffer.\n",
				  __func__));
		ret = QLA_ERROR;
		return ret;
	}

	if (qla4xxx_bootdb_by_index(ha, fw_ddb_entry,
				   fw_ddb_entry_dma, ddb_index)) {
		DEBUG2(ql4_printk(KERN_INFO, ha, "%s: No Flash DDB found at "
				  "index [%d]\n", __func__, ddb_index));
		ret = QLA_ERROR;
		goto exit_boot_target;
	}

	/* Update target name and IP from DDB */
	memcpy(boot_sess->target_name, fw_ddb_entry->iscsi_name,
	       min(sizeof(boot_sess->target_name),
		   sizeof(fw_ddb_entry->iscsi_name)));

	options = le16_to_cpu(fw_ddb_entry->options);
	if (options & DDB_OPT_IPV6_DEVICE) {
		memcpy(&boot_conn->dest_ipaddr.ip_address,
		       &fw_ddb_entry->ip_addr[0], IPv6_ADDR_LEN);
	} else {
		boot_conn->dest_ipaddr.ip_type = 0x1;
		memcpy(&boot_conn->dest_ipaddr.ip_address,
		       &fw_ddb_entry->ip_addr[0], IP_ADDR_LEN);
	}

	boot_conn->dest_port = le16_to_cpu(fw_ddb_entry->port);

	/* update chap information */
	idx = __le16_to_cpu(fw_ddb_entry->chap_tbl_idx);

	if (BIT_7 & le16_to_cpu(fw_ddb_entry->iscsi_options))	{

		DEBUG2(ql4_printk(KERN_INFO, ha, "Setting chap\n"));

		ret = qla4xxx_get_chap(ha, (char *)&boot_conn->chap.
				       target_chap_name,
				       (char *)&boot_conn->chap.target_secret,
				       idx);
		if (ret) {
			ql4_printk(KERN_ERR, ha, "Failed to set chap\n");
			ret = QLA_ERROR;
			goto exit_boot_target;
		}

		boot_conn->chap.target_chap_name_length = QL4_CHAP_MAX_NAME_LEN;
		boot_conn->chap.target_secret_length = QL4_CHAP_MAX_SECRET_LEN;
	}

	if (BIT_4 & le16_to_cpu(fw_ddb_entry->iscsi_options)) {

		DEBUG2(ql4_printk(KERN_INFO, ha, "Setting BIDI chap\n"));

		ret = qla4xxx_get_bidi_chap(ha,
				    (char *)&boot_conn->chap.intr_chap_name,
				    (char *)&boot_conn->chap.intr_secret);

		if (ret) {
			ql4_printk(KERN_ERR, ha, "Failed to set BIDI chap\n");
			ret = QLA_ERROR;
			goto exit_boot_target;
		}

		boot_conn->chap.intr_chap_name_length = QL4_CHAP_MAX_NAME_LEN;
		boot_conn->chap.intr_secret_length = QL4_CHAP_MAX_SECRET_LEN;
	}

exit_boot_target:
	dma_free_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
			  fw_ddb_entry, fw_ddb_entry_dma);
	return ret;
}

static int qla4xxx_get_boot_info(struct scsi_qla_host *ha)
{
	uint16_t ddb_index[2];
	int ret = QLA_ERROR;
	int rval;

	memset(ddb_index, 0, sizeof(ddb_index));
	ddb_index[0] = 0xffff;
	ddb_index[1] = 0xffff;
	ret = get_fw_boot_info(ha, ddb_index);
	if (ret != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_INFO, ha,
				"%s: No boot target configured.\n", __func__));
		return ret;
	}

	if (ql4xdisablesysfsboot)
		return QLA_SUCCESS;

	if (ddb_index[0] == 0xffff)
		goto sec_target;

	rval = qla4xxx_get_boot_target(ha, &(ha->boot_tgt.boot_pri_sess),
				      ddb_index[0]);
	if (rval != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_INFO, ha, "%s: Primary boot target not "
				  "configured\n", __func__));
	} else
		ret = QLA_SUCCESS;

sec_target:
	if (ddb_index[1] == 0xffff)
		goto exit_get_boot_info;

	rval = qla4xxx_get_boot_target(ha, &(ha->boot_tgt.boot_sec_sess),
				      ddb_index[1]);
	if (rval != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_INFO, ha, "%s: Secondary boot target not"
				  " configured\n", __func__));
	} else
		ret = QLA_SUCCESS;

exit_get_boot_info:
	return ret;
}

static int qla4xxx_setup_boot_info(struct scsi_qla_host *ha)
{
	struct iscsi_boot_kobj *boot_kobj;

	if (qla4xxx_get_boot_info(ha) != QLA_SUCCESS)
		return QLA_ERROR;

	if (ql4xdisablesysfsboot) {
		ql4_printk(KERN_INFO, ha,
			   "%s: syfsboot disabled - driver will trigger login "
			   "and publish session for discovery .\n", __func__);
		return QLA_SUCCESS;
	}


	ha->boot_kset = iscsi_boot_create_host_kset(ha->host->host_no);
	if (!ha->boot_kset)
		goto kset_free;

	if (!scsi_host_get(ha->host))
		goto kset_free;
	boot_kobj = iscsi_boot_create_target(ha->boot_kset, 0, ha,
					     qla4xxx_show_boot_tgt_pri_info,
					     qla4xxx_tgt_get_attr_visibility,
					     qla4xxx_boot_release);
	if (!boot_kobj)
		goto put_host;

	if (!scsi_host_get(ha->host))
		goto kset_free;
	boot_kobj = iscsi_boot_create_target(ha->boot_kset, 1, ha,
					     qla4xxx_show_boot_tgt_sec_info,
					     qla4xxx_tgt_get_attr_visibility,
					     qla4xxx_boot_release);
	if (!boot_kobj)
		goto put_host;

	if (!scsi_host_get(ha->host))
		goto kset_free;
	boot_kobj = iscsi_boot_create_initiator(ha->boot_kset, 0, ha,
					       qla4xxx_show_boot_ini_info,
					       qla4xxx_ini_get_attr_visibility,
					       qla4xxx_boot_release);
	if (!boot_kobj)
		goto put_host;

	if (!scsi_host_get(ha->host))
		goto kset_free;
	boot_kobj = iscsi_boot_create_ethernet(ha->boot_kset, 0, ha,
					       qla4xxx_show_boot_eth_info,
					       qla4xxx_eth_get_attr_visibility,
					       qla4xxx_boot_release);
	if (!boot_kobj)
		goto put_host;

	return QLA_SUCCESS;

put_host:
	scsi_host_put(ha->host);
kset_free:
	iscsi_boot_destroy_kset(ha->boot_kset);
	return -ENOMEM;
}


static void qla4xxx_get_param_ddb(struct ddb_entry *ddb_entry,
				  struct ql4_tuple_ddb *tddb)
{
	struct iscsi_cls_session *cls_sess;
	struct iscsi_cls_conn *cls_conn;
	struct iscsi_session *sess;
	struct iscsi_conn *conn;

	DEBUG2(printk(KERN_INFO "Func: %s\n", __func__));
	cls_sess = ddb_entry->sess;
	sess = cls_sess->dd_data;
	cls_conn = ddb_entry->conn;
	conn = cls_conn->dd_data;

	tddb->tpgt = sess->tpgt;
	tddb->port = conn->persistent_port;
	strlcpy(tddb->iscsi_name, sess->targetname, ISCSI_NAME_SIZE);
	strlcpy(tddb->ip_addr, conn->persistent_address, DDB_IPADDR_LEN);
}

static void qla4xxx_convert_param_ddb(struct dev_db_entry *fw_ddb_entry,
				      struct ql4_tuple_ddb *tddb,
				      uint8_t *flash_isid)
{
	uint16_t options = 0;

	tddb->tpgt = le32_to_cpu(fw_ddb_entry->tgt_portal_grp);
	memcpy(&tddb->iscsi_name[0], &fw_ddb_entry->iscsi_name[0],
	       min(sizeof(tddb->iscsi_name), sizeof(fw_ddb_entry->iscsi_name)));

	options = le16_to_cpu(fw_ddb_entry->options);
	if (options & DDB_OPT_IPV6_DEVICE)
		sprintf(tddb->ip_addr, "%pI6", fw_ddb_entry->ip_addr);
	else
		sprintf(tddb->ip_addr, "%pI4", fw_ddb_entry->ip_addr);

	tddb->port = le16_to_cpu(fw_ddb_entry->port);

	if (flash_isid == NULL)
		memcpy(&tddb->isid[0], &fw_ddb_entry->isid[0],
		       sizeof(tddb->isid));
	else
		memcpy(&tddb->isid[0], &flash_isid[0], sizeof(tddb->isid));
}

static int qla4xxx_compare_tuple_ddb(struct scsi_qla_host *ha,
				     struct ql4_tuple_ddb *old_tddb,
				     struct ql4_tuple_ddb *new_tddb,
				     uint8_t is_isid_compare)
{
	if (strcmp(old_tddb->iscsi_name, new_tddb->iscsi_name))
		return QLA_ERROR;

	if (strcmp(old_tddb->ip_addr, new_tddb->ip_addr))
		return QLA_ERROR;

	if (old_tddb->port != new_tddb->port)
		return QLA_ERROR;

	/* For multi sessions, driver generates the ISID, so do not compare
	 * ISID in reset path since it would be a comparison between the
	 * driver generated ISID and firmware generated ISID. This could
	 * lead to adding duplicated DDBs in the list as driver generated
	 * ISID would not match firmware generated ISID.
	 */
	if (is_isid_compare) {
		DEBUG2(ql4_printk(KERN_INFO, ha,
			"%s: old ISID [%pmR] New ISID [%pmR]\n",
			__func__, old_tddb->isid, new_tddb->isid));

		if (memcmp(&old_tddb->isid[0], &new_tddb->isid[0],
			   sizeof(old_tddb->isid)))
			return QLA_ERROR;
	}

	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "Match Found, fw[%d,%d,%s,%s], [%d,%d,%s,%s]",
			  old_tddb->port, old_tddb->tpgt, old_tddb->ip_addr,
			  old_tddb->iscsi_name, new_tddb->port, new_tddb->tpgt,
			  new_tddb->ip_addr, new_tddb->iscsi_name));

	return QLA_SUCCESS;
}

static int qla4xxx_is_session_exists(struct scsi_qla_host *ha,
				     struct dev_db_entry *fw_ddb_entry,
				     uint32_t *index)
{
	struct ddb_entry *ddb_entry;
	struct ql4_tuple_ddb *fw_tddb = NULL;
	struct ql4_tuple_ddb *tmp_tddb = NULL;
	int idx;
	int ret = QLA_ERROR;

	fw_tddb = vzalloc(sizeof(*fw_tddb));
	if (!fw_tddb) {
		DEBUG2(ql4_printk(KERN_WARNING, ha,
				  "Memory Allocation failed.\n"));
		ret = QLA_SUCCESS;
		goto exit_check;
	}

	tmp_tddb = vzalloc(sizeof(*tmp_tddb));
	if (!tmp_tddb) {
		DEBUG2(ql4_printk(KERN_WARNING, ha,
				  "Memory Allocation failed.\n"));
		ret = QLA_SUCCESS;
		goto exit_check;
	}

	qla4xxx_convert_param_ddb(fw_ddb_entry, fw_tddb, NULL);

	for (idx = 0; idx < MAX_DDB_ENTRIES; idx++) {
		ddb_entry = qla4xxx_lookup_ddb_by_fw_index(ha, idx);
		if (ddb_entry == NULL)
			continue;

		qla4xxx_get_param_ddb(ddb_entry, tmp_tddb);
		if (!qla4xxx_compare_tuple_ddb(ha, fw_tddb, tmp_tddb, false)) {
			ret = QLA_SUCCESS; /* found */
			if (index != NULL)
				*index = idx;
			goto exit_check;
		}
	}

exit_check:
	vfree(fw_tddb);
	vfree(tmp_tddb);
	return ret;
}

/**
 * qla4xxx_check_existing_isid - check if target with same isid exist
 *				 in target list
 * @list_nt: list of target
 * @isid: isid to check
 *
 * This routine return QLA_SUCCESS if target with same isid exist
 **/
static int qla4xxx_check_existing_isid(struct list_head *list_nt, uint8_t *isid)
{
	struct qla_ddb_index *nt_ddb_idx, *nt_ddb_idx_tmp;
	struct dev_db_entry *fw_ddb_entry;

	list_for_each_entry_safe(nt_ddb_idx, nt_ddb_idx_tmp, list_nt, list) {
		fw_ddb_entry = &nt_ddb_idx->fw_ddb;

		if (memcmp(&fw_ddb_entry->isid[0], &isid[0],
			   sizeof(nt_ddb_idx->fw_ddb.isid)) == 0) {
			return QLA_SUCCESS;
		}
	}
	return QLA_ERROR;
}

/**
 * qla4xxx_update_isid - compare ddbs and updated isid
 * @ha: Pointer to host adapter structure.
 * @list_nt: list of nt target
 * @fw_ddb_entry: firmware ddb entry
 *
 * This routine update isid if ddbs have same iqn, same isid and
 * different IP addr.
 * Return QLA_SUCCESS if isid is updated.
 **/
static int qla4xxx_update_isid(struct scsi_qla_host *ha,
			       struct list_head *list_nt,
			       struct dev_db_entry *fw_ddb_entry)
{
	uint8_t base_value, i;

	base_value = fw_ddb_entry->isid[1] & 0x1f;
	for (i = 0; i < 8; i++) {
		fw_ddb_entry->isid[1] = (base_value | (i << 5));
		if (qla4xxx_check_existing_isid(list_nt, fw_ddb_entry->isid))
			break;
	}

	if (!qla4xxx_check_existing_isid(list_nt, fw_ddb_entry->isid))
		return QLA_ERROR;

	return QLA_SUCCESS;
}

/**
 * qla4xxx_should_update_isid - check if isid need to update
 * @ha: Pointer to host adapter structure.
 * @old_tddb: ddb tuple
 * @new_tddb: ddb tuple
 *
 * Return QLA_SUCCESS if different IP, different PORT, same iqn,
 * same isid
 **/
static int qla4xxx_should_update_isid(struct scsi_qla_host *ha,
				      struct ql4_tuple_ddb *old_tddb,
				      struct ql4_tuple_ddb *new_tddb)
{
	if (strcmp(old_tddb->ip_addr, new_tddb->ip_addr) == 0) {
		/* Same ip */
		if (old_tddb->port == new_tddb->port)
			return QLA_ERROR;
	}

	if (strcmp(old_tddb->iscsi_name, new_tddb->iscsi_name))
		/* different iqn */
		return QLA_ERROR;

	if (memcmp(&old_tddb->isid[0], &new_tddb->isid[0],
		   sizeof(old_tddb->isid)))
		/* different isid */
		return QLA_ERROR;

	return QLA_SUCCESS;
}

/**
 * qla4xxx_is_flash_ddb_exists - check if fw_ddb_entry already exists in list_nt
 * @ha: Pointer to host adapter structure.
 * @list_nt: list of nt target.
 * @fw_ddb_entry: firmware ddb entry.
 *
 * This routine check if fw_ddb_entry already exists in list_nt to avoid
 * duplicate ddb in list_nt.
 * Return QLA_SUCCESS if duplicate ddb exit in list_nl.
 * Note: This function also update isid of DDB if required.
 **/

static int qla4xxx_is_flash_ddb_exists(struct scsi_qla_host *ha,
				       struct list_head *list_nt,
				       struct dev_db_entry *fw_ddb_entry)
{
	struct qla_ddb_index  *nt_ddb_idx, *nt_ddb_idx_tmp;
	struct ql4_tuple_ddb *fw_tddb = NULL;
	struct ql4_tuple_ddb *tmp_tddb = NULL;
	int rval, ret = QLA_ERROR;

	fw_tddb = vzalloc(sizeof(*fw_tddb));
	if (!fw_tddb) {
		DEBUG2(ql4_printk(KERN_WARNING, ha,
				  "Memory Allocation failed.\n"));
		ret = QLA_SUCCESS;
		goto exit_check;
	}

	tmp_tddb = vzalloc(sizeof(*tmp_tddb));
	if (!tmp_tddb) {
		DEBUG2(ql4_printk(KERN_WARNING, ha,
				  "Memory Allocation failed.\n"));
		ret = QLA_SUCCESS;
		goto exit_check;
	}

	qla4xxx_convert_param_ddb(fw_ddb_entry, fw_tddb, NULL);

	list_for_each_entry_safe(nt_ddb_idx, nt_ddb_idx_tmp, list_nt, list) {
		qla4xxx_convert_param_ddb(&nt_ddb_idx->fw_ddb, tmp_tddb,
					  nt_ddb_idx->flash_isid);
		ret = qla4xxx_compare_tuple_ddb(ha, fw_tddb, tmp_tddb, true);
		/* found duplicate ddb */
		if (ret == QLA_SUCCESS)
			goto exit_check;
	}

	list_for_each_entry_safe(nt_ddb_idx, nt_ddb_idx_tmp, list_nt, list) {
		qla4xxx_convert_param_ddb(&nt_ddb_idx->fw_ddb, tmp_tddb, NULL);

		ret = qla4xxx_should_update_isid(ha, tmp_tddb, fw_tddb);
		if (ret == QLA_SUCCESS) {
			rval = qla4xxx_update_isid(ha, list_nt, fw_ddb_entry);
			if (rval == QLA_SUCCESS)
				ret = QLA_ERROR;
			else
				ret = QLA_SUCCESS;

			goto exit_check;
		}
	}

exit_check:
	vfree(fw_tddb);
	vfree(tmp_tddb);
	return ret;
}

static void qla4xxx_free_ddb_list(struct list_head *list_ddb)
{
	struct qla_ddb_index  *ddb_idx, *ddb_idx_tmp;

	list_for_each_entry_safe(ddb_idx, ddb_idx_tmp, list_ddb, list) {
		list_del_init(&ddb_idx->list);
		vfree(ddb_idx);
	}
}

static struct iscsi_endpoint *qla4xxx_get_ep_fwdb(struct scsi_qla_host *ha,
					struct dev_db_entry *fw_ddb_entry)
{
	struct iscsi_endpoint *ep;
	struct sockaddr_in *addr;
	struct sockaddr_in6 *addr6;
	struct sockaddr *t_addr;
	struct sockaddr_storage *dst_addr;
	char *ip;

	/* TODO: need to destroy on unload iscsi_endpoint*/
	dst_addr = vmalloc(sizeof(*dst_addr));
	if (!dst_addr)
		return NULL;

	if (fw_ddb_entry->options & DDB_OPT_IPV6_DEVICE) {
		t_addr = (struct sockaddr *)dst_addr;
		t_addr->sa_family = AF_INET6;
		addr6 = (struct sockaddr_in6 *)dst_addr;
		ip = (char *)&addr6->sin6_addr;
		memcpy(ip, fw_ddb_entry->ip_addr, IPv6_ADDR_LEN);
		addr6->sin6_port = htons(le16_to_cpu(fw_ddb_entry->port));

	} else {
		t_addr = (struct sockaddr *)dst_addr;
		t_addr->sa_family = AF_INET;
		addr = (struct sockaddr_in *)dst_addr;
		ip = (char *)&addr->sin_addr;
		memcpy(ip, fw_ddb_entry->ip_addr, IP_ADDR_LEN);
		addr->sin_port = htons(le16_to_cpu(fw_ddb_entry->port));
	}

	ep = qla4xxx_ep_connect(ha->host, (struct sockaddr *)dst_addr, 0);
	vfree(dst_addr);
	return ep;
}

static int qla4xxx_verify_boot_idx(struct scsi_qla_host *ha, uint16_t idx)
{
	if (ql4xdisablesysfsboot)
		return QLA_SUCCESS;
	if (idx == ha->pri_ddb_idx || idx == ha->sec_ddb_idx)
		return QLA_ERROR;
	return QLA_SUCCESS;
}

static void qla4xxx_setup_flash_ddb_entry(struct scsi_qla_host *ha,
					  struct ddb_entry *ddb_entry,
					  uint16_t idx)
{
	uint16_t def_timeout;

	ddb_entry->ddb_type = FLASH_DDB;
	ddb_entry->fw_ddb_index = INVALID_ENTRY;
	ddb_entry->fw_ddb_device_state = DDB_DS_NO_CONNECTION_ACTIVE;
	ddb_entry->ha = ha;
	ddb_entry->unblock_sess = qla4xxx_unblock_flash_ddb;
	ddb_entry->ddb_change = qla4xxx_flash_ddb_change;
	ddb_entry->chap_tbl_idx = INVALID_ENTRY;

	atomic_set(&ddb_entry->retry_relogin_timer, INVALID_ENTRY);
	atomic_set(&ddb_entry->relogin_timer, 0);
	atomic_set(&ddb_entry->relogin_retry_count, 0);
	def_timeout = le16_to_cpu(ddb_entry->fw_ddb_entry.def_timeout);
	ddb_entry->default_relogin_timeout =
		(def_timeout > LOGIN_TOV) && (def_timeout < LOGIN_TOV * 10) ?
		def_timeout : LOGIN_TOV;
	ddb_entry->default_time2wait =
		le16_to_cpu(ddb_entry->fw_ddb_entry.iscsi_def_time2wait);

	if (ql4xdisablesysfsboot &&
	    (idx == ha->pri_ddb_idx || idx == ha->sec_ddb_idx))
		set_bit(DF_BOOT_TGT, &ddb_entry->flags);
}

static void qla4xxx_wait_for_ip_configuration(struct scsi_qla_host *ha)
{
	uint32_t idx = 0;
	uint32_t ip_idx[IP_ADDR_COUNT] = {0, 1, 2, 3}; /* 4 IP interfaces */
	uint32_t sts[MBOX_REG_COUNT];
	uint32_t ip_state;
	unsigned long wtime;
	int ret;

	wtime = jiffies + (HZ * IP_CONFIG_TOV);
	do {
		for (idx = 0; idx < IP_ADDR_COUNT; idx++) {
			if (ip_idx[idx] == -1)
				continue;

			ret = qla4xxx_get_ip_state(ha, 0, ip_idx[idx], sts);

			if (ret == QLA_ERROR) {
				ip_idx[idx] = -1;
				continue;
			}

			ip_state = (sts[1] & IP_STATE_MASK) >> IP_STATE_SHIFT;

			DEBUG2(ql4_printk(KERN_INFO, ha,
					  "Waiting for IP state for idx = %d, state = 0x%x\n",
					  ip_idx[idx], ip_state));
			if (ip_state == IP_ADDRSTATE_UNCONFIGURED ||
			    ip_state == IP_ADDRSTATE_INVALID ||
			    ip_state == IP_ADDRSTATE_PREFERRED ||
			    ip_state == IP_ADDRSTATE_DEPRICATED ||
			    ip_state == IP_ADDRSTATE_DISABLING)
				ip_idx[idx] = -1;
		}

		/* Break if all IP states checked */
		if ((ip_idx[0] == -1) &&
		    (ip_idx[1] == -1) &&
		    (ip_idx[2] == -1) &&
		    (ip_idx[3] == -1))
			break;
		schedule_timeout_uninterruptible(HZ);
	} while (time_after(wtime, jiffies));
}

static int qla4xxx_cmp_fw_stentry(struct dev_db_entry *fw_ddb_entry,
				  struct dev_db_entry *flash_ddb_entry)
{
	uint16_t options = 0;
	size_t ip_len = IP_ADDR_LEN;

	options = le16_to_cpu(fw_ddb_entry->options);
	if (options & DDB_OPT_IPV6_DEVICE)
		ip_len = IPv6_ADDR_LEN;

	if (memcmp(fw_ddb_entry->ip_addr, flash_ddb_entry->ip_addr, ip_len))
		return QLA_ERROR;

	if (memcmp(&fw_ddb_entry->isid[0], &flash_ddb_entry->isid[0],
		   sizeof(fw_ddb_entry->isid)))
		return QLA_ERROR;

	if (memcmp(&fw_ddb_entry->port, &flash_ddb_entry->port,
		   sizeof(fw_ddb_entry->port)))
		return QLA_ERROR;

	return QLA_SUCCESS;
}

static int qla4xxx_find_flash_st_idx(struct scsi_qla_host *ha,
				     struct dev_db_entry *fw_ddb_entry,
				     uint32_t fw_idx, uint32_t *flash_index)
{
	struct dev_db_entry *flash_ddb_entry;
	dma_addr_t flash_ddb_entry_dma;
	uint32_t idx = 0;
	int max_ddbs;
	int ret = QLA_ERROR, status;

	max_ddbs =  is_qla40XX(ha) ? MAX_DEV_DB_ENTRIES_40XX :
				     MAX_DEV_DB_ENTRIES;

	flash_ddb_entry = dma_pool_alloc(ha->fw_ddb_dma_pool, GFP_KERNEL,
					 &flash_ddb_entry_dma);
	if (flash_ddb_entry == NULL || fw_ddb_entry == NULL) {
		ql4_printk(KERN_ERR, ha, "Out of memory\n");
		goto exit_find_st_idx;
	}

	status = qla4xxx_flashdb_by_index(ha, flash_ddb_entry,
					  flash_ddb_entry_dma, fw_idx);
	if (status == QLA_SUCCESS) {
		status = qla4xxx_cmp_fw_stentry(fw_ddb_entry, flash_ddb_entry);
		if (status == QLA_SUCCESS) {
			*flash_index = fw_idx;
			ret = QLA_SUCCESS;
			goto exit_find_st_idx;
		}
	}

	for (idx = 0; idx < max_ddbs; idx++) {
		status = qla4xxx_flashdb_by_index(ha, flash_ddb_entry,
						  flash_ddb_entry_dma, idx);
		if (status == QLA_ERROR)
			continue;

		status = qla4xxx_cmp_fw_stentry(fw_ddb_entry, flash_ddb_entry);
		if (status == QLA_SUCCESS) {
			*flash_index = idx;
			ret = QLA_SUCCESS;
			goto exit_find_st_idx;
		}
	}

	if (idx == max_ddbs)
		ql4_printk(KERN_ERR, ha, "Failed to find ST [%d] in flash\n",
			   fw_idx);

exit_find_st_idx:
	if (flash_ddb_entry)
		dma_pool_free(ha->fw_ddb_dma_pool, flash_ddb_entry,
			      flash_ddb_entry_dma);

	return ret;
}

static void qla4xxx_build_st_list(struct scsi_qla_host *ha,
				  struct list_head *list_st)
{
	struct qla_ddb_index  *st_ddb_idx;
	int max_ddbs;
	int fw_idx_size;
	struct dev_db_entry *fw_ddb_entry;
	dma_addr_t fw_ddb_dma;
	int ret;
	uint32_t idx = 0, next_idx = 0;
	uint32_t state = 0, conn_err = 0;
	uint32_t flash_index = -1;
	uint16_t conn_id = 0;

	fw_ddb_entry = dma_pool_alloc(ha->fw_ddb_dma_pool, GFP_KERNEL,
				      &fw_ddb_dma);
	if (fw_ddb_entry == NULL) {
		DEBUG2(ql4_printk(KERN_ERR, ha, "Out of memory\n"));
		goto exit_st_list;
	}

	max_ddbs =  is_qla40XX(ha) ? MAX_DEV_DB_ENTRIES_40XX :
				     MAX_DEV_DB_ENTRIES;
	fw_idx_size = sizeof(struct qla_ddb_index);

	for (idx = 0; idx < max_ddbs; idx = next_idx) {
		ret = qla4xxx_get_fwddb_entry(ha, idx, fw_ddb_entry, fw_ddb_dma,
					      NULL, &next_idx, &state,
					      &conn_err, NULL, &conn_id);
		if (ret == QLA_ERROR)
			break;

		/* Ignore DDB if invalid state (unassigned) */
		if (state == DDB_DS_UNASSIGNED)
			goto continue_next_st;

		/* Check if ST, add to the list_st */
		if (strlen((char *) fw_ddb_entry->iscsi_name) != 0)
			goto continue_next_st;

		st_ddb_idx = vzalloc(fw_idx_size);
		if (!st_ddb_idx)
			break;

		ret = qla4xxx_find_flash_st_idx(ha, fw_ddb_entry, idx,
						&flash_index);
		if (ret == QLA_ERROR) {
			ql4_printk(KERN_ERR, ha,
				   "No flash entry for ST at idx [%d]\n", idx);
			st_ddb_idx->flash_ddb_idx = idx;
		} else {
			ql4_printk(KERN_INFO, ha,
				   "ST at idx [%d] is stored at flash [%d]\n",
				   idx, flash_index);
			st_ddb_idx->flash_ddb_idx = flash_index;
		}

		st_ddb_idx->fw_ddb_idx = idx;

		list_add_tail(&st_ddb_idx->list, list_st);
continue_next_st:
		if (next_idx == 0)
			break;
	}

exit_st_list:
	if (fw_ddb_entry)
		dma_pool_free(ha->fw_ddb_dma_pool, fw_ddb_entry, fw_ddb_dma);
}

/**
 * qla4xxx_remove_failed_ddb - Remove inactive or failed ddb from list
 * @ha: pointer to adapter structure
 * @list_ddb: List from which failed ddb to be removed
 *
 * Iterate over the list of DDBs and find and remove DDBs that are either in
 * no connection active state or failed state
 **/
static void qla4xxx_remove_failed_ddb(struct scsi_qla_host *ha,
				      struct list_head *list_ddb)
{
	struct qla_ddb_index  *ddb_idx, *ddb_idx_tmp;
	uint32_t next_idx = 0;
	uint32_t state = 0, conn_err = 0;
	int ret;

	list_for_each_entry_safe(ddb_idx, ddb_idx_tmp, list_ddb, list) {
		ret = qla4xxx_get_fwddb_entry(ha, ddb_idx->fw_ddb_idx,
					      NULL, 0, NULL, &next_idx, &state,
					      &conn_err, NULL, NULL);
		if (ret == QLA_ERROR)
			continue;

		if (state == DDB_DS_NO_CONNECTION_ACTIVE ||
		    state == DDB_DS_SESSION_FAILED) {
			list_del_init(&ddb_idx->list);
			vfree(ddb_idx);
		}
	}
}

static void qla4xxx_update_sess_disc_idx(struct scsi_qla_host *ha,
					 struct ddb_entry *ddb_entry,
					 struct dev_db_entry *fw_ddb_entry)
{
	struct iscsi_cls_session *cls_sess;
	struct iscsi_session *sess;
	uint32_t max_ddbs = 0;
	uint16_t ddb_link = -1;

	max_ddbs =  is_qla40XX(ha) ? MAX_DEV_DB_ENTRIES_40XX :
				     MAX_DEV_DB_ENTRIES;

	cls_sess = ddb_entry->sess;
	sess = cls_sess->dd_data;

	ddb_link = le16_to_cpu(fw_ddb_entry->ddb_link);
	if (ddb_link < max_ddbs)
		sess->discovery_parent_idx = ddb_link;
	else
		sess->discovery_parent_idx = DDB_NO_LINK;
}

static int qla4xxx_sess_conn_setup(struct scsi_qla_host *ha,
				   struct dev_db_entry *fw_ddb_entry,
				   int is_reset, uint16_t idx)
{
	struct iscsi_cls_session *cls_sess;
	struct iscsi_session *sess;
	struct iscsi_cls_conn *cls_conn;
	struct iscsi_endpoint *ep;
	uint16_t cmds_max = 32;
	uint16_t conn_id = 0;
	uint32_t initial_cmdsn = 0;
	int ret = QLA_SUCCESS;

	struct ddb_entry *ddb_entry = NULL;

	/* Create session object, with INVALID_ENTRY,
	 * the targer_id would get set when we issue the login
	 */
	cls_sess = iscsi_session_setup(&qla4xxx_iscsi_transport, ha->host,
				       cmds_max, sizeof(struct ddb_entry),
				       sizeof(struct ql4_task_data),
				       initial_cmdsn, INVALID_ENTRY);
	if (!cls_sess) {
		ret = QLA_ERROR;
		goto exit_setup;
	}

	/*
	 * so calling module_put function to decrement the
	 * reference count.
	 **/
	module_put(qla4xxx_iscsi_transport.owner);
	sess = cls_sess->dd_data;
	ddb_entry = sess->dd_data;
	ddb_entry->sess = cls_sess;

	cls_sess->recovery_tmo = ql4xsess_recovery_tmo;
	memcpy(&ddb_entry->fw_ddb_entry, fw_ddb_entry,
	       sizeof(struct dev_db_entry));

	qla4xxx_setup_flash_ddb_entry(ha, ddb_entry, idx);

	cls_conn = iscsi_conn_setup(cls_sess, sizeof(struct qla_conn), conn_id);

	if (!cls_conn) {
		ret = QLA_ERROR;
		goto exit_setup;
	}

	ddb_entry->conn = cls_conn;

	/* Setup ep, for displaying attributes in sysfs */
	ep = qla4xxx_get_ep_fwdb(ha, fw_ddb_entry);
	if (ep) {
		ep->conn = cls_conn;
		cls_conn->ep = ep;
	} else {
		DEBUG2(ql4_printk(KERN_ERR, ha, "Unable to get ep\n"));
		ret = QLA_ERROR;
		goto exit_setup;
	}

	/* Update sess/conn params */
	qla4xxx_copy_fwddb_param(ha, fw_ddb_entry, cls_sess, cls_conn);
	qla4xxx_update_sess_disc_idx(ha, ddb_entry, fw_ddb_entry);

	if (is_reset == RESET_ADAPTER) {
		iscsi_block_session(cls_sess);
		/* Use the relogin path to discover new devices
		 *  by short-circuiting the logic of setting
		 *  timer to relogin - instead set the flags
		 *  to initiate login right away.
		 */
		set_bit(DPC_RELOGIN_DEVICE, &ha->dpc_flags);
		set_bit(DF_RELOGIN, &ddb_entry->flags);
	}

exit_setup:
	return ret;
}

static void qla4xxx_update_fw_ddb_link(struct scsi_qla_host *ha,
				       struct list_head *list_ddb,
				       struct dev_db_entry *fw_ddb_entry)
{
	struct qla_ddb_index  *ddb_idx, *ddb_idx_tmp;
	uint16_t ddb_link;

	ddb_link = le16_to_cpu(fw_ddb_entry->ddb_link);

	list_for_each_entry_safe(ddb_idx, ddb_idx_tmp, list_ddb, list) {
		if (ddb_idx->fw_ddb_idx == ddb_link) {
			DEBUG2(ql4_printk(KERN_INFO, ha,
					  "Updating NT parent idx from [%d] to [%d]\n",
					  ddb_link, ddb_idx->flash_ddb_idx));
			fw_ddb_entry->ddb_link =
					    cpu_to_le16(ddb_idx->flash_ddb_idx);
			return;
		}
	}
}

static void qla4xxx_build_nt_list(struct scsi_qla_host *ha,
				  struct list_head *list_nt,
				  struct list_head *list_st,
				  int is_reset)
{
	struct dev_db_entry *fw_ddb_entry;
	struct ddb_entry *ddb_entry = NULL;
	dma_addr_t fw_ddb_dma;
	int max_ddbs;
	int fw_idx_size;
	int ret;
	uint32_t idx = 0, next_idx = 0;
	uint32_t state = 0, conn_err = 0;
	uint32_t ddb_idx = -1;
	uint16_t conn_id = 0;
	uint16_t ddb_link = -1;
	struct qla_ddb_index  *nt_ddb_idx;

	fw_ddb_entry = dma_pool_alloc(ha->fw_ddb_dma_pool, GFP_KERNEL,
				      &fw_ddb_dma);
	if (fw_ddb_entry == NULL) {
		DEBUG2(ql4_printk(KERN_ERR, ha, "Out of memory\n"));
		goto exit_nt_list;
	}
	max_ddbs =  is_qla40XX(ha) ? MAX_DEV_DB_ENTRIES_40XX :
				     MAX_DEV_DB_ENTRIES;
	fw_idx_size = sizeof(struct qla_ddb_index);

	for (idx = 0; idx < max_ddbs; idx = next_idx) {
		ret = qla4xxx_get_fwddb_entry(ha, idx, fw_ddb_entry, fw_ddb_dma,
					      NULL, &next_idx, &state,
					      &conn_err, NULL, &conn_id);
		if (ret == QLA_ERROR)
			break;

		if (qla4xxx_verify_boot_idx(ha, idx) != QLA_SUCCESS)
			goto continue_next_nt;

		/* Check if NT, then add to list it */
		if (strlen((char *) fw_ddb_entry->iscsi_name) == 0)
			goto continue_next_nt;

		ddb_link = le16_to_cpu(fw_ddb_entry->ddb_link);
		if (ddb_link < max_ddbs)
			qla4xxx_update_fw_ddb_link(ha, list_st, fw_ddb_entry);

		if (!(state == DDB_DS_NO_CONNECTION_ACTIVE ||
		    state == DDB_DS_SESSION_FAILED) &&
		    (is_reset == INIT_ADAPTER))
			goto continue_next_nt;

		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "Adding  DDB to session = 0x%x\n", idx));

		if (is_reset == INIT_ADAPTER) {
			nt_ddb_idx = vmalloc(fw_idx_size);
			if (!nt_ddb_idx)
				break;

			nt_ddb_idx->fw_ddb_idx = idx;

			/* Copy original isid as it may get updated in function
			 * qla4xxx_update_isid(). We need original isid in
			 * function qla4xxx_compare_tuple_ddb to find duplicate
			 * target */
			memcpy(&nt_ddb_idx->flash_isid[0],
			       &fw_ddb_entry->isid[0],
			       sizeof(nt_ddb_idx->flash_isid));

			ret = qla4xxx_is_flash_ddb_exists(ha, list_nt,
							  fw_ddb_entry);
			if (ret == QLA_SUCCESS) {
				/* free nt_ddb_idx and do not add to list_nt */
				vfree(nt_ddb_idx);
				goto continue_next_nt;
			}

			/* Copy updated isid */
			memcpy(&nt_ddb_idx->fw_ddb, fw_ddb_entry,
			       sizeof(struct dev_db_entry));

			list_add_tail(&nt_ddb_idx->list, list_nt);
		} else if (is_reset == RESET_ADAPTER) {
			ret = qla4xxx_is_session_exists(ha, fw_ddb_entry,
							&ddb_idx);
			if (ret == QLA_SUCCESS) {
				ddb_entry = qla4xxx_lookup_ddb_by_fw_index(ha,
								       ddb_idx);
				if (ddb_entry != NULL)
					qla4xxx_update_sess_disc_idx(ha,
								     ddb_entry,
								  fw_ddb_entry);
				goto continue_next_nt;
			}
		}

		ret = qla4xxx_sess_conn_setup(ha, fw_ddb_entry, is_reset, idx);
		if (ret == QLA_ERROR)
			goto exit_nt_list;

continue_next_nt:
		if (next_idx == 0)
			break;
	}

exit_nt_list:
	if (fw_ddb_entry)
		dma_pool_free(ha->fw_ddb_dma_pool, fw_ddb_entry, fw_ddb_dma);
}

static void qla4xxx_build_new_nt_list(struct scsi_qla_host *ha,
				      struct list_head *list_nt,
				      uint16_t target_id)
{
	struct dev_db_entry *fw_ddb_entry;
	dma_addr_t fw_ddb_dma;
	int max_ddbs;
	int fw_idx_size;
	int ret;
	uint32_t idx = 0, next_idx = 0;
	uint32_t state = 0, conn_err = 0;
	uint16_t conn_id = 0;
	struct qla_ddb_index  *nt_ddb_idx;

	fw_ddb_entry = dma_pool_alloc(ha->fw_ddb_dma_pool, GFP_KERNEL,
				      &fw_ddb_dma);
	if (fw_ddb_entry == NULL) {
		DEBUG2(ql4_printk(KERN_ERR, ha, "Out of memory\n"));
		goto exit_new_nt_list;
	}
	max_ddbs =  is_qla40XX(ha) ? MAX_DEV_DB_ENTRIES_40XX :
				     MAX_DEV_DB_ENTRIES;
	fw_idx_size = sizeof(struct qla_ddb_index);

	for (idx = 0; idx < max_ddbs; idx = next_idx) {
		ret = qla4xxx_get_fwddb_entry(ha, idx, fw_ddb_entry, fw_ddb_dma,
					      NULL, &next_idx, &state,
					      &conn_err, NULL, &conn_id);
		if (ret == QLA_ERROR)
			break;

		/* Check if NT, then add it to list */
		if (strlen((char *)fw_ddb_entry->iscsi_name) == 0)
			goto continue_next_new_nt;

		if (!(state == DDB_DS_NO_CONNECTION_ACTIVE))
			goto continue_next_new_nt;

		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "Adding  DDB to session = 0x%x\n", idx));

		nt_ddb_idx = vmalloc(fw_idx_size);
		if (!nt_ddb_idx)
			break;

		nt_ddb_idx->fw_ddb_idx = idx;

		ret = qla4xxx_is_session_exists(ha, fw_ddb_entry, NULL);
		if (ret == QLA_SUCCESS) {
			/* free nt_ddb_idx and do not add to list_nt */
			vfree(nt_ddb_idx);
			goto continue_next_new_nt;
		}

		if (target_id < max_ddbs)
			fw_ddb_entry->ddb_link = cpu_to_le16(target_id);

		list_add_tail(&nt_ddb_idx->list, list_nt);

		ret = qla4xxx_sess_conn_setup(ha, fw_ddb_entry, RESET_ADAPTER,
					      idx);
		if (ret == QLA_ERROR)
			goto exit_new_nt_list;

continue_next_new_nt:
		if (next_idx == 0)
			break;
	}

exit_new_nt_list:
	if (fw_ddb_entry)
		dma_pool_free(ha->fw_ddb_dma_pool, fw_ddb_entry, fw_ddb_dma);
}

/**
 * qla4xxx_sysfs_ddb_is_non_persistent - check for non-persistence of ddb entry
 * @dev: dev associated with the sysfs entry
 * @data: pointer to flashnode session object
 *
 * Returns:
 *	1: if flashnode entry is non-persistent
 *	0: if flashnode entry is persistent
 **/
static int qla4xxx_sysfs_ddb_is_non_persistent(struct device *dev, void *data)
{
	struct iscsi_bus_flash_session *fnode_sess;

	if (!iscsi_flashnode_bus_match(dev, NULL))
		return 0;

	fnode_sess = iscsi_dev_to_flash_session(dev);

	return (fnode_sess->flash_state == DEV_DB_NON_PERSISTENT);
}

/**
 * qla4xxx_sysfs_ddb_tgt_create - Create sysfs entry for target
 * @ha: pointer to host
 * @fw_ddb_entry: flash ddb data
 * @idx: target index
 * @user: if set then this call is made from userland else from kernel
 *
 * Returns:
 * On sucess: QLA_SUCCESS
 * On failure: QLA_ERROR
 *
 * This create separate sysfs entries for session and connection attributes of
 * the given fw ddb entry.
 * If this is invoked as a result of a userspace call then the entry is marked
 * as nonpersistent using flash_state field.
 **/
static int qla4xxx_sysfs_ddb_tgt_create(struct scsi_qla_host *ha,
					struct dev_db_entry *fw_ddb_entry,
					uint16_t *idx, int user)
{
	struct iscsi_bus_flash_session *fnode_sess = NULL;
	struct iscsi_bus_flash_conn *fnode_conn = NULL;
	int rc = QLA_ERROR;

	fnode_sess = iscsi_create_flashnode_sess(ha->host, *idx,
						 &qla4xxx_iscsi_transport, 0);
	if (!fnode_sess) {
		ql4_printk(KERN_ERR, ha,
			   "%s: Unable to create session sysfs entry for flashnode %d of host%lu\n",
			   __func__, *idx, ha->host_no);
		goto exit_tgt_create;
	}

	fnode_conn = iscsi_create_flashnode_conn(ha->host, fnode_sess,
						 &qla4xxx_iscsi_transport, 0);
	if (!fnode_conn) {
		ql4_printk(KERN_ERR, ha,
			   "%s: Unable to create conn sysfs entry for flashnode %d of host%lu\n",
			   __func__, *idx, ha->host_no);
		goto free_sess;
	}

	if (user) {
		fnode_sess->flash_state = DEV_DB_NON_PERSISTENT;
	} else {
		fnode_sess->flash_state = DEV_DB_PERSISTENT;

		if (*idx == ha->pri_ddb_idx || *idx == ha->sec_ddb_idx)
			fnode_sess->is_boot_target = 1;
		else
			fnode_sess->is_boot_target = 0;
	}

	rc = qla4xxx_copy_from_fwddb_param(fnode_sess, fnode_conn,
					   fw_ddb_entry);
	if (rc)
		goto free_sess;

	ql4_printk(KERN_INFO, ha, "%s: sysfs entry %s created\n",
		   __func__, fnode_sess->dev.kobj.name);

	ql4_printk(KERN_INFO, ha, "%s: sysfs entry %s created\n",
		   __func__, fnode_conn->dev.kobj.name);

	return QLA_SUCCESS;

free_sess:
	iscsi_destroy_flashnode_sess(fnode_sess);

exit_tgt_create:
	return QLA_ERROR;
}

/**
 * qla4xxx_sysfs_ddb_add - Add new ddb entry in flash
 * @shost: pointer to host
 * @buf: type of ddb entry (ipv4/ipv6)
 * @len: length of buf
 *
 * This creates new ddb entry in the flash by finding first free index and
 * storing default ddb there. And then create sysfs entry for the new ddb entry.
 **/
static int qla4xxx_sysfs_ddb_add(struct Scsi_Host *shost, const char *buf,
				 int len)
{
	struct scsi_qla_host *ha = to_qla_host(shost);
	struct dev_db_entry *fw_ddb_entry = NULL;
	dma_addr_t fw_ddb_entry_dma;
	struct device *dev;
	uint16_t idx = 0;
	uint16_t max_ddbs = 0;
	uint32_t options = 0;
	uint32_t rval = QLA_ERROR;

	if (strncasecmp(PORTAL_TYPE_IPV4, buf, 4) &&
	    strncasecmp(PORTAL_TYPE_IPV6, buf, 4)) {
		DEBUG2(ql4_printk(KERN_ERR, ha, "%s: Invalid portal type\n",
				  __func__));
		goto exit_ddb_add;
	}

	max_ddbs =  is_qla40XX(ha) ? MAX_PRST_DEV_DB_ENTRIES :
				     MAX_DEV_DB_ENTRIES;

	fw_ddb_entry = dma_alloc_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
					  &fw_ddb_entry_dma, GFP_KERNEL);
	if (!fw_ddb_entry) {
		DEBUG2(ql4_printk(KERN_ERR, ha,
				  "%s: Unable to allocate dma buffer\n",
				  __func__));
		goto exit_ddb_add;
	}

	dev = iscsi_find_flashnode_sess(ha->host, NULL,
					qla4xxx_sysfs_ddb_is_non_persistent);
	if (dev) {
		ql4_printk(KERN_ERR, ha,
			   "%s: A non-persistent entry %s found\n",
			   __func__, dev->kobj.name);
		put_device(dev);
		goto exit_ddb_add;
	}

	/* Index 0 and 1 are reserved for boot target entries */
	for (idx = 2; idx < max_ddbs; idx++) {
		if (qla4xxx_flashdb_by_index(ha, fw_ddb_entry,
					     fw_ddb_entry_dma, idx))
			break;
	}

	if (idx == max_ddbs)
		goto exit_ddb_add;

	if (!strncasecmp("ipv6", buf, 4))
		options |= IPV6_DEFAULT_DDB_ENTRY;

	rval = qla4xxx_get_default_ddb(ha, options, fw_ddb_entry_dma);
	if (rval == QLA_ERROR)
		goto exit_ddb_add;

	rval = qla4xxx_sysfs_ddb_tgt_create(ha, fw_ddb_entry, &idx, 1);

exit_ddb_add:
	if (fw_ddb_entry)
		dma_free_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
				  fw_ddb_entry, fw_ddb_entry_dma);
	if (rval == QLA_SUCCESS)
		return idx;
	else
		return -EIO;
}

/**
 * qla4xxx_sysfs_ddb_apply - write the target ddb contents to Flash
 * @fnode_sess: pointer to session attrs of flash ddb entry
 * @fnode_conn: pointer to connection attrs of flash ddb entry
 *
 * This writes the contents of target ddb buffer to Flash with a valid cookie
 * value in order to make the ddb entry persistent.
 **/
static int  qla4xxx_sysfs_ddb_apply(struct iscsi_bus_flash_session *fnode_sess,
				    struct iscsi_bus_flash_conn *fnode_conn)
{
	struct Scsi_Host *shost = iscsi_flash_session_to_shost(fnode_sess);
	struct scsi_qla_host *ha = to_qla_host(shost);
	uint32_t dev_db_start_offset = FLASH_OFFSET_DB_INFO;
	struct dev_db_entry *fw_ddb_entry = NULL;
	dma_addr_t fw_ddb_entry_dma;
	uint32_t options = 0;
	int rval = 0;

	fw_ddb_entry = dma_alloc_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
					  &fw_ddb_entry_dma, GFP_KERNEL);
	if (!fw_ddb_entry) {
		DEBUG2(ql4_printk(KERN_ERR, ha,
				  "%s: Unable to allocate dma buffer\n",
				  __func__));
		rval = -ENOMEM;
		goto exit_ddb_apply;
	}

	if (!strncasecmp(fnode_sess->portal_type, PORTAL_TYPE_IPV6, 4))
		options |= IPV6_DEFAULT_DDB_ENTRY;

	rval = qla4xxx_get_default_ddb(ha, options, fw_ddb_entry_dma);
	if (rval == QLA_ERROR)
		goto exit_ddb_apply;

	dev_db_start_offset += (fnode_sess->target_id *
				sizeof(*fw_ddb_entry));

	qla4xxx_copy_to_fwddb_param(fnode_sess, fnode_conn, fw_ddb_entry);
	fw_ddb_entry->cookie = DDB_VALID_COOKIE;

	rval = qla4xxx_set_flash(ha, fw_ddb_entry_dma, dev_db_start_offset,
				 sizeof(*fw_ddb_entry), FLASH_OPT_RMW_COMMIT);

	if (rval == QLA_SUCCESS) {
		fnode_sess->flash_state = DEV_DB_PERSISTENT;
		ql4_printk(KERN_INFO, ha,
			   "%s: flash node %u of host %lu written to flash\n",
			   __func__, fnode_sess->target_id, ha->host_no);
	} else {
		rval = -EIO;
		ql4_printk(KERN_ERR, ha,
			   "%s: Error while writing flash node %u of host %lu to flash\n",
			   __func__, fnode_sess->target_id, ha->host_no);
	}

exit_ddb_apply:
	if (fw_ddb_entry)
		dma_free_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
				  fw_ddb_entry, fw_ddb_entry_dma);
	return rval;
}

static ssize_t qla4xxx_sysfs_ddb_conn_open(struct scsi_qla_host *ha,
					   struct dev_db_entry *fw_ddb_entry,
					   uint16_t idx)
{
	struct dev_db_entry *ddb_entry = NULL;
	dma_addr_t ddb_entry_dma;
	unsigned long wtime;
	uint32_t mbx_sts = 0;
	uint32_t state = 0, conn_err = 0;
	uint16_t tmo = 0;
	int ret = 0;

	ddb_entry = dma_alloc_coherent(&ha->pdev->dev, sizeof(*ddb_entry),
				       &ddb_entry_dma, GFP_KERNEL);
	if (!ddb_entry) {
		DEBUG2(ql4_printk(KERN_ERR, ha,
				  "%s: Unable to allocate dma buffer\n",
				  __func__));
		return QLA_ERROR;
	}

	memcpy(ddb_entry, fw_ddb_entry, sizeof(*ddb_entry));

	ret = qla4xxx_set_ddb_entry(ha, idx, ddb_entry_dma, &mbx_sts);
	if (ret != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_ERR, ha,
				  "%s: Unable to set ddb entry for index %d\n",
				  __func__, idx));
		goto exit_ddb_conn_open;
	}

	qla4xxx_conn_open(ha, idx);

	/* To ensure that sendtargets is done, wait for at least 12 secs */
	tmo = ((ha->def_timeout > LOGIN_TOV) &&
	       (ha->def_timeout < LOGIN_TOV * 10) ?
	       ha->def_timeout : LOGIN_TOV);

	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "Default time to wait for login to ddb %d\n", tmo));

	wtime = jiffies + (HZ * tmo);
	do {
		ret = qla4xxx_get_fwddb_entry(ha, idx, NULL, 0, NULL,
					      NULL, &state, &conn_err, NULL,
					      NULL);
		if (ret == QLA_ERROR)
			continue;

		if (state == DDB_DS_NO_CONNECTION_ACTIVE ||
		    state == DDB_DS_SESSION_FAILED)
			break;

		schedule_timeout_uninterruptible(HZ / 10);
	} while (time_after(wtime, jiffies));

exit_ddb_conn_open:
	if (ddb_entry)
		dma_free_coherent(&ha->pdev->dev, sizeof(*ddb_entry),
				  ddb_entry, ddb_entry_dma);
	return ret;
}

static int qla4xxx_ddb_login_st(struct scsi_qla_host *ha,
				struct dev_db_entry *fw_ddb_entry,
				uint16_t target_id)
{
	struct qla_ddb_index *ddb_idx, *ddb_idx_tmp;
	struct list_head list_nt;
	uint16_t ddb_index;
	int ret = 0;

	if (test_bit(AF_ST_DISCOVERY_IN_PROGRESS, &ha->flags)) {
		ql4_printk(KERN_WARNING, ha,
			   "%s: A discovery already in progress!\n", __func__);
		return QLA_ERROR;
	}

	INIT_LIST_HEAD(&list_nt);

	set_bit(AF_ST_DISCOVERY_IN_PROGRESS, &ha->flags);

	ret = qla4xxx_get_ddb_index(ha, &ddb_index);
	if (ret == QLA_ERROR)
		goto exit_login_st_clr_bit;

	ret = qla4xxx_sysfs_ddb_conn_open(ha, fw_ddb_entry, ddb_index);
	if (ret == QLA_ERROR)
		goto exit_login_st;

	qla4xxx_build_new_nt_list(ha, &list_nt, target_id);

	list_for_each_entry_safe(ddb_idx, ddb_idx_tmp, &list_nt, list) {
		list_del_init(&ddb_idx->list);
		qla4xxx_clear_ddb_entry(ha, ddb_idx->fw_ddb_idx);
		vfree(ddb_idx);
	}

exit_login_st:
	if (qla4xxx_clear_ddb_entry(ha, ddb_index) == QLA_ERROR) {
		ql4_printk(KERN_ERR, ha,
			   "Unable to clear DDB index = 0x%x\n", ddb_index);
	}

	clear_bit(ddb_index, ha->ddb_idx_map);

exit_login_st_clr_bit:
	clear_bit(AF_ST_DISCOVERY_IN_PROGRESS, &ha->flags);
	return ret;
}

static int qla4xxx_ddb_login_nt(struct scsi_qla_host *ha,
				struct dev_db_entry *fw_ddb_entry,
				uint16_t idx)
{
	int ret = QLA_ERROR;

	ret = qla4xxx_is_session_exists(ha, fw_ddb_entry, NULL);
	if (ret != QLA_SUCCESS)
		ret = qla4xxx_sess_conn_setup(ha, fw_ddb_entry, RESET_ADAPTER,
					      idx);
	else
		ret = -EPERM;

	return ret;
}

/**
 * qla4xxx_sysfs_ddb_login - Login to the specified target
 * @fnode_sess: pointer to session attrs of flash ddb entry
 * @fnode_conn: pointer to connection attrs of flash ddb entry
 *
 * This logs in to the specified target
 **/
static int qla4xxx_sysfs_ddb_login(struct iscsi_bus_flash_session *fnode_sess,
				   struct iscsi_bus_flash_conn *fnode_conn)
{
	struct Scsi_Host *shost = iscsi_flash_session_to_shost(fnode_sess);
	struct scsi_qla_host *ha = to_qla_host(shost);
	struct dev_db_entry *fw_ddb_entry = NULL;
	dma_addr_t fw_ddb_entry_dma;
	uint32_t options = 0;
	int ret = 0;

	if (fnode_sess->flash_state == DEV_DB_NON_PERSISTENT) {
		ql4_printk(KERN_ERR, ha,
			   "%s: Target info is not persistent\n", __func__);
		ret = -EIO;
		goto exit_ddb_login;
	}

	fw_ddb_entry = dma_alloc_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
					  &fw_ddb_entry_dma, GFP_KERNEL);
	if (!fw_ddb_entry) {
		DEBUG2(ql4_printk(KERN_ERR, ha,
				  "%s: Unable to allocate dma buffer\n",
				  __func__));
		ret = -ENOMEM;
		goto exit_ddb_login;
	}

	if (!strncasecmp(fnode_sess->portal_type, PORTAL_TYPE_IPV6, 4))
		options |= IPV6_DEFAULT_DDB_ENTRY;

	ret = qla4xxx_get_default_ddb(ha, options, fw_ddb_entry_dma);
	if (ret == QLA_ERROR)
		goto exit_ddb_login;

	qla4xxx_copy_to_fwddb_param(fnode_sess, fnode_conn, fw_ddb_entry);
	fw_ddb_entry->cookie = DDB_VALID_COOKIE;

	if (strlen((char *)fw_ddb_entry->iscsi_name) == 0)
		ret = qla4xxx_ddb_login_st(ha, fw_ddb_entry,
					   fnode_sess->target_id);
	else
		ret = qla4xxx_ddb_login_nt(ha, fw_ddb_entry,
					   fnode_sess->target_id);

	if (ret > 0)
		ret = -EIO;

exit_ddb_login:
	if (fw_ddb_entry)
		dma_free_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
				  fw_ddb_entry, fw_ddb_entry_dma);
	return ret;
}

/**
 * qla4xxx_sysfs_ddb_logout_sid - Logout session for the specified target
 * @cls_sess: pointer to session to be logged out
 *
 * This performs session log out from the specified target
 **/
static int qla4xxx_sysfs_ddb_logout_sid(struct iscsi_cls_session *cls_sess)
{
	struct iscsi_session *sess;
	struct ddb_entry *ddb_entry = NULL;
	struct scsi_qla_host *ha;
	struct dev_db_entry *fw_ddb_entry = NULL;
	dma_addr_t fw_ddb_entry_dma;
	unsigned long flags;
	unsigned long wtime;
	uint32_t ddb_state;
	int options;
	int ret = 0;

	sess = cls_sess->dd_data;
	ddb_entry = sess->dd_data;
	ha = ddb_entry->ha;

	if (ddb_entry->ddb_type != FLASH_DDB) {
		ql4_printk(KERN_ERR, ha, "%s: Not a flash node session\n",
			   __func__);
		ret = -ENXIO;
		goto exit_ddb_logout;
	}

	if (test_bit(DF_BOOT_TGT, &ddb_entry->flags)) {
		ql4_printk(KERN_ERR, ha,
			   "%s: Logout from boot target entry is not permitted.\n",
			   __func__);
		ret = -EPERM;
		goto exit_ddb_logout;
	}

	fw_ddb_entry = dma_alloc_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
					  &fw_ddb_entry_dma, GFP_KERNEL);
	if (!fw_ddb_entry) {
		ql4_printk(KERN_ERR, ha,
			   "%s: Unable to allocate dma buffer\n", __func__);
		ret = -ENOMEM;
		goto exit_ddb_logout;
	}

	if (test_and_set_bit(DF_DISABLE_RELOGIN, &ddb_entry->flags))
		goto ddb_logout_init;

	ret = qla4xxx_get_fwddb_entry(ha, ddb_entry->fw_ddb_index,
				      fw_ddb_entry, fw_ddb_entry_dma,
				      NULL, NULL, &ddb_state, NULL,
				      NULL, NULL);
	if (ret == QLA_ERROR)
		goto ddb_logout_init;

	if (ddb_state == DDB_DS_SESSION_ACTIVE)
		goto ddb_logout_init;

	/* wait until next relogin is triggered using DF_RELOGIN and
	 * clear DF_RELOGIN to avoid invocation of further relogin
	 */
	wtime = jiffies + (HZ * RELOGIN_TOV);
	do {
		if (test_and_clear_bit(DF_RELOGIN, &ddb_entry->flags))
			goto ddb_logout_init;

		schedule_timeout_uninterruptible(HZ);
	} while ((time_after(wtime, jiffies)));

ddb_logout_init:
	atomic_set(&ddb_entry->retry_relogin_timer, INVALID_ENTRY);
	atomic_set(&ddb_entry->relogin_timer, 0);

	options = LOGOUT_OPTION_CLOSE_SESSION;
	qla4xxx_session_logout_ddb(ha, ddb_entry, options);

	memset(fw_ddb_entry, 0, sizeof(*fw_ddb_entry));
	wtime = jiffies + (HZ * LOGOUT_TOV);
	do {
		ret = qla4xxx_get_fwddb_entry(ha, ddb_entry->fw_ddb_index,
					      fw_ddb_entry, fw_ddb_entry_dma,
					      NULL, NULL, &ddb_state, NULL,
					      NULL, NULL);
		if (ret == QLA_ERROR)
			goto ddb_logout_clr_sess;

		if ((ddb_state == DDB_DS_NO_CONNECTION_ACTIVE) ||
		    (ddb_state == DDB_DS_SESSION_FAILED))
			goto ddb_logout_clr_sess;

		schedule_timeout_uninterruptible(HZ);
	} while ((time_after(wtime, jiffies)));

ddb_logout_clr_sess:
	qla4xxx_clear_ddb_entry(ha, ddb_entry->fw_ddb_index);
	/*
	 * we have decremented the reference count of the driver
	 * when we setup the session to have the driver unload
	 * to be seamless without actually destroying the
	 * session
	 **/
	try_module_get(qla4xxx_iscsi_transport.owner);
	iscsi_destroy_endpoint(ddb_entry->conn->ep);

	spin_lock_irqsave(&ha->hardware_lock, flags);
	qla4xxx_free_ddb(ha, ddb_entry);
	clear_bit(ddb_entry->fw_ddb_index, ha->ddb_idx_map);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	iscsi_session_teardown(ddb_entry->sess);

	clear_bit(DF_DISABLE_RELOGIN, &ddb_entry->flags);
	ret = QLA_SUCCESS;

exit_ddb_logout:
	if (fw_ddb_entry)
		dma_free_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
				  fw_ddb_entry, fw_ddb_entry_dma);
	return ret;
}

/**
 * qla4xxx_sysfs_ddb_logout - Logout from the specified target
 * @fnode_sess: pointer to session attrs of flash ddb entry
 * @fnode_conn: pointer to connection attrs of flash ddb entry
 *
 * This performs log out from the specified target
 **/
static int qla4xxx_sysfs_ddb_logout(struct iscsi_bus_flash_session *fnode_sess,
				    struct iscsi_bus_flash_conn *fnode_conn)
{
	struct Scsi_Host *shost = iscsi_flash_session_to_shost(fnode_sess);
	struct scsi_qla_host *ha = to_qla_host(shost);
	struct ql4_tuple_ddb *flash_tddb = NULL;
	struct ql4_tuple_ddb *tmp_tddb = NULL;
	struct dev_db_entry *fw_ddb_entry = NULL;
	struct ddb_entry *ddb_entry = NULL;
	dma_addr_t fw_ddb_dma;
	uint32_t next_idx = 0;
	uint32_t state = 0, conn_err = 0;
	uint16_t conn_id = 0;
	int idx, index;
	int status, ret = 0;

	fw_ddb_entry = dma_pool_alloc(ha->fw_ddb_dma_pool, GFP_KERNEL,
				      &fw_ddb_dma);
	if (fw_ddb_entry == NULL) {
		ql4_printk(KERN_ERR, ha, "%s:Out of memory\n", __func__);
		ret = -ENOMEM;
		goto exit_ddb_logout;
	}

	flash_tddb = vzalloc(sizeof(*flash_tddb));
	if (!flash_tddb) {
		ql4_printk(KERN_WARNING, ha,
			   "%s:Memory Allocation failed.\n", __func__);
		ret = -ENOMEM;
		goto exit_ddb_logout;
	}

	tmp_tddb = vzalloc(sizeof(*tmp_tddb));
	if (!tmp_tddb) {
		ql4_printk(KERN_WARNING, ha,
			   "%s:Memory Allocation failed.\n", __func__);
		ret = -ENOMEM;
		goto exit_ddb_logout;
	}

	if (!fnode_sess->targetname) {
		ql4_printk(KERN_ERR, ha,
			   "%s:Cannot logout from SendTarget entry\n",
			   __func__);
		ret = -EPERM;
		goto exit_ddb_logout;
	}

	if (fnode_sess->is_boot_target) {
		ql4_printk(KERN_ERR, ha,
			   "%s: Logout from boot target entry is not permitted.\n",
			   __func__);
		ret = -EPERM;
		goto exit_ddb_logout;
	}

	strlcpy(flash_tddb->iscsi_name, fnode_sess->targetname,
		ISCSI_NAME_SIZE);

	if (!strncmp(fnode_sess->portal_type, PORTAL_TYPE_IPV6, 4))
		sprintf(flash_tddb->ip_addr, "%pI6", fnode_conn->ipaddress);
	else
		sprintf(flash_tddb->ip_addr, "%pI4", fnode_conn->ipaddress);

	flash_tddb->tpgt = fnode_sess->tpgt;
	flash_tddb->port = fnode_conn->port;

	COPY_ISID(flash_tddb->isid, fnode_sess->isid);

	for (idx = 0; idx < MAX_DDB_ENTRIES; idx++) {
		ddb_entry = qla4xxx_lookup_ddb_by_fw_index(ha, idx);
		if (ddb_entry == NULL)
			continue;

		if (ddb_entry->ddb_type != FLASH_DDB)
			continue;

		index = ddb_entry->sess->target_id;
		status = qla4xxx_get_fwddb_entry(ha, index, fw_ddb_entry,
						 fw_ddb_dma, NULL, &next_idx,
						 &state, &conn_err, NULL,
						 &conn_id);
		if (status == QLA_ERROR) {
			ret = -ENOMEM;
			break;
		}

		qla4xxx_convert_param_ddb(fw_ddb_entry, tmp_tddb, NULL);

		status = qla4xxx_compare_tuple_ddb(ha, flash_tddb, tmp_tddb,
						   true);
		if (status == QLA_SUCCESS) {
			ret = qla4xxx_sysfs_ddb_logout_sid(ddb_entry->sess);
			break;
		}
	}

	if (idx == MAX_DDB_ENTRIES)
		ret = -ESRCH;

exit_ddb_logout:
	vfree(flash_tddb);
	vfree(tmp_tddb);
	if (fw_ddb_entry)
		dma_pool_free(ha->fw_ddb_dma_pool, fw_ddb_entry, fw_ddb_dma);

	return ret;
}

static int
qla4xxx_sysfs_ddb_get_param(struct iscsi_bus_flash_session *fnode_sess,
			    int param, char *buf)
{
	struct Scsi_Host *shost = iscsi_flash_session_to_shost(fnode_sess);
	struct scsi_qla_host *ha = to_qla_host(shost);
	struct iscsi_bus_flash_conn *fnode_conn;
	struct ql4_chap_table chap_tbl;
	struct device *dev;
	int parent_type;
	int rc = 0;

	dev = iscsi_find_flashnode_conn(fnode_sess);
	if (!dev)
		return -EIO;

	fnode_conn = iscsi_dev_to_flash_conn(dev);

	switch (param) {
	case ISCSI_FLASHNODE_IS_FW_ASSIGNED_IPV6:
		rc = sprintf(buf, "%u\n", fnode_conn->is_fw_assigned_ipv6);
		break;
	case ISCSI_FLASHNODE_PORTAL_TYPE:
		rc = sprintf(buf, "%s\n", fnode_sess->portal_type);
		break;
	case ISCSI_FLASHNODE_AUTO_SND_TGT_DISABLE:
		rc = sprintf(buf, "%u\n", fnode_sess->auto_snd_tgt_disable);
		break;
	case ISCSI_FLASHNODE_DISCOVERY_SESS:
		rc = sprintf(buf, "%u\n", fnode_sess->discovery_sess);
		break;
	case ISCSI_FLASHNODE_ENTRY_EN:
		rc = sprintf(buf, "%u\n", fnode_sess->entry_state);
		break;
	case ISCSI_FLASHNODE_HDR_DGST_EN:
		rc = sprintf(buf, "%u\n", fnode_conn->hdrdgst_en);
		break;
	case ISCSI_FLASHNODE_DATA_DGST_EN:
		rc = sprintf(buf, "%u\n", fnode_conn->datadgst_en);
		break;
	case ISCSI_FLASHNODE_IMM_DATA_EN:
		rc = sprintf(buf, "%u\n", fnode_sess->imm_data_en);
		break;
	case ISCSI_FLASHNODE_INITIAL_R2T_EN:
		rc = sprintf(buf, "%u\n", fnode_sess->initial_r2t_en);
		break;
	case ISCSI_FLASHNODE_DATASEQ_INORDER:
		rc = sprintf(buf, "%u\n", fnode_sess->dataseq_inorder_en);
		break;
	case ISCSI_FLASHNODE_PDU_INORDER:
		rc = sprintf(buf, "%u\n", fnode_sess->pdu_inorder_en);
		break;
	case ISCSI_FLASHNODE_CHAP_AUTH_EN:
		rc = sprintf(buf, "%u\n", fnode_sess->chap_auth_en);
		break;
	case ISCSI_FLASHNODE_SNACK_REQ_EN:
		rc = sprintf(buf, "%u\n", fnode_conn->snack_req_en);
		break;
	case ISCSI_FLASHNODE_DISCOVERY_LOGOUT_EN:
		rc = sprintf(buf, "%u\n", fnode_sess->discovery_logout_en);
		break;
	case ISCSI_FLASHNODE_BIDI_CHAP_EN:
		rc = sprintf(buf, "%u\n", fnode_sess->bidi_chap_en);
		break;
	case ISCSI_FLASHNODE_DISCOVERY_AUTH_OPTIONAL:
		rc = sprintf(buf, "%u\n", fnode_sess->discovery_auth_optional);
		break;
	case ISCSI_FLASHNODE_ERL:
		rc = sprintf(buf, "%u\n", fnode_sess->erl);
		break;
	case ISCSI_FLASHNODE_TCP_TIMESTAMP_STAT:
		rc = sprintf(buf, "%u\n", fnode_conn->tcp_timestamp_stat);
		break;
	case ISCSI_FLASHNODE_TCP_NAGLE_DISABLE:
		rc = sprintf(buf, "%u\n", fnode_conn->tcp_nagle_disable);
		break;
	case ISCSI_FLASHNODE_TCP_WSF_DISABLE:
		rc = sprintf(buf, "%u\n", fnode_conn->tcp_wsf_disable);
		break;
	case ISCSI_FLASHNODE_TCP_TIMER_SCALE:
		rc = sprintf(buf, "%u\n", fnode_conn->tcp_timer_scale);
		break;
	case ISCSI_FLASHNODE_TCP_TIMESTAMP_EN:
		rc = sprintf(buf, "%u\n", fnode_conn->tcp_timestamp_en);
		break;
	case ISCSI_FLASHNODE_IP_FRAG_DISABLE:
		rc = sprintf(buf, "%u\n", fnode_conn->fragment_disable);
		break;
	case ISCSI_FLASHNODE_MAX_RECV_DLENGTH:
		rc = sprintf(buf, "%u\n", fnode_conn->max_recv_dlength);
		break;
	case ISCSI_FLASHNODE_MAX_XMIT_DLENGTH:
		rc = sprintf(buf, "%u\n", fnode_conn->max_xmit_dlength);
		break;
	case ISCSI_FLASHNODE_FIRST_BURST:
		rc = sprintf(buf, "%u\n", fnode_sess->first_burst);
		break;
	case ISCSI_FLASHNODE_DEF_TIME2WAIT:
		rc = sprintf(buf, "%u\n", fnode_sess->time2wait);
		break;
	case ISCSI_FLASHNODE_DEF_TIME2RETAIN:
		rc = sprintf(buf, "%u\n", fnode_sess->time2retain);
		break;
	case ISCSI_FLASHNODE_MAX_R2T:
		rc = sprintf(buf, "%u\n", fnode_sess->max_r2t);
		break;
	case ISCSI_FLASHNODE_KEEPALIVE_TMO:
		rc = sprintf(buf, "%u\n", fnode_conn->keepalive_timeout);
		break;
	case ISCSI_FLASHNODE_ISID:
		rc = sprintf(buf, "%pm\n", fnode_sess->isid);
		break;
	case ISCSI_FLASHNODE_TSID:
		rc = sprintf(buf, "%u\n", fnode_sess->tsid);
		break;
	case ISCSI_FLASHNODE_PORT:
		rc = sprintf(buf, "%d\n", fnode_conn->port);
		break;
	case ISCSI_FLASHNODE_MAX_BURST:
		rc = sprintf(buf, "%u\n", fnode_sess->max_burst);
		break;
	case ISCSI_FLASHNODE_DEF_TASKMGMT_TMO:
		rc = sprintf(buf, "%u\n",
			     fnode_sess->default_taskmgmt_timeout);
		break;
	case ISCSI_FLASHNODE_IPADDR:
		if (!strncmp(fnode_sess->portal_type, PORTAL_TYPE_IPV6, 4))
			rc = sprintf(buf, "%pI6\n", fnode_conn->ipaddress);
		else
			rc = sprintf(buf, "%pI4\n", fnode_conn->ipaddress);
		break;
	case ISCSI_FLASHNODE_ALIAS:
		if (fnode_sess->targetalias)
			rc = sprintf(buf, "%s\n", fnode_sess->targetalias);
		else
			rc = sprintf(buf, "\n");
		break;
	case ISCSI_FLASHNODE_REDIRECT_IPADDR:
		if (!strncmp(fnode_sess->portal_type, PORTAL_TYPE_IPV6, 4))
			rc = sprintf(buf, "%pI6\n",
				     fnode_conn->redirect_ipaddr);
		else
			rc = sprintf(buf, "%pI4\n",
				     fnode_conn->redirect_ipaddr);
		break;
	case ISCSI_FLASHNODE_MAX_SEGMENT_SIZE:
		rc = sprintf(buf, "%u\n", fnode_conn->max_segment_size);
		break;
	case ISCSI_FLASHNODE_LOCAL_PORT:
		rc = sprintf(buf, "%u\n", fnode_conn->local_port);
		break;
	case ISCSI_FLASHNODE_IPV4_TOS:
		rc = sprintf(buf, "%u\n", fnode_conn->ipv4_tos);
		break;
	case ISCSI_FLASHNODE_IPV6_TC:
		if (!strncmp(fnode_sess->portal_type, PORTAL_TYPE_IPV6, 4))
			rc = sprintf(buf, "%u\n",
				     fnode_conn->ipv6_traffic_class);
		else
			rc = sprintf(buf, "\n");
		break;
	case ISCSI_FLASHNODE_IPV6_FLOW_LABEL:
		rc = sprintf(buf, "%u\n", fnode_conn->ipv6_flow_label);
		break;
	case ISCSI_FLASHNODE_LINK_LOCAL_IPV6:
		if (!strncmp(fnode_sess->portal_type, PORTAL_TYPE_IPV6, 4))
			rc = sprintf(buf, "%pI6\n",
				     fnode_conn->link_local_ipv6_addr);
		else
			rc = sprintf(buf, "\n");
		break;
	case ISCSI_FLASHNODE_DISCOVERY_PARENT_IDX:
		rc = sprintf(buf, "%u\n", fnode_sess->discovery_parent_idx);
		break;
	case ISCSI_FLASHNODE_DISCOVERY_PARENT_TYPE:
		if (fnode_sess->discovery_parent_type == DDB_ISNS)
			parent_type = ISCSI_DISC_PARENT_ISNS;
		else if (fnode_sess->discovery_parent_type == DDB_NO_LINK)
			parent_type = ISCSI_DISC_PARENT_UNKNOWN;
		else if (fnode_sess->discovery_parent_type < MAX_DDB_ENTRIES)
			parent_type = ISCSI_DISC_PARENT_SENDTGT;
		else
			parent_type = ISCSI_DISC_PARENT_UNKNOWN;

		rc = sprintf(buf, "%s\n",
			     iscsi_get_discovery_parent_name(parent_type));
		break;
	case ISCSI_FLASHNODE_NAME:
		if (fnode_sess->targetname)
			rc = sprintf(buf, "%s\n", fnode_sess->targetname);
		else
			rc = sprintf(buf, "\n");
		break;
	case ISCSI_FLASHNODE_TPGT:
		rc = sprintf(buf, "%u\n", fnode_sess->tpgt);
		break;
	case ISCSI_FLASHNODE_TCP_XMIT_WSF:
		rc = sprintf(buf, "%u\n", fnode_conn->tcp_xmit_wsf);
		break;
	case ISCSI_FLASHNODE_TCP_RECV_WSF:
		rc = sprintf(buf, "%u\n", fnode_conn->tcp_recv_wsf);
		break;
	case ISCSI_FLASHNODE_CHAP_OUT_IDX:
		rc = sprintf(buf, "%u\n", fnode_sess->chap_out_idx);
		break;
	case ISCSI_FLASHNODE_USERNAME:
		if (fnode_sess->chap_auth_en) {
			qla4xxx_get_uni_chap_at_index(ha,
						      chap_tbl.name,
						      chap_tbl.secret,
						      fnode_sess->chap_out_idx);
			rc = sprintf(buf, "%s\n", chap_tbl.name);
		} else {
			rc = sprintf(buf, "\n");
		}
		break;
	case ISCSI_FLASHNODE_PASSWORD:
		if (fnode_sess->chap_auth_en) {
			qla4xxx_get_uni_chap_at_index(ha,
						      chap_tbl.name,
						      chap_tbl.secret,
						      fnode_sess->chap_out_idx);
			rc = sprintf(buf, "%s\n", chap_tbl.secret);
		} else {
			rc = sprintf(buf, "\n");
		}
		break;
	case ISCSI_FLASHNODE_STATSN:
		rc = sprintf(buf, "%u\n", fnode_conn->statsn);
		break;
	case ISCSI_FLASHNODE_EXP_STATSN:
		rc = sprintf(buf, "%u\n", fnode_conn->exp_statsn);
		break;
	case ISCSI_FLASHNODE_IS_BOOT_TGT:
		rc = sprintf(buf, "%u\n", fnode_sess->is_boot_target);
		break;
	default:
		rc = -ENOSYS;
		break;
	}

	put_device(dev);
	return rc;
}

/**
 * qla4xxx_sysfs_ddb_set_param - Set parameter for firmware DDB entry
 * @fnode_sess: pointer to session attrs of flash ddb entry
 * @fnode_conn: pointer to connection attrs of flash ddb entry
 * @data: Parameters and their values to update
 * @len: len of data
 *
 * This sets the parameter of flash ddb entry and writes them to flash
 **/
static int
qla4xxx_sysfs_ddb_set_param(struct iscsi_bus_flash_session *fnode_sess,
			    struct iscsi_bus_flash_conn *fnode_conn,
			    void *data, int len)
{
	struct Scsi_Host *shost = iscsi_flash_session_to_shost(fnode_sess);
	struct scsi_qla_host *ha = to_qla_host(shost);
	struct iscsi_flashnode_param_info *fnode_param;
	struct ql4_chap_table chap_tbl;
	struct nlattr *attr;
	uint16_t chap_out_idx = INVALID_ENTRY;
	int rc = QLA_ERROR;
	uint32_t rem = len;

	memset((void *)&chap_tbl, 0, sizeof(chap_tbl));
	nla_for_each_attr(attr, data, len, rem) {
		fnode_param = nla_data(attr);

		switch (fnode_param->param) {
		case ISCSI_FLASHNODE_IS_FW_ASSIGNED_IPV6:
			fnode_conn->is_fw_assigned_ipv6 = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_PORTAL_TYPE:
			memcpy(fnode_sess->portal_type, fnode_param->value,
			       strlen(fnode_sess->portal_type));
			break;
		case ISCSI_FLASHNODE_AUTO_SND_TGT_DISABLE:
			fnode_sess->auto_snd_tgt_disable =
							fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_DISCOVERY_SESS:
			fnode_sess->discovery_sess = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_ENTRY_EN:
			fnode_sess->entry_state = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_HDR_DGST_EN:
			fnode_conn->hdrdgst_en = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_DATA_DGST_EN:
			fnode_conn->datadgst_en = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_IMM_DATA_EN:
			fnode_sess->imm_data_en = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_INITIAL_R2T_EN:
			fnode_sess->initial_r2t_en = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_DATASEQ_INORDER:
			fnode_sess->dataseq_inorder_en = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_PDU_INORDER:
			fnode_sess->pdu_inorder_en = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_CHAP_AUTH_EN:
			fnode_sess->chap_auth_en = fnode_param->value[0];
			/* Invalidate chap index if chap auth is disabled */
			if (!fnode_sess->chap_auth_en)
				fnode_sess->chap_out_idx = INVALID_ENTRY;

			break;
		case ISCSI_FLASHNODE_SNACK_REQ_EN:
			fnode_conn->snack_req_en = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_DISCOVERY_LOGOUT_EN:
			fnode_sess->discovery_logout_en = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_BIDI_CHAP_EN:
			fnode_sess->bidi_chap_en = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_DISCOVERY_AUTH_OPTIONAL:
			fnode_sess->discovery_auth_optional =
							fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_ERL:
			fnode_sess->erl = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_TCP_TIMESTAMP_STAT:
			fnode_conn->tcp_timestamp_stat = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_TCP_NAGLE_DISABLE:
			fnode_conn->tcp_nagle_disable = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_TCP_WSF_DISABLE:
			fnode_conn->tcp_wsf_disable = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_TCP_TIMER_SCALE:
			fnode_conn->tcp_timer_scale = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_TCP_TIMESTAMP_EN:
			fnode_conn->tcp_timestamp_en = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_IP_FRAG_DISABLE:
			fnode_conn->fragment_disable = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_MAX_RECV_DLENGTH:
			fnode_conn->max_recv_dlength =
					*(unsigned *)fnode_param->value;
			break;
		case ISCSI_FLASHNODE_MAX_XMIT_DLENGTH:
			fnode_conn->max_xmit_dlength =
					*(unsigned *)fnode_param->value;
			break;
		case ISCSI_FLASHNODE_FIRST_BURST:
			fnode_sess->first_burst =
					*(unsigned *)fnode_param->value;
			break;
		case ISCSI_FLASHNODE_DEF_TIME2WAIT:
			fnode_sess->time2wait = *(uint16_t *)fnode_param->value;
			break;
		case ISCSI_FLASHNODE_DEF_TIME2RETAIN:
			fnode_sess->time2retain =
						*(uint16_t *)fnode_param->value;
			break;
		case ISCSI_FLASHNODE_MAX_R2T:
			fnode_sess->max_r2t =
					*(uint16_t *)fnode_param->value;
			break;
		case ISCSI_FLASHNODE_KEEPALIVE_TMO:
			fnode_conn->keepalive_timeout =
				*(uint16_t *)fnode_param->value;
			break;
		case ISCSI_FLASHNODE_ISID:
			memcpy(fnode_sess->isid, fnode_param->value,
			       sizeof(fnode_sess->isid));
			break;
		case ISCSI_FLASHNODE_TSID:
			fnode_sess->tsid = *(uint16_t *)fnode_param->value;
			break;
		case ISCSI_FLASHNODE_PORT:
			fnode_conn->port = *(uint16_t *)fnode_param->value;
			break;
		case ISCSI_FLASHNODE_MAX_BURST:
			fnode_sess->max_burst = *(unsigned *)fnode_param->value;
			break;
		case ISCSI_FLASHNODE_DEF_TASKMGMT_TMO:
			fnode_sess->default_taskmgmt_timeout =
						*(uint16_t *)fnode_param->value;
			break;
		case ISCSI_FLASHNODE_IPADDR:
			memcpy(fnode_conn->ipaddress, fnode_param->value,
			       IPv6_ADDR_LEN);
			break;
		case ISCSI_FLASHNODE_ALIAS:
			rc = iscsi_switch_str_param(&fnode_sess->targetalias,
						    (char *)fnode_param->value);
			break;
		case ISCSI_FLASHNODE_REDIRECT_IPADDR:
			memcpy(fnode_conn->redirect_ipaddr, fnode_param->value,
			       IPv6_ADDR_LEN);
			break;
		case ISCSI_FLASHNODE_MAX_SEGMENT_SIZE:
			fnode_conn->max_segment_size =
					*(unsigned *)fnode_param->value;
			break;
		case ISCSI_FLASHNODE_LOCAL_PORT:
			fnode_conn->local_port =
						*(uint16_t *)fnode_param->value;
			break;
		case ISCSI_FLASHNODE_IPV4_TOS:
			fnode_conn->ipv4_tos = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_IPV6_TC:
			fnode_conn->ipv6_traffic_class = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_IPV6_FLOW_LABEL:
			fnode_conn->ipv6_flow_label = fnode_param->value[0];
			break;
		case ISCSI_FLASHNODE_NAME:
			rc = iscsi_switch_str_param(&fnode_sess->targetname,
						    (char *)fnode_param->value);
			break;
		case ISCSI_FLASHNODE_TPGT:
			fnode_sess->tpgt = *(uint16_t *)fnode_param->value;
			break;
		case ISCSI_FLASHNODE_LINK_LOCAL_IPV6:
			memcpy(fnode_conn->link_local_ipv6_addr,
			       fnode_param->value, IPv6_ADDR_LEN);
			break;
		case ISCSI_FLASHNODE_DISCOVERY_PARENT_IDX:
			fnode_sess->discovery_parent_idx =
						*(uint16_t *)fnode_param->value;
			break;
		case ISCSI_FLASHNODE_TCP_XMIT_WSF:
			fnode_conn->tcp_xmit_wsf =
						*(uint8_t *)fnode_param->value;
			break;
		case ISCSI_FLASHNODE_TCP_RECV_WSF:
			fnode_conn->tcp_recv_wsf =
						*(uint8_t *)fnode_param->value;
			break;
		case ISCSI_FLASHNODE_STATSN:
			fnode_conn->statsn = *(uint32_t *)fnode_param->value;
			break;
		case ISCSI_FLASHNODE_EXP_STATSN:
			fnode_conn->exp_statsn =
						*(uint32_t *)fnode_param->value;
			break;
		case ISCSI_FLASHNODE_CHAP_OUT_IDX:
			chap_out_idx = *(uint16_t *)fnode_param->value;
			if (!qla4xxx_get_uni_chap_at_index(ha,
							   chap_tbl.name,
							   chap_tbl.secret,
							   chap_out_idx)) {
				fnode_sess->chap_out_idx = chap_out_idx;
				/* Enable chap auth if chap index is valid */
				fnode_sess->chap_auth_en = QL4_PARAM_ENABLE;
			}
			break;
		default:
			ql4_printk(KERN_ERR, ha,
				   "%s: No such sysfs attribute\n", __func__);
			rc = -ENOSYS;
			goto exit_set_param;
		}
	}

	rc = qla4xxx_sysfs_ddb_apply(fnode_sess, fnode_conn);

exit_set_param:
	return rc;
}

/**
 * qla4xxx_sysfs_ddb_delete - Delete firmware DDB entry
 * @fnode_sess: pointer to session attrs of flash ddb entry
 *
 * This invalidates the flash ddb entry at the given index
 **/
static int qla4xxx_sysfs_ddb_delete(struct iscsi_bus_flash_session *fnode_sess)
{
	struct Scsi_Host *shost = iscsi_flash_session_to_shost(fnode_sess);
	struct scsi_qla_host *ha = to_qla_host(shost);
	uint32_t dev_db_start_offset;
	uint32_t dev_db_end_offset;
	struct dev_db_entry *fw_ddb_entry = NULL;
	dma_addr_t fw_ddb_entry_dma;
	uint16_t *ddb_cookie = NULL;
	size_t ddb_size = 0;
	void *pddb = NULL;
	int target_id;
	int rc = 0;

	if (fnode_sess->is_boot_target) {
		rc = -EPERM;
		DEBUG2(ql4_printk(KERN_ERR, ha,
				  "%s: Deletion of boot target entry is not permitted.\n",
				  __func__));
		goto exit_ddb_del;
	}

	if (fnode_sess->flash_state == DEV_DB_NON_PERSISTENT)
		goto sysfs_ddb_del;

	if (is_qla40XX(ha)) {
		dev_db_start_offset = FLASH_OFFSET_DB_INFO;
		dev_db_end_offset = FLASH_OFFSET_DB_END;
		dev_db_start_offset += (fnode_sess->target_id *
				       sizeof(*fw_ddb_entry));
		ddb_size = sizeof(*fw_ddb_entry);
	} else {
		dev_db_start_offset = FLASH_RAW_ACCESS_ADDR +
				      (ha->hw.flt_region_ddb << 2);
		/* flt_ddb_size is DDB table size for both ports
		 * so divide it by 2 to calculate the offset for second port
		 */
		if (ha->port_num == 1)
			dev_db_start_offset += (ha->hw.flt_ddb_size / 2);

		dev_db_end_offset = dev_db_start_offset +
				    (ha->hw.flt_ddb_size / 2);

		dev_db_start_offset += (fnode_sess->target_id *
				       sizeof(*fw_ddb_entry));
		dev_db_start_offset += offsetof(struct dev_db_entry, cookie);

		ddb_size = sizeof(*ddb_cookie);
	}

	DEBUG2(ql4_printk(KERN_ERR, ha, "%s: start offset=%u, end offset=%u\n",
			  __func__, dev_db_start_offset, dev_db_end_offset));

	if (dev_db_start_offset > dev_db_end_offset) {
		rc = -EIO;
		DEBUG2(ql4_printk(KERN_ERR, ha, "%s:Invalid DDB index %u\n",
				  __func__, fnode_sess->target_id));
		goto exit_ddb_del;
	}

	pddb = dma_alloc_coherent(&ha->pdev->dev, ddb_size,
				  &fw_ddb_entry_dma, GFP_KERNEL);
	if (!pddb) {
		rc = -ENOMEM;
		DEBUG2(ql4_printk(KERN_ERR, ha,
				  "%s: Unable to allocate dma buffer\n",
				  __func__));
		goto exit_ddb_del;
	}

	if (is_qla40XX(ha)) {
		fw_ddb_entry = pddb;
		memset(fw_ddb_entry, 0, ddb_size);
		ddb_cookie = &fw_ddb_entry->cookie;
	} else {
		ddb_cookie = pddb;
	}

	/* invalidate the cookie */
	*ddb_cookie = 0xFFEE;
	qla4xxx_set_flash(ha, fw_ddb_entry_dma, dev_db_start_offset,
			  ddb_size, FLASH_OPT_RMW_COMMIT);

sysfs_ddb_del:
	target_id = fnode_sess->target_id;
	iscsi_destroy_flashnode_sess(fnode_sess);
	ql4_printk(KERN_INFO, ha,
		   "%s: session and conn entries for flashnode %u of host %lu deleted\n",
		   __func__, target_id, ha->host_no);
exit_ddb_del:
	if (pddb)
		dma_free_coherent(&ha->pdev->dev, ddb_size, pddb,
				  fw_ddb_entry_dma);
	return rc;
}

/**
 * qla4xxx_sysfs_ddb_export - Create sysfs entries for firmware DDBs
 * @ha: pointer to adapter structure
 *
 * Export the firmware DDB for all send targets and normal targets to sysfs.
 **/
int qla4xxx_sysfs_ddb_export(struct scsi_qla_host *ha)
{
	struct dev_db_entry *fw_ddb_entry = NULL;
	dma_addr_t fw_ddb_entry_dma;
	uint16_t max_ddbs;
	uint16_t idx = 0;
	int ret = QLA_SUCCESS;

	fw_ddb_entry = dma_alloc_coherent(&ha->pdev->dev,
					  sizeof(*fw_ddb_entry),
					  &fw_ddb_entry_dma, GFP_KERNEL);
	if (!fw_ddb_entry) {
		DEBUG2(ql4_printk(KERN_ERR, ha,
				  "%s: Unable to allocate dma buffer\n",
				  __func__));
		return -ENOMEM;
	}

	max_ddbs =  is_qla40XX(ha) ? MAX_PRST_DEV_DB_ENTRIES :
				     MAX_DEV_DB_ENTRIES;

	for (idx = 0; idx < max_ddbs; idx++) {
		if (qla4xxx_flashdb_by_index(ha, fw_ddb_entry, fw_ddb_entry_dma,
					     idx))
			continue;

		ret = qla4xxx_sysfs_ddb_tgt_create(ha, fw_ddb_entry, &idx, 0);
		if (ret) {
			ret = -EIO;
			break;
		}
	}

	dma_free_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry), fw_ddb_entry,
			  fw_ddb_entry_dma);

	return ret;
}

static void qla4xxx_sysfs_ddb_remove(struct scsi_qla_host *ha)
{
	iscsi_destroy_all_flashnode(ha->host);
}

/**
 * qla4xxx_build_ddb_list - Build ddb list and setup sessions
 * @ha: pointer to adapter structure
 * @is_reset: Is this init path or reset path
 *
 * Create a list of sendtargets (st) from firmware DDBs, issue send targets
 * using connection open, then create the list of normal targets (nt)
 * from firmware DDBs. Based on the list of nt setup session and connection
 * objects.
 **/
void qla4xxx_build_ddb_list(struct scsi_qla_host *ha, int is_reset)
{
	uint16_t tmo = 0;
	struct list_head list_st, list_nt;
	struct qla_ddb_index  *st_ddb_idx, *st_ddb_idx_tmp;
	unsigned long wtime;

	if (!test_bit(AF_LINK_UP, &ha->flags)) {
		set_bit(AF_BUILD_DDB_LIST, &ha->flags);
		ha->is_reset = is_reset;
		return;
	}

	INIT_LIST_HEAD(&list_st);
	INIT_LIST_HEAD(&list_nt);

	qla4xxx_build_st_list(ha, &list_st);

	/* Before issuing conn open mbox, ensure all IPs states are configured
	 * Note, conn open fails if IPs are not configured
	 */
	qla4xxx_wait_for_ip_configuration(ha);

	/* Go thru the STs and fire the sendtargets by issuing conn open mbx */
	list_for_each_entry_safe(st_ddb_idx, st_ddb_idx_tmp, &list_st, list) {
		qla4xxx_conn_open(ha, st_ddb_idx->fw_ddb_idx);
	}

	/* Wait to ensure all sendtargets are done for min 12 sec wait */
	tmo = ((ha->def_timeout > LOGIN_TOV) &&
	       (ha->def_timeout < LOGIN_TOV * 10) ?
	       ha->def_timeout : LOGIN_TOV);

	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "Default time to wait for build ddb %d\n", tmo));

	wtime = jiffies + (HZ * tmo);
	do {
		if (list_empty(&list_st))
			break;

		qla4xxx_remove_failed_ddb(ha, &list_st);
		schedule_timeout_uninterruptible(HZ / 10);
	} while (time_after(wtime, jiffies));


	qla4xxx_build_nt_list(ha, &list_nt, &list_st, is_reset);

	qla4xxx_free_ddb_list(&list_st);
	qla4xxx_free_ddb_list(&list_nt);

	qla4xxx_free_ddb_index(ha);
}

/**
 * qla4xxx_wait_login_resp_boot_tgt -  Wait for iSCSI boot target login
 * response.
 * @ha: pointer to adapter structure
 *
 * When the boot entry is normal iSCSI target then DF_BOOT_TGT flag will be
 * set in DDB and we will wait for login response of boot targets during
 * probe.
 **/
static void qla4xxx_wait_login_resp_boot_tgt(struct scsi_qla_host *ha)
{
	struct ddb_entry *ddb_entry;
	struct dev_db_entry *fw_ddb_entry = NULL;
	dma_addr_t fw_ddb_entry_dma;
	unsigned long wtime;
	uint32_t ddb_state;
	int max_ddbs, idx, ret;

	max_ddbs =  is_qla40XX(ha) ? MAX_DEV_DB_ENTRIES_40XX :
				     MAX_DEV_DB_ENTRIES;

	fw_ddb_entry = dma_alloc_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
					  &fw_ddb_entry_dma, GFP_KERNEL);
	if (!fw_ddb_entry) {
		ql4_printk(KERN_ERR, ha,
			   "%s: Unable to allocate dma buffer\n", __func__);
		goto exit_login_resp;
	}

	wtime = jiffies + (HZ * BOOT_LOGIN_RESP_TOV);

	for (idx = 0; idx < max_ddbs; idx++) {
		ddb_entry = qla4xxx_lookup_ddb_by_fw_index(ha, idx);
		if (ddb_entry == NULL)
			continue;

		if (test_bit(DF_BOOT_TGT, &ddb_entry->flags)) {
			DEBUG2(ql4_printk(KERN_INFO, ha,
					  "%s: DDB index [%d]\n", __func__,
					  ddb_entry->fw_ddb_index));
			do {
				ret = qla4xxx_get_fwddb_entry(ha,
						ddb_entry->fw_ddb_index,
						fw_ddb_entry, fw_ddb_entry_dma,
						NULL, NULL, &ddb_state, NULL,
						NULL, NULL);
				if (ret == QLA_ERROR)
					goto exit_login_resp;

				if ((ddb_state == DDB_DS_SESSION_ACTIVE) ||
				    (ddb_state == DDB_DS_SESSION_FAILED))
					break;

				schedule_timeout_uninterruptible(HZ);

			} while ((time_after(wtime, jiffies)));

			if (!time_after(wtime, jiffies)) {
				DEBUG2(ql4_printk(KERN_INFO, ha,
						  "%s: Login response wait timer expired\n",
						  __func__));
				 goto exit_login_resp;
			}
		}
	}

exit_login_resp:
	if (fw_ddb_entry)
		dma_free_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
				  fw_ddb_entry, fw_ddb_entry_dma);
}

/**
 * qla4xxx_probe_adapter - callback function to probe HBA
 * @pdev: pointer to pci_dev structure
 * @ent: pointer to pci_device entry
 *
 * This routine will probe for Qlogic 4xxx iSCSI host adapters.
 * It returns zero if successful. It also initializes all data necessary for
 * the driver.
 **/
static int qla4xxx_probe_adapter(struct pci_dev *pdev,
				 const struct pci_device_id *ent)
{
	int ret = -ENODEV, status;
	struct Scsi_Host *host;
	struct scsi_qla_host *ha;
	uint8_t init_retry_count = 0;
	char buf[34];
	struct qla4_8xxx_legacy_intr_set *nx_legacy_intr;
	uint32_t dev_state;

	if (pci_enable_device(pdev))
		return -1;

	host = iscsi_host_alloc(&qla4xxx_driver_template, sizeof(*ha), 0);
	if (host == NULL) {
		printk(KERN_WARNING
		       "qla4xxx: Couldn't allocate host from scsi layer!\n");
		goto probe_disable_device;
	}

	/* Clear our data area */
	ha = to_qla_host(host);
	memset(ha, 0, sizeof(*ha));

	/* Save the information from PCI BIOS.	*/
	ha->pdev = pdev;
	ha->host = host;
	ha->host_no = host->host_no;
	ha->func_num = PCI_FUNC(ha->pdev->devfn);

	pci_enable_pcie_error_reporting(pdev);

	/* Setup Runtime configurable options */
	if (is_qla8022(ha)) {
		ha->isp_ops = &qla4_82xx_isp_ops;
		ha->reg_tbl = (uint32_t *) qla4_82xx_reg_tbl;
		ha->qdr_sn_window = -1;
		ha->ddr_mn_window = -1;
		ha->curr_window = 255;
		nx_legacy_intr = &legacy_intr[ha->func_num];
		ha->nx_legacy_intr.int_vec_bit = nx_legacy_intr->int_vec_bit;
		ha->nx_legacy_intr.tgt_status_reg =
			nx_legacy_intr->tgt_status_reg;
		ha->nx_legacy_intr.tgt_mask_reg = nx_legacy_intr->tgt_mask_reg;
		ha->nx_legacy_intr.pci_int_reg = nx_legacy_intr->pci_int_reg;
	} else if (is_qla8032(ha) || is_qla8042(ha)) {
		ha->isp_ops = &qla4_83xx_isp_ops;
		ha->reg_tbl = (uint32_t *)qla4_83xx_reg_tbl;
	} else {
		ha->isp_ops = &qla4xxx_isp_ops;
	}

	if (is_qla80XX(ha)) {
		rwlock_init(&ha->hw_lock);
		ha->pf_bit = ha->func_num << 16;
		/* Set EEH reset type to fundamental if required by hba */
		pdev->needs_freset = 1;
	}

	/* Configure PCI I/O space. */
	ret = ha->isp_ops->iospace_config(ha);
	if (ret)
		goto probe_failed_ioconfig;

	ql4_printk(KERN_INFO, ha, "Found an ISP%04x, irq %d, iobase 0x%p\n",
		   pdev->device, pdev->irq, ha->reg);

	qla4xxx_config_dma_addressing(ha);

	/* Initialize lists and spinlocks. */
	INIT_LIST_HEAD(&ha->free_srb_q);

	mutex_init(&ha->mbox_sem);
	mutex_init(&ha->chap_sem);
	init_completion(&ha->mbx_intr_comp);
	init_completion(&ha->disable_acb_comp);
	init_completion(&ha->idc_comp);
	init_completion(&ha->link_up_comp);

	spin_lock_init(&ha->hardware_lock);
	spin_lock_init(&ha->work_lock);

	/* Initialize work list */
	INIT_LIST_HEAD(&ha->work_list);

	/* Allocate dma buffers */
	if (qla4xxx_mem_alloc(ha)) {
		ql4_printk(KERN_WARNING, ha,
		    "[ERROR] Failed to allocate memory for adapter\n");

		ret = -ENOMEM;
		goto probe_failed;
	}

	host->cmd_per_lun = 3;
	host->max_channel = 0;
	host->max_lun = MAX_LUNS - 1;
	host->max_id = MAX_TARGETS;
	host->max_cmd_len = IOCB_MAX_CDB_LEN;
	host->can_queue = MAX_SRBS ;
	host->transportt = qla4xxx_scsi_transport;

	pci_set_drvdata(pdev, ha);

	ret = scsi_add_host(host, &pdev->dev);
	if (ret)
		goto probe_failed;

	if (is_qla80XX(ha))
		qla4_8xxx_get_flash_info(ha);

	if (is_qla8032(ha) || is_qla8042(ha)) {
		qla4_83xx_read_reset_template(ha);
		/*
		 * NOTE: If ql4dontresethba==1, set IDC_CTRL DONTRESET_BIT0.
		 * If DONRESET_BIT0 is set, drivers should not set dev_state
		 * to NEED_RESET. But if NEED_RESET is set, drivers should
		 * should honor the reset.
		 */
		if (ql4xdontresethba == 1)
			qla4_83xx_set_idc_dontreset(ha);
	}

	/*
	 * Initialize the Host adapter request/response queues and
	 * firmware
	 * NOTE: interrupts enabled upon successful completion
	 */
	status = qla4xxx_initialize_adapter(ha, INIT_ADAPTER);

	/* Dont retry adapter initialization if IRQ allocation failed */
	if (is_qla80XX(ha) && (status == QLA_ERROR))
		goto skip_retry_init;

	while ((!test_bit(AF_ONLINE, &ha->flags)) &&
	    init_retry_count++ < MAX_INIT_RETRIES) {

		if (is_qla80XX(ha)) {
			ha->isp_ops->idc_lock(ha);
			dev_state = qla4_8xxx_rd_direct(ha,
							QLA8XXX_CRB_DEV_STATE);
			ha->isp_ops->idc_unlock(ha);
			if (dev_state == QLA8XXX_DEV_FAILED) {
				ql4_printk(KERN_WARNING, ha, "%s: don't retry "
				    "initialize adapter. H/W is in failed state\n",
				    __func__);
				break;
			}
		}
		DEBUG2(printk("scsi: %s: retrying adapter initialization "
			      "(%d)\n", __func__, init_retry_count));

		if (ha->isp_ops->reset_chip(ha) == QLA_ERROR)
			continue;

		status = qla4xxx_initialize_adapter(ha, INIT_ADAPTER);
		if (is_qla80XX(ha) && (status == QLA_ERROR)) {
			if (qla4_8xxx_check_init_adapter_retry(ha) == QLA_ERROR)
				goto skip_retry_init;
		}
	}

skip_retry_init:
	if (!test_bit(AF_ONLINE, &ha->flags)) {
		ql4_printk(KERN_WARNING, ha, "Failed to initialize adapter\n");

		if ((is_qla8022(ha) && ql4xdontresethba) ||
		    ((is_qla8032(ha) || is_qla8042(ha)) &&
		     qla4_83xx_idc_dontreset(ha))) {
			/* Put the device in failed state. */
			DEBUG2(printk(KERN_ERR "HW STATE: FAILED\n"));
			ha->isp_ops->idc_lock(ha);
			qla4_8xxx_wr_direct(ha, QLA8XXX_CRB_DEV_STATE,
					    QLA8XXX_DEV_FAILED);
			ha->isp_ops->idc_unlock(ha);
		}
		ret = -ENODEV;
		goto remove_host;
	}

	/* Startup the kernel thread for this host adapter. */
	DEBUG2(printk("scsi: %s: Starting kernel thread for "
		      "qla4xxx_dpc\n", __func__));
	sprintf(buf, "qla4xxx_%lu_dpc", ha->host_no);
	ha->dpc_thread = create_singlethread_workqueue(buf);
	if (!ha->dpc_thread) {
		ql4_printk(KERN_WARNING, ha, "Unable to start DPC thread!\n");
		ret = -ENODEV;
		goto remove_host;
	}
	INIT_WORK(&ha->dpc_work, qla4xxx_do_dpc);

	ha->task_wq = alloc_workqueue("qla4xxx_%lu_task", WQ_MEM_RECLAIM, 1,
				      ha->host_no);
	if (!ha->task_wq) {
		ql4_printk(KERN_WARNING, ha, "Unable to start task thread!\n");
		ret = -ENODEV;
		goto remove_host;
	}

	/*
	 * For ISP-8XXX, request_irqs is called in qla4_8xxx_load_risc
	 * (which is called indirectly by qla4xxx_initialize_adapter),
	 * so that irqs will be registered after crbinit but before
	 * mbx_intr_enable.
	 */
	if (is_qla40XX(ha)) {
		ret = qla4xxx_request_irqs(ha);
		if (ret) {
			ql4_printk(KERN_WARNING, ha, "Failed to reserve "
			    "interrupt %d already in use.\n", pdev->irq);
			goto remove_host;
		}
	}

	pci_save_state(ha->pdev);
	ha->isp_ops->enable_intrs(ha);

	/* Start timer thread. */
	qla4xxx_start_timer(ha, 1);

	set_bit(AF_INIT_DONE, &ha->flags);

	qla4_8xxx_alloc_sysfs_attr(ha);

	printk(KERN_INFO
	       " QLogic iSCSI HBA Driver version: %s\n"
	       "  QLogic ISP%04x @ %s, host#=%ld, fw=%02d.%02d.%02d.%02d\n",
	       qla4xxx_version_str, ha->pdev->device, pci_name(ha->pdev),
	       ha->host_no, ha->fw_info.fw_major, ha->fw_info.fw_minor,
	       ha->fw_info.fw_patch, ha->fw_info.fw_build);

	/* Set the driver version */
	if (is_qla80XX(ha))
		qla4_8xxx_set_param(ha, SET_DRVR_VERSION);

	if (qla4xxx_setup_boot_info(ha))
		ql4_printk(KERN_ERR, ha,
			   "%s: No iSCSI boot target configured\n", __func__);

	set_bit(DPC_SYSFS_DDB_EXPORT, &ha->dpc_flags);
	/* Perform the build ddb list and login to each */
	qla4xxx_build_ddb_list(ha, INIT_ADAPTER);
	iscsi_host_for_each_session(ha->host, qla4xxx_login_flash_ddb);
	qla4xxx_wait_login_resp_boot_tgt(ha);

	qla4xxx_create_chap_list(ha);

	qla4xxx_create_ifaces(ha);
	return 0;

remove_host:
	scsi_remove_host(ha->host);

probe_failed:
	qla4xxx_free_adapter(ha);

probe_failed_ioconfig:
	pci_disable_pcie_error_reporting(pdev);
	scsi_host_put(ha->host);

probe_disable_device:
	pci_disable_device(pdev);

	return ret;
}

/**
 * qla4xxx_prevent_other_port_reinit - prevent other port from re-initialize
 * @ha: pointer to adapter structure
 *
 * Mark the other ISP-4xxx port to indicate that the driver is being removed,
 * so that the other port will not re-initialize while in the process of
 * removing the ha due to driver unload or hba hotplug.
 **/
static void qla4xxx_prevent_other_port_reinit(struct scsi_qla_host *ha)
{
	struct scsi_qla_host *other_ha = NULL;
	struct pci_dev *other_pdev = NULL;
	int fn = ISP4XXX_PCI_FN_2;

	/*iscsi function numbers for ISP4xxx is 1 and 3*/
	if (PCI_FUNC(ha->pdev->devfn) & BIT_1)
		fn = ISP4XXX_PCI_FN_1;

	other_pdev =
		pci_get_domain_bus_and_slot(pci_domain_nr(ha->pdev->bus),
		ha->pdev->bus->number, PCI_DEVFN(PCI_SLOT(ha->pdev->devfn),
		fn));

	/* Get other_ha if other_pdev is valid and state is enable*/
	if (other_pdev) {
		if (atomic_read(&other_pdev->enable_cnt)) {
			other_ha = pci_get_drvdata(other_pdev);
			if (other_ha) {
				set_bit(AF_HA_REMOVAL, &other_ha->flags);
				DEBUG2(ql4_printk(KERN_INFO, ha, "%s: "
				    "Prevent %s reinit\n", __func__,
				    dev_name(&other_ha->pdev->dev)));
			}
		}
		pci_dev_put(other_pdev);
	}
}

static void qla4xxx_destroy_ddb(struct scsi_qla_host *ha,
		struct ddb_entry *ddb_entry)
{
	struct dev_db_entry *fw_ddb_entry = NULL;
	dma_addr_t fw_ddb_entry_dma;
	unsigned long wtime;
	uint32_t ddb_state;
	int options;
	int status;

	options = LOGOUT_OPTION_CLOSE_SESSION;
	if (qla4xxx_session_logout_ddb(ha, ddb_entry, options) == QLA_ERROR) {
		ql4_printk(KERN_ERR, ha, "%s: Logout failed\n", __func__);
		goto clear_ddb;
	}

	fw_ddb_entry = dma_alloc_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
					  &fw_ddb_entry_dma, GFP_KERNEL);
	if (!fw_ddb_entry) {
		ql4_printk(KERN_ERR, ha,
			   "%s: Unable to allocate dma buffer\n", __func__);
		goto clear_ddb;
	}

	wtime = jiffies + (HZ * LOGOUT_TOV);
	do {
		status = qla4xxx_get_fwddb_entry(ha, ddb_entry->fw_ddb_index,
						 fw_ddb_entry, fw_ddb_entry_dma,
						 NULL, NULL, &ddb_state, NULL,
						 NULL, NULL);
		if (status == QLA_ERROR)
			goto free_ddb;

		if ((ddb_state == DDB_DS_NO_CONNECTION_ACTIVE) ||
		    (ddb_state == DDB_DS_SESSION_FAILED))
			goto free_ddb;

		schedule_timeout_uninterruptible(HZ);
	} while ((time_after(wtime, jiffies)));

free_ddb:
	dma_free_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
			  fw_ddb_entry, fw_ddb_entry_dma);
clear_ddb:
	qla4xxx_clear_ddb_entry(ha, ddb_entry->fw_ddb_index);
}

static void qla4xxx_destroy_fw_ddb_session(struct scsi_qla_host *ha)
{
	struct ddb_entry *ddb_entry;
	int idx;

	for (idx = 0; idx < MAX_DDB_ENTRIES; idx++) {

		ddb_entry = qla4xxx_lookup_ddb_by_fw_index(ha, idx);
		if ((ddb_entry != NULL) &&
		    (ddb_entry->ddb_type == FLASH_DDB)) {

			qla4xxx_destroy_ddb(ha, ddb_entry);
			/*
			 * we have decremented the reference count of the driver
			 * when we setup the session to have the driver unload
			 * to be seamless without actually destroying the
			 * session
			 **/
			try_module_get(qla4xxx_iscsi_transport.owner);
			iscsi_destroy_endpoint(ddb_entry->conn->ep);
			qla4xxx_free_ddb(ha, ddb_entry);
			iscsi_session_teardown(ddb_entry->sess);
		}
	}
}
/**
 * qla4xxx_remove_adapter - callback function to remove adapter.
 * @pdev: PCI device pointer
 **/
static void qla4xxx_remove_adapter(struct pci_dev *pdev)
{
	struct scsi_qla_host *ha;

	/*
	 * If the PCI device is disabled then it means probe_adapter had
	 * failed and resources already cleaned up on probe_adapter exit.
	 */
	if (!pci_is_enabled(pdev))
		return;

	ha = pci_get_drvdata(pdev);

	if (is_qla40XX(ha))
		qla4xxx_prevent_other_port_reinit(ha);

	/* destroy iface from sysfs */
	qla4xxx_destroy_ifaces(ha);

	if ((!ql4xdisablesysfsboot) && ha->boot_kset)
		iscsi_boot_destroy_kset(ha->boot_kset);

	qla4xxx_destroy_fw_ddb_session(ha);
	qla4_8xxx_free_sysfs_attr(ha);

	qla4xxx_sysfs_ddb_remove(ha);
	scsi_remove_host(ha->host);

	qla4xxx_free_adapter(ha);

	scsi_host_put(ha->host);

	pci_disable_pcie_error_reporting(pdev);
	pci_disable_device(pdev);
}

/**
 * qla4xxx_config_dma_addressing() - Configure OS DMA addressing method.
 * @ha: HA context
 */
static void qla4xxx_config_dma_addressing(struct scsi_qla_host *ha)
{
	/* Update our PCI device dma_mask for full 64 bit mask */
	if (dma_set_mask_and_coherent(&ha->pdev->dev, DMA_BIT_MASK(64))) {
		dev_dbg(&ha->pdev->dev,
			  "Failed to set 64 bit PCI consistent mask; "
			   "using 32 bit.\n");
		dma_set_mask_and_coherent(&ha->pdev->dev, DMA_BIT_MASK(32));
	}
}

static int qla4xxx_slave_alloc(struct scsi_device *sdev)
{
	struct iscsi_cls_session *cls_sess;
	struct iscsi_session *sess;
	struct ddb_entry *ddb;
	int queue_depth = QL4_DEF_QDEPTH;

	cls_sess = starget_to_session(sdev->sdev_target);
	sess = cls_sess->dd_data;
	ddb = sess->dd_data;

	sdev->hostdata = ddb;

	if (ql4xmaxqdepth != 0 && ql4xmaxqdepth <= 0xffffU)
		queue_depth = ql4xmaxqdepth;

	scsi_change_queue_depth(sdev, queue_depth);
	return 0;
}

/**
 * qla4xxx_del_from_active_array - returns an active srb
 * @ha: Pointer to host adapter structure.
 * @index: index into the active_array
 *
 * This routine removes and returns the srb at the specified index
 **/
struct srb *qla4xxx_del_from_active_array(struct scsi_qla_host *ha,
    uint32_t index)
{
	struct srb *srb = NULL;
	struct scsi_cmnd *cmd = NULL;

	cmd = scsi_host_find_tag(ha->host, index);
	if (!cmd)
		return srb;

	srb = qla4xxx_cmd_priv(cmd)->srb;
	if (!srb)
		return srb;

	/* update counters */
	if (srb->flags & SRB_DMA_VALID) {
		ha->iocb_cnt -= srb->iocb_cnt;
		if (srb->cmd)
			srb->cmd->host_scribble =
				(unsigned char *)(unsigned long) MAX_SRBS;
	}
	return srb;
}

/**
 * qla4xxx_eh_wait_on_command - waits for command to be returned by firmware
 * @ha: Pointer to host adapter structure.
 * @cmd: Scsi Command to wait on.
 *
 * This routine waits for the command to be returned by the Firmware
 * for some max time.
 **/
static int qla4xxx_eh_wait_on_command(struct scsi_qla_host *ha,
				      struct scsi_cmnd *cmd)
{
	int done = 0;
	struct srb *rp;
	uint32_t max_wait_time = EH_WAIT_CMD_TOV;
	int ret = SUCCESS;

	/* Dont wait on command if PCI error is being handled
	 * by PCI AER driver
	 */
	if (unlikely(pci_channel_offline(ha->pdev)) ||
	    (test_bit(AF_EEH_BUSY, &ha->flags))) {
		ql4_printk(KERN_WARNING, ha, "scsi%ld: Return from %s\n",
		    ha->host_no, __func__);
		return ret;
	}

	do {
		/* Checking to see if its returned to OS */
		rp = qla4xxx_cmd_priv(cmd)->srb;
		if (rp == NULL) {
			done++;
			break;
		}

		msleep(2000);
	} while (max_wait_time--);

	return done;
}

/**
 * qla4xxx_wait_for_hba_online - waits for HBA to come online
 * @ha: Pointer to host adapter structure
 **/
static int qla4xxx_wait_for_hba_online(struct scsi_qla_host *ha)
{
	unsigned long wait_online;

	wait_online = jiffies + (HBA_ONLINE_TOV * HZ);
	while (time_before(jiffies, wait_online)) {

		if (adapter_up(ha))
			return QLA_SUCCESS;

		msleep(2000);
	}

	return QLA_ERROR;
}

/**
 * qla4xxx_eh_wait_for_commands - wait for active cmds to finish.
 * @ha: pointer to HBA
 * @stgt: pointer to SCSI target
 * @sdev: pointer to SCSI device
 *
 * This function waits for all outstanding commands to a lun to complete. It
 * returns 0 if all pending commands are returned and 1 otherwise.
 **/
static int qla4xxx_eh_wait_for_commands(struct scsi_qla_host *ha,
					struct scsi_target *stgt,
					struct scsi_device *sdev)
{
	int cnt;
	int status = 0;
	struct scsi_cmnd *cmd;

	/*
	 * Waiting for all commands for the designated target or dev
	 * in the active array
	 */
	for (cnt = 0; cnt < ha->host->can_queue; cnt++) {
		cmd = scsi_host_find_tag(ha->host, cnt);
		if (cmd && stgt == scsi_target(cmd->device) &&
		    (!sdev || sdev == cmd->device)) {
			if (!qla4xxx_eh_wait_on_command(ha, cmd)) {
				status++;
				break;
			}
		}
	}
	return status;
}

/**
 * qla4xxx_eh_abort - callback for abort task.
 * @cmd: Pointer to Linux's SCSI command structure
 *
 * This routine is called by the Linux OS to abort the specified
 * command.
 **/
static int qla4xxx_eh_abort(struct scsi_cmnd *cmd)
{
	struct scsi_qla_host *ha = to_qla_host(cmd->device->host);
	unsigned int id = cmd->device->id;
	uint64_t lun = cmd->device->lun;
	unsigned long flags;
	struct srb *srb = NULL;
	int ret = SUCCESS;
	int wait = 0;
	int rval;

	ql4_printk(KERN_INFO, ha, "scsi%ld:%d:%llu: Abort command issued cmd=%p, cdb=0x%x\n",
		   ha->host_no, id, lun, cmd, cmd->cmnd[0]);

	rval = qla4xxx_isp_check_reg(ha);
	if (rval != QLA_SUCCESS) {
		ql4_printk(KERN_INFO, ha, "PCI/Register disconnect, exiting.\n");
		return FAILED;
	}

	spin_lock_irqsave(&ha->hardware_lock, flags);
	srb = qla4xxx_cmd_priv(cmd)->srb;
	if (!srb) {
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		ql4_printk(KERN_INFO, ha, "scsi%ld:%d:%llu: Specified command has already completed.\n",
			   ha->host_no, id, lun);
		return SUCCESS;
	}
	kref_get(&srb->srb_ref);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (qla4xxx_abort_task(ha, srb) != QLA_SUCCESS) {
		DEBUG3(printk("scsi%ld:%d:%llu: Abort_task mbx failed.\n",
		    ha->host_no, id, lun));
		ret = FAILED;
	} else {
		DEBUG3(printk("scsi%ld:%d:%llu: Abort_task mbx success.\n",
		    ha->host_no, id, lun));
		wait = 1;
	}

	kref_put(&srb->srb_ref, qla4xxx_srb_compl);

	/* Wait for command to complete */
	if (wait) {
		if (!qla4xxx_eh_wait_on_command(ha, cmd)) {
			DEBUG2(printk("scsi%ld:%d:%llu: Abort handler timed out\n",
			    ha->host_no, id, lun));
			ret = FAILED;
		}
	}

	ql4_printk(KERN_INFO, ha,
	    "scsi%ld:%d:%llu: Abort command - %s\n",
	    ha->host_no, id, lun, (ret == SUCCESS) ? "succeeded" : "failed");

	return ret;
}

/**
 * qla4xxx_eh_device_reset - callback for target reset.
 * @cmd: Pointer to Linux's SCSI command structure
 *
 * This routine is called by the Linux OS to reset all luns on the
 * specified target.
 **/
static int qla4xxx_eh_device_reset(struct scsi_cmnd *cmd)
{
	struct scsi_qla_host *ha = to_qla_host(cmd->device->host);
	struct ddb_entry *ddb_entry = cmd->device->hostdata;
	int ret = FAILED, stat;
	int rval;

	if (!ddb_entry)
		return ret;

	ret = iscsi_block_scsi_eh(cmd);
	if (ret)
		return ret;
	ret = FAILED;

	ql4_printk(KERN_INFO, ha,
		   "scsi%ld:%d:%d:%llu: DEVICE RESET ISSUED.\n", ha->host_no,
		   cmd->device->channel, cmd->device->id, cmd->device->lun);

	DEBUG2(printk(KERN_INFO
		      "scsi%ld: DEVICE_RESET cmd=%p jiffies = 0x%lx, to=%x,"
		      "dpc_flags=%lx, status=%x allowed=%d\n", ha->host_no,
		      cmd, jiffies, scsi_cmd_to_rq(cmd)->timeout / HZ,
		      ha->dpc_flags, cmd->result, cmd->allowed));

	rval = qla4xxx_isp_check_reg(ha);
	if (rval != QLA_SUCCESS) {
		ql4_printk(KERN_INFO, ha, "PCI/Register disconnect, exiting.\n");
		return FAILED;
	}

	/* FIXME: wait for hba to go online */
	stat = qla4xxx_reset_lun(ha, ddb_entry, cmd->device->lun);
	if (stat != QLA_SUCCESS) {
		ql4_printk(KERN_INFO, ha, "DEVICE RESET FAILED. %d\n", stat);
		goto eh_dev_reset_done;
	}

	if (qla4xxx_eh_wait_for_commands(ha, scsi_target(cmd->device),
					 cmd->device)) {
		ql4_printk(KERN_INFO, ha,
			   "DEVICE RESET FAILED - waiting for "
			   "commands.\n");
		goto eh_dev_reset_done;
	}

	/* Send marker. */
	if (qla4xxx_send_marker_iocb(ha, ddb_entry, cmd->device->lun,
		MM_LUN_RESET) != QLA_SUCCESS)
		goto eh_dev_reset_done;

	ql4_printk(KERN_INFO, ha,
		   "scsi(%ld:%d:%d:%llu): DEVICE RESET SUCCEEDED.\n",
		   ha->host_no, cmd->device->channel, cmd->device->id,
		   cmd->device->lun);

	ret = SUCCESS;

eh_dev_reset_done:

	return ret;
}

/**
 * qla4xxx_eh_target_reset - callback for target reset.
 * @cmd: Pointer to Linux's SCSI command structure
 *
 * This routine is called by the Linux OS to reset the target.
 **/
static int qla4xxx_eh_target_reset(struct scsi_cmnd *cmd)
{
	struct scsi_qla_host *ha = to_qla_host(cmd->device->host);
	struct ddb_entry *ddb_entry = cmd->device->hostdata;
	int stat, ret;
	int rval;

	if (!ddb_entry)
		return FAILED;

	ret = iscsi_block_scsi_eh(cmd);
	if (ret)
		return ret;

	starget_printk(KERN_INFO, scsi_target(cmd->device),
		       "WARM TARGET RESET ISSUED.\n");

	DEBUG2(printk(KERN_INFO
		      "scsi%ld: TARGET_DEVICE_RESET cmd=%p jiffies = 0x%lx, "
		      "to=%x,dpc_flags=%lx, status=%x allowed=%d\n",
		      ha->host_no, cmd, jiffies, scsi_cmd_to_rq(cmd)->timeout / HZ,
		      ha->dpc_flags, cmd->result, cmd->allowed));

	rval = qla4xxx_isp_check_reg(ha);
	if (rval != QLA_SUCCESS) {
		ql4_printk(KERN_INFO, ha, "PCI/Register disconnect, exiting.\n");
		return FAILED;
	}

	stat = qla4xxx_reset_target(ha, ddb_entry);
	if (stat != QLA_SUCCESS) {
		starget_printk(KERN_INFO, scsi_target(cmd->device),
			       "WARM TARGET RESET FAILED.\n");
		return FAILED;
	}

	if (qla4xxx_eh_wait_for_commands(ha, scsi_target(cmd->device),
					 NULL)) {
		starget_printk(KERN_INFO, scsi_target(cmd->device),
			       "WARM TARGET DEVICE RESET FAILED - "
			       "waiting for commands.\n");
		return FAILED;
	}

	/* Send marker. */
	if (qla4xxx_send_marker_iocb(ha, ddb_entry, cmd->device->lun,
		MM_TGT_WARM_RESET) != QLA_SUCCESS) {
		starget_printk(KERN_INFO, scsi_target(cmd->device),
			       "WARM TARGET DEVICE RESET FAILED - "
			       "marker iocb failed.\n");
		return FAILED;
	}

	starget_printk(KERN_INFO, scsi_target(cmd->device),
		       "WARM TARGET RESET SUCCEEDED.\n");
	return SUCCESS;
}

/**
 * qla4xxx_is_eh_active - check if error handler is running
 * @shost: Pointer to SCSI Host struct
 *
 * This routine finds that if reset host is called in EH
 * scenario or from some application like sg_reset
 **/
static int qla4xxx_is_eh_active(struct Scsi_Host *shost)
{
	if (shost->shost_state == SHOST_RECOVERY)
		return 1;
	return 0;
}

/**
 * qla4xxx_eh_host_reset - kernel callback
 * @cmd: Pointer to Linux's SCSI command structure
 *
 * This routine is invoked by the Linux kernel to perform fatal error
 * recovery on the specified adapter.
 **/
static int qla4xxx_eh_host_reset(struct scsi_cmnd *cmd)
{
	int return_status = FAILED;
	struct scsi_qla_host *ha;
	int rval;

	ha = to_qla_host(cmd->device->host);

	rval = qla4xxx_isp_check_reg(ha);
	if (rval != QLA_SUCCESS) {
		ql4_printk(KERN_INFO, ha, "PCI/Register disconnect, exiting.\n");
		return FAILED;
	}

	if ((is_qla8032(ha) || is_qla8042(ha)) && ql4xdontresethba)
		qla4_83xx_set_idc_dontreset(ha);

	/*
	 * For ISP8324 and ISP8042, if IDC_CTRL DONTRESET_BIT0 is set by other
	 * protocol drivers, we should not set device_state to NEED_RESET
	 */
	if (ql4xdontresethba ||
	    ((is_qla8032(ha) || is_qla8042(ha)) &&
	     qla4_83xx_idc_dontreset(ha))) {
		DEBUG2(printk("scsi%ld: %s: Don't Reset HBA\n",
		     ha->host_no, __func__));

		/* Clear outstanding srb in queues */
		if (qla4xxx_is_eh_active(cmd->device->host))
			qla4xxx_abort_active_cmds(ha, DID_ABORT << 16);

		return FAILED;
	}

	ql4_printk(KERN_INFO, ha,
		   "scsi(%ld:%d:%d:%llu): HOST RESET ISSUED.\n", ha->host_no,
		   cmd->device->channel, cmd->device->id, cmd->device->lun);

	if (qla4xxx_wait_for_hba_online(ha) != QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld:%d: %s: Unable to reset host.  Adapter "
			      "DEAD.\n", ha->host_no, cmd->device->channel,
			      __func__));

		return FAILED;
	}

	if (!test_bit(DPC_RESET_HA, &ha->dpc_flags)) {
		if (is_qla80XX(ha))
			set_bit(DPC_RESET_HA_FW_CONTEXT, &ha->dpc_flags);
		else
			set_bit(DPC_RESET_HA, &ha->dpc_flags);
	}

	if (qla4xxx_recover_adapter(ha) == QLA_SUCCESS)
		return_status = SUCCESS;

	ql4_printk(KERN_INFO, ha, "HOST RESET %s.\n",
		   return_status == FAILED ? "FAILED" : "SUCCEEDED");

	return return_status;
}

static int qla4xxx_context_reset(struct scsi_qla_host *ha)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	struct addr_ctrl_blk_def *acb = NULL;
	uint32_t acb_len = sizeof(struct addr_ctrl_blk_def);
	int rval = QLA_SUCCESS;
	dma_addr_t acb_dma;

	acb = dma_alloc_coherent(&ha->pdev->dev,
				 sizeof(struct addr_ctrl_blk_def),
				 &acb_dma, GFP_KERNEL);
	if (!acb) {
		ql4_printk(KERN_ERR, ha, "%s: Unable to alloc acb\n",
			   __func__);
		rval = -ENOMEM;
		goto exit_port_reset;
	}

	memset(acb, 0, acb_len);

	rval = qla4xxx_get_acb(ha, acb_dma, PRIMARI_ACB, acb_len);
	if (rval != QLA_SUCCESS) {
		rval = -EIO;
		goto exit_free_acb;
	}

	rval = qla4xxx_disable_acb(ha);
	if (rval != QLA_SUCCESS) {
		rval = -EIO;
		goto exit_free_acb;
	}

	wait_for_completion_timeout(&ha->disable_acb_comp,
				    DISABLE_ACB_TOV * HZ);

	rval = qla4xxx_set_acb(ha, &mbox_cmd[0], &mbox_sts[0], acb_dma);
	if (rval != QLA_SUCCESS) {
		rval = -EIO;
		goto exit_free_acb;
	}

exit_free_acb:
	dma_free_coherent(&ha->pdev->dev, sizeof(struct addr_ctrl_blk_def),
			  acb, acb_dma);
exit_port_reset:
	DEBUG2(ql4_printk(KERN_INFO, ha, "%s %s\n", __func__,
			  rval == QLA_SUCCESS ? "SUCCEEDED" : "FAILED"));
	return rval;
}

static int qla4xxx_host_reset(struct Scsi_Host *shost, int reset_type)
{
	struct scsi_qla_host *ha = to_qla_host(shost);
	int rval = QLA_SUCCESS;
	uint32_t idc_ctrl;

	if (ql4xdontresethba) {
		DEBUG2(ql4_printk(KERN_INFO, ha, "%s: Don't Reset HBA\n",
				  __func__));
		rval = -EPERM;
		goto exit_host_reset;
	}

	if (test_bit(DPC_RESET_HA, &ha->dpc_flags))
		goto recover_adapter;

	switch (reset_type) {
	case SCSI_ADAPTER_RESET:
		set_bit(DPC_RESET_HA, &ha->dpc_flags);
		break;
	case SCSI_FIRMWARE_RESET:
		if (!test_bit(DPC_RESET_HA, &ha->dpc_flags)) {
			if (is_qla80XX(ha))
				/* set firmware context reset */
				set_bit(DPC_RESET_HA_FW_CONTEXT,
					&ha->dpc_flags);
			else {
				rval = qla4xxx_context_reset(ha);
				goto exit_host_reset;
			}
		}
		break;
	}

recover_adapter:
	/* For ISP8324 and ISP8042 set graceful reset bit in IDC_DRV_CTRL if
	 * reset is issued by application */
	if ((is_qla8032(ha) || is_qla8042(ha)) &&
	    test_bit(DPC_RESET_HA, &ha->dpc_flags)) {
		idc_ctrl = qla4_83xx_rd_reg(ha, QLA83XX_IDC_DRV_CTRL);
		qla4_83xx_wr_reg(ha, QLA83XX_IDC_DRV_CTRL,
				 (idc_ctrl | GRACEFUL_RESET_BIT1));
	}

	rval = qla4xxx_recover_adapter(ha);
	if (rval != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_INFO, ha, "%s: recover adapter fail\n",
				  __func__));
		rval = -EIO;
	}

exit_host_reset:
	return rval;
}

/* PCI AER driver recovers from all correctable errors w/o
 * driver intervention. For uncorrectable errors PCI AER
 * driver calls the following device driver's callbacks
 *
 * - Fatal Errors - link_reset
 * - Non-Fatal Errors - driver's error_detected() which
 * returns CAN_RECOVER, NEED_RESET or DISCONNECT.
 *
 * PCI AER driver calls
 * CAN_RECOVER - driver's mmio_enabled(), mmio_enabled()
 *               returns RECOVERED or NEED_RESET if fw_hung
 * NEED_RESET - driver's slot_reset()
 * DISCONNECT - device is dead & cannot recover
 * RECOVERED - driver's resume()
 */
static pci_ers_result_t
qla4xxx_pci_error_detected(struct pci_dev *pdev, pci_channel_state_t state)
{
	struct scsi_qla_host *ha = pci_get_drvdata(pdev);

	ql4_printk(KERN_WARNING, ha, "scsi%ld: %s: error detected:state %x\n",
	    ha->host_no, __func__, state);

	if (!is_aer_supported(ha))
		return PCI_ERS_RESULT_NONE;

	switch (state) {
	case pci_channel_io_normal:
		clear_bit(AF_EEH_BUSY, &ha->flags);
		return PCI_ERS_RESULT_CAN_RECOVER;
	case pci_channel_io_frozen:
		set_bit(AF_EEH_BUSY, &ha->flags);
		qla4xxx_mailbox_premature_completion(ha);
		qla4xxx_free_irqs(ha);
		pci_disable_device(pdev);
		/* Return back all IOs */
		qla4xxx_abort_active_cmds(ha, DID_RESET << 16);
		return PCI_ERS_RESULT_NEED_RESET;
	case pci_channel_io_perm_failure:
		set_bit(AF_EEH_BUSY, &ha->flags);
		set_bit(AF_PCI_CHANNEL_IO_PERM_FAILURE, &ha->flags);
		qla4xxx_abort_active_cmds(ha, DID_NO_CONNECT << 16);
		return PCI_ERS_RESULT_DISCONNECT;
	}
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * qla4xxx_pci_mmio_enabled() - gets called if
 * qla4xxx_pci_error_detected() returns PCI_ERS_RESULT_CAN_RECOVER
 * and read/write to the device still works.
 * @pdev: PCI device pointer
 **/
static pci_ers_result_t
qla4xxx_pci_mmio_enabled(struct pci_dev *pdev)
{
	struct scsi_qla_host *ha = pci_get_drvdata(pdev);

	if (!is_aer_supported(ha))
		return PCI_ERS_RESULT_NONE;

	return PCI_ERS_RESULT_RECOVERED;
}

static uint32_t qla4_8xxx_error_recovery(struct scsi_qla_host *ha)
{
	uint32_t rval = QLA_ERROR;
	int fn;
	struct pci_dev *other_pdev = NULL;

	ql4_printk(KERN_WARNING, ha, "scsi%ld: In %s\n", ha->host_no, __func__);

	set_bit(DPC_RESET_ACTIVE, &ha->dpc_flags);

	if (test_bit(AF_ONLINE, &ha->flags)) {
		clear_bit(AF_ONLINE, &ha->flags);
		clear_bit(AF_LINK_UP, &ha->flags);
		iscsi_host_for_each_session(ha->host, qla4xxx_fail_session);
		qla4xxx_process_aen(ha, FLUSH_DDB_CHANGED_AENS);
	}

	fn = PCI_FUNC(ha->pdev->devfn);
	if (is_qla8022(ha)) {
		while (fn > 0) {
			fn--;
			ql4_printk(KERN_INFO, ha, "scsi%ld: %s: Finding PCI device at func %x\n",
				   ha->host_no, __func__, fn);
			/* Get the pci device given the domain, bus,
			 * slot/function number */
			other_pdev = pci_get_domain_bus_and_slot(
					   pci_domain_nr(ha->pdev->bus),
					   ha->pdev->bus->number,
					   PCI_DEVFN(PCI_SLOT(ha->pdev->devfn),
					   fn));

			if (!other_pdev)
				continue;

			if (atomic_read(&other_pdev->enable_cnt)) {
				ql4_printk(KERN_INFO, ha, "scsi%ld: %s: Found PCI func in enabled state%x\n",
					   ha->host_no, __func__, fn);
				pci_dev_put(other_pdev);
				break;
			}
			pci_dev_put(other_pdev);
		}
	} else {
		/* this case is meant for ISP83xx/ISP84xx only */
		if (qla4_83xx_can_perform_reset(ha)) {
			/* reset fn as iSCSI is going to perform the reset */
			fn = 0;
		}
	}

	/* The first function on the card, the reset owner will
	 * start & initialize the firmware. The other functions
	 * on the card will reset the firmware context
	 */
	if (!fn) {
		ql4_printk(KERN_INFO, ha, "scsi%ld: %s: devfn being reset "
		    "0x%x is the owner\n", ha->host_no, __func__,
		    ha->pdev->devfn);

		ha->isp_ops->idc_lock(ha);
		qla4_8xxx_wr_direct(ha, QLA8XXX_CRB_DEV_STATE,
				    QLA8XXX_DEV_COLD);
		ha->isp_ops->idc_unlock(ha);

		rval = qla4_8xxx_update_idc_reg(ha);
		if (rval == QLA_ERROR) {
			ql4_printk(KERN_INFO, ha, "scsi%ld: %s: HW State: FAILED\n",
				   ha->host_no, __func__);
			ha->isp_ops->idc_lock(ha);
			qla4_8xxx_wr_direct(ha, QLA8XXX_CRB_DEV_STATE,
					    QLA8XXX_DEV_FAILED);
			ha->isp_ops->idc_unlock(ha);
			goto exit_error_recovery;
		}

		clear_bit(AF_FW_RECOVERY, &ha->flags);
		rval = qla4xxx_initialize_adapter(ha, RESET_ADAPTER);

		if (rval != QLA_SUCCESS) {
			ql4_printk(KERN_INFO, ha, "scsi%ld: %s: HW State: "
			    "FAILED\n", ha->host_no, __func__);
			qla4xxx_free_irqs(ha);
			ha->isp_ops->idc_lock(ha);
			qla4_8xxx_clear_drv_active(ha);
			qla4_8xxx_wr_direct(ha, QLA8XXX_CRB_DEV_STATE,
					    QLA8XXX_DEV_FAILED);
			ha->isp_ops->idc_unlock(ha);
		} else {
			ql4_printk(KERN_INFO, ha, "scsi%ld: %s: HW State: "
			    "READY\n", ha->host_no, __func__);
			ha->isp_ops->idc_lock(ha);
			qla4_8xxx_wr_direct(ha, QLA8XXX_CRB_DEV_STATE,
					    QLA8XXX_DEV_READY);
			/* Clear driver state register */
			qla4_8xxx_wr_direct(ha, QLA8XXX_CRB_DRV_STATE, 0);
			qla4_8xxx_set_drv_active(ha);
			ha->isp_ops->idc_unlock(ha);
			ha->isp_ops->enable_intrs(ha);
		}
	} else {
		ql4_printk(KERN_INFO, ha, "scsi%ld: %s: devfn 0x%x is not "
		    "the reset owner\n", ha->host_no, __func__,
		    ha->pdev->devfn);
		if ((qla4_8xxx_rd_direct(ha, QLA8XXX_CRB_DEV_STATE) ==
		     QLA8XXX_DEV_READY)) {
			clear_bit(AF_FW_RECOVERY, &ha->flags);
			rval = qla4xxx_initialize_adapter(ha, RESET_ADAPTER);
			if (rval == QLA_SUCCESS)
				ha->isp_ops->enable_intrs(ha);
			else
				qla4xxx_free_irqs(ha);

			ha->isp_ops->idc_lock(ha);
			qla4_8xxx_set_drv_active(ha);
			ha->isp_ops->idc_unlock(ha);
		}
	}
exit_error_recovery:
	clear_bit(DPC_RESET_ACTIVE, &ha->dpc_flags);
	return rval;
}

static pci_ers_result_t
qla4xxx_pci_slot_reset(struct pci_dev *pdev)
{
	pci_ers_result_t ret = PCI_ERS_RESULT_DISCONNECT;
	struct scsi_qla_host *ha = pci_get_drvdata(pdev);
	int rc;

	ql4_printk(KERN_WARNING, ha, "scsi%ld: %s: slot_reset\n",
	    ha->host_no, __func__);

	if (!is_aer_supported(ha))
		return PCI_ERS_RESULT_NONE;

	/* Restore the saved state of PCIe device -
	 * BAR registers, PCI Config space, PCIX, MSI,
	 * IOV states
	 */
	pci_restore_state(pdev);

	/* pci_restore_state() clears the saved_state flag of the device
	 * save restored state which resets saved_state flag
	 */
	pci_save_state(pdev);

	/* Initialize device or resume if in suspended state */
	rc = pci_enable_device(pdev);
	if (rc) {
		ql4_printk(KERN_WARNING, ha, "scsi%ld: %s: Can't re-enable "
		    "device after reset\n", ha->host_no, __func__);
		goto exit_slot_reset;
	}

	ha->isp_ops->disable_intrs(ha);

	if (is_qla80XX(ha)) {
		if (qla4_8xxx_error_recovery(ha) == QLA_SUCCESS) {
			ret = PCI_ERS_RESULT_RECOVERED;
			goto exit_slot_reset;
		} else
			goto exit_slot_reset;
	}

exit_slot_reset:
	ql4_printk(KERN_WARNING, ha, "scsi%ld: %s: Return=%x\n"
	    "device after reset\n", ha->host_no, __func__, ret);
	return ret;
}

static void
qla4xxx_pci_resume(struct pci_dev *pdev)
{
	struct scsi_qla_host *ha = pci_get_drvdata(pdev);
	int ret;

	ql4_printk(KERN_WARNING, ha, "scsi%ld: %s: pci_resume\n",
	    ha->host_no, __func__);

	ret = qla4xxx_wait_for_hba_online(ha);
	if (ret != QLA_SUCCESS) {
		ql4_printk(KERN_ERR, ha, "scsi%ld: %s: the device failed to "
		    "resume I/O from slot/link_reset\n", ha->host_no,
		     __func__);
	}

	clear_bit(AF_EEH_BUSY, &ha->flags);
}

static const struct pci_error_handlers qla4xxx_err_handler = {
	.error_detected = qla4xxx_pci_error_detected,
	.mmio_enabled = qla4xxx_pci_mmio_enabled,
	.slot_reset = qla4xxx_pci_slot_reset,
	.resume = qla4xxx_pci_resume,
};

static struct pci_device_id qla4xxx_pci_tbl[] = {
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP4010,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
	},
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP4022,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
	},
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP4032,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
	},
	{
		.vendor         = PCI_VENDOR_ID_QLOGIC,
		.device         = PCI_DEVICE_ID_QLOGIC_ISP8022,
		.subvendor      = PCI_ANY_ID,
		.subdevice      = PCI_ANY_ID,
	},
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP8324,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
	},
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP8042,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
	},
	{0, 0},
};
MODULE_DEVICE_TABLE(pci, qla4xxx_pci_tbl);

static struct pci_driver qla4xxx_pci_driver = {
	.name		= DRIVER_NAME,
	.id_table	= qla4xxx_pci_tbl,
	.probe		= qla4xxx_probe_adapter,
	.remove		= qla4xxx_remove_adapter,
	.err_handler = &qla4xxx_err_handler,
};

static int __init qla4xxx_module_init(void)
{
	int ret;

	if (ql4xqfulltracking)
		qla4xxx_driver_template.track_queue_depth = 1;

	/* Allocate cache for SRBs. */
	srb_cachep = kmem_cache_create("qla4xxx_srbs", sizeof(struct srb), 0,
				       SLAB_HWCACHE_ALIGN, NULL);
	if (srb_cachep == NULL) {
		printk(KERN_ERR
		       "%s: Unable to allocate SRB cache..."
		       "Failing load!\n", DRIVER_NAME);
		ret = -ENOMEM;
		goto no_srp_cache;
	}

	/* Derive version string. */
	strcpy(qla4xxx_version_str, QLA4XXX_DRIVER_VERSION);
	if (ql4xextended_error_logging)
		strcat(qla4xxx_version_str, "-debug");

	qla4xxx_scsi_transport =
		iscsi_register_transport(&qla4xxx_iscsi_transport);
	if (!qla4xxx_scsi_transport){
		ret = -ENODEV;
		goto release_srb_cache;
	}

	ret = pci_register_driver(&qla4xxx_pci_driver);
	if (ret)
		goto unregister_transport;

	printk(KERN_INFO "QLogic iSCSI HBA Driver\n");
	return 0;

unregister_transport:
	iscsi_unregister_transport(&qla4xxx_iscsi_transport);
release_srb_cache:
	kmem_cache_destroy(srb_cachep);
no_srp_cache:
	return ret;
}

static void __exit qla4xxx_module_exit(void)
{
	pci_unregister_driver(&qla4xxx_pci_driver);
	iscsi_unregister_transport(&qla4xxx_iscsi_transport);
	kmem_cache_destroy(srb_cachep);
}

module_init(qla4xxx_module_init);
module_exit(qla4xxx_module_exit);

MODULE_AUTHOR("QLogic Corporation");
MODULE_DESCRIPTION("QLogic iSCSI HBA Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(QLA4XXX_DRIVER_VERSION);
