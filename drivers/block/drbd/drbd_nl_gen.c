// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)

#include <net/netlink.h>
#include <net/genetlink.h>

#include <linux/kernel.h>
#include <linux/slab.h>
#include "drbd_nl_gen.h"

#include <uapi/linux/drbd_genl.h>
#include <linux/drbd.h>
#include <linux/drbd_limits.h>

/* Common nested types */
const struct nla_policy drbd_connection_info_nl_policy[DRBD_A_CONNECTION_INFO_CONN_ROLE + 1] = {
	[DRBD_A_CONNECTION_INFO_CONN_CONNECTION_STATE] = { .type = NLA_U32, },
	[DRBD_A_CONNECTION_INFO_CONN_ROLE] = { .type = NLA_U32, },
};

const struct nla_policy drbd_connection_statistics_nl_policy[DRBD_A_CONNECTION_STATISTICS_CONN_CONGESTED + 1] = {
	[DRBD_A_CONNECTION_STATISTICS_CONN_CONGESTED] = { .type = NLA_U8, },
};

const struct nla_policy drbd_detach_parms_nl_policy[DRBD_A_DETACH_PARMS_FORCE_DETACH + 1] = {
	[DRBD_A_DETACH_PARMS_FORCE_DETACH] = { .type = NLA_U8, },
};

const struct nla_policy drbd_device_info_nl_policy[DRBD_A_DEVICE_INFO_DEV_DISK_STATE + 1] = {
	[DRBD_A_DEVICE_INFO_DEV_DISK_STATE] = { .type = NLA_U32, },
};

const struct nla_policy drbd_device_statistics_nl_policy[DRBD_A_DEVICE_STATISTICS_HISTORY_UUIDS + 1] = {
	[DRBD_A_DEVICE_STATISTICS_DEV_SIZE] = { .type = NLA_U64, },
	[DRBD_A_DEVICE_STATISTICS_DEV_READ] = { .type = NLA_U64, },
	[DRBD_A_DEVICE_STATISTICS_DEV_WRITE] = { .type = NLA_U64, },
	[DRBD_A_DEVICE_STATISTICS_DEV_AL_WRITES] = { .type = NLA_U64, },
	[DRBD_A_DEVICE_STATISTICS_DEV_BM_WRITES] = { .type = NLA_U64, },
	[DRBD_A_DEVICE_STATISTICS_DEV_UPPER_PENDING] = { .type = NLA_U32, },
	[DRBD_A_DEVICE_STATISTICS_DEV_LOWER_PENDING] = { .type = NLA_U32, },
	[DRBD_A_DEVICE_STATISTICS_DEV_UPPER_BLOCKED] = { .type = NLA_U8, },
	[DRBD_A_DEVICE_STATISTICS_DEV_LOWER_BLOCKED] = { .type = NLA_U8, },
	[DRBD_A_DEVICE_STATISTICS_DEV_AL_SUSPENDED] = { .type = NLA_U8, },
	[DRBD_A_DEVICE_STATISTICS_DEV_EXPOSED_DATA_UUID] = { .type = NLA_U64, },
	[DRBD_A_DEVICE_STATISTICS_DEV_CURRENT_UUID] = { .type = NLA_U64, },
	[DRBD_A_DEVICE_STATISTICS_DEV_DISK_FLAGS] = { .type = NLA_U32, },
	[DRBD_A_DEVICE_STATISTICS_HISTORY_UUIDS] = NLA_POLICY_MAX_LEN(DRBD_NL_HISTORY_UUIDS_SIZE),
};

const struct nla_policy drbd_disconnect_parms_nl_policy[DRBD_A_DISCONNECT_PARMS_FORCE_DISCONNECT + 1] = {
	[DRBD_A_DISCONNECT_PARMS_FORCE_DISCONNECT] = { .type = NLA_U8, },
};

const struct nla_policy drbd_disk_conf_nl_policy[DRBD_A_DISK_CONF_DISABLE_WRITE_SAME + 1] = {
	[DRBD_A_DISK_CONF_BACKING_DEV] = { .type = NLA_NUL_STRING, .len = 128, },
	[DRBD_A_DISK_CONF_META_DEV] = { .type = NLA_NUL_STRING, .len = 128, },
	[DRBD_A_DISK_CONF_META_DEV_IDX] = { .type = NLA_U32, },
	[DRBD_A_DISK_CONF_DISK_SIZE] = { .type = NLA_U64, },
	[DRBD_A_DISK_CONF_MAX_BIO_BVECS] = { .type = NLA_U32, },
	[DRBD_A_DISK_CONF_ON_IO_ERROR] = { .type = NLA_U32, },
	[DRBD_A_DISK_CONF_FENCING] = { .type = NLA_U32, },
	[DRBD_A_DISK_CONF_RESYNC_RATE] = { .type = NLA_U32, },
	[DRBD_A_DISK_CONF_RESYNC_AFTER] = { .type = NLA_U32, },
	[DRBD_A_DISK_CONF_AL_EXTENTS] = { .type = NLA_U32, },
	[DRBD_A_DISK_CONF_C_PLAN_AHEAD] = { .type = NLA_U32, },
	[DRBD_A_DISK_CONF_C_DELAY_TARGET] = { .type = NLA_U32, },
	[DRBD_A_DISK_CONF_C_FILL_TARGET] = { .type = NLA_U32, },
	[DRBD_A_DISK_CONF_C_MAX_RATE] = { .type = NLA_U32, },
	[DRBD_A_DISK_CONF_C_MIN_RATE] = { .type = NLA_U32, },
	[DRBD_A_DISK_CONF_DISK_BARRIER] = { .type = NLA_U8, },
	[DRBD_A_DISK_CONF_DISK_FLUSHES] = { .type = NLA_U8, },
	[DRBD_A_DISK_CONF_DISK_DRAIN] = { .type = NLA_U8, },
	[DRBD_A_DISK_CONF_MD_FLUSHES] = { .type = NLA_U8, },
	[DRBD_A_DISK_CONF_DISK_TIMEOUT] = { .type = NLA_U32, },
	[DRBD_A_DISK_CONF_READ_BALANCING] = { .type = NLA_U32, },
	[DRBD_A_DISK_CONF_AL_UPDATES] = { .type = NLA_U8, },
	[DRBD_A_DISK_CONF_DISCARD_ZEROES_IF_ALIGNED] = { .type = NLA_U8, },
	[DRBD_A_DISK_CONF_RS_DISCARD_GRANULARITY] = { .type = NLA_U32, },
	[DRBD_A_DISK_CONF_DISABLE_WRITE_SAME] = { .type = NLA_U8, },
};

const struct nla_policy drbd_drbd_cfg_context_nl_policy[DRBD_A_DRBD_CFG_CONTEXT_CTX_PEER_ADDR + 1] = {
	[DRBD_A_DRBD_CFG_CONTEXT_CTX_VOLUME] = { .type = NLA_U32, },
	[DRBD_A_DRBD_CFG_CONTEXT_CTX_RESOURCE_NAME] = { .type = NLA_NUL_STRING, .len = 128, },
	[DRBD_A_DRBD_CFG_CONTEXT_CTX_MY_ADDR] = NLA_POLICY_MAX_LEN(128),
	[DRBD_A_DRBD_CFG_CONTEXT_CTX_PEER_ADDR] = NLA_POLICY_MAX_LEN(128),
};

const struct nla_policy drbd_net_conf_nl_policy[DRBD_A_NET_CONF_SOCK_CHECK_TIMEO + 1] = {
	[DRBD_A_NET_CONF_SHARED_SECRET] = { .type = NLA_NUL_STRING, .len = SHARED_SECRET_MAX, },
	[DRBD_A_NET_CONF_CRAM_HMAC_ALG] = { .type = NLA_NUL_STRING, .len = SHARED_SECRET_MAX, },
	[DRBD_A_NET_CONF_INTEGRITY_ALG] = { .type = NLA_NUL_STRING, .len = SHARED_SECRET_MAX, },
	[DRBD_A_NET_CONF_VERIFY_ALG] = { .type = NLA_NUL_STRING, .len = SHARED_SECRET_MAX, },
	[DRBD_A_NET_CONF_CSUMS_ALG] = { .type = NLA_NUL_STRING, .len = SHARED_SECRET_MAX, },
	[DRBD_A_NET_CONF_WIRE_PROTOCOL] = { .type = NLA_U32, },
	[DRBD_A_NET_CONF_CONNECT_INT] = { .type = NLA_U32, },
	[DRBD_A_NET_CONF_TIMEOUT] = { .type = NLA_U32, },
	[DRBD_A_NET_CONF_PING_INT] = { .type = NLA_U32, },
	[DRBD_A_NET_CONF_PING_TIMEO] = { .type = NLA_U32, },
	[DRBD_A_NET_CONF_SNDBUF_SIZE] = { .type = NLA_U32, },
	[DRBD_A_NET_CONF_RCVBUF_SIZE] = { .type = NLA_U32, },
	[DRBD_A_NET_CONF_KO_COUNT] = { .type = NLA_U32, },
	[DRBD_A_NET_CONF_MAX_BUFFERS] = { .type = NLA_U32, },
	[DRBD_A_NET_CONF_MAX_EPOCH_SIZE] = { .type = NLA_U32, },
	[DRBD_A_NET_CONF_UNPLUG_WATERMARK] = { .type = NLA_U32, },
	[DRBD_A_NET_CONF_AFTER_SB_0P] = { .type = NLA_U32, },
	[DRBD_A_NET_CONF_AFTER_SB_1P] = { .type = NLA_U32, },
	[DRBD_A_NET_CONF_AFTER_SB_2P] = { .type = NLA_U32, },
	[DRBD_A_NET_CONF_RR_CONFLICT] = { .type = NLA_U32, },
	[DRBD_A_NET_CONF_ON_CONGESTION] = { .type = NLA_U32, },
	[DRBD_A_NET_CONF_CONG_FILL] = { .type = NLA_U32, },
	[DRBD_A_NET_CONF_CONG_EXTENTS] = { .type = NLA_U32, },
	[DRBD_A_NET_CONF_TWO_PRIMARIES] = { .type = NLA_U8, },
	[DRBD_A_NET_CONF_DISCARD_MY_DATA] = { .type = NLA_U8, },
	[DRBD_A_NET_CONF_TCP_CORK] = { .type = NLA_U8, },
	[DRBD_A_NET_CONF_ALWAYS_ASBP] = { .type = NLA_U8, },
	[DRBD_A_NET_CONF_TENTATIVE] = { .type = NLA_U8, },
	[DRBD_A_NET_CONF_USE_RLE] = { .type = NLA_U8, },
	[DRBD_A_NET_CONF_CSUMS_AFTER_CRASH_ONLY] = { .type = NLA_U8, },
	[DRBD_A_NET_CONF_SOCK_CHECK_TIMEO] = { .type = NLA_U32, },
};

const struct nla_policy drbd_new_c_uuid_parms_nl_policy[DRBD_A_NEW_C_UUID_PARMS_CLEAR_BM + 1] = {
	[DRBD_A_NEW_C_UUID_PARMS_CLEAR_BM] = { .type = NLA_U8, },
};

const struct nla_policy drbd_peer_device_info_nl_policy[DRBD_A_PEER_DEVICE_INFO_PEER_RESYNC_SUSP_DEPENDENCY + 1] = {
	[DRBD_A_PEER_DEVICE_INFO_PEER_REPL_STATE] = { .type = NLA_U32, },
	[DRBD_A_PEER_DEVICE_INFO_PEER_DISK_STATE] = { .type = NLA_U32, },
	[DRBD_A_PEER_DEVICE_INFO_PEER_RESYNC_SUSP_USER] = { .type = NLA_U32, },
	[DRBD_A_PEER_DEVICE_INFO_PEER_RESYNC_SUSP_PEER] = { .type = NLA_U32, },
	[DRBD_A_PEER_DEVICE_INFO_PEER_RESYNC_SUSP_DEPENDENCY] = { .type = NLA_U32, },
};

const struct nla_policy drbd_peer_device_statistics_nl_policy[DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_FLAGS + 1] = {
	[DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_RECEIVED] = { .type = NLA_U64, },
	[DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_SENT] = { .type = NLA_U64, },
	[DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_PENDING] = { .type = NLA_U32, },
	[DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_UNACKED] = { .type = NLA_U32, },
	[DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_OUT_OF_SYNC] = { .type = NLA_U64, },
	[DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_RESYNC_FAILED] = { .type = NLA_U64, },
	[DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_BITMAP_UUID] = { .type = NLA_U64, },
	[DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_FLAGS] = { .type = NLA_U32, },
};

