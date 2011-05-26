#ifndef TARGET_CORE_STAT_H
#define TARGET_CORE_STAT_H

extern void target_stat_setup_dev_default_groups(struct se_subsystem_dev *);
extern void target_stat_setup_port_default_groups(struct se_lun *);
extern void target_stat_setup_mappedlun_default_groups(struct se_lun_acl *);

#endif   /*** TARGET_CORE_STAT_H ***/
