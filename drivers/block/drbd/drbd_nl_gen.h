/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */

#ifndef _LINUX_DRBD_GEN_H
#define _LINUX_DRBD_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <uapi/linux/drbd_genl.h>
#include <linux/drbd.h>
#include <linux/drbd_limits.h>

/* Common nested types */
extern const struct nla_policy drbd_connection_info_nl_policy[DRBD_A_CONNECTION_INFO_CONN_ROLE + 1];
extern const struct nla_policy drbd_connection_statistics_nl_policy[DRBD_A_CONNECTION_STATISTICS_CONN_CONGESTED + 1];
extern const struct nla_policy drbd_detach_parms_nl_policy[DRBD_A_DETACH_PARMS_FORCE_DETACH + 1];
extern const struct nla_policy drbd_device_info_nl_policy[DRBD_A_DEVICE_INFO_DEV_DISK_STATE + 1];
extern const struct nla_policy drbd_device_statistics_nl_policy[DRBD_A_DEVICE_STATISTICS_HISTORY_UUIDS + 1];
extern const struct nla_policy drbd_disconnect_parms_nl_policy[DRBD_A_DISCONNECT_PARMS_FORCE_DISCONNECT + 1];
extern const struct nla_policy drbd_disk_conf_nl_policy[DRBD_A_DISK_CONF_DISABLE_WRITE_SAME + 1];
extern const struct nla_policy drbd_drbd_cfg_context_nl_policy[DRBD_A_DRBD_CFG_CONTEXT_CTX_PEER_ADDR + 1];
extern const struct nla_policy drbd_net_conf_nl_policy[DRBD_A_NET_CONF_SOCK_CHECK_TIMEO + 1];
extern const struct nla_policy drbd_new_c_uuid_parms_nl_policy[DRBD_A_NEW_C_UUID_PARMS_CLEAR_BM + 1];
extern const struct nla_policy drbd_peer_device_info_nl_policy[DRBD_A_PEER_DEVICE_INFO_PEER_RESYNC_SUSP_DEPENDENCY + 1];
extern const struct nla_policy drbd_peer_device_statistics_nl_policy[DRBD_A_PEER_DEVICE_STATISTICS_PEER_DEV_FLAGS + 1];
extern const struct nla_policy drbd_res_opts_nl_policy[DRBD_A_RES_OPTS_ON_NO_DATA + 1];
extern const struct nla_policy drbd_resize_parms_nl_policy[DRBD_A_RESIZE_PARMS_AL_STRIPE_SIZE + 1];
extern const struct nla_policy drbd_resource_info_nl_policy[DRBD_A_RESOURCE_INFO_RES_SUSP_FEN + 1];
extern const struct nla_policy drbd_resource_statistics_nl_policy[DRBD_A_RESOURCE_STATISTICS_RES_STAT_WRITE_ORDERING + 1];
extern const struct nla_policy drbd_set_role_parms_nl_policy[DRBD_A_SET_ROLE_PARMS_ASSUME_UPTODATE + 1];
extern const struct nla_policy drbd_start_ov_parms_nl_policy[DRBD_A_START_OV_PARMS_OV_STOP_SECTOR + 1];

/* Ops table for drbd */
extern const struct genl_split_ops drbd_nl_ops[32];

int drbd_pre_doit(const struct genl_split_ops *ops, struct sk_buff *skb,
		  struct genl_info *info);
void
drbd_post_doit(const struct genl_split_ops *ops, struct sk_buff *skb,
	       struct genl_info *info);
int drbd_adm_dump_devices_done(struct netlink_callback *cb);
int drbd_adm_dump_connections_done(struct netlink_callback *cb);
int drbd_adm_dump_peer_devices_done(struct netlink_callback *cb);

int drbd_nl_get_status_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_get_status_dumpit(struct sk_buff *skb, struct netlink_callback *cb);
int drbd_nl_new_minor_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_del_minor_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_new_resource_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_del_resource_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_resource_opts_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_connect_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_disconnect_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_attach_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_resize_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_primary_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_secondary_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_new_c_uuid_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_start_ov_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_detach_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_invalidate_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_inval_peer_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_pause_sync_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_resume_sync_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_suspend_io_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_resume_io_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_outdate_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_get_timeout_type_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_down_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_chg_disk_opts_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_chg_net_opts_doit(struct sk_buff *skb, struct genl_info *info);
int drbd_nl_get_resources_dumpit(struct sk_buff *skb,
				 struct netlink_callback *cb);