const struct nla_policy drbd_res_opts_nl_policy[DRBD_A_RES_OPTS_ON_NO_DATA + 1] = {
	[DRBD_A_RES_OPTS_CPU_MASK] = { .type = NLA_NUL_STRING, .len = DRBD_CPU_MASK_SIZE, },
	[DRBD_A_RES_OPTS_ON_NO_DATA] = { .type = NLA_U32, },
};

const struct nla_policy drbd_resize_parms_nl_policy[DRBD_A_RESIZE_PARMS_AL_STRIPE_SIZE + 1] = {
	[DRBD_A_RESIZE_PARMS_RESIZE_SIZE] = { .type = NLA_U64, },
	[DRBD_A_RESIZE_PARMS_RESIZE_FORCE] = { .type = NLA_U8, },
	[DRBD_A_RESIZE_PARMS_NO_RESYNC] = { .type = NLA_U8, },
	[DRBD_A_RESIZE_PARMS_AL_STRIPES] = { .type = NLA_U32, },
	[DRBD_A_RESIZE_PARMS_AL_STRIPE_SIZE] = { .type = NLA_U32, },
};

const struct nla_policy drbd_resource_info_nl_policy[DRBD_A_RESOURCE_INFO_RES_SUSP_FEN + 1] = {
	[DRBD_A_RESOURCE_INFO_RES_ROLE] = { .type = NLA_U32, },
	[DRBD_A_RESOURCE_INFO_RES_SUSP] = { .type = NLA_U8, },
	[DRBD_A_RESOURCE_INFO_RES_SUSP_NOD] = { .type = NLA_U8, },
	[DRBD_A_RESOURCE_INFO_RES_SUSP_FEN] = { .type = NLA_U8, },
};

const struct nla_policy drbd_resource_statistics_nl_policy[DRBD_A_RESOURCE_STATISTICS_RES_STAT_WRITE_ORDERING + 1] = {
	[DRBD_A_RESOURCE_STATISTICS_RES_STAT_WRITE_ORDERING] = { .type = NLA_U32, },
};

const struct nla_policy drbd_set_role_parms_nl_policy[DRBD_A_SET_ROLE_PARMS_ASSUME_UPTODATE + 1] = {
	[DRBD_A_SET_ROLE_PARMS_ASSUME_UPTODATE] = { .type = NLA_U8, },
};

const struct nla_policy drbd_start_ov_parms_nl_policy[DRBD_A_START_OV_PARMS_OV_STOP_SECTOR + 1] = {
	[DRBD_A_START_OV_PARMS_OV_START_SECTOR] = { .type = NLA_U64, },
	[DRBD_A_START_OV_PARMS_OV_STOP_SECTOR] = { .type = NLA_U64, },
};

/* DRBD_ADM_GET_STATUS - do */
static const struct nla_policy drbd_get_status_do_nl_policy[DRBD_NLA_CFG_CONTEXT + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
};

/* DRBD_ADM_GET_STATUS - dump */
static const struct nla_policy drbd_get_status_dump_nl_policy[DRBD_NLA_CFG_CONTEXT + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
};

/* DRBD_ADM_NEW_MINOR - do */
static const struct nla_policy drbd_new_minor_nl_policy[DRBD_NLA_CFG_CONTEXT + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
};

/* DRBD_ADM_DEL_MINOR - do */
static const struct nla_policy drbd_del_minor_nl_policy[DRBD_NLA_CFG_CONTEXT + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
};

/* DRBD_ADM_NEW_RESOURCE - do */
static const struct nla_policy drbd_new_resource_nl_policy[DRBD_NLA_RESOURCE_OPTS + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
	[DRBD_NLA_RESOURCE_OPTS] = NLA_POLICY_NESTED(drbd_res_opts_nl_policy),
};

/* DRBD_ADM_DEL_RESOURCE - do */
static const struct nla_policy drbd_del_resource_nl_policy[DRBD_NLA_CFG_CONTEXT + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
};

/* DRBD_ADM_RESOURCE_OPTS - do */
static const struct nla_policy drbd_resource_opts_nl_policy[DRBD_NLA_RESOURCE_OPTS + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
	[DRBD_NLA_RESOURCE_OPTS] = NLA_POLICY_NESTED(drbd_res_opts_nl_policy),
};

/* DRBD_ADM_CONNECT - do */
static const struct nla_policy drbd_connect_nl_policy[DRBD_NLA_NET_CONF + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
	[DRBD_NLA_NET_CONF] = NLA_POLICY_NESTED(drbd_net_conf_nl_policy),
};

/* DRBD_ADM_DISCONNECT - do */
static const struct nla_policy drbd_disconnect_nl_policy[DRBD_NLA_DISCONNECT_PARMS + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
	[DRBD_NLA_DISCONNECT_PARMS] = NLA_POLICY_NESTED(drbd_disconnect_parms_nl_policy),
};

/* DRBD_ADM_ATTACH - do */
static const struct nla_policy drbd_attach_nl_policy[DRBD_NLA_DISK_CONF + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
	[DRBD_NLA_DISK_CONF] = NLA_POLICY_NESTED(drbd_disk_conf_nl_policy),
};

/* DRBD_ADM_RESIZE - do */
static const struct nla_policy drbd_resize_nl_policy[DRBD_NLA_RESIZE_PARMS + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
	[DRBD_NLA_RESIZE_PARMS] = NLA_POLICY_NESTED(drbd_resize_parms_nl_policy),
};

/* DRBD_ADM_PRIMARY - do */
static const struct nla_policy drbd_primary_nl_policy[DRBD_NLA_SET_ROLE_PARMS + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
	[DRBD_NLA_SET_ROLE_PARMS] = NLA_POLICY_NESTED(drbd_set_role_parms_nl_policy),
};

/* DRBD_ADM_SECONDARY - do */
static const struct nla_policy drbd_secondary_nl_policy[DRBD_NLA_SET_ROLE_PARMS + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
	[DRBD_NLA_SET_ROLE_PARMS] = NLA_POLICY_NESTED(drbd_set_role_parms_nl_policy),
};

/* DRBD_ADM_NEW_C_UUID - do */
static const struct nla_policy drbd_new_c_uuid_nl_policy[DRBD_NLA_NEW_C_UUID_PARMS + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
	[DRBD_NLA_NEW_C_UUID_PARMS] = NLA_POLICY_NESTED(drbd_new_c_uuid_parms_nl_policy),
};

/* DRBD_ADM_START_OV - do */
static const struct nla_policy drbd_start_ov_nl_policy[DRBD_NLA_START_OV_PARMS + 1] = {
	[DRBD_NLA_START_OV_PARMS] = NLA_POLICY_NESTED(drbd_start_ov_parms_nl_policy),
};

/* DRBD_ADM_DETACH - do */
static const struct nla_policy drbd_detach_nl_policy[DRBD_NLA_DETACH_PARMS + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
	[DRBD_NLA_DETACH_PARMS] = NLA_POLICY_NESTED(drbd_detach_parms_nl_policy),
};

/* DRBD_ADM_INVALIDATE - do */
static const struct nla_policy drbd_invalidate_nl_policy[DRBD_NLA_CFG_CONTEXT + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
};

/* DRBD_ADM_INVAL_PEER - do */
static const struct nla_policy drbd_inval_peer_nl_policy[DRBD_NLA_CFG_CONTEXT + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
};

/* DRBD_ADM_PAUSE_SYNC - do */
static const struct nla_policy drbd_pause_sync_nl_policy[DRBD_NLA_CFG_CONTEXT + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
};

/* DRBD_ADM_RESUME_SYNC - do */
static const struct nla_policy drbd_resume_sync_nl_policy[DRBD_NLA_CFG_CONTEXT + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
};

/* DRBD_ADM_SUSPEND_IO - do */
static const struct nla_policy drbd_suspend_io_nl_policy[DRBD_NLA_CFG_CONTEXT + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
};

/* DRBD_ADM_RESUME_IO - do */
static const struct nla_policy drbd_resume_io_nl_policy[DRBD_NLA_CFG_CONTEXT + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
};

/* DRBD_ADM_OUTDATE - do */
static const struct nla_policy drbd_outdate_nl_policy[DRBD_NLA_CFG_CONTEXT + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
};

/* DRBD_ADM_GET_TIMEOUT_TYPE - do */
static const struct nla_policy drbd_get_timeout_type_nl_policy[DRBD_NLA_CFG_CONTEXT + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
};

/* DRBD_ADM_DOWN - do */
static const struct nla_policy drbd_down_nl_policy[DRBD_NLA_CFG_CONTEXT + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
};

/* DRBD_ADM_CHG_DISK_OPTS - do */
static const struct nla_policy drbd_chg_disk_opts_nl_policy[DRBD_NLA_DISK_CONF + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
	[DRBD_NLA_DISK_CONF] = NLA_POLICY_NESTED(drbd_disk_conf_nl_policy),
};

/* DRBD_ADM_CHG_NET_OPTS - do */
static const struct nla_policy drbd_chg_net_opts_nl_policy[DRBD_NLA_NET_CONF + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
	[DRBD_NLA_NET_CONF] = NLA_POLICY_NESTED(drbd_net_conf_nl_policy),
};

/* DRBD_ADM_GET_RESOURCES - dump */
static const struct nla_policy drbd_get_resources_nl_policy[DRBD_NLA_RESOURCE_STATISTICS + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
	[DRBD_NLA_RESOURCE_INFO] = NLA_POLICY_NESTED(drbd_resource_info_nl_policy),
	[DRBD_NLA_RESOURCE_STATISTICS] = NLA_POLICY_NESTED(drbd_resource_statistics_nl_policy),
};

/* DRBD_ADM_GET_DEVICES - dump */
static const struct nla_policy drbd_get_devices_nl_policy[DRBD_NLA_DEVICE_STATISTICS + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
	[DRBD_NLA_DEVICE_INFO] = NLA_POLICY_NESTED(drbd_device_info_nl_policy),
	[DRBD_NLA_DEVICE_STATISTICS] = NLA_POLICY_NESTED(drbd_device_statistics_nl_policy),
};

/* DRBD_ADM_GET_CONNECTIONS - dump */
static const struct nla_policy drbd_get_connections_nl_policy[DRBD_NLA_CONNECTION_STATISTICS + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
	[DRBD_NLA_CONNECTION_INFO] = NLA_POLICY_NESTED(drbd_connection_info_nl_policy),
	[DRBD_NLA_CONNECTION_STATISTICS] = NLA_POLICY_NESTED(drbd_connection_statistics_nl_policy),
};

/* DRBD_ADM_GET_PEER_DEVICES - dump */
static const struct nla_policy drbd_get_peer_devices_nl_policy[DRBD_NLA_PEER_DEVICE_STATISTICS + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
	[DRBD_NLA_PEER_DEVICE_INFO] = NLA_POLICY_NESTED(drbd_peer_device_info_nl_policy),
	[DRBD_NLA_PEER_DEVICE_STATISTICS] = NLA_POLICY_NESTED(drbd_peer_device_statistics_nl_policy),
};

/* DRBD_ADM_GET_INITIAL_STATE - dump */
static const struct nla_policy drbd_get_initial_state_nl_policy[DRBD_NLA_CFG_CONTEXT + 1] = {
	[DRBD_NLA_CFG_CONTEXT] = NLA_POLICY_NESTED(drbd_drbd_cfg_context_nl_policy),
};

