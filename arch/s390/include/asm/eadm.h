#ifndef _ASM_S390_EADM_H
#define _ASM_S390_EADM_H

#include <linux/types.h>
#include <linux/device.h>

struct arqb {
	u64 data;
	u16 fmt:4;
	u16:12;
	u16 cmd_code;
	u16:16;
	u16 msb_count;
	u32 reserved[12];
} __packed;

#define ARQB_CMD_MOVE	1

struct arsb {
	u16 fmt:4;
	u32:28;
	u8 ef;
	u8:8;
	u8 ecbi;
	u8:8;
	u8 fvf;
	u16:16;
	u8 eqc;
	u32:32;
	u64 fail_msb;
	u64 fail_aidaw;
	u64 fail_ms;
	u64 fail_scm;
	u32 reserved[4];
} __packed;

#define EQC_WR_PROHIBIT 22

struct msb {
	u8 fmt:4;
	u8 oc:4;
	u8 flags;
	u16:12;
	u16 bs:4;
	u32 blk_count;
	u64 data_addr;
	u64 scm_addr;
	u64:64;
} __packed;

struct aidaw {
	u8 flags;
	u32 :24;
	u32 :32;
	u64 data_addr;
} __packed;

#define MSB_OC_CLEAR	0
#define MSB_OC_READ	1
#define MSB_OC_WRITE	2
#define MSB_OC_RELEASE	3

#define MSB_FLAG_BNM	0x80
#define MSB_FLAG_IDA	0x40

#define MSB_BS_4K	0
#define MSB_BS_1M	1

#define AOB_NR_MSB	124

struct aob {
	struct arqb request;
	struct arsb response;
	struct msb msb[AOB_NR_MSB];
} __packed __aligned(PAGE_SIZE);

struct aob_rq_header {
	struct scm_device *scmdev;
	char data[0];
};

struct scm_device {
	u64 address;
	u64 size;
	unsigned int nr_max_block;
	struct device dev;
	struct {
		unsigned int persistence:4;
		unsigned int oper_state:4;
		unsigned int data_state:4;
		unsigned int rank:4;
		unsigned int release:1;
		unsigned int res_id:8;
	} __packed attrs;
};

#define OP_STATE_GOOD		1
#define OP_STATE_TEMP_ERR	2
#define OP_STATE_PERM_ERR	3

enum scm_event {SCM_CHANGE};

struct scm_driver {
	struct device_driver drv;
	int (*probe) (struct scm_device *scmdev);
	int (*remove) (struct scm_device *scmdev);
	void (*notify) (struct scm_device *scmdev, enum scm_event event);
	void (*handler) (struct scm_device *scmdev, void *data, int error);
};

int scm_driver_register(struct scm_driver *scmdrv);
void scm_driver_unregister(struct scm_driver *scmdrv);

int scm_start_aob(struct aob *aob);
void scm_irq_handler(struct aob *aob, int error);

struct eadm_ops {
	int (*eadm_start) (struct aob *aob);
	struct module *owner;
};

int scm_get_ref(void);
void scm_put_ref(void);

void register_eadm_ops(struct eadm_ops *ops);
void unregister_eadm_ops(struct eadm_ops *ops);

#endif /* _ASM_S390_EADM_H */