int drbd_nl_get_devices_dumpit(struct sk_buff *skb,
			       struct netlink_callback *cb);
int drbd_nl_get_connections_dumpit(struct sk_buff *skb,
				   struct netlink_callback *cb);
int drbd_nl_get_peer_devices_dumpit(struct sk_buff *skb,
				    struct netlink_callback *cb);
int drbd_nl_get_initial_state_dumpit(struct sk_buff *skb,
				     struct netlink_callback *cb);

enum {
	DRBD_NLGRP_EVENTS,
};

struct drbd_cfg_reply {
	char info_text[0];
	__u32 info_text_len;
};

struct drbd_cfg_context {
	__u32 ctx_volume;
	char ctx_resource_name[128];
	__u32 ctx_resource_name_len;
	char ctx_my_addr[128];
	__u32 ctx_my_addr_len;
	char ctx_peer_addr[128];
	__u32 ctx_peer_addr_len;
};

struct disk_conf {
	char backing_dev[128];
	__u32 backing_dev_len;
	char meta_dev[128];
	__u32 meta_dev_len;
	__s32 meta_dev_idx;
	__u64 disk_size;
	__u32 max_bio_bvecs;
	__u32 on_io_error;
	__u32 fencing;
	__u32 resync_rate;
	__s32 resync_after;
	__u32 al_extents;
	__u32 c_plan_ahead;
	__u32 c_delay_target;
	__u32 c_fill_target;
	__u32 c_max_rate;
	__u32 c_min_rate;
	unsigned char disk_barrier;
	unsigned char disk_flushes;
	unsigned char disk_drain;
	unsigned char md_flushes;
	__u32 disk_timeout;
	__u32 read_balancing;
	unsigned char al_updates;
	unsigned char discard_zeroes_if_aligned;
	__u32 rs_discard_granularity;
	unsigned char disable_write_same;
};

struct res_opts {
	char cpu_mask[DRBD_CPU_MASK_SIZE];
	__u32 cpu_mask_len;
	__u32 on_no_data;
};

struct net_conf {
	char shared_secret[SHARED_SECRET_MAX];
	__u32 shared_secret_len;
	char cram_hmac_alg[SHARED_SECRET_MAX];
	__u32 cram_hmac_alg_len;
	char integrity_alg[SHARED_SECRET_MAX];
	__u32 integrity_alg_len;
	char verify_alg[SHARED_SECRET_MAX];
	__u32 verify_alg_len;
	char csums_alg[SHARED_SECRET_MAX];
	__u32 csums_alg_len;
	__u32 wire_protocol;
	__u32 connect_int;
	__u32 timeout;
	__u32 ping_int;
	__u32 ping_timeo;
	__u32 sndbuf_size;
	__u32 rcvbuf_size;
	__u32 ko_count;
	__u32 max_buffers;
	__u32 max_epoch_size;
	__u32 unplug_watermark;
	__u32 after_sb_0p;
	__u32 after_sb_1p;
	__u32 after_sb_2p;
	__u32 rr_conflict;
	__u32 on_congestion;
	__u32 cong_fill;
	__u32 cong_extents;
	unsigned char two_primaries;
	unsigned char discard_my_data;
	unsigned char tcp_cork;
	unsigned char always_asbp;
	unsigned char tentative;
	unsigned char use_rle;
	unsigned char csums_after_crash_only;
	__u32 sock_check_timeo;
};

struct set_role_parms {
	unsigned char assume_uptodate;
};

struct resize_parms {
	__u64 resize_size;
	unsigned char resize_force;
	unsigned char no_resync;
	__u32 al_stripes;
	__u32 al_stripe_size;
};

struct state_info {
	__u32 sib_reason;
	__u32 current_state;
	__u64 capacity;
	__u64 ed_uuid;
	__u32 prev_state;
	__u32 new_state;
	char uuids[DRBD_NL_UUIDS_SIZE];
	__u32 uuids_len;
	__u32 disk_flags;
	__u64 bits_total;
	__u64 bits_oos;
	__u64 bits_rs_total;
	__u64 bits_rs_failed;
	char helper[32];
	__u32 helper_len;
	__u32 helper_exit_code;
	__u64 send_cnt;
	__u64 recv_cnt;
	__u64 read_cnt;
	__u64 writ_cnt;
	__u64 al_writ_cnt;
	__u64 bm_writ_cnt;
	__u32 ap_bio_cnt;
	__u32 ap_pending_cnt;
	__u32 rs_pending_cnt;
};