/* Ops table for drbd */
const struct genl_split_ops drbd_nl_ops[32] = {
	{
		.cmd		= DRBD_ADM_GET_STATUS,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_get_status_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_get_status_do_nl_policy,
		.maxattr	= DRBD_NLA_CFG_CONTEXT,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_GET_STATUS,
		.dumpit		= drbd_nl_get_status_dumpit,
		.policy		= drbd_get_status_dump_nl_policy,
		.maxattr	= DRBD_NLA_CFG_CONTEXT,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DRBD_ADM_NEW_MINOR,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_new_minor_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_new_minor_nl_policy,
		.maxattr	= DRBD_NLA_CFG_CONTEXT,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_DEL_MINOR,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_del_minor_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_del_minor_nl_policy,
		.maxattr	= DRBD_NLA_CFG_CONTEXT,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_NEW_RESOURCE,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_new_resource_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_new_resource_nl_policy,
		.maxattr	= DRBD_NLA_RESOURCE_OPTS,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_DEL_RESOURCE,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_del_resource_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_del_resource_nl_policy,
		.maxattr	= DRBD_NLA_CFG_CONTEXT,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_RESOURCE_OPTS,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_resource_opts_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_resource_opts_nl_policy,
		.maxattr	= DRBD_NLA_RESOURCE_OPTS,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_CONNECT,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_connect_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_connect_nl_policy,
		.maxattr	= DRBD_NLA_NET_CONF,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_DISCONNECT,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_disconnect_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_disconnect_nl_policy,
		.maxattr	= DRBD_NLA_DISCONNECT_PARMS,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_ATTACH,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_attach_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_attach_nl_policy,
		.maxattr	= DRBD_NLA_DISK_CONF,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_RESIZE,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_resize_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_resize_nl_policy,
		.maxattr	= DRBD_NLA_RESIZE_PARMS,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_PRIMARY,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_primary_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_primary_nl_policy,
		.maxattr	= DRBD_NLA_SET_ROLE_PARMS,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_SECONDARY,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_secondary_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_secondary_nl_policy,
		.maxattr	= DRBD_NLA_SET_ROLE_PARMS,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_NEW_C_UUID,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_new_c_uuid_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_new_c_uuid_nl_policy,
		.maxattr	= DRBD_NLA_NEW_C_UUID_PARMS,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_START_OV,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_start_ov_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_start_ov_nl_policy,
		.maxattr	= DRBD_NLA_START_OV_PARMS,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_DETACH,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_detach_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_detach_nl_policy,
		.maxattr	= DRBD_NLA_DETACH_PARMS,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_INVALIDATE,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_invalidate_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_invalidate_nl_policy,
		.maxattr	= DRBD_NLA_CFG_CONTEXT,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_INVAL_PEER,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_inval_peer_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_inval_peer_nl_policy,
		.maxattr	= DRBD_NLA_CFG_CONTEXT,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_PAUSE_SYNC,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_pause_sync_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_pause_sync_nl_policy,
		.maxattr	= DRBD_NLA_CFG_CONTEXT,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_RESUME_SYNC,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_resume_sync_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_resume_sync_nl_policy,
		.maxattr	= DRBD_NLA_CFG_CONTEXT,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_SUSPEND_IO,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_suspend_io_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_suspend_io_nl_policy,
		.maxattr	= DRBD_NLA_CFG_CONTEXT,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_RESUME_IO,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_resume_io_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_resume_io_nl_policy,
		.maxattr	= DRBD_NLA_CFG_CONTEXT,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_OUTDATE,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_outdate_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_outdate_nl_policy,
		.maxattr	= DRBD_NLA_CFG_CONTEXT,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_GET_TIMEOUT_TYPE,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_get_timeout_type_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_get_timeout_type_nl_policy,
		.maxattr	= DRBD_NLA_CFG_CONTEXT,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_DOWN,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_down_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_down_nl_policy,
		.maxattr	= DRBD_NLA_CFG_CONTEXT,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_CHG_DISK_OPTS,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_chg_disk_opts_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_chg_disk_opts_nl_policy,
		.maxattr	= DRBD_NLA_DISK_CONF,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_CHG_NET_OPTS,
		.pre_doit	= drbd_pre_doit,
		.doit		= drbd_nl_chg_net_opts_doit,
		.post_doit	= drbd_post_doit,
		.policy		= drbd_chg_net_opts_nl_policy,
		.maxattr	= DRBD_NLA_NET_CONF,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRBD_ADM_GET_RESOURCES,
		.dumpit		= drbd_nl_get_resources_dumpit,
		.policy		= drbd_get_resources_nl_policy,
		.maxattr	= DRBD_NLA_RESOURCE_STATISTICS,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DRBD_ADM_GET_DEVICES,
		.dumpit		= drbd_nl_get_devices_dumpit,
		.done		= drbd_adm_dump_devices_done,
		.policy		= drbd_get_devices_nl_policy,
		.maxattr	= DRBD_NLA_DEVICE_STATISTICS,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DRBD_ADM_GET_CONNECTIONS,
		.dumpit		= drbd_nl_get_connections_dumpit,
		.done		= drbd_adm_dump_connections_done,
		.policy		= drbd_get_connections_nl_policy,
		.maxattr	= DRBD_NLA_CONNECTION_STATISTICS,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DRBD_ADM_GET_PEER_DEVICES,
		.dumpit		= drbd_nl_get_peer_devices_dumpit,
		.done		= drbd_adm_dump_peer_devices_done,
		.policy		= drbd_get_peer_devices_nl_policy,
		.maxattr	= DRBD_NLA_PEER_DEVICE_STATISTICS,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DRBD_ADM_GET_INITIAL_STATE,
		.dumpit		= drbd_nl_get_initial_state_dumpit,
		.policy		= drbd_get_initial_state_nl_policy,
		.maxattr	= DRBD_NLA_CFG_CONTEXT,
		.flags		= GENL_CMD_CAP_DUMP,
	},
};

static const struct genl_multicast_group drbd_nl_mcgrps[] = {
	[DRBD_NLGRP_EVENTS] = { "events", },
};

static int __drbd_cfg_context_from_attrs(struct drbd_cfg_context *s,
		struct nlattr ***ret_nested_attribute_table,
		struct genl_info *info)
{
	const int maxtype = DRBD_A_DRBD_CFG_CONTEXT_CTX_PEER_ADDR;
	struct nlattr *tla = info->attrs[DRBD_NLA_CFG_CONTEXT];
	struct nlattr **ntb;
	struct nlattr *nla;
	int err = 0;

	if (ret_nested_attribute_table)
		*ret_nested_attribute_table = NULL;
	if (!tla)
		return -ENOMSG;
	ntb = kcalloc(DRBD_A_DRBD_CFG_CONTEXT_CTX_PEER_ADDR + 1, sizeof(*ntb), GFP_KERNEL);
	if (!ntb)
		return -ENOMEM;
	err = nla_parse_nested_deprecated(ntb, maxtype, tla, drbd_drbd_cfg_context_nl_policy, NULL);
	if (err)
		goto out;

	nla = ntb[DRBD_A_DRBD_CFG_CONTEXT_CTX_VOLUME];
	if (nla && s)
		s->ctx_volume = nla_get_u32(nla);

	nla = ntb[DRBD_A_DRBD_CFG_CONTEXT_CTX_RESOURCE_NAME];
	if (nla && s)
		s->ctx_resource_name_len = nla_strscpy(s->ctx_resource_name, nla, 128);

	nla = ntb[DRBD_A_DRBD_CFG_CONTEXT_CTX_MY_ADDR];
	if (nla && s)
		s->ctx_my_addr_len = nla_memcpy(s->ctx_my_addr, nla, 128);

	nla = ntb[DRBD_A_DRBD_CFG_CONTEXT_CTX_PEER_ADDR];
	if (nla && s)
		s->ctx_peer_addr_len = nla_memcpy(s->ctx_peer_addr, nla, 128);

out:
	if (ret_nested_attribute_table && (!err || err == -ENOMSG))
		*ret_nested_attribute_table = ntb;
	else
		kfree(ntb);
	return err;
}

int drbd_cfg_context_from_attrs(struct drbd_cfg_context *s,
				struct genl_info *info)
{
	return __drbd_cfg_context_from_attrs(s, NULL, info);
}

int drbd_cfg_context_ntb_from_attrs(
			struct nlattr ***ret_nested_attribute_table,
			struct genl_info *info)
{
	return __drbd_cfg_context_from_attrs(NULL, ret_nested_attribute_table, info);
}

static int __disk_conf_from_attrs(struct disk_conf *s,
		struct nlattr ***ret_nested_attribute_table,
		struct genl_info *info)
{
	const int maxtype = DRBD_A_DISK_CONF_DISABLE_WRITE_SAME;
	struct nlattr *tla = info->attrs[DRBD_NLA_DISK_CONF];
	struct nlattr **ntb;
	struct nlattr *nla;
	int err = 0;

	if (ret_nested_attribute_table)
		*ret_nested_attribute_table = NULL;
	if (!tla)
		return -ENOMSG;
	ntb = kcalloc(DRBD_A_DISK_CONF_DISABLE_WRITE_SAME + 1, sizeof(*ntb), GFP_KERNEL);
	if (!ntb)
		return -ENOMEM;
	err = nla_parse_nested_deprecated(ntb, maxtype, tla, drbd_disk_conf_nl_policy, NULL);
	if (err)
		goto out;

	nla = ntb[DRBD_A_DISK_CONF_BACKING_DEV];
	if (nla) {
		if (s)
			s->backing_dev_len = nla_strscpy(s->backing_dev, nla, 128);
	} else {
		pr_info("<< missing required attr: backing_dev\n");
		err = -ENOMSG;
	}

	nla = ntb[DRBD_A_DISK_CONF_META_DEV];
	if (nla) {
		if (s)
			s->meta_dev_len = nla_strscpy(s->meta_dev, nla, 128);
	} else {
		pr_info("<< missing required attr: meta_dev\n");
		err = -ENOMSG;
	}

	nla = ntb[DRBD_A_DISK_CONF_META_DEV_IDX];
	if (nla) {
		if (s)
			s->meta_dev_idx = nla_get_s32(nla);
	} else {
		pr_info("<< missing required attr: meta_dev_idx\n");
		err = -ENOMSG;
	}

	nla = ntb[DRBD_A_DISK_CONF_DISK_SIZE];
	if (nla && s)
		s->disk_size = nla_get_u64(nla);

	nla = ntb[DRBD_A_DISK_CONF_MAX_BIO_BVECS];
	if (nla && s)
		s->max_bio_bvecs = nla_get_u32(nla);

	nla = ntb[DRBD_A_DISK_CONF_ON_IO_ERROR];
	if (nla && s)
		s->on_io_error = nla_get_u32(nla);

	nla = ntb[DRBD_A_DISK_CONF_FENCING];
	if (nla && s)
		s->fencing = nla_get_u32(nla);

	nla = ntb[DRBD_A_DISK_CONF_RESYNC_RATE];
	if (nla && s)
		s->resync_rate = nla_get_u32(nla);

	nla = ntb[DRBD_A_DISK_CONF_RESYNC_AFTER];
	if (nla && s)
		s->resync_after = nla_get_s32(nla);

	nla = ntb[DRBD_A_DISK_CONF_AL_EXTENTS];
	if (nla && s)
		s->al_extents = nla_get_u32(nla);

	nla = ntb[DRBD_A_DISK_CONF_C_PLAN_AHEAD];
	if (nla && s)
		s->c_plan_ahead = nla_get_u32(nla);

	nla = ntb[DRBD_A_DISK_CONF_C_DELAY_TARGET];
	if (nla && s)
		s->c_delay_target = nla_get_u32(nla);

	nla = ntb[DRBD_A_DISK_CONF_C_FILL_TARGET];
	if (nla && s)
		s->c_fill_target = nla_get_u32(nla);

	nla = ntb[DRBD_A_DISK_CONF_C_MAX_RATE];
	if (nla && s)
		s->c_max_rate = nla_get_u32(nla);

	nla = ntb[DRBD_A_DISK_CONF_C_MIN_RATE];
	if (nla && s)
		s->c_min_rate = nla_get_u32(nla);

	nla = ntb[DRBD_A_DISK_CONF_DISK_BARRIER];
	if (nla && s)
		s->disk_barrier = nla_get_u8(nla);

	nla = ntb[DRBD_A_DISK_CONF_DISK_FLUSHES];
	if (nla && s)
		s->disk_flushes = nla_get_u8(nla);

	nla = ntb[DRBD_A_DISK_CONF_DISK_DRAIN];
	if (nla && s)
		s->disk_drain = nla_get_u8(nla);

	nla = ntb[DRBD_A_DISK_CONF_MD_FLUSHES];
	if (nla && s)
		s->md_flushes = nla_get_u8(nla);

	nla = ntb[DRBD_A_DISK_CONF_DISK_TIMEOUT];
	if (nla && s)
		s->disk_timeout = nla_get_u32(nla);

	nla = ntb[DRBD_A_DISK_CONF_READ_BALANCING];
	if (nla && s)
		s->read_balancing = nla_get_u32(nla);

	nla = ntb[DRBD_A_DISK_CONF_AL_UPDATES];
	if (nla && s)
		s->al_updates = nla_get_u8(nla);

