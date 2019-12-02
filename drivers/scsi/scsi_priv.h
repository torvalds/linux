/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SCSI_PRIV_H
#define _SCSI_PRIV_H

#include <linux/device.h>
#include <linux/async.h>
#include <scsi/scsi_device.h>

struct request_queue;
struct request;
struct scsi_cmnd;
struct scsi_device;
struct scsi_target;
struct scsi_host_template;
struct Scsi_Host;
struct scsi_nl_hdr;


/*
 * Scsi Error Handler Flags
 */
#define SCSI_EH_ABORT_SCHEDULED	0x0002	/* Abort has been scheduled */

#define SCSI_SENSE_VALID(scmd) \
	(((scmd)->sense_buffer[0] & 0x70) == 0x70)

/* hosts.c */
extern int scsi_init_hosts(void);
extern void scsi_exit_hosts(void);

/* scsi.c */
extern bool scsi_use_blk_mq;
int scsi_init_sense_cache(struct Scsi_Host *shost);
void scsi_init_command(struct scsi_device *dev, struct scsi_cmnd *cmd);
#ifdef CONFIG_SCSI_LOGGING
void scsi_log_send(struct scsi_cmnd *cmd);
void scsi_log_completion(struct scsi_cmnd *cmd, int disposition);
#else
static inline void scsi_log_send(struct scsi_cmnd *cmd) 
	{ };
static inline void scsi_log_completion(struct scsi_cmnd *cmd, int disposition)
	{ };
#endif

/* scsi_devinfo.c */

/* list of keys for the lists */
enum scsi_devinfo_key {
	SCSI_DEVINFO_GLOBAL = 0,
	SCSI_DEVINFO_SPI,
};

extern blist_flags_t scsi_get_device_flags(struct scsi_device *sdev,
					   const unsigned char *vendor,
					   const unsigned char *model);
extern blist_flags_t scsi_get_device_flags_keyed(struct scsi_device *sdev,
						 const unsigned char *vendor,
						 const unsigned char *model,
						 enum scsi_devinfo_key key);
extern int scsi_dev_info_list_add_keyed(int compatible, char *vendor,
					char *model, char *strflags,
					blist_flags_t flags,
					enum scsi_devinfo_key key);
extern int scsi_dev_info_list_del_keyed(char *vendor, char *model,
					enum scsi_devinfo_key key);
extern int scsi_dev_info_add_list(enum scsi_devinfo_key key, const char *name);
extern int scsi_dev_info_remove_list(enum scsi_devinfo_key key);

extern int __init scsi_init_devinfo(void);
extern void scsi_exit_devinfo(void);

/* scsi_error.c */
extern void scmd_eh_abort_handler(struct work_struct *work);
extern enum blk_eh_timer_return scsi_times_out(struct request *req);
extern int scsi_error_handler(void *host);
extern int scsi_decide_disposition(struct scsi_cmnd *cmd);
extern void scsi_eh_wakeup(struct Scsi_Host *shost);
extern void scsi_eh_scmd_add(struct scsi_cmnd *);
void scsi_eh_ready_devs(struct Scsi_Host *shost,
			struct list_head *work_q,
			struct list_head *done_q);
int scsi_eh_get_sense(struct list_head *work_q,
		      struct list_head *done_q);
int scsi_noretry_cmd(struct scsi_cmnd *scmd);

/* scsi_lib.c */
extern void scsi_add_cmd_to_list(struct scsi_cmnd *cmd);
extern void scsi_del_cmd_from_list(struct scsi_cmnd *cmd);
extern int scsi_maybe_unblock_host(struct scsi_device *sdev);
extern void scsi_device_unbusy(struct scsi_device *sdev, struct scsi_cmnd *cmd);
extern void scsi_queue_insert(struct scsi_cmnd *cmd, int reason);
extern void scsi_io_completion(struct scsi_cmnd *, unsigned int);
extern void scsi_run_host_queues(struct Scsi_Host *shost);
extern void scsi_requeue_run_queue(struct work_struct *work);
extern struct request_queue *scsi_mq_alloc_queue(struct scsi_device *sdev);
extern void scsi_start_queue(struct scsi_device *sdev);
extern int scsi_mq_setup_tags(struct Scsi_Host *shost);
extern void scsi_mq_destroy_tags(struct Scsi_Host *shost);
extern int scsi_init_queue(void);
extern void scsi_exit_queue(void);
extern void scsi_evt_thread(struct work_struct *work);
struct request_queue;
struct request;