struct start_ov_parms {
	__u64 ov_start_sector;
	__u64 ov_stop_sector;
};

struct new_c_uuid_parms {
	unsigned char clear_bm;
};

struct timeout_parms {
	__u32 timeout_type;
};

struct disconnect_parms {
	unsigned char force_disconnect;
};

struct detach_parms {
	unsigned char force_detach;
};

struct resource_info {
	__u32 res_role;
	unsigned char res_susp;
	unsigned char res_susp_nod;
	unsigned char res_susp_fen;
};

struct device_info {
	__u32 dev_disk_state;
};

struct connection_info {
	__u32 conn_connection_state;
	__u32 conn_role;
};

struct peer_device_info {
	__u32 peer_repl_state;
	__u32 peer_disk_state;
	__u32 peer_resync_susp_user;
	__u32 peer_resync_susp_peer;
	__u32 peer_resync_susp_dependency;
};

struct resource_statistics {
	__u32 res_stat_write_ordering;
};

struct device_statistics {
	__u64 dev_size;
	__u64 dev_read;
	__u64 dev_write;
	__u64 dev_al_writes;
	__u64 dev_bm_writes;
	__u32 dev_upper_pending;
	__u32 dev_lower_pending;
	unsigned char dev_upper_blocked;
	unsigned char dev_lower_blocked;
	unsigned char dev_al_suspended;
	__u64 dev_exposed_data_uuid;
	__u64 dev_current_uuid;
	__u32 dev_disk_flags;
	char history_uuids[DRBD_NL_HISTORY_UUIDS_SIZE];
	__u32 history_uuids_len;
};

struct connection_statistics {
	unsigned char conn_congested;
};

struct peer_device_statistics {
	__u64 peer_dev_received;
	__u64 peer_dev_sent;
	__u32 peer_dev_pending;
	__u32 peer_dev_unacked;
	__u64 peer_dev_out_of_sync;
	__u64 peer_dev_resync_failed;
	__u64 peer_dev_bitmap_uuid;
	__u32 peer_dev_flags;
};

struct drbd_notification_header {
	__u32 nh_type;
};

struct drbd_helper_info {
	char helper_name[32];
	__u32 helper_name_len;
	__u32 helper_status;
};

int drbd_cfg_reply_to_skb(struct sk_buff *skb, struct drbd_cfg_reply *s);

int drbd_cfg_context_from_attrs(struct drbd_cfg_context *s, struct genl_info *info);
int drbd_cfg_context_ntb_from_attrs(struct nlattr ***ret_nested_attribute_table, struct genl_info *info);
int drbd_cfg_context_to_skb(struct sk_buff *skb, struct drbd_cfg_context *s);

int disk_conf_from_attrs(struct disk_conf *s, struct genl_info *info);
int disk_conf_ntb_from_attrs(struct nlattr ***ret_nested_attribute_table, struct genl_info *info);
int disk_conf_to_skb(struct sk_buff *skb, struct disk_conf *s);
void set_disk_conf_defaults(struct disk_conf *x);

int res_opts_from_attrs(struct res_opts *s, struct genl_info *info);
int res_opts_ntb_from_attrs(struct nlattr ***ret_nested_attribute_table, struct genl_info *info);
int res_opts_to_skb(struct sk_buff *skb, struct res_opts *s);
void set_res_opts_defaults(struct res_opts *x);

int net_conf_from_attrs(struct net_conf *s, struct genl_info *info);
int net_conf_ntb_from_attrs(struct nlattr ***ret_nested_attribute_table, struct genl_info *info);
int net_conf_to_skb(struct sk_buff *skb, struct net_conf *s);
void set_net_conf_defaults(struct net_conf *x);

int set_role_parms_from_attrs(struct set_role_parms *s, struct genl_info *info);
int set_role_parms_ntb_from_attrs(struct nlattr ***ret_nested_attribute_table, struct genl_info *info);
int set_role_parms_to_skb(struct sk_buff *skb, struct set_role_parms *s);