	nla = ntb[DRBD_A_DISK_CONF_DISCARD_ZEROES_IF_ALIGNED];
	if (nla && s)
		s->discard_zeroes_if_aligned = nla_get_u8(nla);

	nla = ntb[DRBD_A_DISK_CONF_RS_DISCARD_GRANULARITY];
	if (nla && s)
		s->rs_discard_granularity = nla_get_u32(nla);

	nla = ntb[DRBD_A_DISK_CONF_DISABLE_WRITE_SAME];
	if (nla && s)
		s->disable_write_same = nla_get_u8(nla);

out:
	if (ret_nested_attribute_table && (!err || err == -ENOMSG))
		*ret_nested_attribute_table = ntb;
	else
		kfree(ntb);
	return err;
}

int disk_conf_from_attrs(struct disk_conf *s,
				struct genl_info *info)
{
	return __disk_conf_from_attrs(s, NULL, info);
}

int disk_conf_ntb_from_attrs(
			struct nlattr ***ret_nested_attribute_table,
			struct genl_info *info)
{
	return __disk_conf_from_attrs(NULL, ret_nested_attribute_table, info);
}

static int __res_opts_from_attrs(struct res_opts *s,
		struct nlattr ***ret_nested_attribute_table,
		struct genl_info *info)
{
	const int maxtype = DRBD_A_RES_OPTS_ON_NO_DATA;
	struct nlattr *tla = info->attrs[DRBD_NLA_RESOURCE_OPTS];
	struct nlattr **ntb;
	struct nlattr *nla;
	int err = 0;

	if (ret_nested_attribute_table)
		*ret_nested_attribute_table = NULL;
	if (!tla)
		return -ENOMSG;
	ntb = kcalloc(DRBD_A_RES_OPTS_ON_NO_DATA + 1, sizeof(*ntb), GFP_KERNEL);
	if (!ntb)
		return -ENOMEM;
	err = nla_parse_nested_deprecated(ntb, maxtype, tla, drbd_res_opts_nl_policy, NULL);
	if (err)
		goto out;

	nla = ntb[DRBD_A_RES_OPTS_CPU_MASK];
	if (nla && s)
		s->cpu_mask_len = nla_strscpy(s->cpu_mask, nla, DRBD_CPU_MASK_SIZE);

	nla = ntb[DRBD_A_RES_OPTS_ON_NO_DATA];
	if (nla && s)
		s->on_no_data = nla_get_u32(nla);

out:
	if (ret_nested_attribute_table && (!err || err == -ENOMSG))
		*ret_nested_attribute_table = ntb;
	else
		kfree(ntb);
	return err;
}

int res_opts_from_attrs(struct res_opts *s,
				struct genl_info *info)
{
	return __res_opts_from_attrs(s, NULL, info);
}

int res_opts_ntb_from_attrs(
			struct nlattr ***ret_nested_attribute_table,
			struct genl_info *info)
{
	return __res_opts_from_attrs(NULL, ret_nested_attribute_table, info);
}

static int __net_conf_from_attrs(struct net_conf *s,
		struct nlattr ***ret_nested_attribute_table,
		struct genl_info *info)
{
	const int maxtype = DRBD_A_NET_CONF_SOCK_CHECK_TIMEO;
	struct nlattr *tla = info->attrs[DRBD_NLA_NET_CONF];
	struct nlattr **ntb;
	struct nlattr *nla;
	int err = 0;

	if (ret_nested_attribute_table)
		*ret_nested_attribute_table = NULL;
	if (!tla)
		return -ENOMSG;
	ntb = kcalloc(DRBD_A_NET_CONF_SOCK_CHECK_TIMEO + 1, sizeof(*ntb), GFP_KERNEL);
	if (!ntb)
		return -ENOMEM;
	err = nla_parse_nested_deprecated(ntb, maxtype, tla, drbd_net_conf_nl_policy, NULL);
	if (err)
		goto out;

	nla = ntb[DRBD_A_NET_CONF_SHARED_SECRET];
	if (nla && s)
		s->shared_secret_len = nla_strscpy(s->shared_secret, nla, SHARED_SECRET_MAX);

	nla = ntb[DRBD_A_NET_CONF_CRAM_HMAC_ALG];
	if (nla && s)
		s->cram_hmac_alg_len = nla_strscpy(s->cram_hmac_alg, nla, SHARED_SECRET_MAX);

	nla = ntb[DRBD_A_NET_CONF_INTEGRITY_ALG];
	if (nla && s)
		s->integrity_alg_len = nla_strscpy(s->integrity_alg, nla, SHARED_SECRET_MAX);

	nla = ntb[DRBD_A_NET_CONF_VERIFY_ALG];
	if (nla && s)
		s->verify_alg_len = nla_strscpy(s->verify_alg, nla, SHARED_SECRET_MAX);

	nla = ntb[DRBD_A_NET_CONF_CSUMS_ALG];
	if (nla && s)
		s->csums_alg_len = nla_strscpy(s->csums_alg, nla, SHARED_SECRET_MAX);

	nla = ntb[DRBD_A_NET_CONF_WIRE_PROTOCOL];
	if (nla && s)
		s->wire_protocol = nla_get_u32(nla);

	nla = ntb[DRBD_A_NET_CONF_CONNECT_INT];
	if (nla && s)
		s->connect_int = nla_get_u32(nla);

	nla = ntb[DRBD_A_NET_CONF_TIMEOUT];
	if (nla && s)
		s->timeout = nla_get_u32(nla);

	nla = ntb[DRBD_A_NET_CONF_PING_INT];
	if (nla && s)
		s->ping_int = nla_get_u32(nla);

	nla = ntb[DRBD_A_NET_CONF_PING_TIMEO];
	if (nla && s)
		s->ping_timeo = nla_get_u32(nla);

	nla = ntb[DRBD_A_NET_CONF_SNDBUF_SIZE];
	if (nla && s)
		s->sndbuf_size = nla_get_u32(nla);

	nla = ntb[DRBD_A_NET_CONF_RCVBUF_SIZE];
	if (nla && s)
		s->rcvbuf_size = nla_get_u32(nla);

	nla = ntb[DRBD_A_NET_CONF_KO_COUNT];
	if (nla && s)
		s->ko_count = nla_get_u32(nla);

	nla = ntb[DRBD_A_NET_CONF_MAX_BUFFERS];
	if (nla && s)
		s->max_buffers = nla_get_u32(nla);

	nla = ntb[DRBD_A_NET_CONF_MAX_EPOCH_SIZE];
	if (nla && s)
		s->max_epoch_size = nla_get_u32(nla);

	nla = ntb[DRBD_A_NET_CONF_UNPLUG_WATERMARK];
	if (nla && s)
		s->unplug_watermark = nla_get_u32(nla);

	nla = ntb[DRBD_A_NET_CONF_AFTER_SB_0P];
	if (nla && s)
		s->after_sb_0p = nla_get_u32(nla);

	nla = ntb[DRBD_A_NET_CONF_AFTER_SB_1P];
	if (nla && s)
		s->after_sb_1p = nla_get_u32(nla);

	nla = ntb[DRBD_A_NET_CONF_AFTER_SB_2P];
	if (nla && s)
		s->after_sb_2p = nla_get_u32(nla);

	nla = ntb[DRBD_A_NET_CONF_RR_CONFLICT];
	if (nla && s)
		s->rr_conflict = nla_get_u32(nla);

	nla = ntb[DRBD_A_NET_CONF_ON_CONGESTION];
	if (nla && s)
		s->on_congestion = nla_get_u32(nla);

	nla = ntb[DRBD_A_NET_CONF_CONG_FILL];
	if (nla && s)
		s->cong_fill = nla_get_u32(nla);

	nla = ntb[DRBD_A_NET_CONF_CONG_EXTENTS];
	if (nla && s)
		s->cong_extents = nla_get_u32(nla);

	nla = ntb[DRBD_A_NET_CONF_TWO_PRIMARIES];
	if (nla && s)
		s->two_primaries = nla_get_u8(nla);

	nla = ntb[DRBD_A_NET_CONF_DISCARD_MY_DATA];
	if (nla && s)
		s->discard_my_data = nla_get_u8(nla);

	nla = ntb[DRBD_A_NET_CONF_TCP_CORK];
	if (nla && s)
		s->tcp_cork = nla_get_u8(nla);

	nla = ntb[DRBD_A_NET_CONF_ALWAYS_ASBP];
	if (nla && s)
		s->always_asbp = nla_get_u8(nla);

	nla = ntb[DRBD_A_NET_CONF_TENTATIVE];
	if (nla && s)
		s->tentative = nla_get_u8(nla);

	nla = ntb[DRBD_A_NET_CONF_USE_RLE];
	if (nla && s)
		s->use_rle = nla_get_u8(nla);

	nla = ntb[DRBD_A_NET_CONF_CSUMS_AFTER_CRASH_ONLY];
	if (nla && s)
		s->csums_after_crash_only = nla_get_u8(nla);

	nla = ntb[DRBD_A_NET_CONF_SOCK_CHECK_TIMEO];
	if (nla && s)
		s->sock_check_timeo = nla_get_u32(nla);

out:
	if (ret_nested_attribute_table && (!err || err == -ENOMSG))
		*ret_nested_attribute_table = ntb;
	else
		kfree(ntb);
	return err;
}

int net_conf_from_attrs(struct net_conf *s,
				struct genl_info *info)
{
	return __net_conf_from_attrs(s, NULL, info);
}

int net_conf_ntb_from_attrs(
			struct nlattr ***ret_nested_attribute_table,
			struct genl_info *info)
{
	return __net_conf_from_attrs(NULL, ret_nested_attribute_table, info);
}

static int __set_role_parms_from_attrs(struct set_role_parms *s,
		struct nlattr ***ret_nested_attribute_table,
		struct genl_info *info)
{
	const int maxtype = DRBD_A_SET_ROLE_PARMS_ASSUME_UPTODATE;
	struct nlattr *tla = info->attrs[DRBD_NLA_SET_ROLE_PARMS];
	struct nlattr **ntb;
	struct nlattr *nla;
	int err = 0;

	if (ret_nested_attribute_table)
		*ret_nested_attribute_table = NULL;
	if (!tla)
		return -ENOMSG;
	ntb = kcalloc(DRBD_A_SET_ROLE_PARMS_ASSUME_UPTODATE + 1, sizeof(*ntb), GFP_KERNEL);
	if (!ntb)
		return -ENOMEM;
	err = nla_parse_nested_deprecated(ntb, maxtype, tla, drbd_set_role_parms_nl_policy, NULL);
	if (err)
		goto out;

	nla = ntb[DRBD_A_SET_ROLE_PARMS_ASSUME_UPTODATE];
	if (nla && s)
		s->assume_uptodate = nla_get_u8(nla);

out:
	if (ret_nested_attribute_table && (!err || err == -ENOMSG))
		*ret_nested_attribute_table = ntb;
	else
		kfree(ntb);
	return err;
}

int set_role_parms_from_attrs(struct set_role_parms *s,
				struct genl_info *info)
{
	return __set_role_parms_from_attrs(s, NULL, info);
}

int set_role_parms_ntb_from_attrs(
			struct nlattr ***ret_nested_attribute_table,
			struct genl_info *info)
{
	return __set_role_parms_from_attrs(NULL, ret_nested_attribute_table, info);
}

static int __resize_parms_from_attrs(struct resize_parms *s,
		struct nlattr ***ret_nested_attribute_table,
		struct genl_info *info)
{
	const int maxtype = DRBD_A_RESIZE_PARMS_AL_STRIPE_SIZE;
	struct nlattr *tla = info->attrs[DRBD_NLA_RESIZE_PARMS];
	struct nlattr **ntb;
	struct nlattr *nla;
	int err = 0;

	if (ret_nested_attribute_table)
		*ret_nested_attribute_table = NULL;
	if (!tla)
		return -ENOMSG;
	ntb = kcalloc(DRBD_A_RESIZE_PARMS_AL_STRIPE_SIZE + 1, sizeof(*ntb), GFP_KERNEL);
	if (!ntb)
		return -ENOMEM;
	err = nla_parse_nested_deprecated(ntb, maxtype, tla, drbd_resize_parms_nl_policy, NULL);
	if (err)
		goto out;

	nla = ntb[DRBD_A_RESIZE_PARMS_RESIZE_SIZE];
	if (nla && s)
		s->resize_size = nla_get_u64(nla);

