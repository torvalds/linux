#ifndef _SCSI_PRIV_H
#define _SCSI_PRIV_H

#include <linux/config.h>
#include <linux/device.h>

struct request_queue;
struct scsi_cmnd;
struct scsi_device;
struct scsi_host_template;
struct scsi_request;
struct Scsi_Host;


/*
 * Magic values for certain scsi structs. Shouldn't ever be used.
 */
#define SCSI_CMND_MAGIC		0xE25C23A5
#define SCSI_REQ_MAGIC		0x75F6D354

/*
 * Scsi Error Handler Flags
 */
#define SCSI_EH_CANCEL_CMD	0x0001	/* Cancel this cmd */

#define SCSI_SENSE_VALID(scmd) \
	(((scmd)->sense_buffer[0] & 0x70) == 0x70)

/*
 * Special value for scanning to specify scanning or rescanning of all
 * possible channels, (target) ids, or luns on a given shost.
 */
#define SCAN_WILD_CARD	~0

/* hosts.c */
extern int scsi_init_hosts(void);
extern void scsi_exit_hosts(void);

/* scsi.c */
extern int scsi_dispatch_cmd(struct scsi_cmnd *cmd);
extern int scsi_setup_command_freelist(struct Scsi_Host *shost);
extern void scsi_destroy_command_freelist(struct Scsi_Host *shost);
extern int scsi_insert_special_req(struct scsi_request *sreq, int);
extern void scsi_init_cmd_from_req(struct scsi_cmnd *cmd,
		struct scsi_request *sreq);
extern void __scsi_release_request(struct scsi_request *sreq);
extern void __scsi_done(struct scsi_cmnd *cmd);
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
extern int scsi_get_device_flags(struct scsi_device *sdev,
				 unsigned char *vendor, unsigned char *model);
extern int __init scsi_init_devinfo(void);
extern void scsi_exit_devinfo(void);

/* scsi_error.c */
extern void scsi_add_timer(struct scsi_cmnd *, int,
		void (*)(struct scsi_cmnd *));
extern int scsi_delete_timer(struct scsi_cmnd *);
extern void scsi_times_out(struct scsi_cmnd *cmd);
extern int scsi_error_handler(void *host);
extern int scsi_decide_disposition(struct scsi_cmnd *cmd);
extern void scsi_eh_wakeup(struct Scsi_Host *shost);
extern int scsi_eh_scmd_add(struct scsi_cmnd *, int);

/* scsi_lib.c */
extern int scsi_maybe_unblock_host(struct scsi_device *sdev);
extern void scsi_setup_cmd_retry(struct scsi_cmnd *cmd);
extern void scsi_device_unbusy(struct scsi_device *sdev);
extern int scsi_queue_insert(struct scsi_cmnd *cmd, int reason);
extern void scsi_next_command(struct scsi_cmnd *cmd);
extern void scsi_run_host_queues(struct Scsi_Host *shost);
extern struct request_queue *scsi_alloc_queue(struct scsi_device *sdev);
extern void scsi_free_queue(struct request_queue *q);
extern int scsi_init_queue(void);
extern void scsi_exit_queue(void);

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
extern int scsi_scan_host_selected(struct Scsi_Host *, unsigned int,
				   unsigned int, unsigned int, int);
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

/* 
 * internal scsi timeout functions: for use by mid-layer and transport
 * classes.
 */

#define SCSI_DEVICE_BLOCK_MAX_TIMEOUT	(HZ*60)
extern int scsi_internal_device_block(struct scsi_device *sdev);
extern int scsi_internal_device_unblock(struct scsi_device *sdev);

#endif /* _SCSI_PRIV_H */