int resize_parms_from_attrs(struct resize_parms *s, struct genl_info *info);
int resize_parms_ntb_from_attrs(struct nlattr ***ret_nested_attribute_table, struct genl_info *info);
int resize_parms_to_skb(struct sk_buff *skb, struct resize_parms *s);
void set_resize_parms_defaults(struct resize_parms *x);

int state_info_to_skb(struct sk_buff *skb, struct state_info *s);

int start_ov_parms_from_attrs(struct start_ov_parms *s, struct genl_info *info);
int start_ov_parms_ntb_from_attrs(struct nlattr ***ret_nested_attribute_table, struct genl_info *info);
int start_ov_parms_to_skb(struct sk_buff *skb, struct start_ov_parms *s);

int new_c_uuid_parms_from_attrs(struct new_c_uuid_parms *s, struct genl_info *info);
int new_c_uuid_parms_ntb_from_attrs(struct nlattr ***ret_nested_attribute_table, struct genl_info *info);
int new_c_uuid_parms_to_skb(struct sk_buff *skb, struct new_c_uuid_parms *s);

int timeout_parms_to_skb(struct sk_buff *skb, struct timeout_parms *s);

int disconnect_parms_from_attrs(struct disconnect_parms *s, struct genl_info *info);
int disconnect_parms_ntb_from_attrs(struct nlattr ***ret_nested_attribute_table, struct genl_info *info);
int disconnect_parms_to_skb(struct sk_buff *skb, struct disconnect_parms *s);

int detach_parms_from_attrs(struct detach_parms *s, struct genl_info *info);
int detach_parms_ntb_from_attrs(struct nlattr ***ret_nested_attribute_table, struct genl_info *info);
int detach_parms_to_skb(struct sk_buff *skb, struct detach_parms *s);

int resource_info_from_attrs(struct resource_info *s, struct genl_info *info);
int resource_info_ntb_from_attrs(struct nlattr ***ret_nested_attribute_table, struct genl_info *info);
int resource_info_to_skb(struct sk_buff *skb, struct resource_info *s);

int device_info_from_attrs(struct device_info *s, struct genl_info *info);
int device_info_ntb_from_attrs(struct nlattr ***ret_nested_attribute_table, struct genl_info *info);
int device_info_to_skb(struct sk_buff *skb, struct device_info *s);

int connection_info_from_attrs(struct connection_info *s, struct genl_info *info);
int connection_info_ntb_from_attrs(struct nlattr ***ret_nested_attribute_table, struct genl_info *info);
int connection_info_to_skb(struct sk_buff *skb, struct connection_info *s);

int peer_device_info_from_attrs(struct peer_device_info *s, struct genl_info *info);
int peer_device_info_ntb_from_attrs(struct nlattr ***ret_nested_attribute_table, struct genl_info *info);
int peer_device_info_to_skb(struct sk_buff *skb, struct peer_device_info *s);

int resource_statistics_from_attrs(struct resource_statistics *s, struct genl_info *info);
int resource_statistics_ntb_from_attrs(struct nlattr ***ret_nested_attribute_table, struct genl_info *info);
int resource_statistics_to_skb(struct sk_buff *skb, struct resource_statistics *s);

int device_statistics_from_attrs(struct device_statistics *s, struct genl_info *info);
int device_statistics_ntb_from_attrs(struct nlattr ***ret_nested_attribute_table, struct genl_info *info);
int device_statistics_to_skb(struct sk_buff *skb, struct device_statistics *s);

int connection_statistics_from_attrs(struct connection_statistics *s, struct genl_info *info);
int connection_statistics_ntb_from_attrs(struct nlattr ***ret_nested_attribute_table, struct genl_info *info);
int connection_statistics_to_skb(struct sk_buff *skb, struct connection_statistics *s);

int peer_device_statistics_from_attrs(struct peer_device_statistics *s, struct genl_info *info);
int peer_device_statistics_ntb_from_attrs(struct nlattr ***ret_nested_attribute_table, struct genl_info *info);
int peer_device_statistics_to_skb(struct sk_buff *skb, struct peer_device_statistics *s);

int drbd_notification_header_to_skb(struct sk_buff *skb, struct drbd_notification_header *s);

int drbd_helper_info_to_skb(struct sk_buff *skb, struct drbd_helper_info *s);

#endif /* _LINUX_DRBD_GEN_H */