	nla = ntb[DRBD_A_RESIZE_PARMS_RESIZE_FORCE];
	if (nla && s)
		s->resize_force = nla_get_u8(nla);

	nla = ntb[DRBD_A_RESIZE_PARMS_NO_RESYNC];
	if (nla && s)
		s->no_resync = nla_get_u8(nla);

	nla = ntb[DRBD_A_RESIZE_PARMS_AL_STRIPES];
	if (nla && s)
		s->al_stripes = nla_get_u32(nla);

	nla = ntb[DRBD_A_RESIZE_PARMS_AL_STRIPE_SIZE];
	if (nla && s)
		s->al_stripe_size = nla_get_u32(nla);

out:
	if (ret_nested_attribute_table && (!err || err == -ENOMSG))
		*ret_nested_attribute_table = ntb;
	else
		kfree(ntb);
	return err;
}

int resize_parms_from_attrs(struct resize_parms *s,
				struct genl_info *info)
{
	return __resize_parms_from_attrs(s, NULL, info);
}

int resize_parms_ntb_from_attrs(
			struct nlattr ***ret_nested_attribute_table,
			struct genl_info *info)
{
	return __resize_parms_from_attrs(NULL, ret_nested_attribute_table, info);
}

static int __start_ov_parms_from_attrs(struct start_ov_parms *s,
		struct nlattr ***ret_nested_attribute_table,
		struct genl_info *info)
{
	const int maxtype = DRBD_A_START_OV_PARMS_OV_STOP_SECTOR;
	struct nlattr *tla = info->attrs[DRBD_NLA_START_OV_PARMS];
	struct nlattr **ntb;
	struct nlattr *nla;
	int err = 0;

	if (ret_nested_attribute_table)
		*ret_nested_attribute_table = NULL;
	if (!tla)
		return -ENOMSG;
	ntb = kcalloc(DRBD_A_START_OV_PARMS_OV_STOP_SECTOR + 1, sizeof(*ntb), GFP_KERNEL);
	if (!ntb)
		return -ENOMEM;
	err = nla_parse_nested_deprecated(ntb, maxtype, tla, drbd_start_ov_parms_nl_policy, NULL);
	if (err)
		goto out;

	nla = ntb[DRBD_A_START_OV_PARMS_OV_START_SECTOR];
	if (nla && s)
		s->ov_start_sector = nla_get_u64(nla);

	nla = ntb[DRBD_A_START_OV_PARMS_OV_STOP_SECTOR];
	if (nla && s)
		s->ov_stop_sector = nla_get_u64(nla);

out:
	if (ret_nested_attribute_table && (!err || err == -ENOMSG))
		*ret_nested_attribute_table = ntb;
	else
		kfree(ntb);
	return err;
}

int start_ov_parms_from_attrs(struct start_ov_parms *s,
				struct genl_info *info)
{
	return __start_ov_parms_from_attrs(s, NULL, info);
}

int start_ov_parms_ntb_from_attrs(
			struct nlattr ***ret_nested_attribute_table,
			struct genl_info *info)
{
	return __start_ov_parms_from_attrs(NULL, ret_nested_attribute_table, info);
}

static int __new_c_uuid_parms_from_attrs(struct new_c_uuid_parms *s,
		struct nlattr ***ret_nested_attribute_table,
		struct genl_info *info)
{
	const int maxtype = DRBD_A_NEW_C_UUID_PARMS_CLEAR_BM;
	struct nlattr *tla = info->attrs[DRBD_NLA_NEW_C_UUID_PARMS];
	struct nlattr **ntb;
	struct nlattr *nla;
	int err = 0;

	if (ret_nested_attribute_table)
		*ret_nested_attribute_table = NULL;
	if (!tla)
		return -ENOMSG;
	ntb = kcalloc(DRBD_A_NEW_C_UUID_PARMS_CLEAR_BM + 1, sizeof(*ntb), GFP_KERNEL);
	if (!ntb)
		return -ENOMEM;
	err = nla_parse_nested_deprecated(ntb, maxtype, tla, drbd_new_c_uuid_parms_nl_policy, NULL);
	if (err)
		goto out;

	nla = ntb[DRBD_A_NEW_C_UUID_PARMS_CLEAR_BM];
	if (nla && s)
		s->clear_bm = nla_get_u8(nla);

out:
	if (ret_nested_attribute_table && (!err || err == -ENOMSG))
		*ret_nested_attribute_table = ntb;
	else
		kfree(ntb);
	return err;
}

int new_c_uuid_parms_from_attrs(struct new_c_uuid_parms *s,
				struct genl_info *info)
{
	return __new_c_uuid_parms_from_attrs(s, NULL, info);
}

int new_c_uuid_parms_ntb_from_attrs(
			struct nlattr ***ret_nested_attribute_table,
			struct genl_info *info)
{
	return __new_c_uuid_parms_from_attrs(NULL, ret_nested_attribute_table, info);
}

static int __disconnect_parms_from_attrs(struct disconnect_parms *s,
		struct nlattr ***ret_nested_attribute_table,
		struct genl_info *info)
{
	const int maxtype = DRBD_A_DISCONNECT_PARMS_FORCE_DISCONNECT;
	struct nlattr *tla = info->attrs[DRBD_NLA_DISCONNECT_PARMS];
	struct nlattr **ntb;
	struct nlattr *nla;
	int err = 0;

	if (ret_nested_attribute_table)
		*ret_nested_attribute_table = NULL;
	if (!tla)
		return -ENOMSG;
	ntb = kcalloc(DRBD_A_DISCONNECT_PARMS_FORCE_DISCONNECT + 1, sizeof(*ntb), GFP_KERNEL);
	if (!ntb)
		return -ENOMEM;
	err = nla_parse_nested_deprecated(ntb, maxtype, tla, drbd_disconnect_parms_nl_policy, NULL);
	if (err)
		goto out;

	nla = ntb[DRBD_A_DISCONNECT_PARMS_FORCE_DISCONNECT];
	if (nla && s)
		s->force_disconnect = nla_get_u8(nla);

out:
	if (ret_nested_attribute_table && (!err || err == -ENOMSG))
		*ret_nested_attribute_table = ntb;
	else
		kfree(ntb);
	return err;
}

int disconnect_parms_from_attrs(struct disconnect_parms *s,
				struct genl_info *info)
{
	return __disconnect_parms_from_attrs(s, NULL, info);
}

int disconnect_parms_ntb_from_attrs(
			struct nlattr ***ret_nested_attribute_table,
			struct genl_info *info)
{
	return __disconnect_parms_from_attrs(NULL, ret_nested_attribute_table, info);
}

static int __detach_parms_from_attrs(struct detach_parms *s,
		struct nlattr ***ret_nested_attribute_table,
		struct genl_info *info)
{
	const int maxtype = DRBD_A_DETACH_PARMS_FORCE_DETACH;
	struct nlattr *tla = info->attrs[DRBD_NLA_DETACH_PARMS];
	struct nlattr **ntb;
	struct nlattr *nla;
	int err = 0;

	if (ret_nested_attribute_table)
		*ret_nested_attribute_table = NULL;
	if (!tla)
		return -ENOMSG;
	ntb = kcalloc(DRBD_A_DETACH_PARMS_FORCE_DETACH + 1, sizeof(*ntb), GFP_KERNEL);
	if (!ntb)
		return -ENOMEM;
	err = nla_parse_nested_deprecated(ntb, maxtype, tla, drbd_detach_parms_nl_policy, NULL);
	if (err)
		goto out;

	nla = ntb[DRBD_A_DETACH_PARMS_FORCE_DETACH];
	if (nla && s)
		s->force_detach = nla_get_u8(nla);

out:
	if (ret_nested_attribute_table && (!err || err == -ENOMSG))
		*ret_nested_attribute_table = ntb;
	else
		kfree(ntb);
	return err;
}

int detach_parms_from_attrs(struct detach_parms *s,
				struct genl_info *info)
{
	return __detach_parms_from_attrs(s, NULL, info);
}

int detach_parms_ntb_from_attrs(
			struct nlattr ***ret_nested_attribute_table,
			struct genl_info *info)
{
	return __detach_parms_from_attrs(NULL, ret_nested_attribute_table, info);
}

static int __resource_info_from_attrs(struct resource_info *s,
		struct nlattr ***ret_nested_attribute_table,
		struct genl_info *info)
{
	const int maxtype = DRBD_A_RESOURCE_INFO_RES_SUSP_FEN;
	struct nlattr *tla = info->attrs[DRBD_NLA_RESOURCE_INFO];
	struct nlattr **ntb;
	struct nlattr *nla;
	int err = 0;

	if (ret_nested_attribute_table)
		*ret_nested_attribute_table = NULL;
	if (!tla)
		return -ENOMSG;
	ntb = kcalloc(DRBD_A_RESOURCE_INFO_RES_SUSP_FEN + 1, sizeof(*ntb), GFP_KERNEL);
	if (!ntb)
		return -ENOMEM;
	err = nla_parse_nested_deprecated(ntb, maxtype, tla, drbd_resource_info_nl_policy, NULL);
	if (err)
		goto out;

	nla = ntb[DRBD_A_RESOURCE_INFO_RES_ROLE];
	if (nla && s)
		s->res_role = nla_get_u32(nla);

	nla = ntb[DRBD_A_RESOURCE_INFO_RES_SUSP];
	if (nla && s)
		s->res_susp = nla_get_u8(nla);

	nla = ntb[DRBD_A_RESOURCE_INFO_RES_SUSP_NOD];
	if (nla && s)
		s->res_susp_nod = nla_get_u8(nla);

	nla = ntb[DRBD_A_RESOURCE_INFO_RES_SUSP_FEN];
	if (nla && s)
		s->res_susp_fen = nla_get_u8(nla);

out:
	if (ret_nested_attribute_table && (!err || err == -ENOMSG))
		*ret_nested_attribute_table = ntb;
	else
		kfree(ntb);
	return err;
}

int resource_info_from_attrs(struct resource_info *s,
				struct genl_info *info)
{
	return __resource_info_from_attrs(s, NULL, info);
}

int resource_info_ntb_from_attrs(
			struct nlattr ***ret_nested_attribute_table,
			struct genl_info *info)
{
	return __resource_info_from_attrs(NULL, ret_nested_attribute_table, info);
}

static int __device_info_from_attrs(struct device_info *s,
		struct nlattr ***ret_nested_attribute_table,
		struct genl_info *info)
{
	const int maxtype = DRBD_A_DEVICE_INFO_DEV_DISK_STATE;
	struct nlattr *tla = info->attrs[DRBD_NLA_DEVICE_INFO];
	struct nlattr **ntb;
	struct nlattr *nla;
	int err = 0;

	if (ret_nested_attribute_table)
		*ret_nested_attribute_table = NULL;
	if (!tla)
		return -ENOMSG;
	ntb = kcalloc(DRBD_A_DEVICE_INFO_DEV_DISK_STATE + 1, sizeof(*ntb), GFP_KERNEL);
	if (!ntb)
		return -ENOMEM;
	err = nla_parse_nested_deprecated(ntb, maxtype, tla, drbd_device_info_nl_policy, NULL);
	if (err)
		goto out;

	nla = ntb[DRBD_A_DEVICE_INFO_DEV_DISK_STATE];
	if (nla && s)
		s->dev_disk_state = nla_get_u32(nla);

out:
	if (ret_nested_attribute_table && (!err || err == -ENOMSG))
		*ret_nested_attribute_table = ntb;
	else
		kfree(ntb);
	return err;
}

int device_info_from_attrs(struct device_info *s,
				struct genl_info *info)
{
	return __device_info_from_attrs(s, NULL, info);
}

int device_info_ntb_from_attrs(
			struct nlattr ***ret_nested_attribute_table,
			struct genl_info *info)
{
	return __device_info_from_attrs(NULL, ret_nested_attribute_table, info);
}

static int __connection_info_from_attrs(struct connection_info *s,
		struct nlattr ***ret_nested_attribute_table,
		struct genl_info *info)
{
	const int maxtype = DRBD_A_CONNECTION_INFO_CONN_ROLE;
	struct nlattr *tla = info->attrs[DRBD_NLA_CONNECTION_INFO];
	struct nlattr **ntb;
	struct nlattr *nla;
	int err = 0;