/* scsi_proc.c */
#ifdef CONFIG_SCSI_PROC_FS
extern void scsi_proc_hostdir_add(struct scsi_host_template *);
extern void scsi_proc_hostdir_rm(struct scsi_host_template *);
extern void scsi_proc_host_add(struct Scsi_Host *);
extern void scsi_proc_host_rm(struct Scsi_Host *);
extern int scsi_init_procfs(void);
extern void scsi_exit_procfs(void);
#else
# define scsi_proc_hostdir_add(sht)	do { } while (0)
# define scsi_proc_hostdir_rm(sht)	do { } while (0)
# define scsi_proc_host_add(shost)	do { } while (0)
# define scsi_proc_host_rm(shost)	do { } while (0)
# define scsi_init_procfs()		(0)
# define scsi_exit_procfs()		do { } while (0)
#endif /* CONFIG_PROC_FS */

/* scsi_scan.c */
extern char scsi_scan_type[];
extern int scsi_complete_async_scans(void);
extern int scsi_scan_host_selected(struct Scsi_Host *, unsigned int,
				   unsigned int, u64, enum scsi_scan_mode);
extern void scsi_forget_host(struct Scsi_Host *);
extern void scsi_rescan_device(struct device *);

/* scsi_sysctl.c */
#ifdef CONFIG_SYSCTL
extern int scsi_init_sysctl(void);
extern void scsi_exit_sysctl(void);
#else
# define scsi_init_sysctl()		(0)
# define scsi_exit_sysctl()		do { } while (0)
#endif /* CONFIG_SYSCTL */

/* scsi_sysfs.c */
extern int scsi_sysfs_add_sdev(struct scsi_device *);
extern int scsi_sysfs_add_host(struct Scsi_Host *);
extern int scsi_sysfs_register(void);
extern void scsi_sysfs_unregister(void);
extern void scsi_sysfs_device_initialize(struct scsi_device *);
extern int scsi_sysfs_target_initialize(struct scsi_device *);
extern struct scsi_transport_template blank_transport_template;
extern void __scsi_remove_device(struct scsi_device *);

extern struct bus_type scsi_bus_type;
extern const struct attribute_group *scsi_sysfs_shost_attr_groups[];

/* scsi_netlink.c */
#ifdef CONFIG_SCSI_NETLINK
extern void scsi_netlink_init(void);
extern void scsi_netlink_exit(void);
extern struct sock *scsi_nl_sock;
#else
static inline void scsi_netlink_init(void) {}
static inline void scsi_netlink_exit(void) {}
#endif

/* scsi_pm.c */
#ifdef CONFIG_PM
extern const struct dev_pm_ops scsi_bus_pm_ops;

extern void scsi_autopm_get_target(struct scsi_target *);
extern void scsi_autopm_put_target(struct scsi_target *);
extern int scsi_autopm_get_host(struct Scsi_Host *);
extern void scsi_autopm_put_host(struct Scsi_Host *);
#else
static inline void scsi_autopm_get_target(struct scsi_target *t) {}
static inline void scsi_autopm_put_target(struct scsi_target *t) {}
static inline int scsi_autopm_get_host(struct Scsi_Host *h) { return 0; }
static inline void scsi_autopm_put_host(struct Scsi_Host *h) {}
#endif /* CONFIG_PM */

extern struct async_domain scsi_sd_pm_domain;

/* scsi_dh.c */
#ifdef CONFIG_SCSI_DH
void scsi_dh_add_device(struct scsi_device *sdev);
void scsi_dh_release_device(struct scsi_device *sdev);
#else
static inline void scsi_dh_add_device(struct scsi_device *sdev) { }
static inline void scsi_dh_release_device(struct scsi_device *sdev) { }
#endif

/* 
 * internal scsi timeout functions: for use by mid-layer and transport
 * classes.
 */

#define SCSI_DEVICE_BLOCK_MAX_TIMEOUT	600	/* units in seconds */

#endif /* _SCSI_PRIV_H */
