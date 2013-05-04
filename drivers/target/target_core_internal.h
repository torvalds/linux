#ifndef TARGET_CORE_INTERNAL_H
#define TARGET_CORE_INTERNAL_H

/* target_core_alua.c */
extern struct t10_alua_lu_gp *default_lu_gp;

/* target_core_cdb.c */
int	target_emulate_inquiry(struct se_task *task);
int	target_emulate_readcapacity(struct se_task *task);
int	target_emulate_readcapacity_16(struct se_task *task);
int	target_emulate_modesense(struct se_task *task);
int	target_emulate_request_sense(struct se_task *task);
int	target_emulate_unmap(struct se_task *task);
int	target_emulate_write_same(struct se_task *task);
int	target_emulate_synchronize_cache(struct se_task *task);
int	target_emulate_noop(struct se_task *task);

/* target_core_device.c */
struct se_dev_entry *core_get_se_deve_from_rtpi(struct se_node_acl *, u16);
int	core_free_device_list_for_node(struct se_node_acl *,
		struct se_portal_group *);
void	core_dec_lacl_count(struct se_node_acl *, struct se_cmd *);
void	core_update_device_list_access(u32, u32, struct se_node_acl *);
int	core_update_device_list_for_node(struct se_lun *, struct se_lun_acl *,
		u32, u32, struct se_node_acl *, struct se_portal_group *, int);
void	core_clear_lun_from_tpg(struct se_lun *, struct se_portal_group *);
int	core_dev_export(struct se_device *, struct se_portal_group *,
		struct se_lun *);
void	core_dev_unexport(struct se_device *, struct se_portal_group *,
		struct se_lun *);
int	target_report_luns(struct se_task *);
void	se_release_device_for_hba(struct se_device *);
void	se_release_vpd_for_dev(struct se_device *);
int	se_free_virtual_device(struct se_device *, struct se_hba *);
int	se_dev_check_online(struct se_device *);
int	se_dev_check_shutdown(struct se_device *);
void	se_dev_set_default_attribs(struct se_device *, struct se_dev_limits *);
int	se_dev_set_task_timeout(struct se_device *, u32);
int	se_dev_set_max_unmap_lba_count(struct se_device *, u32);
int	se_dev_set_max_unmap_block_desc_count(struct se_device *, u32);
int	se_dev_set_unmap_granularity(struct se_device *, u32);
int	se_dev_set_unmap_granularity_alignment(struct se_device *, u32);
int	se_dev_set_emulate_dpo(struct se_device *, int);
int	se_dev_set_emulate_fua_write(struct se_device *, int);
int	se_dev_set_emulate_fua_read(struct se_device *, int);
int	se_dev_set_emulate_write_cache(struct se_device *, int);
int	se_dev_set_emulate_ua_intlck_ctrl(struct se_device *, int);
int	se_dev_set_emulate_tas(struct se_device *, int);
int	se_dev_set_emulate_tpu(struct se_device *, int);
int	se_dev_set_emulate_tpws(struct se_device *, int);
int	se_dev_set_enforce_pr_isids(struct se_device *, int);
int	se_dev_set_is_nonrot(struct se_device *, int);
int	se_dev_set_emulate_rest_reord(struct se_device *dev, int);
int	se_dev_set_queue_depth(struct se_device *, u32);
int	se_dev_set_max_sectors(struct se_device *, u32);
int	se_dev_set_fabric_max_sectors(struct se_device *, u32);
int	se_dev_set_optimal_sectors(struct se_device *, u32);
int	se_dev_set_block_size(struct se_device *, u32);
struct se_lun *core_dev_add_lun(struct se_portal_group *, struct se_hba *,
		struct se_device *, u32);
int	core_dev_del_lun(struct se_portal_group *, u32);
struct se_lun *core_get_lun_from_tpg(struct se_portal_group *, u32);
struct se_lun_acl *core_dev_init_initiator_node_lun_acl(struct se_portal_group *,
		struct se_node_acl *, u32, int *);
int	core_dev_add_initiator_node_lun_acl(struct se_portal_group *,
		struct se_lun_acl *, u32, u32);
int	core_dev_del_initiator_node_lun_acl(struct se_portal_group *,
		struct se_lun *, struct se_lun_acl *);
void	core_dev_free_initiator_node_lun_acl(struct se_portal_group *,
		struct se_lun_acl *lacl);
int	core_dev_setup_virtual_lun0(void);
void	core_dev_release_virtual_lun0(void);

/* target_core_hba.c */
struct se_hba *core_alloc_hba(const char *, u32, u32);
int	core_delete_hba(struct se_hba *);

/* target_core_tmr.c */
void	core_tmr_abort_task(struct se_device *, struct se_tmr_req *,
			struct se_session *);
int	core_tmr_lun_reset(struct se_device *, struct se_tmr_req *,
		struct list_head *, struct se_cmd *);

/* target_core_tpg.c */
extern struct se_device *g_lun0_dev;

struct se_node_acl *__core_tpg_get_initiator_node_acl(struct se_portal_group *tpg,
		const char *);
struct se_node_acl *core_tpg_get_initiator_node_acl(struct se_portal_group *tpg,
		unsigned char *);
void	core_tpg_add_node_to_devs(struct se_node_acl *, struct se_portal_group *);
void	core_tpg_wait_for_nacl_pr_ref(struct se_node_acl *);
struct se_lun *core_tpg_pre_addlun(struct se_portal_group *, u32);
int	core_tpg_post_addlun(struct se_portal_group *, struct se_lun *,
		u32, void *);
struct se_lun *core_tpg_pre_dellun(struct se_portal_group *, u32 unpacked_lun);
int	core_tpg_post_dellun(struct se_portal_group *, struct se_lun *);

/* target_core_transport.c */
extern struct kmem_cache *se_tmr_req_cache;

int	init_se_kmem_caches(void);
void	release_se_kmem_caches(void);
u32	scsi_get_new_index(scsi_index_t);
void	transport_subsystem_check_init(void);
void	transport_cmd_finish_abort(struct se_cmd *, int);
void	__transport_remove_task_from_execute_queue(struct se_task *,
		struct se_device *);
unsigned char *transport_dump_cmd_direction(struct se_cmd *);
void	transport_dump_dev_state(struct se_device *, char *, int *);
void	transport_dump_dev_info(struct se_device *, struct se_lun *,
		unsigned long long, char *, int *);
void	transport_dump_vpd_proto_id(struct t10_vpd *, unsigned char *, int);
int	transport_dump_vpd_assoc(struct t10_vpd *, unsigned char *, int);
int	transport_dump_vpd_ident_type(struct t10_vpd *, unsigned char *, int);
int	transport_dump_vpd_ident(struct t10_vpd *, unsigned char *, int);
bool	target_stop_task(struct se_task *task, unsigned long *flags);
int	transport_clear_lun_from_sessions(struct se_lun *);
void	transport_send_task_abort(struct se_cmd *);

/* target_core_stat.c */
void	target_stat_setup_dev_default_groups(struct se_subsystem_dev *);
void	target_stat_setup_port_default_groups(struct se_lun *);
void	target_stat_setup_mappedlun_default_groups(struct se_lun_acl *);

#endif /* TARGET_CORE_INTERNAL_H */