	if (ret_nested_attribute_table)
		*ret_nested_attribute_table = NULL;
	if (!tla)
		return -ENOMSG;
	ntb = kcalloc(DRBD_A_CONNECTION_INFO_CONN_ROLE + 1, sizeof(*ntb), GFP_KERNEL);
	if (!ntb)
		return -ENOMEM;
	err = nla_parse_nested_deprecated(ntb, maxtype, tla, drbd_connection_info_nl_policy, NULL);
	if (err)
		goto out;

	nla = ntb[DRBD_A_CONNECTION_INFO_CONN_CONNECTION_STATE];
	if (nla && s)
		s->conn_connection_state = nla_get_u32(nla);

	nla = ntb[DRBD_A_CONNECTION_INFO_CONN_ROLE];
	if (nla && s)
		s->conn_role = nla_get_u32(nla);

out:
	if (ret_nested_attribute_table && (!err || err == -ENOMSG))
		*ret_nested_attribute_table = ntb;
	else
		kfree(ntb);
	return err;
}

int connection_info_from_attrs(struct connection_info *s,
				struct genl_info *info)
{
	return __connection_info_from_attrs(s, NULL, info);
}

int connection_info_ntb_from_attrs(
			struct nlattr ***ret_nested_attribute_table,
			struct genl_info *info)
{
	return __connection_info_from_attrs(NULL, ret_nested_attribute_table, info);
}

static int __peer_device_info_from_attrs(struct peer_device_info *s,
		struct nlattr ***ret_nested_attribute_table,
		struct genl_info *info)
{
	const int maxtype = DRBD_A_PEER_DEVICE_INFO_PEER_RESYNC_SUSP_DEPENDENCY;
	struct nlattr *tla = info->attrs[DRBD_NLA_PEER_DEVICE_INFO];
	struct nlattr **ntb;
	struct nlattr *nla;
	int err = 0;

	if (ret_nested_attribute_table)
		*ret_nested_attribute_table = NULL;
	if (!tla)
		return -ENOMSG;
	ntb = kcalloc(DRBD_A_PEER_DEVICE_INFO_PEER_RESYNC_SUSP_DEPENDENCY + 1, sizeof(*ntb), GFP_KERNEL);
	if (!ntb)
		return -ENOMEM;
	err = nla_parse_nested_deprecated(ntb, maxtype, tla, drbd_peer_device_info_nl_policy, NULL);
	if (err)
		goto out;

	nla = ntb[DRBD_A_PEER_DEVICE_INFO_PEER_REPL_STATE];
	if (nla && s)
		s->peer_repl_state = nla_get_u32(nla);

	nla = ntb[DRBD_A_PEER_DEVICE_INFO_PEER_DISK_STATE];
	if (nla && s)
		s->peer_disk_state = nla_get_u32(nla);

	nla = ntb[DRBD_A_PEER_DEVICE_INFO_PEER_RESYNC_SUSP_USER];
	if (nla && s)
		s->peer_resync_susp_user = nla_get_u32(nla);

	nla = ntb[DRBD_A_PEER_DEVICE_INFO_PEER_RESYNC_SUSP_PEER];
	if (nla && s)
		s->peer_resync_susp_peer = nla_get_u32(nla);

	nla = ntb[DRBD_A_PEER_DEVICE_INFO_PEER_RESYNC_SUSP_DEPENDENCY];
	if (nla && s)
		s->peer_resync_susp_dependency = nla_get_u32(nla);

out:
	if (ret_nested_attribute_table && (!err || err == -ENOMSG))
		*ret_nested_attribute_table = ntb;
	else
		kfree(ntb);
	return err;
}

int peer_device_info_from_attrs(struct peer_device_info *s,
				struct genl_info *info)
{
	return __peer_device_info_from_attrs(s, NULL, info);
}

int peer_device_info_ntb_from_attrs(
			struct nlattr ***ret_nested_attribute_table,
			struct genl_info *info)
{
	return __peer_device_info_from_attrs(NULL, ret_nested_attribute_table, info);
}

static int __resource_statistics_from_attrs(struct resource_statistics *s,
		struct nlattr ***ret_nested_attribute_table,
		struct genl_info *info)
{
	const int maxtype = DRBD_A_RESOURCE_STATISTICS_RES_STAT_WRITE_ORDERING;
	struct nlattr *tla = info->attrs[DRBD_NLA_RESOURCE_STATISTICS];
	struct nlattr **ntb;
	struct nlattr *nla;
	int err = 0;

	if (ret_nested_attribute_table)
		*ret_nested_attribute_table = NULL;
	if (!tla)
		return -ENOMSG;
	ntb = kcalloc(DRBD_A_RESOURCE_STATISTICS_RES_STAT_WRITE_ORDERING + 1, sizeof(*ntb), GFP_KERNEL);
	if (!ntb)
		return -ENOMEM;
	err = nla_parse_nested_deprecated(ntb, maxtype, tla, drbd_resource_statistics_nl_policy, NULL);
	if (err)
		goto out;

	nla = ntb[DRBD_A_RESOURCE_STATISTICS_RES_STAT_WRITE_ORDERING];
	if (nla && s)
		s->res_stat_write_ordering = nla_get_u32(nla);

out:
	if (ret_nested_attribute_table && (!err || err == -ENOMSG))
		*ret_nested_attribute_table = ntb;
	else
		kfree(ntb);
	return err;
}

int resource_statistics_from_attrs(struct resource_statistics *s,
				struct genl_info *info)
{
	return __resource_statistics_from_attrs(s, NULL, info);
}

int resource_statistics_ntb_from_attrs(
			struct nlattr ***ret_nested_attribute_table,
			struct genl_info *info)
{
	return __resource_statistics_from_attrs(NULL, ret_nested_attribute_table, info);
}

static int __device_statistics_from_attrs(struct device_statistics *s,
		struct nlattr ***ret_nested_attribute_table,
		struct genl_info *info)
{
	const int maxtype = DRBD_A_DEVICE_STATISTICS_HISTORY_UUIDS;
	struct nlattr *tla = info->attrs[DRBD_NLA_DEVICE_STATISTICS];
	struct nlattr **ntb;
	struct nlattr *nla;
	int err = 0;

	if (ret_nested_attribute_table)
		*ret_nested_attribute_table = NULL;
	if (!tla)
		return -ENOMSG;
	ntb = kcalloc(DRBD_A_DEVICE_STATISTICS_HISTORY_UUIDS + 1, sizeof(*ntb), GFP_KERNEL);
	if (!ntb)
		return -ENOMEM;
	err = nla_parse_nested_deprecated(ntb, maxtype, tla, drbd_device_statistics_nl_policy, NULL);
	if (err)
		goto out;

	nla = ntb[DRBD_A_DEVICE_STATISTICS_DEV_SIZE];
	if (nla && s)
		s->dev_size = nla_get_u64(nla);

	nla = ntb[DRBD_A_DEVICE_STATISTICS_DEV_READ];
	if (nla && s)
		s->dev_read = nla_get_u64(nla);

	nla = ntb[DRBD_A_DEVICE_STATISTICS_DEV_WRITE];
	if (nla && s)
		s->dev_write = nla_get_u64(nla);

	nla = ntb[DRBD_A_DEVICE_STATISTICS_DEV_AL_WRITES];
	if (nla && s)
		s->dev_al_writes = nla_get_u64(nla);

	nla = ntb[DRBD_A_DEVICE_STATISTICS_DEV_BM_WRITES];
	if (nla && s)
		s->dev_bm_writes = nla_get_u64(nla);

	nla = ntb[DRBD_A_DEVICE_STATISTICS_DEV_UPPER_PENDING];
	if (nla && s)
		s->dev_upper_pending = nla_get_u32(nla);

	nla = ntb[DRBD_A_DEVICE_STATISTICS_DEV_LOWER_PENDING];
	if (nla && s)
		s->dev_lower_pending = nla_get_u32(nla);

	nla = ntb[DRBD_A_DEVICE_STATISTICS_DEV_UPPER_BLOCKED];
	if (nla && s)
		s->dev_upper_blocked = nla_get_u8(nla);

	nla = ntb[DRBD_A_DEVICE_STATISTICS_DEV_LOWER_BLOCKED];
	if (nla && s)
		s->dev_lower_blocked = nla_get_u8(nla);

	nla = ntb[DRBD_A_DEVICE_STATISTICS_DEV_AL_SUSPENDED];
	if (nla && s)
		s->dev_al_suspended = nla_get_u8(nla);

	nla = ntb[DRBD_A_DEVICE_STATISTICS_DEV_EXPOSED_DATA_UUID];
	if (nla && s)
		s->dev_exposed_data_uuid = nla_get_u64(nla);

	nla = ntb[DRBD_A_DEVICE_STATISTICS_DEV_CURRENT_UUID];
	if (nla && s)
		s->dev_current_uuid = nla_get_u64(nla);

	nla = ntb[DRBD_A_DEVICE_STATISTICS_DEV_DISK_FLAGS];
	if (nla && s)
		s->dev_disk_flags = nla_get_u32(nla);

	nla = ntb[DRBD_A_DEVICE_STATISTICS_HISTORY_UUIDS];
	if (nla && s)
		s->history_uuids_len = nla_memcpy(s->history_uuids, nla, DRBD_NL_HISTORY_UUIDS_SIZE);

out:
	if (ret_nested_attribute_table && (!err || err == -ENOMSG))
		*ret_nested_attribute_table = ntb;
	else
		kfree(ntb);
	return err;
}

int device_statistics_from_attrs(struct device_statistics *s,
				struct genl_info *info)
{
	return __device_statistics_from_attrs(s, NULL, info);
}

int device_statistics_ntb_from_attrs(
			struct nlattr ***ret_nested_attribute_table,
			struct genl_info *info)
{
	return __device_statistics_from_attrs(NULL, ret_nested_attribute_table, info);
}

static int __connection_statistics_from_attrs(struct connection_statistics *s,
		struct nlattr ***ret_nested_attribute_table,
		struct genl_info *info)
{
	const int maxtype = DRBD_A_CONNECTION_STATISTICS_CONN_CONGESTED;
	struct nlattr *tla = info->attrs[DRBD_NLA_CONNECTION_STATISTICS];
	struct nlattr **ntb;
	struct nlattr *nla;
	int err = 0;

	if (ret_nested_attribute_table)
		*ret_nested_attribute_table = NULL;
	if (!tla)
		return -ENOMSG;
	ntb = kcalloc(DRBD_A_CONNECTION_STATISTICS_CONN_CONGESTED + 1, sizeof(*ntb), GFP_KERNEL);
	if (!ntb)
		return -ENOMEM;
	err = nla_parse_nested_deprecated(ntb, maxtype, tla, drbd_connection_statistics_nl_policy, NULL);
	if (err)
		goto out;

	nla = ntb[DRBD_A_CONNECTION_STATISTICS_CONN_CONGESTED];
	if (nla && s)
		s->conn_congested = nla_get_u8(nla);

out:
	if (ret_nested_attribute_table && (!err || err == -ENOMSG))
		*ret_nested_attribute_table = ntb;
	else
		kfree(ntb);
	return err;
}

int connection_statistics_from_attrs(struct connection_statistics *s,
				struct genl_info *info)
{
	return __connection_statistics_from_attrs(s, NULL, info);
}

int connection_statistics_ntb_from_attrs(
			struct nlattr ***ret_nested_attribute_table,
			struct genl_info *info)
{
	return __connection_statistics_from_attrs(NULL, ret_nested_attribute_table, info);
}

static int __peer_device_statistics_from_attrs(struct peer_device_statistics *s,
		struct nlattr ***ret_nested_attribute_table,
		struct genl_info *info)
{
	const int maxtype = DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_FLAGS;
	struct nlattr *tla = info->attrs[DRBD_NLA_PEER_DEVICE_STATISTICS];
	struct nlattr **ntb;
	struct nlattr *nla;
	int err = 0;

	if (ret_nested_attribute_table)
		*ret_nested_attribute_table = NULL;
	if (!tla)
		return -ENOMSG;
	ntb = kcalloc(DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_FLAGS + 1, sizeof(*ntb), GFP_KERNEL);
	if (!ntb)
		return -ENOMEM;
	err = nla_parse_nested_deprecated(ntb, maxtype, tla, drbd_peer_device_statistics_nl_policy, NULL);
	if (err)
		goto out;

	nla = ntb[DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_RECEIVED];
	if (nla && s)
		s->peer_dev_received = nla_get_u64(nla);

	nla = ntb[DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_SENT];
	if (nla && s)
		s->peer_dev_sent = nla_get_u64(nla);

