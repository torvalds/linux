struct scsi_cmnd;
struct scsi_lun;
struct Scsi_Host;
struct task_struct;

/* tmp - will replace with SCSI logging stuff */
#define eprintk(fmt, args...)					\
do {								\
	printk("%s(%d) " fmt, __FUNCTION__, __LINE__, ##args);	\
} while (0)

#define dprintk(fmt, args...)
/* #define dprintk eprintk */

extern void scsi_tgt_if_exit(void);
extern int scsi_tgt_if_init(void);

extern int scsi_tgt_uspace_send_cmd(struct scsi_cmnd *cmd, u64 it_nexus_id,
				    struct scsi_lun *lun, u64 tag);
extern int scsi_tgt_uspace_send_status(struct scsi_cmnd *cmd, u64 it_nexus_id,
				       u64 tag);
extern int scsi_tgt_kspace_exec(int host_no, u64 it_nexus_id, int result, u64 tag,
				unsigned long uaddr, u32 len,
				unsigned long sense_uaddr, u32 sense_len, u8 rw);
extern int scsi_tgt_uspace_send_tsk_mgmt(int host_no, u64 it_nexus_id,
					 int function, u64 tag,
					 struct scsi_lun *scsilun, void *data);
extern int scsi_tgt_kspace_tsk_mgmt(int host_no, u64 it_nexus_id,
				    u64 mid, int result);
extern int scsi_tgt_uspace_send_it_nexus_request(int host_no, u64 it_nexus_id,
						 int function, char *initiator);
extern int scsi_tgt_kspace_it_nexus_rsp(int host_no, u64 it_nexus_id, int result);