	nla = ntb[DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_PENDING];
	if (nla && s)
		s->peer_dev_pending = nla_get_u32(nla);

	nla = ntb[DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_UNACKED];
	if (nla && s)
		s->peer_dev_unacked = nla_get_u32(nla);

	nla = ntb[DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_OUT_OF_SYNC];
	if (nla && s)
		s->peer_dev_out_of_sync = nla_get_u64(nla);

	nla = ntb[DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_RESYNC_FAILED];
	if (nla && s)
		s->peer_dev_resync_failed = nla_get_u64(nla);

	nla = ntb[DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_BITMAP_UUID];
	if (nla && s)
		s->peer_dev_bitmap_uuid = nla_get_u64(nla);

	nla = ntb[DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_FLAGS];
	if (nla && s)
		s->peer_dev_flags = nla_get_u32(nla);

out:
	if (ret_nested_attribute_table && (!err || err == -ENOMSG))
		*ret_nested_attribute_table = ntb;
	else
		kfree(ntb);
	return err;
}

int peer_device_statistics_from_attrs(struct peer_device_statistics *s,
				struct genl_info *info)
{
	return __peer_device_statistics_from_attrs(s, NULL, info);
}

int peer_device_statistics_ntb_from_attrs(
			struct nlattr ***ret_nested_attribute_table,
			struct genl_info *info)
{
	return __peer_device_statistics_from_attrs(NULL, ret_nested_attribute_table, info);
}

int drbd_cfg_reply_to_skb(struct sk_buff *skb, struct drbd_cfg_reply *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_CFG_REPLY);

	if (!tla)
		goto nla_put_failure;

	if (nla_put(skb, DRBD_A_DRBD_CFG_REPLY_INFO_TEXT, min_t(int, 0,
			s->info_text_len + (s->info_text_len < 0)), s->info_text))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int drbd_cfg_context_to_skb(struct sk_buff *skb, struct drbd_cfg_context *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_CFG_CONTEXT);

	if (!tla)
		goto nla_put_failure;

	if (nla_put_u32(skb, DRBD_A_DRBD_CFG_CONTEXT_CTX_VOLUME, s->ctx_volume))
		goto nla_put_failure;
	if (nla_put(skb, DRBD_A_DRBD_CFG_CONTEXT_CTX_RESOURCE_NAME, min_t(int, 128,
			s->ctx_resource_name_len + (s->ctx_resource_name_len < 128)), s->ctx_resource_name))
		goto nla_put_failure;
	if (nla_put(skb, DRBD_A_DRBD_CFG_CONTEXT_CTX_MY_ADDR, min_t(int, 128,
			s->ctx_my_addr_len), s->ctx_my_addr))
		goto nla_put_failure;
	if (nla_put(skb, DRBD_A_DRBD_CFG_CONTEXT_CTX_PEER_ADDR, min_t(int, 128,
			s->ctx_peer_addr_len), s->ctx_peer_addr))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int disk_conf_to_skb(struct sk_buff *skb, struct disk_conf *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_DISK_CONF);

	if (!tla)
		goto nla_put_failure;

	if (nla_put(skb, DRBD_A_DISK_CONF_BACKING_DEV, min_t(int, 128,
			s->backing_dev_len + (s->backing_dev_len < 128)), s->backing_dev))
		goto nla_put_failure;
	if (nla_put(skb, DRBD_A_DISK_CONF_META_DEV, min_t(int, 128,
			s->meta_dev_len + (s->meta_dev_len < 128)), s->meta_dev))
		goto nla_put_failure;
	if (nla_put_s32(skb, DRBD_A_DISK_CONF_META_DEV_IDX, s->meta_dev_idx))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_DISK_CONF_DISK_SIZE, s->disk_size, 0))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_DISK_CONF_MAX_BIO_BVECS, s->max_bio_bvecs))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_DISK_CONF_ON_IO_ERROR, s->on_io_error))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_DISK_CONF_FENCING, s->fencing))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_DISK_CONF_RESYNC_RATE, s->resync_rate))
		goto nla_put_failure;
	if (nla_put_s32(skb, DRBD_A_DISK_CONF_RESYNC_AFTER, s->resync_after))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_DISK_CONF_AL_EXTENTS, s->al_extents))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_DISK_CONF_C_PLAN_AHEAD, s->c_plan_ahead))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_DISK_CONF_C_DELAY_TARGET, s->c_delay_target))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_DISK_CONF_C_FILL_TARGET, s->c_fill_target))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_DISK_CONF_C_MAX_RATE, s->c_max_rate))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_DISK_CONF_C_MIN_RATE, s->c_min_rate))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_DISK_CONF_DISK_BARRIER, s->disk_barrier))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_DISK_CONF_DISK_FLUSHES, s->disk_flushes))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_DISK_CONF_DISK_DRAIN, s->disk_drain))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_DISK_CONF_MD_FLUSHES, s->md_flushes))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_DISK_CONF_DISK_TIMEOUT, s->disk_timeout))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_DISK_CONF_READ_BALANCING, s->read_balancing))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_DISK_CONF_AL_UPDATES, s->al_updates))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_DISK_CONF_DISCARD_ZEROES_IF_ALIGNED, s->discard_zeroes_if_aligned))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_DISK_CONF_RS_DISCARD_GRANULARITY, s->rs_discard_granularity))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_DISK_CONF_DISABLE_WRITE_SAME, s->disable_write_same))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int res_opts_to_skb(struct sk_buff *skb, struct res_opts *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_RESOURCE_OPTS);

	if (!tla)
		goto nla_put_failure;

	if (nla_put(skb, DRBD_A_RES_OPTS_CPU_MASK, min_t(int, DRBD_CPU_MASK_SIZE,
			s->cpu_mask_len + (s->cpu_mask_len < DRBD_CPU_MASK_SIZE)), s->cpu_mask))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_RES_OPTS_ON_NO_DATA, s->on_no_data))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int net_conf_to_skb(struct sk_buff *skb, struct net_conf *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_NET_CONF);

	if (!tla)
		goto nla_put_failure;

	if (nla_put(skb, DRBD_A_NET_CONF_SHARED_SECRET, min_t(int, SHARED_SECRET_MAX,
			s->shared_secret_len + (s->shared_secret_len < SHARED_SECRET_MAX)), s->shared_secret))
		goto nla_put_failure;
	if (nla_put(skb, DRBD_A_NET_CONF_CRAM_HMAC_ALG, min_t(int, SHARED_SECRET_MAX,
			s->cram_hmac_alg_len + (s->cram_hmac_alg_len < SHARED_SECRET_MAX)), s->cram_hmac_alg))
		goto nla_put_failure;
	if (nla_put(skb, DRBD_A_NET_CONF_INTEGRITY_ALG, min_t(int, SHARED_SECRET_MAX,
			s->integrity_alg_len + (s->integrity_alg_len < SHARED_SECRET_MAX)), s->integrity_alg))
		goto nla_put_failure;
	if (nla_put(skb, DRBD_A_NET_CONF_VERIFY_ALG, min_t(int, SHARED_SECRET_MAX,
			s->verify_alg_len + (s->verify_alg_len < SHARED_SECRET_MAX)), s->verify_alg))
		goto nla_put_failure;
	if (nla_put(skb, DRBD_A_NET_CONF_CSUMS_ALG, min_t(int, SHARED_SECRET_MAX,
			s->csums_alg_len + (s->csums_alg_len < SHARED_SECRET_MAX)), s->csums_alg))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_NET_CONF_WIRE_PROTOCOL, s->wire_protocol))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_NET_CONF_CONNECT_INT, s->connect_int))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_NET_CONF_TIMEOUT, s->timeout))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_NET_CONF_PING_INT, s->ping_int))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_NET_CONF_PING_TIMEO, s->ping_timeo))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_NET_CONF_SNDBUF_SIZE, s->sndbuf_size))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_NET_CONF_RCVBUF_SIZE, s->rcvbuf_size))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_NET_CONF_KO_COUNT, s->ko_count))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_NET_CONF_MAX_BUFFERS, s->max_buffers))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_NET_CONF_MAX_EPOCH_SIZE, s->max_epoch_size))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_NET_CONF_UNPLUG_WATERMARK, s->unplug_watermark))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_NET_CONF_AFTER_SB_0P, s->after_sb_0p))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_NET_CONF_AFTER_SB_1P, s->after_sb_1p))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_NET_CONF_AFTER_SB_2P, s->after_sb_2p))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_NET_CONF_RR_CONFLICT, s->rr_conflict))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_NET_CONF_ON_CONGESTION, s->on_congestion))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_NET_CONF_CONG_FILL, s->cong_fill))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_NET_CONF_CONG_EXTENTS, s->cong_extents))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_NET_CONF_TWO_PRIMARIES, s->two_primaries))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_NET_CONF_DISCARD_MY_DATA, s->discard_my_data))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_NET_CONF_TCP_CORK, s->tcp_cork))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_NET_CONF_ALWAYS_ASBP, s->always_asbp))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_NET_CONF_TENTATIVE, s->tentative))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_NET_CONF_USE_RLE, s->use_rle))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_NET_CONF_CSUMS_AFTER_CRASH_ONLY, s->csums_after_crash_only))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_NET_CONF_SOCK_CHECK_TIMEO, s->sock_check_timeo))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int set_role_parms_to_skb(struct sk_buff *skb, struct set_role_parms *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_SET_ROLE_PARMS);

	if (!tla)
		goto nla_put_failure;

	if (nla_put_u8(skb, DRBD_A_SET_ROLE_PARMS_ASSUME_UPTODATE, s->assume_uptodate))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int resize_parms_to_skb(struct sk_buff *skb, struct resize_parms *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_RESIZE_PARMS);

	if (!tla)
		goto nla_put_failure;

	if (nla_put_u64_64bit(skb, DRBD_A_RESIZE_PARMS_RESIZE_SIZE, s->resize_size, 0))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_RESIZE_PARMS_RESIZE_FORCE, s->resize_force))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_RESIZE_PARMS_NO_RESYNC, s->no_resync))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_RESIZE_PARMS_AL_STRIPES, s->al_stripes))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_RESIZE_PARMS_AL_STRIPE_SIZE, s->al_stripe_size))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int state_info_to_skb(struct sk_buff *skb, struct state_info *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_STATE_INFO);

	if (!tla)
		goto nla_put_failure;

	if (nla_put_u32(skb, DRBD_A_STATE_INFO_SIB_REASON, s->sib_reason))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_STATE_INFO_CURRENT_STATE, s->current_state))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_STATE_INFO_CAPACITY, s->capacity, 0))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_STATE_INFO_ED_UUID, s->ed_uuid, 0))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_STATE_INFO_PREV_STATE, s->prev_state))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_STATE_INFO_NEW_STATE, s->new_state))
		goto nla_put_failure;
	if (nla_put(skb, DRBD_A_STATE_INFO_UUIDS, min_t(int, DRBD_NL_UUIDS_SIZE,
			s->uuids_len), s->uuids))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_STATE_INFO_DISK_FLAGS, s->disk_flags))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_STATE_INFO_BITS_TOTAL, s->bits_total, 0))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_STATE_INFO_BITS_OOS, s->bits_oos, 0))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_STATE_INFO_BITS_RS_TOTAL, s->bits_rs_total, 0))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_STATE_INFO_BITS_RS_FAILED, s->bits_rs_failed, 0))
		goto nla_put_failure;
	if (nla_put(skb, DRBD_A_STATE_INFO_HELPER, min_t(int, 32,
			s->helper_len + (s->helper_len < 32)), s->helper))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_STATE_INFO_HELPER_EXIT_CODE, s->helper_exit_code))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_STATE_INFO_SEND_CNT, s->send_cnt, 0))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_STATE_INFO_RECV_CNT, s->recv_cnt, 0))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_STATE_INFO_READ_CNT, s->read_cnt, 0))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_STATE_INFO_WRIT_CNT, s->writ_cnt, 0))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_STATE_INFO_AL_WRIT_CNT, s->al_writ_cnt, 0))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_STATE_INFO_BM_WRIT_CNT, s->bm_writ_cnt, 0))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_STATE_INFO_AP_BIO_CNT, s->ap_bio_cnt))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_STATE_INFO_AP_PENDING_CNT, s->ap_pending_cnt))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_STATE_INFO_RS_PENDING_CNT, s->rs_pending_cnt))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int start_ov_parms_to_skb(struct sk_buff *skb, struct start_ov_parms *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_START_OV_PARMS);

	if (!tla)
		goto nla_put_failure;

	if (nla_put_u64_64bit(skb, DRBD_A_START_OV_PARMS_OV_START_SECTOR, s->ov_start_sector, 0))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_START_OV_PARMS_OV_STOP_SECTOR, s->ov_stop_sector, 0))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int new_c_uuid_parms_to_skb(struct sk_buff *skb, struct new_c_uuid_parms *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_NEW_C_UUID_PARMS);

	if (!tla)
		goto nla_put_failure;

	if (nla_put_u8(skb, DRBD_A_NEW_C_UUID_PARMS_CLEAR_BM, s->clear_bm))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int timeout_parms_to_skb(struct sk_buff *skb, struct timeout_parms *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_TIMEOUT_PARMS);

	if (!tla)
		goto nla_put_failure;

	if (nla_put_u32(skb, DRBD_A_TIMEOUT_PARMS_TIMEOUT_TYPE, s->timeout_type))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int disconnect_parms_to_skb(struct sk_buff *skb, struct disconnect_parms *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_DISCONNECT_PARMS);

	if (!tla)
		goto nla_put_failure;

	if (nla_put_u8(skb, DRBD_A_DISCONNECT_PARMS_FORCE_DISCONNECT, s->force_disconnect))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int detach_parms_to_skb(struct sk_buff *skb, struct detach_parms *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_DETACH_PARMS);

	if (!tla)
		goto nla_put_failure;

	if (nla_put_u8(skb, DRBD_A_DETACH_PARMS_FORCE_DETACH, s->force_detach))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int resource_info_to_skb(struct sk_buff *skb, struct resource_info *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_RESOURCE_INFO);

	if (!tla)
		goto nla_put_failure;

	if (nla_put_u32(skb, DRBD_A_RESOURCE_INFO_RES_ROLE, s->res_role))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_RESOURCE_INFO_RES_SUSP, s->res_susp))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_RESOURCE_INFO_RES_SUSP_NOD, s->res_susp_nod))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_RESOURCE_INFO_RES_SUSP_FEN, s->res_susp_fen))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int device_info_to_skb(struct sk_buff *skb, struct device_info *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_DEVICE_INFO);

	if (!tla)
		goto nla_put_failure;

	if (nla_put_u32(skb, DRBD_A_DEVICE_INFO_DEV_DISK_STATE, s->dev_disk_state))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int connection_info_to_skb(struct sk_buff *skb, struct connection_info *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_CONNECTION_INFO);

	if (!tla)
		goto nla_put_failure;

	if (nla_put_u32(skb, DRBD_A_CONNECTION_INFO_CONN_CONNECTION_STATE, s->conn_connection_state))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_CONNECTION_INFO_CONN_ROLE, s->conn_role))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int peer_device_info_to_skb(struct sk_buff *skb, struct peer_device_info *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_PEER_DEVICE_INFO);

	if (!tla)
		goto nla_put_failure;

	if (nla_put_u32(skb, DRBD_A_PEER_DEVICE_INFO_PEER_REPL_STATE, s->peer_repl_state))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_PEER_DEVICE_INFO_PEER_DISK_STATE, s->peer_disk_state))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_PEER_DEVICE_INFO_PEER_RESYNC_SUSP_USER, s->peer_resync_susp_user))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_PEER_DEVICE_INFO_PEER_RESYNC_SUSP_PEER, s->peer_resync_susp_peer))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_PEER_DEVICE_INFO_PEER_RESYNC_SUSP_DEPENDENCY, s->peer_resync_susp_dependency))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int resource_statistics_to_skb(struct sk_buff *skb, struct resource_statistics *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_RESOURCE_STATISTICS);

	if (!tla)
		goto nla_put_failure;

	if (nla_put_u32(skb, DRBD_A_RESOURCE_STATISTICS_RES_STAT_WRITE_ORDERING, s->res_stat_write_ordering))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int device_statistics_to_skb(struct sk_buff *skb, struct device_statistics *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_DEVICE_STATISTICS);

	if (!tla)
		goto nla_put_failure;

	if (nla_put_u64_64bit(skb, DRBD_A_DEVICE_STATISTICS_DEV_SIZE, s->dev_size, 0))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_DEVICE_STATISTICS_DEV_READ, s->dev_read, 0))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_DEVICE_STATISTICS_DEV_WRITE, s->dev_write, 0))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_DEVICE_STATISTICS_DEV_AL_WRITES, s->dev_al_writes, 0))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_DEVICE_STATISTICS_DEV_BM_WRITES, s->dev_bm_writes, 0))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_DEVICE_STATISTICS_DEV_UPPER_PENDING, s->dev_upper_pending))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_DEVICE_STATISTICS_DEV_LOWER_PENDING, s->dev_lower_pending))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_DEVICE_STATISTICS_DEV_UPPER_BLOCKED, s->dev_upper_blocked))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_DEVICE_STATISTICS_DEV_LOWER_BLOCKED, s->dev_lower_blocked))
		goto nla_put_failure;
	if (nla_put_u8(skb, DRBD_A_DEVICE_STATISTICS_DEV_AL_SUSPENDED, s->dev_al_suspended))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_DEVICE_STATISTICS_DEV_EXPOSED_DATA_UUID, s->dev_exposed_data_uuid, 0))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_DEVICE_STATISTICS_DEV_CURRENT_UUID, s->dev_current_uuid, 0))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_DEVICE_STATISTICS_DEV_DISK_FLAGS, s->dev_disk_flags))
		goto nla_put_failure;
	if (nla_put(skb, DRBD_A_DEVICE_STATISTICS_HISTORY_UUIDS, min_t(int, DRBD_NL_HISTORY_UUIDS_SIZE,
			s->history_uuids_len), s->history_uuids))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int connection_statistics_to_skb(struct sk_buff *skb, struct connection_statistics *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_CONNECTION_STATISTICS);

	if (!tla)
		goto nla_put_failure;

	if (nla_put_u8(skb, DRBD_A_CONNECTION_STATISTICS_CONN_CONGESTED, s->conn_congested))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int peer_device_statistics_to_skb(struct sk_buff *skb, struct peer_device_statistics *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_PEER_DEVICE_STATISTICS);

	if (!tla)
		goto nla_put_failure;

	if (nla_put_u64_64bit(skb, DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_RECEIVED, s->peer_dev_received, 0))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_SENT, s->peer_dev_sent, 0))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_PENDING, s->peer_dev_pending))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_UNACKED, s->peer_dev_unacked))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_OUT_OF_SYNC, s->peer_dev_out_of_sync, 0))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_RESYNC_FAILED, s->peer_dev_resync_failed, 0))
		goto nla_put_failure;
	if (nla_put_u64_64bit(skb, DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_BITMAP_UUID, s->peer_dev_bitmap_uuid, 0))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_FLAGS, s->peer_dev_flags))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int drbd_notification_header_to_skb(struct sk_buff *skb, struct drbd_notification_header *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_NOTIFICATION_HEADER);

	if (!tla)
		goto nla_put_failure;

	if (nla_put_u32(skb, DRBD_A_DRBD_NOTIFICATION_HEADER_NH_TYPE, s->nh_type))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

int drbd_helper_info_to_skb(struct sk_buff *skb, struct drbd_helper_info *s)
{
	struct nlattr *tla = nla_nest_start(skb, DRBD_NLA_HELPER);

	if (!tla)
		goto nla_put_failure;

	if (nla_put(skb, DRBD_A_DRBD_HELPER_INFO_HELPER_NAME, min_t(int, 32,
			s->helper_name_len + (s->helper_name_len < 32)), s->helper_name))
		goto nla_put_failure;
	if (nla_put_u32(skb, DRBD_A_DRBD_HELPER_INFO_HELPER_STATUS, s->helper_status))
		goto nla_put_failure;

	nla_nest_end(skb, tla);
	return 0;

nla_put_failure:
	if (tla)
		nla_nest_cancel(skb, tla);
	return -EMSGSIZE;
}

void set_disk_conf_defaults(struct disk_conf *x)
{
	x->on_io_error = DRBD_ON_IO_ERROR_DEF;
	x->fencing = DRBD_FENCING_DEF;
	x->resync_rate = DRBD_RESYNC_RATE_DEF;
	x->resync_after = DRBD_MINOR_NUMBER_DEF;
	x->al_extents = DRBD_AL_EXTENTS_DEF;
	x->c_plan_ahead = DRBD_C_PLAN_AHEAD_DEF;
	x->c_delay_target = DRBD_C_DELAY_TARGET_DEF;
	x->c_fill_target = DRBD_C_FILL_TARGET_DEF;
	x->c_max_rate = DRBD_C_MAX_RATE_DEF;
	x->c_min_rate = DRBD_C_MIN_RATE_DEF;
	x->disk_barrier = DRBD_DISK_BARRIER_DEF;
	x->disk_flushes = DRBD_DISK_FLUSHES_DEF;
	x->disk_drain = DRBD_DISK_DRAIN_DEF;
	x->md_flushes = DRBD_MD_FLUSHES_DEF;
	x->disk_timeout = DRBD_DISK_TIMEOUT_DEF;
	x->read_balancing = DRBD_READ_BALANCING_DEF;
	x->al_updates = DRBD_AL_UPDATES_DEF;
	x->discard_zeroes_if_aligned = DRBD_DISCARD_ZEROES_IF_ALIGNED_DEF;
	x->rs_discard_granularity = DRBD_RS_DISCARD_GRANULARITY_DEF;
	x->disable_write_same = DRBD_DISABLE_WRITE_SAME_DEF;
}

void set_res_opts_defaults(struct res_opts *x)
{
	memset(x->cpu_mask, 0, sizeof(x->cpu_mask));
	x->cpu_mask_len = 0;
	x->on_no_data = DRBD_ON_NO_DATA_DEF;
}

void set_net_conf_defaults(struct net_conf *x)
{
	memset(x->shared_secret, 0, sizeof(x->shared_secret));
	x->shared_secret_len = 0;
	memset(x->cram_hmac_alg, 0, sizeof(x->cram_hmac_alg));
	x->cram_hmac_alg_len = 0;
	memset(x->integrity_alg, 0, sizeof(x->integrity_alg));
	x->integrity_alg_len = 0;
	memset(x->verify_alg, 0, sizeof(x->verify_alg));
	x->verify_alg_len = 0;
	memset(x->csums_alg, 0, sizeof(x->csums_alg));
	x->csums_alg_len = 0;
	x->wire_protocol = DRBD_PROTOCOL_DEF;
	x->connect_int = DRBD_CONNECT_INT_DEF;
	x->timeout = DRBD_TIMEOUT_DEF;
	x->ping_int = DRBD_PING_INT_DEF;
	x->ping_timeo = DRBD_PING_TIMEO_DEF;
	x->sndbuf_size = DRBD_SNDBUF_SIZE_DEF;
	x->rcvbuf_size = DRBD_RCVBUF_SIZE_DEF;
	x->ko_count = DRBD_KO_COUNT_DEF;
	x->max_buffers = DRBD_MAX_BUFFERS_DEF;
	x->max_epoch_size = DRBD_MAX_EPOCH_SIZE_DEF;
	x->unplug_watermark = DRBD_UNPLUG_WATERMARK_DEF;
	x->after_sb_0p = DRBD_AFTER_SB_0P_DEF;
	x->after_sb_1p = DRBD_AFTER_SB_1P_DEF;
	x->after_sb_2p = DRBD_AFTER_SB_2P_DEF;
	x->rr_conflict = DRBD_RR_CONFLICT_DEF;
	x->on_congestion = DRBD_ON_CONGESTION_DEF;
	x->cong_fill = DRBD_CONG_FILL_DEF;
	x->cong_extents = DRBD_CONG_EXTENTS_DEF;
	x->two_primaries = DRBD_ALLOW_TWO_PRIMARIES_DEF;
	x->tcp_cork = DRBD_TCP_CORK_DEF;
	x->always_asbp = DRBD_ALWAYS_ASBP_DEF;
	x->use_rle = DRBD_USE_RLE_DEF;
	x->csums_after_crash_only = DRBD_CSUMS_AFTER_CRASH_ONLY_DEF;
	x->sock_check_timeo = DRBD_SOCKET_CHECK_TIMEO_DEF;
}

void set_resize_parms_defaults(struct resize_parms *x)
{
	x->al_stripes = DRBD_AL_STRIPES_DEF;
	x->al_stripe_size = DRBD_AL_STRIPE_SIZE_DEF;
}
