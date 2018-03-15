/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#ifndef __QLA_DEF_H
#define __QLA_DEF_H

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/dmapool.h>
#include <linux/mempool.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/firmware.h>
#include <linux/aer.h>
#include <linux/mutex.h>
#include <linux/btree.h>

#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/scsi_bsg_fc.h>

#include "qla_bsg.h"
#include "qla_nx.h"
#include "qla_nx2.h"
#include "qla_nvme.h"
#define QLA2XXX_DRIVER_NAME	"qla2xxx"
#define QLA2XXX_APIDEV		"ql2xapidev"
#define QLA2XXX_MANUFACTURER	"QLogic Corporation"

/*
 * We have MAILBOX_REGISTER_COUNT sized arrays in a few places,
 * but that's fine as we don't look at the last 24 ones for
 * ISP2100 HBAs.
 */
#define MAILBOX_REGISTER_COUNT_2100	8
#define MAILBOX_REGISTER_COUNT_2200	24
#define MAILBOX_REGISTER_COUNT		32

#define QLA2200A_RISC_ROM_VER	4
#define FPM_2300		6
#define FPM_2310		7

#include "qla_settings.h"

#define MODE_DUAL (MODE_TARGET | MODE_INITIATOR)

/*
 * Data bit definitions
 */
#define BIT_0	0x1
#define BIT_1	0x2
#define BIT_2	0x4
#define BIT_3	0x8
#define BIT_4	0x10
#define BIT_5	0x20
#define BIT_6	0x40
#define BIT_7	0x80
#define BIT_8	0x100
#define BIT_9	0x200
#define BIT_10	0x400
#define BIT_11	0x800
#define BIT_12	0x1000
#define BIT_13	0x2000
#define BIT_14	0x4000
#define BIT_15	0x8000
#define BIT_16	0x10000
#define BIT_17	0x20000
#define BIT_18	0x40000
#define BIT_19	0x80000
#define BIT_20	0x100000
#define BIT_21	0x200000
#define BIT_22	0x400000
#define BIT_23	0x800000
#define BIT_24	0x1000000
#define BIT_25	0x2000000
#define BIT_26	0x4000000
#define BIT_27	0x8000000
#define BIT_28	0x10000000
#define BIT_29	0x20000000
#define BIT_30	0x40000000
#define BIT_31	0x80000000

#define LSB(x)	((uint8_t)(x))
#define MSB(x)	((uint8_t)((uint16_t)(x) >> 8))

#define LSW(x)	((uint16_t)(x))
#define MSW(x)	((uint16_t)((uint32_t)(x) >> 16))

#define LSD(x)	((uint32_t)((uint64_t)(x)))
#define MSD(x)	((uint32_t)((((uint64_t)(x)) >> 16) >> 16))

#define MAKE_HANDLE(x, y) ((uint32_t)((((uint32_t)(x)) << 16) | (uint32_t)(y)))

/*
 * I/O register
*/

#define RD_REG_BYTE(addr)		readb(addr)
#define RD_REG_WORD(addr)		readw(addr)
#define RD_REG_DWORD(addr)		readl(addr)
#define RD_REG_BYTE_RELAXED(addr)	readb_relaxed(addr)
#define RD_REG_WORD_RELAXED(addr)	readw_relaxed(addr)
#define RD_REG_DWORD_RELAXED(addr)	readl_relaxed(addr)
#define WRT_REG_BYTE(addr, data)	writeb(data,addr)
#define WRT_REG_WORD(addr, data)	writew(data,addr)
#define WRT_REG_DWORD(addr, data)	writel(data,addr)

/*
 * ISP83XX specific remote register addresses
 */
#define QLA83XX_LED_PORT0			0x00201320
#define QLA83XX_LED_PORT1			0x00201328
#define QLA83XX_IDC_DEV_STATE		0x22102384
#define QLA83XX_IDC_MAJOR_VERSION	0x22102380
#define QLA83XX_IDC_MINOR_VERSION	0x22102398
#define QLA83XX_IDC_DRV_PRESENCE	0x22102388
#define QLA83XX_IDC_DRIVER_ACK		0x2210238c
#define QLA83XX_IDC_CONTROL			0x22102390
#define QLA83XX_IDC_AUDIT			0x22102394
#define QLA83XX_IDC_LOCK_RECOVERY	0x2210239c
#define QLA83XX_DRIVER_LOCKID		0x22102104
#define QLA83XX_DRIVER_LOCK			0x8111c028
#define QLA83XX_DRIVER_UNLOCK		0x8111c02c
#define QLA83XX_FLASH_LOCKID		0x22102100
#define QLA83XX_FLASH_LOCK			0x8111c010
#define QLA83XX_FLASH_UNLOCK		0x8111c014
#define QLA83XX_DEV_PARTINFO1		0x221023e0
#define QLA83XX_DEV_PARTINFO2		0x221023e4
#define QLA83XX_FW_HEARTBEAT		0x221020b0
#define QLA83XX_PEG_HALT_STATUS1	0x221020a8
#define QLA83XX_PEG_HALT_STATUS2	0x221020ac

/* 83XX: Macros defining 8200 AEN Reason codes */
#define IDC_DEVICE_STATE_CHANGE BIT_0
#define IDC_PEG_HALT_STATUS_CHANGE BIT_1
#define IDC_NIC_FW_REPORTED_FAILURE BIT_2
#define IDC_HEARTBEAT_FAILURE BIT_3

/* 83XX: Macros defining 8200 AEN Error-levels */
#define ERR_LEVEL_NON_FATAL 0x1
#define ERR_LEVEL_RECOVERABLE_FATAL 0x2
#define ERR_LEVEL_UNRECOVERABLE_FATAL 0x4

/* 83XX: Macros for IDC Version */
#define QLA83XX_SUPP_IDC_MAJOR_VERSION 0x01
#define QLA83XX_SUPP_IDC_MINOR_VERSION 0x0

/* 83XX: Macros for scheduling dpc tasks */
#define QLA83XX_NIC_CORE_RESET 0x1
#define QLA83XX_IDC_STATE_HANDLER 0x2
#define QLA83XX_NIC_CORE_UNRECOVERABLE 0x3

/* 83XX: Macros for defining IDC-Control bits */
#define QLA83XX_IDC_RESET_DISABLED BIT_0
#define QLA83XX_IDC_GRACEFUL_RESET BIT_1

/* 83XX: Macros for different timeouts */
#define QLA83XX_IDC_INITIALIZATION_TIMEOUT 30
#define QLA83XX_IDC_RESET_ACK_TIMEOUT 10
#define QLA83XX_MAX_LOCK_RECOVERY_WAIT (2 * HZ)

/* 83XX: Macros for defining class in DEV-Partition Info register */
#define QLA83XX_CLASS_TYPE_NONE		0x0
#define QLA83XX_CLASS_TYPE_NIC		0x1
#define QLA83XX_CLASS_TYPE_FCOE		0x2
#define QLA83XX_CLASS_TYPE_ISCSI	0x3

/* 83XX: Macros for IDC Lock-Recovery stages */
#define IDC_LOCK_RECOVERY_STAGE1	0x1 /* Stage1: Intent for
					     * lock-recovery
					     */
#define IDC_LOCK_RECOVERY_STAGE2	0x2 /* Stage2: Perform lock-recovery */

/* 83XX: Macros for IDC Audit type */
#define IDC_AUDIT_TIMESTAMP		0x0 /* IDC-AUDIT: Record timestamp of
					     * dev-state change to NEED-RESET
					     * or NEED-QUIESCENT
					     */
#define IDC_AUDIT_COMPLETION		0x1 /* IDC-AUDIT: Record duration of
					     * reset-recovery completion is
					     * second
					     */
/* ISP2031: Values for laser on/off */
#define PORT_0_2031	0x00201340
#define PORT_1_2031	0x00201350
#define LASER_ON_2031	0x01800100
#define LASER_OFF_2031	0x01800180

/*
 * The ISP2312 v2 chip cannot access the FLASH/GPIO registers via MMIO in an
 * 133Mhz slot.
 */
#define RD_REG_WORD_PIO(addr)		(inw((unsigned long)addr))
#define WRT_REG_WORD_PIO(addr, data)	(outw(data,(unsigned long)addr))

/*
 * Fibre Channel device definitions.
 */
#define WWN_SIZE		8	/* Size of WWPN, WWN & WWNN */
#define MAX_FIBRE_DEVICES_2100	512
#define MAX_FIBRE_DEVICES_2400	2048
#define MAX_FIBRE_DEVICES_LOOP	128
#define MAX_FIBRE_DEVICES_MAX	MAX_FIBRE_DEVICES_2400
#define LOOPID_MAP_SIZE		(ha->max_fibre_devices)
#define MAX_FIBRE_LUNS  	0xFFFF
#define	MAX_HOST_COUNT		16

/*
 * Host adapter default definitions.
 */
#define MAX_BUSES		1  /* We only have one bus today */
#define MIN_LUNS		8
#define MAX_LUNS		MAX_FIBRE_LUNS
#define MAX_CMDS_PER_LUN	255

/*
 * Fibre Channel device definitions.
 */
#define SNS_LAST_LOOP_ID_2100	0xfe
#define SNS_LAST_LOOP_ID_2300	0x7ff

#define LAST_LOCAL_LOOP_ID	0x7d
#define SNS_FL_PORT		0x7e
#define FABRIC_CONTROLLER	0x7f
#define SIMPLE_NAME_SERVER	0x80
#define SNS_FIRST_LOOP_ID	0x81
#define MANAGEMENT_SERVER	0xfe
#define BROADCAST		0xff

/*
 * There is no correspondence between an N-PORT id and an AL_PA.  Therefore the
 * valid range of an N-PORT id is 0 through 0x7ef.
 */
#define NPH_LAST_HANDLE		0x7ef
#define NPH_MGMT_SERVER		0x7fa		/*  FFFFFA */
#define NPH_SNS			0x7fc		/*  FFFFFC */
#define NPH_FABRIC_CONTROLLER	0x7fd		/*  FFFFFD */
#define NPH_F_PORT		0x7fe		/*  FFFFFE */
#define NPH_IP_BROADCAST	0x7ff		/*  FFFFFF */

#define NPH_SNS_LID(ha)	(IS_FWI2_CAPABLE(ha) ? NPH_SNS : SIMPLE_NAME_SERVER)

#define MAX_CMDSZ	16		/* SCSI maximum CDB size. */
#include "qla_fw.h"

struct name_list_extended {
	struct get_name_list_extended *l;
	dma_addr_t		ldma;
	struct list_head 	fcports;	/* protect by sess_list */
	u32			size;
	u8			sent;
};
/*
 * Timeout timer counts in seconds
 */
#define PORT_RETRY_TIME			1
#define LOOP_DOWN_TIMEOUT		60
#define LOOP_DOWN_TIME			255	/* 240 */
#define	LOOP_DOWN_RESET			(LOOP_DOWN_TIME - 30)

#define DEFAULT_OUTSTANDING_COMMANDS	4096
#define MIN_OUTSTANDING_COMMANDS	128

/* ISP request and response entry counts (37-65535) */
#define REQUEST_ENTRY_CNT_2100		128	/* Number of request entries. */
#define REQUEST_ENTRY_CNT_2200		2048	/* Number of request entries. */
#define REQUEST_ENTRY_CNT_24XX		2048	/* Number of request entries. */
#define REQUEST_ENTRY_CNT_83XX		8192	/* Number of request entries. */
#define RESPONSE_ENTRY_CNT_83XX		4096	/* Number of response entries.*/
#define RESPONSE_ENTRY_CNT_2100		64	/* Number of response entries.*/
#define RESPONSE_ENTRY_CNT_2300		512	/* Number of response entries.*/
#define RESPONSE_ENTRY_CNT_MQ		128	/* Number of response entries.*/
#define ATIO_ENTRY_CNT_24XX		4096	/* Number of ATIO entries. */
#define RESPONSE_ENTRY_CNT_FX00		256     /* Number of response entries.*/
#define FW_DEF_EXCHANGES_CNT 2048

struct req_que;
struct qla_tgt_sess;

/*
 * SCSI Request Block
 */
struct srb_cmd {
	struct scsi_cmnd *cmd;		/* Linux SCSI command pkt */
	uint32_t request_sense_length;
	uint32_t fw_sense_length;
	uint8_t *request_sense_ptr;
	void *ctx;
};

/*
 * SRB flag definitions
 */
#define SRB_DMA_VALID			BIT_0	/* Command sent to ISP */
#define SRB_FCP_CMND_DMA_VALID		BIT_12	/* DIF: DSD List valid */
#define SRB_CRC_CTX_DMA_VALID		BIT_2	/* DIF: context DMA valid */
#define SRB_CRC_PROT_DMA_VALID		BIT_4	/* DIF: prot DMA valid */
#define SRB_CRC_CTX_DSD_VALID		BIT_5	/* DIF: dsd_list valid */

/* To identify if a srb is of T10-CRC type. @sp => srb_t pointer */
#define IS_PROT_IO(sp)	(sp->flags & SRB_CRC_CTX_DSD_VALID)

/*
 * 24 bit port ID type definition.
 */
typedef union {
	uint32_t b24 : 24;

	struct {
#ifdef __BIG_ENDIAN
		uint8_t domain;
		uint8_t area;
		uint8_t al_pa;
#elif defined(__LITTLE_ENDIAN)
		uint8_t al_pa;
		uint8_t area;
		uint8_t domain;
#else
#error "__BIG_ENDIAN or __LITTLE_ENDIAN must be defined!"
#endif
		uint8_t rsvd_1;
	} b;
} port_id_t;
#define INVALID_PORT_ID	0xFFFFFF

struct els_logo_payload {
	uint8_t opcode;
	uint8_t rsvd[3];
	uint8_t s_id[3];
	uint8_t rsvd1[1];
	uint8_t wwpn[WWN_SIZE];
};

struct ct_arg {
	void		*iocb;
	u16		nport_handle;
	dma_addr_t	req_dma;
	dma_addr_t	rsp_dma;
	u32		req_size;
	u32		rsp_size;
	void		*req;
	void		*rsp;
	port_id_t	id;
};

/*
 * SRB extensions.
 */
struct srb_iocb {
	union {
		struct {
			uint16_t flags;
#define SRB_LOGIN_RETRIED	BIT_0
#define SRB_LOGIN_COND_PLOGI	BIT_1
#define SRB_LOGIN_SKIP_PRLI	BIT_2
#define SRB_LOGIN_NVME_PRLI	BIT_3
			uint16_t data[2];
			u32 iop[2];
		} logio;
		struct {
#define ELS_DCMD_TIMEOUT 20
#define ELS_DCMD_LOGO 0x5
			uint32_t flags;
			uint32_t els_cmd;
			struct completion comp;
			struct els_logo_payload *els_logo_pyld;
			dma_addr_t els_logo_pyld_dma;
		} els_logo;
		struct {
			/*
			 * Values for flags field below are as
			 * defined in tsk_mgmt_entry struct
			 * for control_flags field in qla_fw.h.
			 */
			uint64_t lun;
			uint32_t flags;
			uint32_t data;
			struct completion comp;
			__le16 comp_status;
		} tmf;
		struct {
#define SRB_FXDISC_REQ_DMA_VALID	BIT_0
#define SRB_FXDISC_RESP_DMA_VALID	BIT_1
#define SRB_FXDISC_REQ_DWRD_VALID	BIT_2
#define SRB_FXDISC_RSP_DWRD_VALID	BIT_3
#define FXDISC_TIMEOUT 20
			uint8_t flags;
			uint32_t req_len;
			uint32_t rsp_len;
			void *req_addr;
			void *rsp_addr;
			dma_addr_t req_dma_handle;
			dma_addr_t rsp_dma_handle;
			__le32 adapter_id;
			__le32 adapter_id_hi;
			__le16 req_func_type;
			__le32 req_data;
			__le32 req_data_extra;
			__le32 result;
			__le32 seq_number;
			__le16 fw_flags;
			struct completion fxiocb_comp;
			__le32 reserved_0;
			uint8_t reserved_1;
		} fxiocb;
		struct {
			uint32_t cmd_hndl;
			__le16 comp_status;
			struct completion comp;
		} abt;
		struct ct_arg ctarg;
#define MAX_IOCB_MB_REG 28
#define SIZEOF_IOCB_MB_REG (MAX_IOCB_MB_REG * sizeof(uint16_t))
		struct {
			__le16 in_mb[MAX_IOCB_MB_REG];	/* from FW */
			__le16 out_mb[MAX_IOCB_MB_REG];	/* to FW */
			void *out, *in;
			dma_addr_t out_dma, in_dma;
			struct completion comp;
			int rc;
		} mbx;
		struct {
			struct imm_ntfy_from_isp *ntfy;
		} nack;
		struct {
			__le16 comp_status;
			uint16_t rsp_pyld_len;
			uint8_t	aen_op;
			void *desc;

			/* These are only used with ls4 requests */
			int cmd_len;
			int rsp_len;
			dma_addr_t cmd_dma;
			dma_addr_t rsp_dma;
			enum nvmefc_fcp_datadir dir;
			uint32_t dl;
			uint32_t timeout_sec;
			struct	list_head   entry;
		} nvme;
	} u;

	struct timer_list timer;
	void (*timeout)(void *);
};

/* Values for srb_ctx type */
#define SRB_LOGIN_CMD	1
#define SRB_LOGOUT_CMD	2
#define SRB_ELS_CMD_RPT 3
#define SRB_ELS_CMD_HST 4
#define SRB_CT_CMD	5
#define SRB_ADISC_CMD	6
#define SRB_TM_CMD	7
#define SRB_SCSI_CMD	8
#define SRB_BIDI_CMD	9
#define SRB_FXIOCB_DCMD	10
#define SRB_FXIOCB_BCMD	11
#define SRB_ABT_CMD	12
#define SRB_ELS_DCMD	13
#define SRB_MB_IOCB	14
#define SRB_CT_PTHRU_CMD 15
#define SRB_NACK_PLOGI	16
#define SRB_NACK_PRLI	17
#define SRB_NACK_LOGO	18
#define SRB_NVME_CMD	19
#define SRB_NVME_LS	20
#define SRB_PRLI_CMD	21

enum {
	TYPE_SRB,
	TYPE_TGT_CMD,
};

typedef struct srb {
	/*
	 * Do not move cmd_type field, it needs to
	 * line up with qla_tgt_cmd->cmd_type
	 */
	uint8_t cmd_type;
	uint8_t pad[3];
	atomic_t ref_count;
	wait_queue_head_t nvme_ls_waitq;
	struct fc_port *fcport;
	struct scsi_qla_host *vha;
	uint32_t handle;
	uint16_t flags;
	uint16_t type;
	const char *name;
	int iocbs;
	struct qla_qpair *qpair;
	struct list_head elem;
	u32 gen1;	/* scratch */
	u32 gen2;	/* scratch */
	union {
		struct srb_iocb iocb_cmd;
		struct bsg_job *bsg_job;
		struct srb_cmd scmd;
	} u;
	void (*done)(void *, int);
	void (*free)(void *);
} srb_t;

#define GET_CMD_SP(sp) (sp->u.scmd.cmd)
#define SET_CMD_SP(sp, cmd) (sp->u.scmd.cmd = cmd)
#define GET_CMD_CTX_SP(sp) (sp->u.scmd.ctx)

#define GET_CMD_SENSE_LEN(sp) \
	(sp->u.scmd.request_sense_length)
#define SET_CMD_SENSE_LEN(sp, len) \
	(sp->u.scmd.request_sense_length = len)
#define GET_CMD_SENSE_PTR(sp) \
	(sp->u.scmd.request_sense_ptr)
#define SET_CMD_SENSE_PTR(sp, ptr) \
	(sp->u.scmd.request_sense_ptr = ptr)
#define GET_FW_SENSE_LEN(sp) \
	(sp->u.scmd.fw_sense_length)
#define SET_FW_SENSE_LEN(sp, len) \
	(sp->u.scmd.fw_sense_length = len)

struct msg_echo_lb {
	dma_addr_t send_dma;
	dma_addr_t rcv_dma;
	uint16_t req_sg_cnt;
	uint16_t rsp_sg_cnt;
	uint16_t options;
	uint32_t transfer_size;
	uint32_t iteration_count;
};

/*
 * ISP I/O Register Set structure definitions.
 */
struct device_reg_2xxx {
	uint16_t flash_address; 	/* Flash BIOS address */
	uint16_t flash_data;		/* Flash BIOS data */
	uint16_t unused_1[1];		/* Gap */
	uint16_t ctrl_status;		/* Control/Status */
#define CSR_FLASH_64K_BANK	BIT_3	/* Flash upper 64K bank select */
#define CSR_FLASH_ENABLE	BIT_1	/* Flash BIOS Read/Write enable */
#define CSR_ISP_SOFT_RESET	BIT_0	/* ISP soft reset */

	uint16_t ictrl;			/* Interrupt control */
#define ICR_EN_INT		BIT_15	/* ISP enable interrupts. */
#define ICR_EN_RISC		BIT_3	/* ISP enable RISC interrupts. */

	uint16_t istatus;		/* Interrupt status */
#define ISR_RISC_INT		BIT_3	/* RISC interrupt */

	uint16_t semaphore;		/* Semaphore */
	uint16_t nvram;			/* NVRAM register. */
#define NVR_DESELECT		0
#define NVR_BUSY		BIT_15
#define NVR_WRT_ENABLE		BIT_14	/* Write enable */
#define NVR_PR_ENABLE		BIT_13	/* Protection register enable */
#define NVR_DATA_IN		BIT_3
#define NVR_DATA_OUT		BIT_2
#define NVR_SELECT		BIT_1
#define NVR_CLOCK		BIT_0

#define NVR_WAIT_CNT		20000

	union {
		struct {
			uint16_t mailbox0;
			uint16_t mailbox1;
			uint16_t mailbox2;
			uint16_t mailbox3;
			uint16_t mailbox4;
			uint16_t mailbox5;
			uint16_t mailbox6;
			uint16_t mailbox7;
			uint16_t unused_2[59];	/* Gap */
		} __attribute__((packed)) isp2100;
		struct {
						/* Request Queue */
			uint16_t req_q_in;	/*  In-Pointer */
			uint16_t req_q_out;	/*  Out-Pointer */
						/* Response Queue */
			uint16_t rsp_q_in;	/*  In-Pointer */
			uint16_t rsp_q_out;	/*  Out-Pointer */

						/* RISC to Host Status */
			uint32_t host_status;
#define HSR_RISC_INT		BIT_15	/* RISC interrupt */
#define HSR_RISC_PAUSED		BIT_8	/* RISC Paused */

					/* Host to Host Semaphore */
			uint16_t host_semaphore;
			uint16_t unused_3[17];	/* Gap */
			uint16_t mailbox0;
			uint16_t mailbox1;
			uint16_t mailbox2;
			uint16_t mailbox3;
			uint16_t mailbox4;
			uint16_t mailbox5;
			uint16_t mailbox6;
			uint16_t mailbox7;
			uint16_t mailbox8;
			uint16_t mailbox9;
			uint16_t mailbox10;
			uint16_t mailbox11;
			uint16_t mailbox12;
			uint16_t mailbox13;
			uint16_t mailbox14;
			uint16_t mailbox15;
			uint16_t mailbox16;
			uint16_t mailbox17;
			uint16_t mailbox18;
			uint16_t mailbox19;
			uint16_t mailbox20;
			uint16_t mailbox21;
			uint16_t mailbox22;
			uint16_t mailbox23;
			uint16_t mailbox24;
			uint16_t mailbox25;
			uint16_t mailbox26;
			uint16_t mailbox27;
			uint16_t mailbox28;
			uint16_t mailbox29;
			uint16_t mailbox30;
			uint16_t mailbox31;
			uint16_t fb_cmd;
			uint16_t unused_4[10];	/* Gap */
		} __attribute__((packed)) isp2300;
	} u;

	uint16_t fpm_diag_config;
	uint16_t unused_5[0x4];		/* Gap */
	uint16_t risc_hw;
	uint16_t unused_5_1;		/* Gap */
	uint16_t pcr;			/* Processor Control Register. */
	uint16_t unused_6[0x5];		/* Gap */
	uint16_t mctr;			/* Memory Configuration and Timing. */
	uint16_t unused_7[0x3];		/* Gap */
	uint16_t fb_cmd_2100;		/* Unused on 23XX */
	uint16_t unused_8[0x3];		/* Gap */
	uint16_t hccr;			/* Host command & control register. */
#define HCCR_HOST_INT		BIT_7	/* Host interrupt bit */
#define HCCR_RISC_PAUSE		BIT_5	/* Pause mode bit */
					/* HCCR commands */
#define HCCR_RESET_RISC		0x1000	/* Reset RISC */
#define HCCR_PAUSE_RISC		0x2000	/* Pause RISC */
#define HCCR_RELEASE_RISC	0x3000	/* Release RISC from reset. */
#define HCCR_SET_HOST_INT	0x5000	/* Set host interrupt */
#define HCCR_CLR_HOST_INT	0x6000	/* Clear HOST interrupt */
#define HCCR_CLR_RISC_INT	0x7000	/* Clear RISC interrupt */
#define	HCCR_DISABLE_PARITY_PAUSE 0x4001 /* Disable parity error RISC pause. */
#define HCCR_ENABLE_PARITY	0xA000	/* Enable PARITY interrupt */

	uint16_t unused_9[5];		/* Gap */
	uint16_t gpiod;			/* GPIO Data register. */
	uint16_t gpioe;			/* GPIO Enable register. */
#define GPIO_LED_MASK			0x00C0
#define GPIO_LED_GREEN_OFF_AMBER_OFF	0x0000
#define GPIO_LED_GREEN_ON_AMBER_OFF	0x0040
#define GPIO_LED_GREEN_OFF_AMBER_ON	0x0080
#define GPIO_LED_GREEN_ON_AMBER_ON	0x00C0
#define GPIO_LED_ALL_OFF		0x0000
#define GPIO_LED_RED_ON_OTHER_OFF	0x0001	/* isp2322 */
#define GPIO_LED_RGA_ON			0x00C1	/* isp2322: red green amber */

	union {
		struct {
			uint16_t unused_10[8];	/* Gap */
			uint16_t mailbox8;
			uint16_t mailbox9;
			uint16_t mailbox10;
			uint16_t mailbox11;
			uint16_t mailbox12;
			uint16_t mailbox13;
			uint16_t mailbox14;
			uint16_t mailbox15;
			uint16_t mailbox16;
			uint16_t mailbox17;
			uint16_t mailbox18;
			uint16_t mailbox19;
			uint16_t mailbox20;
			uint16_t mailbox21;
			uint16_t mailbox22;
			uint16_t mailbox23;	/* Also probe reg. */
		} __attribute__((packed)) isp2200;
	} u_end;
};

struct device_reg_25xxmq {
	uint32_t req_q_in;
	uint32_t req_q_out;
	uint32_t rsp_q_in;
	uint32_t rsp_q_out;
	uint32_t atio_q_in;
	uint32_t atio_q_out;
};


struct device_reg_fx00 {
	uint32_t mailbox0;		/* 00 */
	uint32_t mailbox1;		/* 04 */
	uint32_t mailbox2;		/* 08 */
	uint32_t mailbox3;		/* 0C */
	uint32_t mailbox4;		/* 10 */
	uint32_t mailbox5;		/* 14 */
	uint32_t mailbox6;		/* 18 */
	uint32_t mailbox7;		/* 1C */
	uint32_t mailbox8;		/* 20 */
	uint32_t mailbox9;		/* 24 */
	uint32_t mailbox10;		/* 28 */
	uint32_t mailbox11;
	uint32_t mailbox12;
	uint32_t mailbox13;
	uint32_t mailbox14;
	uint32_t mailbox15;
	uint32_t mailbox16;
	uint32_t mailbox17;
	uint32_t mailbox18;
	uint32_t mailbox19;
	uint32_t mailbox20;
	uint32_t mailbox21;
	uint32_t mailbox22;
	uint32_t mailbox23;
	uint32_t mailbox24;
	uint32_t mailbox25;
	uint32_t mailbox26;
	uint32_t mailbox27;
	uint32_t mailbox28;
	uint32_t mailbox29;
	uint32_t mailbox30;
	uint32_t mailbox31;
	uint32_t aenmailbox0;
	uint32_t aenmailbox1;
	uint32_t aenmailbox2;
	uint32_t aenmailbox3;
	uint32_t aenmailbox4;
	uint32_t aenmailbox5;
	uint32_t aenmailbox6;
	uint32_t aenmailbox7;
	/* Request Queue. */
	uint32_t req_q_in;		/* A0 - Request Queue In-Pointer */
	uint32_t req_q_out;		/* A4 - Request Queue Out-Pointer */
	/* Response Queue. */
	uint32_t rsp_q_in;		/* A8 - Response Queue In-Pointer */
	uint32_t rsp_q_out;		/* AC - Response Queue Out-Pointer */
	/* Init values shadowed on FW Up Event */
	uint32_t initval0;		/* B0 */
	uint32_t initval1;		/* B4 */
	uint32_t initval2;		/* B8 */
	uint32_t initval3;		/* BC */
	uint32_t initval4;		/* C0 */
	uint32_t initval5;		/* C4 */
	uint32_t initval6;		/* C8 */
	uint32_t initval7;		/* CC */
	uint32_t fwheartbeat;		/* D0 */
	uint32_t pseudoaen;		/* D4 */
};



typedef union {
		struct device_reg_2xxx isp;
		struct device_reg_24xx isp24;
		struct device_reg_25xxmq isp25mq;
		struct device_reg_82xx isp82;
		struct device_reg_fx00 ispfx00;
} __iomem device_reg_t;

#define ISP_REQ_Q_IN(ha, reg) \
	(IS_QLA2100(ha) || IS_QLA2200(ha) ? \
	 &(reg)->u.isp2100.mailbox4 : \
	 &(reg)->u.isp2300.req_q_in)
#define ISP_REQ_Q_OUT(ha, reg) \
	(IS_QLA2100(ha) || IS_QLA2200(ha) ? \
	 &(reg)->u.isp2100.mailbox4 : \
	 &(reg)->u.isp2300.req_q_out)
#define ISP_RSP_Q_IN(ha, reg) \
	(IS_QLA2100(ha) || IS_QLA2200(ha) ? \
	 &(reg)->u.isp2100.mailbox5 : \
	 &(reg)->u.isp2300.rsp_q_in)
#define ISP_RSP_Q_OUT(ha, reg) \
	(IS_QLA2100(ha) || IS_QLA2200(ha) ? \
	 &(reg)->u.isp2100.mailbox5 : \
	 &(reg)->u.isp2300.rsp_q_out)

#define ISP_ATIO_Q_IN(vha) (vha->hw->tgt.atio_q_in)
#define ISP_ATIO_Q_OUT(vha) (vha->hw->tgt.atio_q_out)

#define MAILBOX_REG(ha, reg, num) \
	(IS_QLA2100(ha) || IS_QLA2200(ha) ? \
	 (num < 8 ? \
	  &(reg)->u.isp2100.mailbox0 + (num) : \
	  &(reg)->u_end.isp2200.mailbox8 + (num) - 8) : \
	 &(reg)->u.isp2300.mailbox0 + (num))
#define RD_MAILBOX_REG(ha, reg, num) \
	RD_REG_WORD(MAILBOX_REG(ha, reg, num))
#define WRT_MAILBOX_REG(ha, reg, num, data) \
	WRT_REG_WORD(MAILBOX_REG(ha, reg, num), data)

#define FB_CMD_REG(ha, reg) \
	(IS_QLA2100(ha) || IS_QLA2200(ha) ? \
	 &(reg)->fb_cmd_2100 : \
	 &(reg)->u.isp2300.fb_cmd)
#define RD_FB_CMD_REG(ha, reg) \
	RD_REG_WORD(FB_CMD_REG(ha, reg))
#define WRT_FB_CMD_REG(ha, reg, data) \
	WRT_REG_WORD(FB_CMD_REG(ha, reg), data)

typedef struct {
	uint32_t	out_mb;		/* outbound from driver */
	uint32_t	in_mb;			/* Incoming from RISC */
	uint16_t	mb[MAILBOX_REGISTER_COUNT];
	long		buf_size;
	void		*bufp;
	uint32_t	tov;
	uint8_t		flags;
#define MBX_DMA_IN	BIT_0
#define	MBX_DMA_OUT	BIT_1
#define IOCTL_CMD	BIT_2
} mbx_cmd_t;

struct mbx_cmd_32 {
	uint32_t	out_mb;		/* outbound from driver */
	uint32_t	in_mb;			/* Incoming from RISC */
	uint32_t	mb[MAILBOX_REGISTER_COUNT];
	long		buf_size;
	void		*bufp;
	uint32_t	tov;
	uint8_t		flags;
#define MBX_DMA_IN	BIT_0
#define	MBX_DMA_OUT	BIT_1
#define IOCTL_CMD	BIT_2
};


#define	MBX_TOV_SECONDS	30

/*
 *  ISP product identification definitions in mailboxes after reset.
 */
#define PROD_ID_1		0x4953
#define PROD_ID_2		0x0000
#define PROD_ID_2a		0x5020
#define PROD_ID_3		0x2020

/*
 * ISP mailbox Self-Test status codes
 */
#define MBS_FRM_ALIVE		0	/* Firmware Alive. */
#define MBS_CHKSUM_ERR		1	/* Checksum Error. */
#define MBS_BUSY		4	/* Busy. */

/*
 * ISP mailbox command complete status codes
 */
#define MBS_COMMAND_COMPLETE		0x4000
#define MBS_INVALID_COMMAND		0x4001
#define MBS_HOST_INTERFACE_ERROR	0x4002
#define MBS_TEST_FAILED			0x4003
#define MBS_COMMAND_ERROR		0x4005
#define MBS_COMMAND_PARAMETER_ERROR	0x4006
#define MBS_PORT_ID_USED		0x4007
#define MBS_LOOP_ID_USED		0x4008
#define MBS_ALL_IDS_IN_USE		0x4009
#define MBS_NOT_LOGGED_IN		0x400A
#define MBS_LINK_DOWN_ERROR		0x400B
#define MBS_DIAG_ECHO_TEST_ERROR	0x400C

/*
 * ISP mailbox asynchronous event status codes
 */
#define MBA_ASYNC_EVENT		0x8000	/* Asynchronous event. */
#define MBA_RESET		0x8001	/* Reset Detected. */
#define MBA_SYSTEM_ERR		0x8002	/* System Error. */
#define MBA_REQ_TRANSFER_ERR	0x8003	/* Request Transfer Error. */
#define MBA_RSP_TRANSFER_ERR	0x8004	/* Response Transfer Error. */
#define MBA_WAKEUP_THRES	0x8005	/* Request Queue Wake-up. */
#define MBA_LIP_OCCURRED	0x8010	/* Loop Initialization Procedure */
					/* occurred. */
#define MBA_LOOP_UP		0x8011	/* FC Loop UP. */
#define MBA_LOOP_DOWN		0x8012	/* FC Loop Down. */
#define MBA_LIP_RESET		0x8013	/* LIP reset occurred. */
#define MBA_PORT_UPDATE		0x8014	/* Port Database update. */
#define MBA_RSCN_UPDATE		0x8015	/* Register State Chg Notification. */
#define MBA_LIP_F8		0x8016	/* Received a LIP F8. */
#define MBA_LOOP_INIT_ERR	0x8017	/* Loop Initialization Error. */
#define MBA_FABRIC_AUTH_REQ	0x801b	/* Fabric Authentication Required. */
#define MBA_SCSI_COMPLETION	0x8020	/* SCSI Command Complete. */
#define MBA_CTIO_COMPLETION	0x8021	/* CTIO Complete. */
#define MBA_IP_COMPLETION	0x8022	/* IP Transmit Command Complete. */
#define MBA_IP_RECEIVE		0x8023	/* IP Received. */
#define MBA_IP_BROADCAST	0x8024	/* IP Broadcast Received. */
#define MBA_IP_LOW_WATER_MARK	0x8025	/* IP Low Water Mark reached. */
#define MBA_IP_RCV_BUFFER_EMPTY 0x8026	/* IP receive buffer queue empty. */
#define MBA_IP_HDR_DATA_SPLIT	0x8027	/* IP header/data splitting feature */
					/* used. */
#define MBA_TRACE_NOTIFICATION	0x8028	/* Trace/Diagnostic notification. */
#define MBA_POINT_TO_POINT	0x8030	/* Point to point mode. */
#define MBA_CMPLT_1_16BIT	0x8031	/* Completion 1 16bit IOSB. */
#define MBA_CMPLT_2_16BIT	0x8032	/* Completion 2 16bit IOSB. */
#define MBA_CMPLT_3_16BIT	0x8033	/* Completion 3 16bit IOSB. */
#define MBA_CMPLT_4_16BIT	0x8034	/* Completion 4 16bit IOSB. */
#define MBA_CMPLT_5_16BIT	0x8035	/* Completion 5 16bit IOSB. */
#define MBA_CHG_IN_CONNECTION	0x8036	/* Change in connection mode. */
#define MBA_RIO_RESPONSE	0x8040	/* RIO response queue update. */
#define MBA_ZIO_RESPONSE	0x8040	/* ZIO response queue update. */
#define MBA_CMPLT_2_32BIT	0x8042	/* Completion 2 32bit IOSB. */
#define MBA_BYPASS_NOTIFICATION	0x8043	/* Auto bypass notification. */
#define MBA_DISCARD_RND_FRAME	0x8048	/* discard RND frame due to error. */
#define MBA_REJECTED_FCP_CMD	0x8049	/* rejected FCP_CMD. */
#define MBA_FW_NOT_STARTED	0x8050	/* Firmware not started */
#define MBA_FW_STARTING		0x8051	/* Firmware starting */
#define MBA_FW_RESTART_CMPLT	0x8060	/* Firmware restart complete */
#define MBA_INIT_REQUIRED	0x8061	/* Initialization required */
#define MBA_SHUTDOWN_REQUESTED	0x8062	/* Shutdown Requested */
#define MBA_TEMPERATURE_ALERT	0x8070	/* Temperature Alert */
#define MBA_DPORT_DIAGNOSTICS	0x8080	/* D-port Diagnostics */
#define MBA_TRANS_INSERT	0x8130	/* Transceiver Insertion */
#define MBA_FW_INIT_FAILURE	0x8401	/* Firmware initialization failure */
#define MBA_MIRROR_LUN_CHANGE	0x8402	/* Mirror LUN State Change
					   Notification */
#define MBA_FW_POLL_STATE	0x8600  /* Firmware in poll diagnostic state */
#define MBA_FW_RESET_FCT	0x8502	/* Firmware reset factory defaults */
#define MBA_FW_INIT_INPROGRESS	0x8500	/* Firmware boot in progress */
/* 83XX FCoE specific */
#define MBA_IDC_AEN		0x8200  /* FCoE: NIC Core state change AEN */

/* Interrupt type codes */
#define INTR_ROM_MB_SUCCESS		0x1
#define INTR_ROM_MB_FAILED		0x2
#define INTR_MB_SUCCESS			0x10
#define INTR_MB_FAILED			0x11
#define INTR_ASYNC_EVENT		0x12
#define INTR_RSP_QUE_UPDATE		0x13
#define INTR_RSP_QUE_UPDATE_83XX	0x14
#define INTR_ATIO_QUE_UPDATE		0x1C
#define INTR_ATIO_RSP_QUE_UPDATE	0x1D

/* ISP mailbox loopback echo diagnostic error code */
#define MBS_LB_RESET	0x17
/*
 * Firmware options 1, 2, 3.
 */
#define FO1_AE_ON_LIPF8			BIT_0
#define FO1_AE_ALL_LIP_RESET		BIT_1
#define FO1_CTIO_RETRY			BIT_3
#define FO1_DISABLE_LIP_F7_SW		BIT_4
#define FO1_DISABLE_100MS_LOS_WAIT	BIT_5
#define FO1_DISABLE_GPIO6_7		BIT_6	/* LED bits */
#define FO1_AE_ON_LOOP_INIT_ERR		BIT_7
#define FO1_SET_EMPHASIS_SWING		BIT_8
#define FO1_AE_AUTO_BYPASS		BIT_9
#define FO1_ENABLE_PURE_IOCB		BIT_10
#define FO1_AE_PLOGI_RJT		BIT_11
#define FO1_ENABLE_ABORT_SEQUENCE	BIT_12
#define FO1_AE_QUEUE_FULL		BIT_13

#define FO2_ENABLE_ATIO_TYPE_3		BIT_0
#define FO2_REV_LOOPBACK		BIT_1

#define FO3_ENABLE_EMERG_IOCB		BIT_0
#define FO3_AE_RND_ERROR		BIT_1

/* 24XX additional firmware options */
#define ADD_FO_COUNT			3
#define ADD_FO1_DISABLE_GPIO_LED_CTRL	BIT_6	/* LED bits */
#define ADD_FO1_ENABLE_PUREX_IOCB	BIT_10

#define ADD_FO2_ENABLE_SEL_CLS2		BIT_5

#define ADD_FO3_NO_ABT_ON_LINK_DOWN	BIT_14

/*
 * ISP mailbox commands
 */
#define MBC_LOAD_RAM			1	/* Load RAM. */
#define MBC_EXECUTE_FIRMWARE		2	/* Execute firmware. */
#define MBC_READ_RAM_WORD		5	/* Read RAM word. */
#define MBC_MAILBOX_REGISTER_TEST	6	/* Wrap incoming mailboxes */
#define MBC_VERIFY_CHECKSUM		7	/* Verify checksum. */
#define MBC_GET_FIRMWARE_VERSION	8	/* Get firmware revision. */
#define MBC_LOAD_RISC_RAM		9	/* Load RAM command. */
#define MBC_DUMP_RISC_RAM		0xa	/* Dump RAM command. */
#define MBC_LOAD_RISC_RAM_EXTENDED	0xb	/* Load RAM extended. */
#define MBC_DUMP_RISC_RAM_EXTENDED	0xc	/* Dump RAM extended. */
#define MBC_WRITE_RAM_WORD_EXTENDED	0xd	/* Write RAM word extended */
#define MBC_READ_RAM_EXTENDED		0xf	/* Read RAM extended. */
#define MBC_IOCB_COMMAND		0x12	/* Execute IOCB command. */
#define MBC_STOP_FIRMWARE		0x14	/* Stop firmware. */
#define MBC_ABORT_COMMAND		0x15	/* Abort IOCB command. */
#define MBC_ABORT_DEVICE		0x16	/* Abort device (ID/LUN). */
#define MBC_ABORT_TARGET		0x17	/* Abort target (ID). */
#define MBC_RESET			0x18	/* Reset. */
#define MBC_GET_ADAPTER_LOOP_ID		0x20	/* Get loop id of ISP2200. */
#define MBC_GET_SET_ZIO_THRESHOLD	0x21	/* Get/SET ZIO THRESHOLD. */
#define MBC_GET_RETRY_COUNT		0x22	/* Get f/w retry cnt/delay. */
#define MBC_DISABLE_VI			0x24	/* Disable VI operation. */
#define MBC_ENABLE_VI			0x25	/* Enable VI operation. */
#define MBC_GET_FIRMWARE_OPTION		0x28	/* Get Firmware Options. */
#define MBC_GET_MEM_OFFLOAD_CNTRL_STAT	0x34	/* Memory Offload ctrl/Stat*/
#define MBC_SET_FIRMWARE_OPTION		0x38	/* Set Firmware Options. */
#define MBC_LOOP_PORT_BYPASS		0x40	/* Loop Port Bypass. */
#define MBC_LOOP_PORT_ENABLE		0x41	/* Loop Port Enable. */
#define MBC_GET_RESOURCE_COUNTS		0x42	/* Get Resource Counts. */
#define MBC_NON_PARTICIPATE		0x43	/* Non-Participating Mode. */
#define MBC_DIAGNOSTIC_ECHO		0x44	/* Diagnostic echo. */
#define MBC_DIAGNOSTIC_LOOP_BACK	0x45	/* Diagnostic loop back. */
#define MBC_ONLINE_SELF_TEST		0x46	/* Online self-test. */
#define MBC_ENHANCED_GET_PORT_DATABASE	0x47	/* Get port database + login */
#define MBC_CONFIGURE_VF		0x4b	/* Configure VFs */
#define MBC_RESET_LINK_STATUS		0x52	/* Reset Link Error Status */
#define MBC_IOCB_COMMAND_A64		0x54	/* Execute IOCB command (64) */
#define MBC_PORT_LOGOUT			0x56	/* Port Logout request */
#define MBC_SEND_RNID_ELS		0x57	/* Send RNID ELS request */
#define MBC_SET_RNID_PARAMS		0x59	/* Set RNID parameters */
#define MBC_GET_RNID_PARAMS		0x5a	/* Get RNID parameters */
#define MBC_DATA_RATE			0x5d	/* Data Rate */
#define MBC_INITIALIZE_FIRMWARE		0x60	/* Initialize firmware */
#define MBC_INITIATE_LIP		0x62	/* Initiate Loop */
						/* Initialization Procedure */
#define MBC_GET_FC_AL_POSITION_MAP	0x63	/* Get FC_AL Position Map. */
#define MBC_GET_PORT_DATABASE		0x64	/* Get Port Database. */
#define MBC_CLEAR_ACA			0x65	/* Clear ACA. */
#define MBC_TARGET_RESET		0x66	/* Target Reset. */
#define MBC_CLEAR_TASK_SET		0x67	/* Clear Task Set. */
#define MBC_ABORT_TASK_SET		0x68	/* Abort Task Set. */
#define MBC_GET_FIRMWARE_STATE		0x69	/* Get firmware state. */
#define MBC_GET_PORT_NAME		0x6a	/* Get port name. */
#define MBC_GET_LINK_STATUS		0x6b	/* Get port link status. */
#define MBC_LIP_RESET			0x6c	/* LIP reset. */
#define MBC_SEND_SNS_COMMAND		0x6e	/* Send Simple Name Server */
						/* commandd. */
#define MBC_LOGIN_FABRIC_PORT		0x6f	/* Login fabric port. */
#define MBC_SEND_CHANGE_REQUEST		0x70	/* Send Change Request. */
#define MBC_LOGOUT_FABRIC_PORT		0x71	/* Logout fabric port. */
#define MBC_LIP_FULL_LOGIN		0x72	/* Full login LIP. */
#define MBC_LOGIN_LOOP_PORT		0x74	/* Login Loop Port. */
#define MBC_PORT_NODE_NAME_LIST		0x75	/* Get port/node name list. */
#define MBC_INITIALIZE_RECEIVE_QUEUE	0x77	/* Initialize receive queue */
#define MBC_UNLOAD_IP			0x79	/* Shutdown IP */
#define MBC_GET_ID_LIST			0x7C	/* Get Port ID list. */
#define MBC_SEND_LFA_COMMAND		0x7D	/* Send Loop Fabric Address */
#define MBC_LUN_RESET			0x7E	/* Send LUN reset */

/*
 * all the Mt. Rainier mailbox command codes that clash with FC/FCoE ones
 * should be defined with MBC_MR_*
 */
#define MBC_MR_DRV_SHUTDOWN		0x6A

/*
 * ISP24xx mailbox commands
 */
#define MBC_WRITE_SERDES		0x3	/* Write serdes word. */
#define MBC_READ_SERDES			0x4	/* Read serdes word. */
#define MBC_LOAD_DUMP_MPI_RAM		0x5	/* Load/Dump MPI RAM. */
#define MBC_SERDES_PARAMS		0x10	/* Serdes Tx Parameters. */
#define MBC_GET_IOCB_STATUS		0x12	/* Get IOCB status command. */
#define MBC_PORT_PARAMS			0x1A	/* Port iDMA Parameters. */
#define MBC_GET_TIMEOUT_PARAMS		0x22	/* Get FW timeouts. */
#define MBC_TRACE_CONTROL		0x27	/* Trace control command. */
#define MBC_GEN_SYSTEM_ERROR		0x2a	/* Generate System Error. */
#define MBC_WRITE_SFP			0x30	/* Write SFP Data. */
#define MBC_READ_SFP			0x31	/* Read SFP Data. */
#define MBC_SET_TIMEOUT_PARAMS		0x32	/* Set FW timeouts. */
#define MBC_DPORT_DIAGNOSTICS		0x47	/* D-Port Diagnostics */
#define MBC_MID_INITIALIZE_FIRMWARE	0x48	/* MID Initialize firmware. */
#define MBC_MID_GET_VP_DATABASE		0x49	/* MID Get VP Database. */
#define MBC_MID_GET_VP_ENTRY		0x4a	/* MID Get VP Entry. */
#define MBC_HOST_MEMORY_COPY		0x53	/* Host Memory Copy. */
#define MBC_SEND_RNFT_ELS		0x5e	/* Send RNFT ELS request */
#define MBC_GET_LINK_PRIV_STATS		0x6d	/* Get link & private data. */
#define MBC_LINK_INITIALIZATION		0x72	/* Do link initialization. */
#define MBC_SET_VENDOR_ID		0x76	/* Set Vendor ID. */
#define MBC_PORT_RESET			0x120	/* Port Reset */
#define MBC_SET_PORT_CONFIG		0x122	/* Set port configuration */
#define MBC_GET_PORT_CONFIG		0x123	/* Get port configuration */

/*
 * ISP81xx mailbox commands
 */
#define MBC_WRITE_MPI_REGISTER		0x01    /* Write MPI Register. */

/*
 * ISP8044 mailbox commands
 */
#define MBC_SET_GET_ETH_SERDES_REG	0x150
#define HCS_WRITE_SERDES		0x3
#define HCS_READ_SERDES			0x4

/* Firmware return data sizes */
#define FCAL_MAP_SIZE	128

/* Mailbox bit definitions for out_mb and in_mb */
#define	MBX_31		BIT_31
#define	MBX_30		BIT_30
#define	MBX_29		BIT_29
#define	MBX_28		BIT_28
#define	MBX_27		BIT_27
#define	MBX_26		BIT_26
#define	MBX_25		BIT_25
#define	MBX_24		BIT_24
#define	MBX_23		BIT_23
#define	MBX_22		BIT_22
#define	MBX_21		BIT_21
#define	MBX_20		BIT_20
#define	MBX_19		BIT_19
#define	MBX_18		BIT_18
#define	MBX_17		BIT_17
#define	MBX_16		BIT_16
#define	MBX_15		BIT_15
#define	MBX_14		BIT_14
#define	MBX_13		BIT_13
#define	MBX_12		BIT_12
#define	MBX_11		BIT_11
#define	MBX_10		BIT_10
#define	MBX_9		BIT_9
#define	MBX_8		BIT_8
#define	MBX_7		BIT_7
#define	MBX_6		BIT_6
#define	MBX_5		BIT_5
#define	MBX_4		BIT_4
#define	MBX_3		BIT_3
#define	MBX_2		BIT_2
#define	MBX_1		BIT_1
#define	MBX_0		BIT_0

#define RNID_TYPE_PORT_LOGIN	0x7
#define RNID_TYPE_SET_VERSION	0x9
#define RNID_TYPE_ASIC_TEMP	0xC

/*
 * Firmware state codes from get firmware state mailbox command
 */
#define FSTATE_CONFIG_WAIT      0
#define FSTATE_WAIT_AL_PA       1
#define FSTATE_WAIT_LOGIN       2
#define FSTATE_READY            3
#define FSTATE_LOSS_OF_SYNC     4
#define FSTATE_ERROR            5
#define FSTATE_REINIT           6
#define FSTATE_NON_PART         7

#define FSTATE_CONFIG_CORRECT      0
#define FSTATE_P2P_RCV_LIP         1
#define FSTATE_P2P_CHOOSE_LOOP     2
#define FSTATE_P2P_RCV_UNIDEN_LIP  3
#define FSTATE_FATAL_ERROR         4
#define FSTATE_LOOP_BACK_CONN      5

#define QLA27XX_IMG_STATUS_VER_MAJOR   0x01
#define QLA27XX_IMG_STATUS_VER_MINOR    0x00
#define QLA27XX_IMG_STATUS_SIGN   0xFACEFADE
#define QLA27XX_PRIMARY_IMAGE  1
#define QLA27XX_SECONDARY_IMAGE    2

/*
 * Port Database structure definition
 * Little endian except where noted.
 */
#define	PORT_DATABASE_SIZE	128	/* bytes */
typedef struct {
	uint8_t options;
	uint8_t control;
	uint8_t master_state;
	uint8_t slave_state;
	uint8_t reserved[2];
	uint8_t hard_address;
	uint8_t reserved_1;
	uint8_t port_id[4];
	uint8_t node_name[WWN_SIZE];
	uint8_t port_name[WWN_SIZE];
	uint16_t execution_throttle;
	uint16_t execution_count;
	uint8_t reset_count;
	uint8_t reserved_2;
	uint16_t resource_allocation;
	uint16_t current_allocation;
	uint16_t queue_head;
	uint16_t queue_tail;
	uint16_t transmit_execution_list_next;
	uint16_t transmit_execution_list_previous;
	uint16_t common_features;
	uint16_t total_concurrent_sequences;
	uint16_t RO_by_information_category;
	uint8_t recipient;
	uint8_t initiator;
	uint16_t receive_data_size;
	uint16_t concurrent_sequences;
	uint16_t open_sequences_per_exchange;
	uint16_t lun_abort_flags;
	uint16_t lun_stop_flags;
	uint16_t stop_queue_head;
	uint16_t stop_queue_tail;
	uint16_t port_retry_timer;
	uint16_t next_sequence_id;
	uint16_t frame_count;
	uint16_t PRLI_payload_length;
	uint8_t prli_svc_param_word_0[2];	/* Big endian */
						/* Bits 15-0 of word 0 */
	uint8_t prli_svc_param_word_3[2];	/* Big endian */
						/* Bits 15-0 of word 3 */
	uint16_t loop_id;
	uint16_t extended_lun_info_list_pointer;
	uint16_t extended_lun_stop_list_pointer;
} port_database_t;

/*
 * Port database slave/master states
 */
#define PD_STATE_DISCOVERY			0
#define PD_STATE_WAIT_DISCOVERY_ACK		1
#define PD_STATE_PORT_LOGIN			2
#define PD_STATE_WAIT_PORT_LOGIN_ACK		3
#define PD_STATE_PROCESS_LOGIN			4
#define PD_STATE_WAIT_PROCESS_LOGIN_ACK		5
#define PD_STATE_PORT_LOGGED_IN			6
#define PD_STATE_PORT_UNAVAILABLE		7
#define PD_STATE_PROCESS_LOGOUT			8
#define PD_STATE_WAIT_PROCESS_LOGOUT_ACK	9
#define PD_STATE_PORT_LOGOUT			10
#define PD_STATE_WAIT_PORT_LOGOUT_ACK		11


#define QLA_ZIO_MODE_6		(BIT_2 | BIT_1)
#define QLA_ZIO_DISABLED	0
#define QLA_ZIO_DEFAULT_TIMER	2

/*
 * ISP Initialization Control Block.
 * Little endian except where noted.
 */
#define	ICB_VERSION 1
typedef struct {
	uint8_t  version;
	uint8_t  reserved_1;

	/*
	 * LSB BIT 0  = Enable Hard Loop Id
	 * LSB BIT 1  = Enable Fairness
	 * LSB BIT 2  = Enable Full-Duplex
	 * LSB BIT 3  = Enable Fast Posting
	 * LSB BIT 4  = Enable Target Mode
	 * LSB BIT 5  = Disable Initiator Mode
	 * LSB BIT 6  = Enable ADISC
	 * LSB BIT 7  = Enable Target Inquiry Data
	 *
	 * MSB BIT 0  = Enable PDBC Notify
	 * MSB BIT 1  = Non Participating LIP
	 * MSB BIT 2  = Descending Loop ID Search
	 * MSB BIT 3  = Acquire Loop ID in LIPA
	 * MSB BIT 4  = Stop PortQ on Full Status
	 * MSB BIT 5  = Full Login after LIP
	 * MSB BIT 6  = Node Name Option
	 * MSB BIT 7  = Ext IFWCB enable bit
	 */
	uint8_t  firmware_options[2];

	uint16_t frame_payload_size;
	uint16_t max_iocb_allocation;
	uint16_t execution_throttle;
	uint8_t  retry_count;
	uint8_t	 retry_delay;			/* unused */
	uint8_t	 port_name[WWN_SIZE];		/* Big endian. */
	uint16_t hard_address;
	uint8_t	 inquiry_data;
	uint8_t	 login_timeout;
	uint8_t	 node_name[WWN_SIZE];		/* Big endian. */

	uint16_t request_q_outpointer;
	uint16_t response_q_inpointer;
	uint16_t request_q_length;
	uint16_t response_q_length;
	uint32_t request_q_address[2];
	uint32_t response_q_address[2];

	uint16_t lun_enables;
	uint8_t  command_resource_count;
	uint8_t  immediate_notify_resource_count;
	uint16_t timeout;
	uint8_t  reserved_2[2];

	/*
	 * LSB BIT 0 = Timer Operation mode bit 0
	 * LSB BIT 1 = Timer Operation mode bit 1
	 * LSB BIT 2 = Timer Operation mode bit 2
	 * LSB BIT 3 = Timer Operation mode bit 3
	 * LSB BIT 4 = Init Config Mode bit 0
	 * LSB BIT 5 = Init Config Mode bit 1
	 * LSB BIT 6 = Init Config Mode bit 2
	 * LSB BIT 7 = Enable Non part on LIHA failure
	 *
	 * MSB BIT 0 = Enable class 2
	 * MSB BIT 1 = Enable ACK0
	 * MSB BIT 2 =
	 * MSB BIT 3 =
	 * MSB BIT 4 = FC Tape Enable
	 * MSB BIT 5 = Enable FC Confirm
	 * MSB BIT 6 = Enable command queuing in target mode
	 * MSB BIT 7 = No Logo On Link Down
	 */
	uint8_t	 add_firmware_options[2];

	uint8_t	 response_accumulation_timer;
	uint8_t	 interrupt_delay_timer;

	/*
	 * LSB BIT 0 = Enable Read xfr_rdy
	 * LSB BIT 1 = Soft ID only
	 * LSB BIT 2 =
	 * LSB BIT 3 =
	 * LSB BIT 4 = FCP RSP Payload [0]
	 * LSB BIT 5 = FCP RSP Payload [1] / Sbus enable - 2200
	 * LSB BIT 6 = Enable Out-of-Order frame handling
	 * LSB BIT 7 = Disable Automatic PLOGI on Local Loop
	 *
	 * MSB BIT 0 = Sbus enable - 2300
	 * MSB BIT 1 =
	 * MSB BIT 2 =
	 * MSB BIT 3 =
	 * MSB BIT 4 = LED mode
	 * MSB BIT 5 = enable 50 ohm termination
	 * MSB BIT 6 = Data Rate (2300 only)
	 * MSB BIT 7 = Data Rate (2300 only)
	 */
	uint8_t	 special_options[2];

	uint8_t  reserved_3[26];
} init_cb_t;

/*
 * Get Link Status mailbox command return buffer.
 */
#define GLSO_SEND_RPS	BIT_0
#define GLSO_USE_DID	BIT_3

struct link_statistics {
	uint32_t link_fail_cnt;
	uint32_t loss_sync_cnt;
	uint32_t loss_sig_cnt;
	uint32_t prim_seq_err_cnt;
	uint32_t inval_xmit_word_cnt;
	uint32_t inval_crc_cnt;
	uint32_t lip_cnt;
	uint32_t link_up_cnt;
	uint32_t link_down_loop_init_tmo;
	uint32_t link_down_los;
	uint32_t link_down_loss_rcv_clk;
	uint32_t reserved0[5];
	uint32_t port_cfg_chg;
	uint32_t reserved1[11];
	uint32_t rsp_q_full;
	uint32_t atio_q_full;
	uint32_t drop_ae;
	uint32_t els_proto_err;
	uint32_t reserved2;
	uint32_t tx_frames;
	uint32_t rx_frames;
	uint32_t discarded_frames;
	uint32_t dropped_frames;
	uint32_t reserved3;
	uint32_t nos_rcvd;
	uint32_t reserved4[4];
	uint32_t tx_prjt;
	uint32_t rcv_exfail;
	uint32_t rcv_abts;
	uint32_t seq_frm_miss;
	uint32_t corr_err;
	uint32_t mb_rqst;
	uint32_t nport_full;
	uint32_t eofa;
	uint32_t reserved5;
	uint32_t fpm_recv_word_cnt_lo;
	uint32_t fpm_recv_word_cnt_hi;
	uint32_t fpm_disc_word_cnt_lo;
	uint32_t fpm_disc_word_cnt_hi;
	uint32_t fpm_xmit_word_cnt_lo;
	uint32_t fpm_xmit_word_cnt_hi;
	uint32_t reserved6[70];
};

/*
 * NVRAM Command values.
 */
#define NV_START_BIT            BIT_2
#define NV_WRITE_OP             (BIT_26+BIT_24)
#define NV_READ_OP              (BIT_26+BIT_25)
#define NV_ERASE_OP             (BIT_26+BIT_25+BIT_24)
#define NV_MASK_OP              (BIT_26+BIT_25+BIT_24)
#define NV_DELAY_COUNT          10

/*
 * QLogic ISP2100, ISP2200 and ISP2300 NVRAM structure definition.
 */
typedef struct {
	/*
	 * NVRAM header
	 */
	uint8_t	id[4];
	uint8_t	nvram_version;
	uint8_t	reserved_0;

	/*
	 * NVRAM RISC parameter block
	 */
	uint8_t	parameter_block_version;
	uint8_t	reserved_1;

	/*
	 * LSB BIT 0  = Enable Hard Loop Id
	 * LSB BIT 1  = Enable Fairness
	 * LSB BIT 2  = Enable Full-Duplex
	 * LSB BIT 3  = Enable Fast Posting
	 * LSB BIT 4  = Enable Target Mode
	 * LSB BIT 5  = Disable Initiator Mode
	 * LSB BIT 6  = Enable ADISC
	 * LSB BIT 7  = Enable Target Inquiry Data
	 *
	 * MSB BIT 0  = Enable PDBC Notify
	 * MSB BIT 1  = Non Participating LIP
	 * MSB BIT 2  = Descending Loop ID Search
	 * MSB BIT 3  = Acquire Loop ID in LIPA
	 * MSB BIT 4  = Stop PortQ on Full Status
	 * MSB BIT 5  = Full Login after LIP
	 * MSB BIT 6  = Node Name Option
	 * MSB BIT 7  = Ext IFWCB enable bit
	 */
	uint8_t	 firmware_options[2];

	uint16_t frame_payload_size;
	uint16_t max_iocb_allocation;
	uint16_t execution_throttle;
	uint8_t	 retry_count;
	uint8_t	 retry_delay;			/* unused */
	uint8_t	 port_name[WWN_SIZE];		/* Big endian. */
	uint16_t hard_address;
	uint8_t	 inquiry_data;
	uint8_t	 login_timeout;
	uint8_t	 node_name[WWN_SIZE];		/* Big endian. */

	/*
	 * LSB BIT 0 = Timer Operation mode bit 0
	 * LSB BIT 1 = Timer Operation mode bit 1
	 * LSB BIT 2 = Timer Operation mode bit 2
	 * LSB BIT 3 = Timer Operation mode bit 3
	 * LSB BIT 4 = Init Config Mode bit 0
	 * LSB BIT 5 = Init Config Mode bit 1
	 * LSB BIT 6 = Init Config Mode bit 2
	 * LSB BIT 7 = Enable Non part on LIHA failure
	 *
	 * MSB BIT 0 = Enable class 2
	 * MSB BIT 1 = Enable ACK0
	 * MSB BIT 2 =
	 * MSB BIT 3 =
	 * MSB BIT 4 = FC Tape Enable
	 * MSB BIT 5 = Enable FC Confirm
	 * MSB BIT 6 = Enable command queuing in target mode
	 * MSB BIT 7 = No Logo On Link Down
	 */
	uint8_t	 add_firmware_options[2];

	uint8_t	 response_accumulation_timer;
	uint8_t	 interrupt_delay_timer;

	/*
	 * LSB BIT 0 = Enable Read xfr_rdy
	 * LSB BIT 1 = Soft ID only
	 * LSB BIT 2 =
	 * LSB BIT 3 =
	 * LSB BIT 4 = FCP RSP Payload [0]
	 * LSB BIT 5 = FCP RSP Payload [1] / Sbus enable - 2200
	 * LSB BIT 6 = Enable Out-of-Order frame handling
	 * LSB BIT 7 = Disable Automatic PLOGI on Local Loop
	 *
	 * MSB BIT 0 = Sbus enable - 2300
	 * MSB BIT 1 =
	 * MSB BIT 2 =
	 * MSB BIT 3 =
	 * MSB BIT 4 = LED mode
	 * MSB BIT 5 = enable 50 ohm termination
	 * MSB BIT 6 = Data Rate (2300 only)
	 * MSB BIT 7 = Data Rate (2300 only)
	 */
	uint8_t	 special_options[2];

	/* Reserved for expanded RISC parameter block */
	uint8_t reserved_2[22];

	/*
	 * LSB BIT 0 = Tx Sensitivity 1G bit 0
	 * LSB BIT 1 = Tx Sensitivity 1G bit 1
	 * LSB BIT 2 = Tx Sensitivity 1G bit 2
	 * LSB BIT 3 = Tx Sensitivity 1G bit 3
	 * LSB BIT 4 = Rx Sensitivity 1G bit 0
	 * LSB BIT 5 = Rx Sensitivity 1G bit 1
	 * LSB BIT 6 = Rx Sensitivity 1G bit 2
	 * LSB BIT 7 = Rx Sensitivity 1G bit 3
	 *
	 * MSB BIT 0 = Tx Sensitivity 2G bit 0
	 * MSB BIT 1 = Tx Sensitivity 2G bit 1
	 * MSB BIT 2 = Tx Sensitivity 2G bit 2
	 * MSB BIT 3 = Tx Sensitivity 2G bit 3
	 * MSB BIT 4 = Rx Sensitivity 2G bit 0
	 * MSB BIT 5 = Rx Sensitivity 2G bit 1
	 * MSB BIT 6 = Rx Sensitivity 2G bit 2
	 * MSB BIT 7 = Rx Sensitivity 2G bit 3
	 *
	 * LSB BIT 0 = Output Swing 1G bit 0
	 * LSB BIT 1 = Output Swing 1G bit 1
	 * LSB BIT 2 = Output Swing 1G bit 2
	 * LSB BIT 3 = Output Emphasis 1G bit 0
	 * LSB BIT 4 = Output Emphasis 1G bit 1
	 * LSB BIT 5 = Output Swing 2G bit 0
	 * LSB BIT 6 = Output Swing 2G bit 1
	 * LSB BIT 7 = Output Swing 2G bit 2
	 *
	 * MSB BIT 0 = Output Emphasis 2G bit 0
	 * MSB BIT 1 = Output Emphasis 2G bit 1
	 * MSB BIT 2 = Output Enable
	 * MSB BIT 3 =
	 * MSB BIT 4 =
	 * MSB BIT 5 =
	 * MSB BIT 6 =
	 * MSB BIT 7 =
	 */
	uint8_t seriallink_options[4];

	/*
	 * NVRAM host parameter block
	 *
	 * LSB BIT 0 = Enable spinup delay
	 * LSB BIT 1 = Disable BIOS
	 * LSB BIT 2 = Enable Memory Map BIOS
	 * LSB BIT 3 = Enable Selectable Boot
	 * LSB BIT 4 = Disable RISC code load
	 * LSB BIT 5 = Set cache line size 1
	 * LSB BIT 6 = PCI Parity Disable
	 * LSB BIT 7 = Enable extended logging
	 *
	 * MSB BIT 0 = Enable 64bit addressing
	 * MSB BIT 1 = Enable lip reset
	 * MSB BIT 2 = Enable lip full login
	 * MSB BIT 3 = Enable target reset
	 * MSB BIT 4 = Enable database storage
	 * MSB BIT 5 = Enable cache flush read
	 * MSB BIT 6 = Enable database load
	 * MSB BIT 7 = Enable alternate WWN
	 */
	uint8_t host_p[2];

	uint8_t boot_node_name[WWN_SIZE];
	uint8_t boot_lun_number;
	uint8_t reset_delay;
	uint8_t port_down_retry_count;
	uint8_t boot_id_number;
	uint16_t max_luns_per_target;
	uint8_t fcode_boot_port_name[WWN_SIZE];
	uint8_t alternate_port_name[WWN_SIZE];
	uint8_t alternate_node_name[WWN_SIZE];

	/*
	 * BIT 0 = Selective Login
	 * BIT 1 = Alt-Boot Enable
	 * BIT 2 =
	 * BIT 3 = Boot Order List
	 * BIT 4 =
	 * BIT 5 = Selective LUN
	 * BIT 6 =
	 * BIT 7 = unused
	 */
	uint8_t efi_parameters;

	uint8_t link_down_timeout;

	uint8_t adapter_id[16];

	uint8_t alt1_boot_node_name[WWN_SIZE];
	uint16_t alt1_boot_lun_number;
	uint8_t alt2_boot_node_name[WWN_SIZE];
	uint16_t alt2_boot_lun_number;
	uint8_t alt3_boot_node_name[WWN_SIZE];
	uint16_t alt3_boot_lun_number;
	uint8_t alt4_boot_node_name[WWN_SIZE];
	uint16_t alt4_boot_lun_number;
	uint8_t alt5_boot_node_name[WWN_SIZE];
	uint16_t alt5_boot_lun_number;
	uint8_t alt6_boot_node_name[WWN_SIZE];
	uint16_t alt6_boot_lun_number;
	uint8_t alt7_boot_node_name[WWN_SIZE];
	uint16_t alt7_boot_lun_number;

	uint8_t reserved_3[2];

	/* Offset 200-215 : Model Number */
	uint8_t model_number[16];

	/* OEM related items */
	uint8_t oem_specific[16];

	/*
	 * NVRAM Adapter Features offset 232-239
	 *
	 * LSB BIT 0 = External GBIC
	 * LSB BIT 1 = Risc RAM parity
	 * LSB BIT 2 = Buffer Plus Module
	 * LSB BIT 3 = Multi Chip Adapter
	 * LSB BIT 4 = Internal connector
	 * LSB BIT 5 =
	 * LSB BIT 6 =
	 * LSB BIT 7 =
	 *
	 * MSB BIT 0 =
	 * MSB BIT 1 =
	 * MSB BIT 2 =
	 * MSB BIT 3 =
	 * MSB BIT 4 =
	 * MSB BIT 5 =
	 * MSB BIT 6 =
	 * MSB BIT 7 =
	 */
	uint8_t	adapter_features[2];

	uint8_t reserved_4[16];

	/* Subsystem vendor ID for ISP2200 */
	uint16_t subsystem_vendor_id_2200;

	/* Subsystem device ID for ISP2200 */
	uint16_t subsystem_device_id_2200;

	uint8_t	 reserved_5;
	uint8_t	 checksum;
} nvram_t;

/*
 * ISP queue - response queue entry definition.
 */
typedef struct {
	uint8_t		entry_type;		/* Entry type. */
	uint8_t		entry_count;		/* Entry count. */
	uint8_t		sys_define;		/* System defined. */
	uint8_t		entry_status;		/* Entry Status. */
	uint32_t	handle;			/* System defined handle */
	uint8_t		data[52];
	uint32_t	signature;
#define RESPONSE_PROCESSED	0xDEADDEAD	/* Signature */
} response_t;

/*
 * ISP queue - ATIO queue entry definition.
 */
struct atio {
	uint8_t		entry_type;		/* Entry type. */
	uint8_t		entry_count;		/* Entry count. */
	__le16		attr_n_length;
	uint8_t		data[56];
	uint32_t	signature;
#define ATIO_PROCESSED 0xDEADDEAD		/* Signature */
};

typedef union {
	uint16_t extended;
	struct {
		uint8_t reserved;
		uint8_t standard;
	} id;
} target_id_t;

#define SET_TARGET_ID(ha, to, from)			\
do {							\
	if (HAS_EXTENDED_IDS(ha))			\
		to.extended = cpu_to_le16(from);	\
	else						\
		to.id.standard = (uint8_t)from;		\
} while (0)

/*
 * ISP queue - command entry structure definition.
 */
#define COMMAND_TYPE	0x11		/* Command entry */
typedef struct {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */
	uint32_t handle;		/* System handle. */
	target_id_t target;		/* SCSI ID */
	uint16_t lun;			/* SCSI LUN */
	uint16_t control_flags;		/* Control flags. */
#define CF_WRITE	BIT_6
#define CF_READ		BIT_5
#define CF_SIMPLE_TAG	BIT_3
#define CF_ORDERED_TAG	BIT_2
#define CF_HEAD_TAG	BIT_1
	uint16_t reserved_1;
	uint16_t timeout;		/* Command timeout. */
	uint16_t dseg_count;		/* Data segment count. */
	uint8_t scsi_cdb[MAX_CMDSZ]; 	/* SCSI command words. */
	uint32_t byte_count;		/* Total byte count. */
	uint32_t dseg_0_address;	/* Data segment 0 address. */
	uint32_t dseg_0_length;		/* Data segment 0 length. */
	uint32_t dseg_1_address;	/* Data segment 1 address. */
	uint32_t dseg_1_length;		/* Data segment 1 length. */
	uint32_t dseg_2_address;	/* Data segment 2 address. */
	uint32_t dseg_2_length;		/* Data segment 2 length. */
} cmd_entry_t;

/*
 * ISP queue - 64-Bit addressing, command entry structure definition.
 */
#define COMMAND_A64_TYPE	0x19	/* Command A64 entry */
typedef struct {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */
	uint32_t handle;		/* System handle. */
	target_id_t target;		/* SCSI ID */
	uint16_t lun;			/* SCSI LUN */
	uint16_t control_flags;		/* Control flags. */
	uint16_t reserved_1;
	uint16_t timeout;		/* Command timeout. */
	uint16_t dseg_count;		/* Data segment count. */
	uint8_t scsi_cdb[MAX_CMDSZ];	/* SCSI command words. */
	uint32_t byte_count;		/* Total byte count. */
	uint32_t dseg_0_address[2];	/* Data segment 0 address. */
	uint32_t dseg_0_length;		/* Data segment 0 length. */
	uint32_t dseg_1_address[2];	/* Data segment 1 address. */
	uint32_t dseg_1_length;		/* Data segment 1 length. */
} cmd_a64_entry_t, request_t;

/*
 * ISP queue - continuation entry structure definition.
 */
#define CONTINUE_TYPE		0x02	/* Continuation entry. */
typedef struct {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */
	uint32_t reserved;
	uint32_t dseg_0_address;	/* Data segment 0 address. */
	uint32_t dseg_0_length;		/* Data segment 0 length. */
	uint32_t dseg_1_address;	/* Data segment 1 address. */
	uint32_t dseg_1_length;		/* Data segment 1 length. */
	uint32_t dseg_2_address;	/* Data segment 2 address. */
	uint32_t dseg_2_length;		/* Data segment 2 length. */
	uint32_t dseg_3_address;	/* Data segment 3 address. */
	uint32_t dseg_3_length;		/* Data segment 3 length. */
	uint32_t dseg_4_address;	/* Data segment 4 address. */
	uint32_t dseg_4_length;		/* Data segment 4 length. */
	uint32_t dseg_5_address;	/* Data segment 5 address. */
	uint32_t dseg_5_length;		/* Data segment 5 length. */
	uint32_t dseg_6_address;	/* Data segment 6 address. */
	uint32_t dseg_6_length;		/* Data segment 6 length. */
} cont_entry_t;

/*
 * ISP queue - 64-Bit addressing, continuation entry structure definition.
 */
#define CONTINUE_A64_TYPE	0x0A	/* Continuation A64 entry. */
typedef struct {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */
	uint32_t dseg_0_address[2];	/* Data segment 0 address. */
	uint32_t dseg_0_length;		/* Data segment 0 length. */
	uint32_t dseg_1_address[2];	/* Data segment 1 address. */
	uint32_t dseg_1_length;		/* Data segment 1 length. */
	uint32_t dseg_2_address	[2];	/* Data segment 2 address. */
	uint32_t dseg_2_length;		/* Data segment 2 length. */
	uint32_t dseg_3_address[2];	/* Data segment 3 address. */
	uint32_t dseg_3_length;		/* Data segment 3 length. */
	uint32_t dseg_4_address[2];	/* Data segment 4 address. */
	uint32_t dseg_4_length;		/* Data segment 4 length. */
} cont_a64_entry_t;

#define PO_MODE_DIF_INSERT	0
#define PO_MODE_DIF_REMOVE	1
#define PO_MODE_DIF_PASS	2
#define PO_MODE_DIF_REPLACE	3
#define PO_MODE_DIF_TCP_CKSUM	6
#define PO_ENABLE_INCR_GUARD_SEED	BIT_3
#define PO_DISABLE_GUARD_CHECK	BIT_4
#define PO_DISABLE_INCR_REF_TAG	BIT_5
#define PO_DIS_HEADER_MODE	BIT_7
#define PO_ENABLE_DIF_BUNDLING	BIT_8
#define PO_DIS_FRAME_MODE	BIT_9
#define PO_DIS_VALD_APP_ESC	BIT_10 /* Dis validation for escape tag/ffffh */
#define PO_DIS_VALD_APP_REF_ESC BIT_11

#define PO_DIS_APP_TAG_REPL	BIT_12 /* disable REG Tag replacement */
#define PO_DIS_REF_TAG_REPL	BIT_13
#define PO_DIS_APP_TAG_VALD	BIT_14 /* disable REF Tag validation */
#define PO_DIS_REF_TAG_VALD	BIT_15

/*
 * ISP queue - 64-Bit addressing, continuation crc entry structure definition.
 */
struct crc_context {
	uint32_t handle;		/* System handle. */
	__le32 ref_tag;
	__le16 app_tag;
	uint8_t ref_tag_mask[4];	/* Validation/Replacement Mask*/
	uint8_t app_tag_mask[2];	/* Validation/Replacement Mask*/
	__le16 guard_seed;		/* Initial Guard Seed */
	__le16 prot_opts;		/* Requested Data Protection Mode */
	__le16 blk_size;		/* Data size in bytes */
	uint16_t runt_blk_guard;	/* Guard value for runt block (tape
					 * only) */
	__le32 byte_count;		/* Total byte count/ total data
					 * transfer count */
	union {
		struct {
			uint32_t	reserved_1;
			uint16_t	reserved_2;
			uint16_t	reserved_3;
			uint32_t	reserved_4;
			uint32_t	data_address[2];
			uint32_t	data_length;
			uint32_t	reserved_5[2];
			uint32_t	reserved_6;
		} nobundling;
		struct {
			__le32	dif_byte_count;	/* Total DIF byte
							 * count */
			uint16_t	reserved_1;
			__le16	dseg_count;	/* Data segment count */
			uint32_t	reserved_2;
			uint32_t	data_address[2];
			uint32_t	data_length;
			uint32_t	dif_address[2];
			uint32_t	dif_length;	/* Data segment 0
							 * length */
		} bundling;
	} u;

	struct fcp_cmnd	fcp_cmnd;
	dma_addr_t	crc_ctx_dma;
	/* List of DMA context transfers */
	struct list_head dsd_list;

	/* This structure should not exceed 512 bytes */
};

#define CRC_CONTEXT_LEN_FW	(offsetof(struct crc_context, fcp_cmnd.lun))
#define CRC_CONTEXT_FCPCMND_OFF	(offsetof(struct crc_context, fcp_cmnd.lun))

/*
 * ISP queue - status entry structure definition.
 */
#define	STATUS_TYPE	0x03		/* Status entry. */
typedef struct {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */
	uint32_t handle;		/* System handle. */
	uint16_t scsi_status;		/* SCSI status. */
	uint16_t comp_status;		/* Completion status. */
	uint16_t state_flags;		/* State flags. */
	uint16_t status_flags;		/* Status flags. */
	uint16_t rsp_info_len;		/* Response Info Length. */
	uint16_t req_sense_length;	/* Request sense data length. */
	uint32_t residual_length;	/* Residual transfer length. */
	uint8_t rsp_info[8];		/* FCP response information. */
	uint8_t req_sense_data[32];	/* Request sense data. */
} sts_entry_t;

/*
 * Status entry entry status
 */
#define RF_RQ_DMA_ERROR	BIT_6		/* Request Queue DMA error. */
#define RF_INV_E_ORDER	BIT_5		/* Invalid entry order. */
#define RF_INV_E_COUNT	BIT_4		/* Invalid entry count. */
#define RF_INV_E_PARAM	BIT_3		/* Invalid entry parameter. */
#define RF_INV_E_TYPE	BIT_2		/* Invalid entry type. */
#define RF_BUSY		BIT_1		/* Busy */
#define RF_MASK		(RF_RQ_DMA_ERROR | RF_INV_E_ORDER | RF_INV_E_COUNT | \
			 RF_INV_E_PARAM | RF_INV_E_TYPE | RF_BUSY)
#define RF_MASK_24XX	(RF_INV_E_ORDER | RF_INV_E_COUNT | RF_INV_E_PARAM | \
			 RF_INV_E_TYPE)

/*
 * Status entry SCSI status bit definitions.
 */
#define SS_MASK				0xfff	/* Reserved bits BIT_12-BIT_15*/
#define SS_RESIDUAL_UNDER		BIT_11
#define SS_RESIDUAL_OVER		BIT_10
#define SS_SENSE_LEN_VALID		BIT_9
#define SS_RESPONSE_INFO_LEN_VALID	BIT_8
#define SS_SCSI_STATUS_BYTE	0xff

#define SS_RESERVE_CONFLICT		(BIT_4 | BIT_3)
#define SS_BUSY_CONDITION		BIT_3
#define SS_CONDITION_MET		BIT_2
#define SS_CHECK_CONDITION		BIT_1

/*
 * Status entry completion status
 */
#define CS_COMPLETE		0x0	/* No errors */
#define CS_INCOMPLETE		0x1	/* Incomplete transfer of cmd. */
#define CS_DMA			0x2	/* A DMA direction error. */
#define CS_TRANSPORT		0x3	/* Transport error. */
#define CS_RESET		0x4	/* SCSI bus reset occurred */
#define CS_ABORTED		0x5	/* System aborted command. */
#define CS_TIMEOUT		0x6	/* Timeout error. */
#define CS_DATA_OVERRUN		0x7	/* Data overrun. */
#define CS_DIF_ERROR		0xC	/* DIF error detected  */

#define CS_DATA_UNDERRUN	0x15	/* Data Underrun. */
#define CS_QUEUE_FULL		0x1C	/* Queue Full. */
#define CS_PORT_UNAVAILABLE	0x28	/* Port unavailable */
					/* (selection timeout) */
#define CS_PORT_LOGGED_OUT	0x29	/* Port Logged Out */
#define CS_PORT_CONFIG_CHG	0x2A	/* Port Configuration Changed */
#define CS_PORT_BUSY		0x2B	/* Port Busy */
#define CS_COMPLETE_CHKCOND	0x30	/* Error? */
#define CS_IOCB_ERROR		0x31	/* Generic error for IOCB request
					   failure */
#define CS_BAD_PAYLOAD		0x80	/* Driver defined */
#define CS_UNKNOWN		0x81	/* Driver defined */
#define CS_RETRY		0x82	/* Driver defined */
#define CS_LOOP_DOWN_ABORT	0x83	/* Driver defined */

#define CS_BIDIR_RD_OVERRUN			0x700
#define CS_BIDIR_RD_WR_OVERRUN			0x707
#define CS_BIDIR_RD_OVERRUN_WR_UNDERRUN		0x715
#define CS_BIDIR_RD_UNDERRUN			0x1500
#define CS_BIDIR_RD_UNDERRUN_WR_OVERRUN		0x1507
#define CS_BIDIR_RD_WR_UNDERRUN			0x1515
#define CS_BIDIR_DMA				0x200
/*
 * Status entry status flags
 */
#define SF_ABTS_TERMINATED	BIT_10
#define SF_LOGOUT_SENT		BIT_13

/*
 * ISP queue - status continuation entry structure definition.
 */
#define	STATUS_CONT_TYPE	0x10	/* Status continuation entry. */
typedef struct {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */
	uint8_t data[60];		/* data */
} sts_cont_entry_t;

/*
 * ISP queue -	RIO Type 1 status entry (32 bit I/O entry handles)
 *		structure definition.
 */
#define	STATUS_TYPE_21 0x21		/* Status entry. */
typedef struct {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t handle_count;		/* Handle count. */
	uint8_t entry_status;		/* Entry Status. */
	uint32_t handle[15];		/* System handles. */
} sts21_entry_t;

/*
 * ISP queue -	RIO Type 2 status entry (16 bit I/O entry handles)
 *		structure definition.
 */
#define	STATUS_TYPE_22	0x22		/* Status entry. */
typedef struct {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t handle_count;		/* Handle count. */
	uint8_t entry_status;		/* Entry Status. */
	uint16_t handle[30];		/* System handles. */
} sts22_entry_t;

/*
 * ISP queue - marker entry structure definition.
 */
#define MARKER_TYPE	0x04		/* Marker entry. */
typedef struct {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t handle_count;		/* Handle count. */
	uint8_t entry_status;		/* Entry Status. */
	uint32_t sys_define_2;		/* System defined. */
	target_id_t target;		/* SCSI ID */
	uint8_t modifier;		/* Modifier (7-0). */
#define MK_SYNC_ID_LUN	0		/* Synchronize ID/LUN */
#define MK_SYNC_ID	1		/* Synchronize ID */
#define MK_SYNC_ALL	2		/* Synchronize all ID/LUN */
#define MK_SYNC_LIP	3		/* Synchronize all ID/LUN, */
					/* clear port changed, */
					/* use sequence number. */
	uint8_t reserved_1;
	uint16_t sequence_number;	/* Sequence number of event */
	uint16_t lun;			/* SCSI LUN */
	uint8_t reserved_2[48];
} mrk_entry_t;

/*
 * ISP queue - Management Server entry structure definition.
 */
#define MS_IOCB_TYPE		0x29	/* Management Server IOCB entry */
typedef struct {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t handle_count;		/* Handle count. */
	uint8_t entry_status;		/* Entry Status. */
	uint32_t handle1;		/* System handle. */
	target_id_t loop_id;
	uint16_t status;
	uint16_t control_flags;		/* Control flags. */
	uint16_t reserved2;
	uint16_t timeout;
	uint16_t cmd_dsd_count;
	uint16_t total_dsd_count;
	uint8_t type;
	uint8_t r_ctl;
	uint16_t rx_id;
	uint16_t reserved3;
	uint32_t handle2;
	uint32_t rsp_bytecount;
	uint32_t req_bytecount;
	uint32_t dseg_req_address[2];	/* Data segment 0 address. */
	uint32_t dseg_req_length;	/* Data segment 0 length. */
	uint32_t dseg_rsp_address[2];	/* Data segment 1 address. */
	uint32_t dseg_rsp_length;	/* Data segment 1 length. */
} ms_iocb_entry_t;


/*
 * ISP queue - Mailbox Command entry structure definition.
 */
#define MBX_IOCB_TYPE	0x39
struct mbx_entry {
	uint8_t entry_type;
	uint8_t entry_count;
	uint8_t sys_define1;
	/* Use sys_define1 for source type */
#define SOURCE_SCSI	0x00
#define SOURCE_IP	0x01
#define SOURCE_VI	0x02
#define SOURCE_SCTP	0x03
#define SOURCE_MP	0x04
#define SOURCE_MPIOCTL	0x05
#define SOURCE_ASYNC_IOCB 0x07

	uint8_t entry_status;

	uint32_t handle;
	target_id_t loop_id;

	uint16_t status;
	uint16_t state_flags;
	uint16_t status_flags;

	uint32_t sys_define2[2];

	uint16_t mb0;
	uint16_t mb1;
	uint16_t mb2;
	uint16_t mb3;
	uint16_t mb6;
	uint16_t mb7;
	uint16_t mb9;
	uint16_t mb10;
	uint32_t reserved_2[2];
	uint8_t node_name[WWN_SIZE];
	uint8_t port_name[WWN_SIZE];
};

#ifndef IMMED_NOTIFY_TYPE
#define IMMED_NOTIFY_TYPE 0x0D		/* Immediate notify entry. */
/*
 * ISP queue -	immediate notify entry structure definition.
 *		This is sent by the ISP to the Target driver.
 *		This IOCB would have report of events sent by the
 *		initiator, that needs to be handled by the target
 *		driver immediately.
 */
struct imm_ntfy_from_isp {
	uint8_t	 entry_type;		    /* Entry type. */
	uint8_t	 entry_count;		    /* Entry count. */
	uint8_t	 sys_define;		    /* System defined. */
	uint8_t	 entry_status;		    /* Entry Status. */
	union {
		struct {
			uint32_t sys_define_2; /* System defined. */
			target_id_t target;
			uint16_t lun;
			uint8_t  target_id;
			uint8_t  reserved_1;
			uint16_t status_modifier;
			uint16_t status;
			uint16_t task_flags;
			uint16_t seq_id;
			uint16_t srr_rx_id;
			uint32_t srr_rel_offs;
			uint16_t srr_ui;
#define SRR_IU_DATA_IN	0x1
#define SRR_IU_DATA_OUT	0x5
#define SRR_IU_STATUS	0x7
			uint16_t srr_ox_id;
			uint8_t reserved_2[28];
		} isp2x;
		struct {
			uint32_t reserved;
			uint16_t nport_handle;
			uint16_t reserved_2;
			uint16_t flags;
#define NOTIFY24XX_FLAGS_GLOBAL_TPRLO   BIT_1
#define NOTIFY24XX_FLAGS_PUREX_IOCB     BIT_0
			uint16_t srr_rx_id;
			uint16_t status;
			uint8_t  status_subcode;
			uint8_t  fw_handle;
			uint32_t exchange_address;
			uint32_t srr_rel_offs;
			uint16_t srr_ui;
			uint16_t srr_ox_id;
			union {
				struct {
					uint8_t node_name[8];
				} plogi; /* PLOGI/ADISC/PDISC */
				struct {
					/* PRLI word 3 bit 0-15 */
					uint16_t wd3_lo;
					uint8_t resv0[6];
				} prli;
				struct {
					uint8_t port_id[3];
					uint8_t resv1;
					uint16_t nport_handle;
					uint16_t resv2;
				} req_els;
			} u;
			uint8_t port_name[8];
			uint8_t resv3[3];
			uint8_t  vp_index;
			uint32_t reserved_5;
			uint8_t  port_id[3];
			uint8_t  reserved_6;
		} isp24;
	} u;
	uint16_t reserved_7;
	uint16_t ox_id;
} __packed;
#endif

/*
 * ISP request and response queue entry sizes
 */
#define RESPONSE_ENTRY_SIZE	(sizeof(response_t))
#define REQUEST_ENTRY_SIZE	(sizeof(request_t))



/*
 * Switch info gathering structure.
 */
typedef struct {
	port_id_t d_id;
	uint8_t node_name[WWN_SIZE];
	uint8_t port_name[WWN_SIZE];
	uint8_t fabric_port_name[WWN_SIZE];
	uint16_t fp_speed;
	uint8_t fc4_type;
	uint8_t fc4f_nvme;	/* nvme fc4 feature bits */
} sw_info_t;

/* FCP-4 types */
#define FC4_TYPE_FCP_SCSI	0x08
#define FC4_TYPE_OTHER		0x0
#define FC4_TYPE_UNKNOWN	0xff

/* mailbox command 4G & above */
struct mbx_24xx_entry {
	uint8_t		entry_type;
	uint8_t		entry_count;
	uint8_t		sys_define1;
	uint8_t		entry_status;
	uint32_t	handle;
	uint16_t	mb[28];
};

#define IOCB_SIZE 64

/*
 * Fibre channel port type.
 */
typedef enum {
	FCT_UNKNOWN,
	FCT_RSCN,
	FCT_SWITCH,
	FCT_BROADCAST,
	FCT_INITIATOR,
	FCT_TARGET,
	FCT_NVME
} fc_port_type_t;

enum qla_sess_deletion {
	QLA_SESS_DELETION_NONE		= 0,
	QLA_SESS_DELETION_IN_PROGRESS,
	QLA_SESS_DELETED,
};

enum qlt_plogi_link_t {
	QLT_PLOGI_LINK_SAME_WWN,
	QLT_PLOGI_LINK_CONFLICT,
	QLT_PLOGI_LINK_MAX
};

struct qlt_plogi_ack_t {
	struct list_head	list;
	struct imm_ntfy_from_isp iocb;
	port_id_t	id;
	int		ref_count;
	void		*fcport;
};

struct ct_sns_desc {
	struct ct_sns_pkt	*ct_sns;
	dma_addr_t		ct_sns_dma;
};

enum discovery_state {
	DSC_DELETED,
	DSC_GID_PN,
	DSC_GNL,
	DSC_LOGIN_PEND,
	DSC_LOGIN_FAILED,
	DSC_GPDB,
	DSC_GPSC,
	DSC_UPD_FCPORT,
	DSC_LOGIN_COMPLETE,
	DSC_DELETE_PEND,
};

enum login_state {	/* FW control Target side */
	DSC_LS_LLIOCB_SENT = 2,
	DSC_LS_PLOGI_PEND,
	DSC_LS_PLOGI_COMP,
	DSC_LS_PRLI_PEND,
	DSC_LS_PRLI_COMP,
	DSC_LS_PORT_UNAVAIL,
	DSC_LS_PRLO_PEND = 9,
	DSC_LS_LOGO_PEND,
};

enum fcport_mgt_event {
	FCME_RELOGIN = 1,
	FCME_RSCN,
	FCME_GIDPN_DONE,
	FCME_PLOGI_DONE,	/* Initiator side sent LLIOCB */
	FCME_PRLI_DONE,
	FCME_GNL_DONE,
	FCME_GPSC_DONE,
	FCME_GPDB_DONE,
	FCME_GPNID_DONE,
	FCME_GFFID_DONE,
	FCME_DELETE_DONE,
};

enum rscn_addr_format {
	RSCN_PORT_ADDR,
	RSCN_AREA_ADDR,
	RSCN_DOM_ADDR,
	RSCN_FAB_ADDR,
};

/*
 * Fibre channel port structure.
 */
typedef struct fc_port {
	struct list_head list;
	struct scsi_qla_host *vha;

	uint8_t node_name[WWN_SIZE];
	uint8_t port_name[WWN_SIZE];
	port_id_t d_id;
	uint16_t loop_id;
	uint16_t old_loop_id;

	unsigned int conf_compl_supported:1;
	unsigned int deleted:2;
	unsigned int local:1;
	unsigned int logout_on_delete:1;
	unsigned int logo_ack_needed:1;
	unsigned int keep_nport_handle:1;
	unsigned int send_els_logo:1;
	unsigned int login_pause:1;
	unsigned int login_succ:1;

	struct work_struct nvme_del_work;
	struct completion nvme_del_done;
	uint32_t nvme_prli_service_param;
#define NVME_PRLI_SP_CONF       BIT_7
#define NVME_PRLI_SP_INITIATOR  BIT_5
#define NVME_PRLI_SP_TARGET     BIT_4
#define NVME_PRLI_SP_DISCOVERY  BIT_3
	uint8_t nvme_flag;
#define NVME_FLAG_REGISTERED 4

	struct fc_port *conflict;
	unsigned char logout_completed;
	int generation;

	struct se_session *se_sess;
	struct kref sess_kref;
	struct qla_tgt *tgt;
	unsigned long expires;
	struct list_head del_list_entry;
	struct work_struct free_work;

	struct qlt_plogi_ack_t *plogi_link[QLT_PLOGI_LINK_MAX];

	uint16_t tgt_id;
	uint16_t old_tgt_id;

	uint8_t fcp_prio;

	uint8_t fabric_port_name[WWN_SIZE];
	uint16_t fp_speed;

	fc_port_type_t port_type;

	atomic_t state;
	uint32_t flags;

	int login_retry;

	struct fc_rport *rport, *drport;
	u32 supported_classes;

	uint8_t fc4_type;
	uint8_t	fc4f_nvme;
	uint8_t scan_state;

	unsigned long last_queue_full;
	unsigned long last_ramp_up;

	uint16_t port_id;

	struct nvme_fc_remote_port *nvme_remote_port;

	unsigned long retry_delay_timestamp;
	struct qla_tgt_sess *tgt_session;
	struct ct_sns_desc ct_desc;
	enum discovery_state disc_state;
	enum login_state fw_login_state;
	unsigned long plogi_nack_done_deadline;

	u32 login_gen, last_login_gen;
	u32 rscn_gen, last_rscn_gen;
	u32 chip_reset;
	struct list_head gnl_entry;
	struct work_struct del_work;
	u8 iocb[IOCB_SIZE];
} fc_port_t;

#define QLA_FCPORT_SCAN		1
#define QLA_FCPORT_FOUND	2

struct event_arg {
	enum fcport_mgt_event	event;
	fc_port_t		*fcport;
	srb_t			*sp;
	port_id_t		id;
	u16			data[2], rc;
	u8			port_name[WWN_SIZE];
	u32			iop[2];
};

#include "qla_mr.h"

/*
 * Fibre channel port/lun states.
 */
#define FCS_UNCONFIGURED	1
#define FCS_DEVICE_DEAD		2
#define FCS_DEVICE_LOST		3
#define FCS_ONLINE		4

static const char * const port_state_str[] = {
	"Unknown",
	"UNCONFIGURED",
	"DEAD",
	"LOST",
	"ONLINE"
};

/*
 * FC port flags.
 */
#define FCF_FABRIC_DEVICE	BIT_0
#define FCF_LOGIN_NEEDED	BIT_1
#define FCF_FCP2_DEVICE		BIT_2
#define FCF_ASYNC_SENT		BIT_3
#define FCF_CONF_COMP_SUPPORTED BIT_4

/* No loop ID flag. */
#define FC_NO_LOOP_ID		0x1000

/*
 * FC-CT interface
 *
 * NOTE: All structures are big-endian in form.
 */

#define CT_REJECT_RESPONSE	0x8001
#define CT_ACCEPT_RESPONSE	0x8002
#define CT_REASON_INVALID_COMMAND_CODE		0x01
#define CT_REASON_CANNOT_PERFORM		0x09
#define CT_REASON_COMMAND_UNSUPPORTED		0x0b
#define CT_EXPL_ALREADY_REGISTERED		0x10
#define CT_EXPL_HBA_ATTR_NOT_REGISTERED		0x11
#define CT_EXPL_MULTIPLE_HBA_ATTR		0x12
#define CT_EXPL_INVALID_HBA_BLOCK_LENGTH	0x13
#define CT_EXPL_MISSING_REQ_HBA_ATTR		0x14
#define CT_EXPL_PORT_NOT_REGISTERED_		0x15
#define CT_EXPL_MISSING_HBA_ID_PORT_LIST	0x16
#define CT_EXPL_HBA_NOT_REGISTERED		0x17
#define CT_EXPL_PORT_ATTR_NOT_REGISTERED	0x20
#define CT_EXPL_PORT_NOT_REGISTERED		0x21
#define CT_EXPL_MULTIPLE_PORT_ATTR		0x22
#define CT_EXPL_INVALID_PORT_BLOCK_LENGTH	0x23

#define NS_N_PORT_TYPE	0x01
#define NS_NL_PORT_TYPE	0x02
#define NS_NX_PORT_TYPE	0x7F

#define	GA_NXT_CMD	0x100
#define	GA_NXT_REQ_SIZE	(16 + 4)
#define	GA_NXT_RSP_SIZE	(16 + 620)

#define	GID_PT_CMD	0x1A1
#define	GID_PT_REQ_SIZE	(16 + 4)

#define	GPN_ID_CMD	0x112
#define	GPN_ID_REQ_SIZE	(16 + 4)
#define	GPN_ID_RSP_SIZE	(16 + 8)

#define	GNN_ID_CMD	0x113
#define	GNN_ID_REQ_SIZE	(16 + 4)
#define	GNN_ID_RSP_SIZE	(16 + 8)

#define	GFT_ID_CMD	0x117
#define	GFT_ID_REQ_SIZE	(16 + 4)
#define	GFT_ID_RSP_SIZE	(16 + 32)

#define GID_PN_CMD 0x121
#define GID_PN_REQ_SIZE (16 + 8)
#define GID_PN_RSP_SIZE (16 + 4)

#define	RFT_ID_CMD	0x217
#define	RFT_ID_REQ_SIZE	(16 + 4 + 32)
#define	RFT_ID_RSP_SIZE	16

#define	RFF_ID_CMD	0x21F
#define	RFF_ID_REQ_SIZE	(16 + 4 + 2 + 1 + 1)
#define	RFF_ID_RSP_SIZE	16

#define	RNN_ID_CMD	0x213
#define	RNN_ID_REQ_SIZE	(16 + 4 + 8)
#define	RNN_ID_RSP_SIZE	16

#define	RSNN_NN_CMD	 0x239
#define	RSNN_NN_REQ_SIZE (16 + 8 + 1 + 255)
#define	RSNN_NN_RSP_SIZE 16

#define	GFPN_ID_CMD	0x11C
#define	GFPN_ID_REQ_SIZE (16 + 4)
#define	GFPN_ID_RSP_SIZE (16 + 8)

#define	GPSC_CMD	0x127
#define	GPSC_REQ_SIZE	(16 + 8)
#define	GPSC_RSP_SIZE	(16 + 2 + 2)

#define GFF_ID_CMD	0x011F
#define GFF_ID_REQ_SIZE	(16 + 4)
#define GFF_ID_RSP_SIZE (16 + 128)

/*
 * HBA attribute types.
 */
#define FDMI_HBA_ATTR_COUNT			9
#define FDMIV2_HBA_ATTR_COUNT			17
#define FDMI_HBA_NODE_NAME			0x1
#define FDMI_HBA_MANUFACTURER			0x2
#define FDMI_HBA_SERIAL_NUMBER			0x3
#define FDMI_HBA_MODEL				0x4
#define FDMI_HBA_MODEL_DESCRIPTION		0x5
#define FDMI_HBA_HARDWARE_VERSION		0x6
#define FDMI_HBA_DRIVER_VERSION			0x7
#define FDMI_HBA_OPTION_ROM_VERSION		0x8
#define FDMI_HBA_FIRMWARE_VERSION		0x9
#define FDMI_HBA_OS_NAME_AND_VERSION		0xa
#define FDMI_HBA_MAXIMUM_CT_PAYLOAD_LENGTH	0xb
#define FDMI_HBA_NODE_SYMBOLIC_NAME		0xc
#define FDMI_HBA_VENDOR_ID			0xd
#define FDMI_HBA_NUM_PORTS			0xe
#define FDMI_HBA_FABRIC_NAME			0xf
#define FDMI_HBA_BOOT_BIOS_NAME			0x10
#define FDMI_HBA_TYPE_VENDOR_IDENTIFIER		0xe0

struct ct_fdmi_hba_attr {
	uint16_t type;
	uint16_t len;
	union {
		uint8_t node_name[WWN_SIZE];
		uint8_t manufacturer[64];
		uint8_t serial_num[32];
		uint8_t model[16+1];
		uint8_t model_desc[80];
		uint8_t hw_version[32];
		uint8_t driver_version[32];
		uint8_t orom_version[16];
		uint8_t fw_version[32];
		uint8_t os_version[128];
		uint32_t max_ct_len;
	} a;
};

struct ct_fdmi_hba_attributes {
	uint32_t count;
	struct ct_fdmi_hba_attr entry[FDMI_HBA_ATTR_COUNT];
};

struct ct_fdmiv2_hba_attr {
	uint16_t type;
	uint16_t len;
	union {
		uint8_t node_name[WWN_SIZE];
		uint8_t manufacturer[64];
		uint8_t serial_num[32];
		uint8_t model[16+1];
		uint8_t model_desc[80];
		uint8_t hw_version[16];
		uint8_t driver_version[32];
		uint8_t orom_version[16];
		uint8_t fw_version[32];
		uint8_t os_version[128];
		uint32_t max_ct_len;
		uint8_t sym_name[256];
		uint32_t vendor_id;
		uint32_t num_ports;
		uint8_t fabric_name[WWN_SIZE];
		uint8_t bios_name[32];
		uint8_t vendor_identifier[8];
	} a;
};

struct ct_fdmiv2_hba_attributes {
	uint32_t count;
	struct ct_fdmiv2_hba_attr entry[FDMIV2_HBA_ATTR_COUNT];
};

/*
 * Port attribute types.
 */
#define FDMI_PORT_ATTR_COUNT		6
#define FDMIV2_PORT_ATTR_COUNT		16
#define FDMI_PORT_FC4_TYPES		0x1
#define FDMI_PORT_SUPPORT_SPEED		0x2
#define FDMI_PORT_CURRENT_SPEED		0x3
#define FDMI_PORT_MAX_FRAME_SIZE	0x4
#define FDMI_PORT_OS_DEVICE_NAME	0x5
#define FDMI_PORT_HOST_NAME		0x6
#define FDMI_PORT_NODE_NAME		0x7
#define FDMI_PORT_NAME			0x8
#define FDMI_PORT_SYM_NAME		0x9
#define FDMI_PORT_TYPE			0xa
#define FDMI_PORT_SUPP_COS		0xb
#define FDMI_PORT_FABRIC_NAME		0xc
#define FDMI_PORT_FC4_TYPE		0xd
#define FDMI_PORT_STATE			0x101
#define FDMI_PORT_COUNT			0x102
#define FDMI_PORT_ID			0x103

#define FDMI_PORT_SPEED_1GB		0x1
#define FDMI_PORT_SPEED_2GB		0x2
#define FDMI_PORT_SPEED_10GB		0x4
#define FDMI_PORT_SPEED_4GB		0x8
#define FDMI_PORT_SPEED_8GB		0x10
#define FDMI_PORT_SPEED_16GB		0x20
#define FDMI_PORT_SPEED_32GB		0x40
#define FDMI_PORT_SPEED_UNKNOWN		0x8000

#define FC_CLASS_2	0x04
#define FC_CLASS_3	0x08
#define FC_CLASS_2_3	0x0C

struct ct_fdmiv2_port_attr {
	uint16_t type;
	uint16_t len;
	union {
		uint8_t fc4_types[32];
		uint32_t sup_speed;
		uint32_t cur_speed;
		uint32_t max_frame_size;
		uint8_t os_dev_name[32];
		uint8_t host_name[256];
		uint8_t node_name[WWN_SIZE];
		uint8_t port_name[WWN_SIZE];
		uint8_t port_sym_name[128];
		uint32_t port_type;
		uint32_t port_supported_cos;
		uint8_t fabric_name[WWN_SIZE];
		uint8_t port_fc4_type[32];
		uint32_t port_state;
		uint32_t num_ports;
		uint32_t port_id;
	} a;
};

/*
 * Port Attribute Block.
 */
struct ct_fdmiv2_port_attributes {
	uint32_t count;
	struct ct_fdmiv2_port_attr entry[FDMIV2_PORT_ATTR_COUNT];
};

struct ct_fdmi_port_attr {
	uint16_t type;
	uint16_t len;
	union {
		uint8_t fc4_types[32];
		uint32_t sup_speed;
		uint32_t cur_speed;
		uint32_t max_frame_size;
		uint8_t os_dev_name[32];
		uint8_t host_name[256];
	} a;
};

struct ct_fdmi_port_attributes {
	uint32_t count;
	struct ct_fdmi_port_attr entry[FDMI_PORT_ATTR_COUNT];
};

/* FDMI definitions. */
#define GRHL_CMD	0x100
#define GHAT_CMD	0x101
#define GRPL_CMD	0x102
#define GPAT_CMD	0x110

#define RHBA_CMD	0x200
#define RHBA_RSP_SIZE	16

#define RHAT_CMD	0x201
#define RPRT_CMD	0x210

#define RPA_CMD		0x211
#define RPA_RSP_SIZE	16

#define DHBA_CMD	0x300
#define DHBA_REQ_SIZE	(16 + 8)
#define DHBA_RSP_SIZE	16

#define DHAT_CMD	0x301
#define DPRT_CMD	0x310
#define DPA_CMD		0x311

/* CT command header -- request/response common fields */
struct ct_cmd_hdr {
	uint8_t revision;
	uint8_t in_id[3];
	uint8_t gs_type;
	uint8_t gs_subtype;
	uint8_t options;
	uint8_t reserved;
};

/* CT command request */
struct ct_sns_req {
	struct ct_cmd_hdr header;
	uint16_t command;
	uint16_t max_rsp_size;
	uint8_t fragment_id;
	uint8_t reserved[3];

	union {
		/* GA_NXT, GPN_ID, GNN_ID, GFT_ID, GFPN_ID */
		struct {
			uint8_t reserved;
			uint8_t port_id[3];
		} port_id;

		struct {
			uint8_t port_type;
			uint8_t domain;
			uint8_t area;
			uint8_t reserved;
		} gid_pt;

		struct {
			uint8_t reserved;
			uint8_t port_id[3];
			uint8_t fc4_types[32];
		} rft_id;

		struct {
			uint8_t reserved;
			uint8_t port_id[3];
			uint16_t reserved2;
			uint8_t fc4_feature;
			uint8_t fc4_type;
		} rff_id;

		struct {
			uint8_t reserved;
			uint8_t port_id[3];
			uint8_t node_name[8];
		} rnn_id;

		struct {
			uint8_t node_name[8];
			uint8_t name_len;
			uint8_t sym_node_name[255];
		} rsnn_nn;

		struct {
			uint8_t hba_identifier[8];
		} ghat;

		struct {
			uint8_t hba_identifier[8];
			uint32_t entry_count;
			uint8_t port_name[8];
			struct ct_fdmi_hba_attributes attrs;
		} rhba;

		struct {
			uint8_t hba_identifier[8];
			uint32_t entry_count;
			uint8_t port_name[8];
			struct ct_fdmiv2_hba_attributes attrs;
		} rhba2;

		struct {
			uint8_t hba_identifier[8];
			struct ct_fdmi_hba_attributes attrs;
		} rhat;

		struct {
			uint8_t port_name[8];
			struct ct_fdmi_port_attributes attrs;
		} rpa;

		struct {
			uint8_t port_name[8];
			struct ct_fdmiv2_port_attributes attrs;
		} rpa2;

		struct {
			uint8_t port_name[8];
		} dhba;

		struct {
			uint8_t port_name[8];
		} dhat;

		struct {
			uint8_t port_name[8];
		} dprt;

		struct {
			uint8_t port_name[8];
		} dpa;

		struct {
			uint8_t port_name[8];
		} gpsc;

		struct {
			uint8_t reserved;
			uint8_t port_id[3];
		} gff_id;

		struct {
			uint8_t port_name[8];
		} gid_pn;
	} req;
};

/* CT command response header */
struct ct_rsp_hdr {
	struct ct_cmd_hdr header;
	uint16_t response;
	uint16_t residual;
	uint8_t fragment_id;
	uint8_t reason_code;
	uint8_t explanation_code;
	uint8_t vendor_unique;
};

struct ct_sns_gid_pt_data {
	uint8_t control_byte;
	uint8_t port_id[3];
};

struct ct_sns_rsp {
	struct ct_rsp_hdr header;

	union {
		struct {
			uint8_t port_type;
			uint8_t port_id[3];
			uint8_t port_name[8];
			uint8_t sym_port_name_len;
			uint8_t sym_port_name[255];
			uint8_t node_name[8];
			uint8_t sym_node_name_len;
			uint8_t sym_node_name[255];
			uint8_t init_proc_assoc[8];
			uint8_t node_ip_addr[16];
			uint8_t class_of_service[4];
			uint8_t fc4_types[32];
			uint8_t ip_address[16];
			uint8_t fabric_port_name[8];
			uint8_t reserved;
			uint8_t hard_address[3];
		} ga_nxt;

		struct {
			/* Assume the largest number of targets for the union */
			struct ct_sns_gid_pt_data
			    entries[MAX_FIBRE_DEVICES_MAX];
		} gid_pt;

		struct {
			uint8_t port_name[8];
		} gpn_id;

		struct {
			uint8_t node_name[8];
		} gnn_id;

		struct {
			uint8_t fc4_types[32];
		} gft_id;

		struct {
			uint32_t entry_count;
			uint8_t port_name[8];
			struct ct_fdmi_hba_attributes attrs;
		} ghat;

		struct {
			uint8_t port_name[8];
		} gfpn_id;

		struct {
			uint16_t speeds;
			uint16_t speed;
		} gpsc;

#define GFF_FCP_SCSI_OFFSET	7
#define GFF_NVME_OFFSET		23 /* type = 28h */
		struct {
			uint8_t fc4_features[128];
		} gff_id;
		struct {
			uint8_t reserved;
			uint8_t port_id[3];
		} gid_pn;
	} rsp;
};

struct ct_sns_pkt {
	union {
		struct ct_sns_req req;
		struct ct_sns_rsp rsp;
	} p;
};

/*
 * SNS command structures -- for 2200 compatibility.
 */
#define	RFT_ID_SNS_SCMD_LEN	22
#define	RFT_ID_SNS_CMD_SIZE	60
#define	RFT_ID_SNS_DATA_SIZE	16

#define	RNN_ID_SNS_SCMD_LEN	10
#define	RNN_ID_SNS_CMD_SIZE	36
#define	RNN_ID_SNS_DATA_SIZE	16

#define	GA_NXT_SNS_SCMD_LEN	6
#define	GA_NXT_SNS_CMD_SIZE	28
#define	GA_NXT_SNS_DATA_SIZE	(620 + 16)

#define	GID_PT_SNS_SCMD_LEN	6
#define	GID_PT_SNS_CMD_SIZE	28
/*
 * Assume MAX_FIBRE_DEVICES_2100 as these defines are only used with older
 * adapters.
 */
#define	GID_PT_SNS_DATA_SIZE	(MAX_FIBRE_DEVICES_2100 * 4 + 16)

#define	GPN_ID_SNS_SCMD_LEN	6
#define	GPN_ID_SNS_CMD_SIZE	28
#define	GPN_ID_SNS_DATA_SIZE	(8 + 16)

#define	GNN_ID_SNS_SCMD_LEN	6
#define	GNN_ID_SNS_CMD_SIZE	28
#define	GNN_ID_SNS_DATA_SIZE	(8 + 16)

struct sns_cmd_pkt {
	union {
		struct {
			uint16_t buffer_length;
			uint16_t reserved_1;
			uint32_t buffer_address[2];
			uint16_t subcommand_length;
			uint16_t reserved_2;
			uint16_t subcommand;
			uint16_t size;
			uint32_t reserved_3;
			uint8_t param[36];
		} cmd;

		uint8_t rft_data[RFT_ID_SNS_DATA_SIZE];
		uint8_t rnn_data[RNN_ID_SNS_DATA_SIZE];
		uint8_t gan_data[GA_NXT_SNS_DATA_SIZE];
		uint8_t gid_data[GID_PT_SNS_DATA_SIZE];
		uint8_t gpn_data[GPN_ID_SNS_DATA_SIZE];
		uint8_t gnn_data[GNN_ID_SNS_DATA_SIZE];
	} p;
};

struct fw_blob {
	char *name;
	uint32_t segs[4];
	const struct firmware *fw;
};

/* Return data from MBC_GET_ID_LIST call. */
struct gid_list_info {
	uint8_t	al_pa;
	uint8_t	area;
	uint8_t	domain;
	uint8_t	loop_id_2100;	/* ISP2100/ISP2200 -- 4 bytes. */
	uint16_t loop_id;	/* ISP23XX         -- 6 bytes. */
	uint16_t reserved_1;	/* ISP24XX         -- 8 bytes. */
};

/* NPIV */
typedef struct vport_info {
	uint8_t		port_name[WWN_SIZE];
	uint8_t		node_name[WWN_SIZE];
	int		vp_id;
	uint16_t	loop_id;
	unsigned long	host_no;
	uint8_t		port_id[3];
	int		loop_state;
} vport_info_t;

typedef struct vport_params {
	uint8_t 	port_name[WWN_SIZE];
	uint8_t 	node_name[WWN_SIZE];
	uint32_t 	options;
#define	VP_OPTS_RETRY_ENABLE	BIT_0
#define	VP_OPTS_VP_DISABLE	BIT_1
} vport_params_t;

/* NPIV - return codes of VP create and modify */
#define VP_RET_CODE_OK			0
#define VP_RET_CODE_FATAL		1
#define VP_RET_CODE_WRONG_ID		2
#define VP_RET_CODE_WWPN		3
#define VP_RET_CODE_RESOURCES		4
#define VP_RET_CODE_NO_MEM		5
#define VP_RET_CODE_NOT_FOUND		6

struct qla_hw_data;
struct rsp_que;
/*
 * ISP operations
 */
struct isp_operations {

	int (*pci_config) (struct scsi_qla_host *);
	void (*reset_chip) (struct scsi_qla_host *);
	int (*chip_diag) (struct scsi_qla_host *);
	void (*config_rings) (struct scsi_qla_host *);
	void (*reset_adapter) (struct scsi_qla_host *);
	int (*nvram_config) (struct scsi_qla_host *);
	void (*update_fw_options) (struct scsi_qla_host *);
	int (*load_risc) (struct scsi_qla_host *, uint32_t *);

	char * (*pci_info_str) (struct scsi_qla_host *, char *);
	char * (*fw_version_str)(struct scsi_qla_host *, char *, size_t);

	irq_handler_t intr_handler;
	void (*enable_intrs) (struct qla_hw_data *);
	void (*disable_intrs) (struct qla_hw_data *);

	int (*abort_command) (srb_t *);
	int (*target_reset) (struct fc_port *, uint64_t, int);
	int (*lun_reset) (struct fc_port *, uint64_t, int);
	int (*fabric_login) (struct scsi_qla_host *, uint16_t, uint8_t,
		uint8_t, uint8_t, uint16_t *, uint8_t);
	int (*fabric_logout) (struct scsi_qla_host *, uint16_t, uint8_t,
	    uint8_t, uint8_t);

	uint16_t (*calc_req_entries) (uint16_t);
	void (*build_iocbs) (srb_t *, cmd_entry_t *, uint16_t);
	void *(*prep_ms_iocb) (struct scsi_qla_host *, struct ct_arg *);
	void *(*prep_ms_fdmi_iocb) (struct scsi_qla_host *, uint32_t,
	    uint32_t);

	uint8_t *(*read_nvram) (struct scsi_qla_host *, uint8_t *,
		uint32_t, uint32_t);
	int (*write_nvram) (struct scsi_qla_host *, uint8_t *, uint32_t,
		uint32_t);

	void (*fw_dump) (struct scsi_qla_host *, int);

	int (*beacon_on) (struct scsi_qla_host *);
	int (*beacon_off) (struct scsi_qla_host *);
	void (*beacon_blink) (struct scsi_qla_host *);

	uint8_t * (*read_optrom) (struct scsi_qla_host *, uint8_t *,
		uint32_t, uint32_t);
	int (*write_optrom) (struct scsi_qla_host *, uint8_t *, uint32_t,
		uint32_t);

	int (*get_flash_version) (struct scsi_qla_host *, void *);
	int (*start_scsi) (srb_t *);
	int (*start_scsi_mq) (srb_t *);
	int (*abort_isp) (struct scsi_qla_host *);
	int (*iospace_config)(struct qla_hw_data*);
	int (*initialize_adapter)(struct scsi_qla_host *);
};

/* MSI-X Support *************************************************************/

#define QLA_MSIX_CHIP_REV_24XX	3
#define QLA_MSIX_FW_MODE(m)	(((m) & (BIT_7|BIT_8|BIT_9)) >> 7)
#define QLA_MSIX_FW_MODE_1(m)	(QLA_MSIX_FW_MODE(m) == 1)

#define QLA_BASE_VECTORS	2 /* default + RSP */
#define QLA_MSIX_RSP_Q			0x01
#define QLA_ATIO_VECTOR		0x02
#define QLA_MSIX_QPAIR_MULTIQ_RSP_Q	0x03

#define QLA_MIDX_DEFAULT	0
#define QLA_MIDX_RSP_Q		1
#define QLA_PCI_MSIX_CONTROL	0xa2
#define QLA_83XX_PCI_MSIX_CONTROL	0x92

struct scsi_qla_host;


#define QLA83XX_RSPQ_MSIX_ENTRY_NUMBER 1 /* refer to qla83xx_msix_entries */

struct qla_msix_entry {
	int have_irq;
	int in_use;
	uint32_t vector;
	uint16_t entry;
	char name[30];
	void *handle;
	int cpuid;
};

#define	WATCH_INTERVAL		1       /* number of seconds */

/* Work events.  */
enum qla_work_type {
	QLA_EVT_AEN,
	QLA_EVT_IDC_ACK,
	QLA_EVT_ASYNC_LOGIN,
	QLA_EVT_ASYNC_LOGOUT,
	QLA_EVT_ASYNC_LOGOUT_DONE,
	QLA_EVT_ASYNC_ADISC,
	QLA_EVT_ASYNC_ADISC_DONE,
	QLA_EVT_UEVENT,
	QLA_EVT_AENFX,
	QLA_EVT_GIDPN,
	QLA_EVT_GPNID,
	QLA_EVT_GPNID_DONE,
	QLA_EVT_NEW_SESS,
	QLA_EVT_GPDB,
	QLA_EVT_PRLI,
	QLA_EVT_GPSC,
	QLA_EVT_UPD_FCPORT,
	QLA_EVT_GNL,
	QLA_EVT_NACK,
};


struct qla_work_evt {
	struct list_head	list;
	enum qla_work_type	type;
	u32			flags;
#define QLA_EVT_FLAG_FREE	0x1

	union {
		struct {
			enum fc_host_event_code code;
			u32 data;
		} aen;
		struct {
#define QLA_IDC_ACK_REGS	7
			uint16_t mb[QLA_IDC_ACK_REGS];
		} idc_ack;
		struct {
			struct fc_port *fcport;
#define QLA_LOGIO_LOGIN_RETRIED	BIT_0
			u16 data[2];
		} logio;
		struct {
			u32 code;
#define QLA_UEVENT_CODE_FW_DUMP	0
		} uevent;
		struct {
			uint32_t        evtcode;
			uint32_t        mbx[8];
			uint32_t        count;
		} aenfx;
		struct {
			srb_t *sp;
		} iosb;
		struct {
			port_id_t id;
		} gpnid;
		struct {
			port_id_t id;
			u8 port_name[8];
			void *pla;
		} new_sess;
		struct { /*Get PDB, Get Speed, update fcport, gnl, gidpn */
			fc_port_t *fcport;
			u8 opt;
		} fcport;
		struct {
			fc_port_t *fcport;
			u8 iocb[IOCB_SIZE];
			int type;
		} nack;
	 } u;
};

struct qla_chip_state_84xx {
	struct list_head list;
	struct kref kref;

	void *bus;
	spinlock_t access_lock;
	struct mutex fw_update_mutex;
	uint32_t fw_update;
	uint32_t op_fw_version;
	uint32_t op_fw_size;
	uint32_t op_fw_seq_size;
	uint32_t diag_fw_version;
	uint32_t gold_fw_version;
};

struct qla_dif_statistics {
	uint64_t dif_input_bytes;
	uint64_t dif_output_bytes;
	uint64_t dif_input_requests;
	uint64_t dif_output_requests;
	uint32_t dif_guard_err;
	uint32_t dif_ref_tag_err;
	uint32_t dif_app_tag_err;
};

struct qla_statistics {
	uint32_t total_isp_aborts;
	uint64_t input_bytes;
	uint64_t output_bytes;
	uint64_t input_requests;
	uint64_t output_requests;
	uint32_t control_requests;

	uint64_t jiffies_at_last_reset;
	uint32_t stat_max_pend_cmds;
	uint32_t stat_max_qfull_cmds_alloc;
	uint32_t stat_max_qfull_cmds_dropped;

	struct qla_dif_statistics qla_dif_stats;
};

struct bidi_statistics {
	unsigned long long io_count;
	unsigned long long transfer_bytes;
};

struct qla_tc_param {
	struct scsi_qla_host *vha;
	uint32_t blk_sz;
	uint32_t bufflen;
	struct scatterlist *sg;
	struct scatterlist *prot_sg;
	struct crc_context *ctx;
	uint8_t *ctx_dsd_alloced;
};

/* Multi queue support */
#define MBC_INITIALIZE_MULTIQ 0x1f
#define QLA_QUE_PAGE 0X1000
#define QLA_MQ_SIZE 32
#define QLA_MAX_QUEUES 256
#define ISP_QUE_REG(ha, id) \
	((ha->mqenable || IS_QLA83XX(ha) || IS_QLA27XX(ha)) ? \
	 ((void __iomem *)ha->mqiobase + (QLA_QUE_PAGE * id)) :\
	 ((void __iomem *)ha->iobase))
#define QLA_REQ_QUE_ID(tag) \
	((tag < QLA_MAX_QUEUES && tag > 0) ? tag : 0)
#define QLA_DEFAULT_QUE_QOS 5
#define QLA_PRECONFIG_VPORTS 32
#define QLA_MAX_VPORTS_QLA24XX	128
#define QLA_MAX_VPORTS_QLA25XX	256

struct qla_tgt_counters {
	uint64_t qla_core_sbt_cmd;
	uint64_t core_qla_que_buf;
	uint64_t qla_core_ret_ctio;
	uint64_t core_qla_snd_status;
	uint64_t qla_core_ret_sta_ctio;
	uint64_t core_qla_free_cmd;
	uint64_t num_q_full_sent;
	uint64_t num_alloc_iocb_failed;
	uint64_t num_term_xchg_sent;
};

struct qla_qpair;

/* Response queue data structure */
struct rsp_que {
	dma_addr_t  dma;
	response_t *ring;
	response_t *ring_ptr;
	uint32_t __iomem *rsp_q_in;	/* FWI2-capable only. */
	uint32_t __iomem *rsp_q_out;
	uint16_t  ring_index;
	uint16_t  out_ptr;
	uint16_t  *in_ptr;		/* queue shadow in index */
	uint16_t  length;
	uint16_t  options;
	uint16_t  rid;
	uint16_t  id;
	uint16_t  vp_idx;
	struct qla_hw_data *hw;
	struct qla_msix_entry *msix;
	struct req_que *req;
	srb_t *status_srb; /* status continuation entry */
	struct qla_qpair *qpair;

	dma_addr_t  dma_fx00;
	response_t *ring_fx00;
	uint16_t  length_fx00;
	uint8_t rsp_pkt[REQUEST_ENTRY_SIZE];
};

/* Request queue data structure */
struct req_que {
	dma_addr_t  dma;
	request_t *ring;
	request_t *ring_ptr;
	uint32_t __iomem *req_q_in;	/* FWI2-capable only. */
	uint32_t __iomem *req_q_out;
	uint16_t  ring_index;
	uint16_t  in_ptr;
	uint16_t  *out_ptr;		/* queue shadow out index */
	uint16_t  cnt;
	uint16_t  length;
	uint16_t  options;
	uint16_t  rid;
	uint16_t  id;
	uint16_t  qos;
	uint16_t  vp_idx;
	struct rsp_que *rsp;
	srb_t **outstanding_cmds;
	uint32_t current_outstanding_cmd;
	uint16_t num_outstanding_cmds;
	int max_q_depth;

	dma_addr_t  dma_fx00;
	request_t *ring_fx00;
	uint16_t  length_fx00;
	uint8_t req_pkt[REQUEST_ENTRY_SIZE];
};

/*Queue pair data structure */
struct qla_qpair {
	spinlock_t qp_lock;
	atomic_t ref_count;
	uint32_t lun_cnt;
	/*
	 * For qpair 0, qp_lock_ptr will point at hardware_lock due to
	 * legacy code. For other Qpair(s), it will point at qp_lock.
	 */
	spinlock_t *qp_lock_ptr;
	struct scsi_qla_host *vha;
	u32 chip_reset;

	/* distill these fields down to 'online=0/1'
	 * ha->flags.eeh_busy
	 * ha->flags.pci_channel_io_perm_failure
	 * base_vha->loop_state
	 */
	uint32_t online:1;
	/* move vha->flags.difdix_supported here */
	uint32_t difdix_supported:1;
	uint32_t delete_in_progress:1;
	uint32_t fw_started:1;
	uint32_t enable_class_2:1;
	uint32_t enable_explicit_conf:1;
	uint32_t use_shadow_reg:1;

	uint16_t id;			/* qp number used with FW */
	uint16_t vp_idx;		/* vport ID */
	mempool_t *srb_mempool;

	struct pci_dev  *pdev;
	void (*reqq_start_iocbs)(struct qla_qpair *);

	/* to do: New driver: move queues to here instead of pointers */
	struct req_que *req;
	struct rsp_que *rsp;
	struct atio_que *atio;
	struct qla_msix_entry *msix; /* point to &ha->msix_entries[x] */
	struct qla_hw_data *hw;
	struct work_struct q_work;
	struct list_head qp_list_elem; /* vha->qp_list */
	struct list_head hints_list;
	struct list_head nvme_done_list;
	uint16_t cpuid;
	struct qla_tgt_counters tgt_counters;
};

/* Place holder for FW buffer parameters */
struct qlfc_fw {
	void *fw_buf;
	dma_addr_t fw_dma;
	uint32_t len;
};

struct scsi_qlt_host {
	void *target_lport_ptr;
	struct mutex tgt_mutex;
	struct mutex tgt_host_action_mutex;
	struct qla_tgt *qla_tgt;
};

struct qlt_hw_data {
	/* Protected by hw lock */
	uint32_t node_name_set:1;

	dma_addr_t atio_dma;	/* Physical address. */
	struct atio *atio_ring;	/* Base virtual address */
	struct atio *atio_ring_ptr;	/* Current address. */
	uint16_t atio_ring_index; /* Current index. */
	uint16_t atio_q_length;
	uint32_t __iomem *atio_q_in;
	uint32_t __iomem *atio_q_out;

	struct qla_tgt_func_tmpl *tgt_ops;
	struct qla_tgt_vp_map *tgt_vp_map;

	int saved_set;
	uint16_t saved_exchange_count;
	uint32_t saved_firmware_options_1;
	uint32_t saved_firmware_options_2;
	uint32_t saved_firmware_options_3;
	uint8_t saved_firmware_options[2];
	uint8_t saved_add_firmware_options[2];

	uint8_t tgt_node_name[WWN_SIZE];

	struct dentry *dfs_tgt_sess;
	struct dentry *dfs_tgt_port_database;
	struct dentry *dfs_naqp;

	struct list_head q_full_list;
	uint32_t num_pend_cmds;
	uint32_t num_qfull_cmds_alloc;
	uint32_t num_qfull_cmds_dropped;
	spinlock_t q_full_lock;
	uint32_t leak_exchg_thresh_hold;
	spinlock_t sess_lock;
	int num_act_qpairs;
#define DEFAULT_NAQP 2
	spinlock_t atio_lock ____cacheline_aligned;
	struct btree_head32 host_map;
};

#define MAX_QFULL_CMDS_ALLOC	8192
#define Q_FULL_THRESH_HOLD_PERCENT 90
#define Q_FULL_THRESH_HOLD(ha) \
	((ha->cur_fw_xcb_count/100) * Q_FULL_THRESH_HOLD_PERCENT)

#define LEAK_EXCHG_THRESH_HOLD_PERCENT 75	/* 75 percent */

#define QLA_EARLY_LINKUP(_ha) \
	((_ha->flags.n2n_ae || _ha->flags.lip_ae) && \
	 _ha->flags.fw_started && !_ha->flags.fw_init_done)

/*
 * Qlogic host adapter specific data structure.
*/
struct qla_hw_data {
	struct pci_dev  *pdev;
	/* SRB cache. */
#define SRB_MIN_REQ     128
	mempool_t       *srb_mempool;

	volatile struct {
		uint32_t	mbox_int		:1;
		uint32_t	mbox_busy		:1;
		uint32_t	disable_risc_code_load	:1;
		uint32_t	enable_64bit_addressing	:1;
		uint32_t	enable_lip_reset	:1;
		uint32_t	enable_target_reset	:1;
		uint32_t	enable_lip_full_login	:1;
		uint32_t	enable_led_scheme	:1;

		uint32_t	msi_enabled		:1;
		uint32_t	msix_enabled		:1;
		uint32_t	disable_serdes		:1;
		uint32_t	gpsc_supported		:1;
		uint32_t	npiv_supported		:1;
		uint32_t	pci_channel_io_perm_failure	:1;
		uint32_t	fce_enabled		:1;
		uint32_t	fac_supported		:1;

		uint32_t	chip_reset_done		:1;
		uint32_t	running_gold_fw		:1;
		uint32_t	eeh_busy		:1;
		uint32_t	disable_msix_handshake	:1;
		uint32_t	fcp_prio_enabled	:1;
		uint32_t	isp82xx_fw_hung:1;
		uint32_t	nic_core_hung:1;

		uint32_t	quiesce_owner:1;
		uint32_t	nic_core_reset_hdlr_active:1;
		uint32_t	nic_core_reset_owner:1;
		uint32_t	isp82xx_no_md_cap:1;
		uint32_t	host_shutting_down:1;
		uint32_t	idc_compl_status:1;
		uint32_t        mr_reset_hdlr_active:1;
		uint32_t        mr_intr_valid:1;

		uint32_t        dport_enabled:1;
		uint32_t	fawwpn_enabled:1;
		uint32_t	exlogins_enabled:1;
		uint32_t	exchoffld_enabled:1;

		uint32_t	lip_ae:1;
		uint32_t	n2n_ae:1;
		uint32_t	fw_started:1;
		uint32_t	fw_init_done:1;

		uint32_t	detected_lr_sfp:1;
		uint32_t	using_lr_setting:1;
	} flags;

	uint16_t long_range_distance;	/* 32G & above */
#define LR_DISTANCE_5K  1
#define LR_DISTANCE_10K 0

	/* This spinlock is used to protect "io transactions", you must
	* acquire it before doing any IO to the card, eg with RD_REG*() and
	* WRT_REG*() for the duration of your entire commandtransaction.
	*
	* This spinlock is of lower priority than the io request lock.
	*/

	spinlock_t	hardware_lock ____cacheline_aligned;
	int		bars;
	int		mem_only;
	device_reg_t *iobase;           /* Base I/O address */
	resource_size_t pio_address;

#define MIN_IOBASE_LEN          0x100
	dma_addr_t		bar0_hdl;

	void __iomem *cregbase;
	dma_addr_t		bar2_hdl;
#define BAR0_LEN_FX00			(1024 * 1024)
#define BAR2_LEN_FX00			(128 * 1024)

	uint32_t		rqstq_intr_code;
	uint32_t		mbx_intr_code;
	uint32_t		req_que_len;
	uint32_t		rsp_que_len;
	uint32_t		req_que_off;
	uint32_t		rsp_que_off;

	/* Multi queue data structs */
	device_reg_t *mqiobase;
	device_reg_t *msixbase;
	uint16_t        msix_count;
	uint8_t         mqenable;
	struct req_que **req_q_map;
	struct rsp_que **rsp_q_map;
	struct qla_qpair **queue_pair_map;
	unsigned long req_qid_map[(QLA_MAX_QUEUES / 8) / sizeof(unsigned long)];
	unsigned long rsp_qid_map[(QLA_MAX_QUEUES / 8) / sizeof(unsigned long)];
	unsigned long qpair_qid_map[(QLA_MAX_QUEUES / 8)
		/ sizeof(unsigned long)];
	uint8_t 	max_req_queues;
	uint8_t 	max_rsp_queues;
	uint8_t		max_qpairs;
	uint8_t		num_qpairs;
	struct qla_qpair *base_qpair;
	struct qla_npiv_entry *npiv_info;
	uint16_t	nvram_npiv_size;

	uint16_t        switch_cap;
#define FLOGI_SEQ_DEL           BIT_8
#define FLOGI_MID_SUPPORT       BIT_10
#define FLOGI_VSAN_SUPPORT      BIT_12
#define FLOGI_SP_SUPPORT        BIT_13

	uint8_t		port_no;		/* Physical port of adapter */
	uint8_t		exch_starvation;

	/* Timeout timers. */
	uint8_t 	loop_down_abort_time;    /* port down timer */
	atomic_t	loop_down_timer;         /* loop down timer */
	uint8_t		link_down_timeout;       /* link down timeout */
	uint16_t	max_loop_id;
	uint16_t	max_fibre_devices;	/* Maximum number of targets */

	uint16_t	fb_rev;
	uint16_t	min_external_loopid;    /* First external loop Id */

#define PORT_SPEED_UNKNOWN 0xFFFF
#define PORT_SPEED_1GB  0x00
#define PORT_SPEED_2GB  0x01
#define PORT_SPEED_4GB  0x03
#define PORT_SPEED_8GB  0x04
#define PORT_SPEED_16GB 0x05
#define PORT_SPEED_32GB 0x06
#define PORT_SPEED_10GB	0x13
	uint16_t	link_data_rate;         /* F/W operating speed */

	uint8_t		current_topology;
	uint8_t		prev_topology;
#define ISP_CFG_NL	1
#define ISP_CFG_N	2
#define ISP_CFG_FL	4
#define ISP_CFG_F	8

	uint8_t		operating_mode;         /* F/W operating mode */
#define LOOP      0
#define P2P       1
#define LOOP_P2P  2
#define P2P_LOOP  3
	uint8_t		interrupts_on;
	uint32_t	isp_abort_cnt;
#define PCI_DEVICE_ID_QLOGIC_ISP2532    0x2532
#define PCI_DEVICE_ID_QLOGIC_ISP8432    0x8432
#define PCI_DEVICE_ID_QLOGIC_ISP8001	0x8001
#define PCI_DEVICE_ID_QLOGIC_ISP8031	0x8031
#define PCI_DEVICE_ID_QLOGIC_ISP2031	0x2031
#define PCI_DEVICE_ID_QLOGIC_ISP2071	0x2071
#define PCI_DEVICE_ID_QLOGIC_ISP2271	0x2271
#define PCI_DEVICE_ID_QLOGIC_ISP2261	0x2261

	uint32_t	isp_type;
#define DT_ISP2100                      BIT_0
#define DT_ISP2200                      BIT_1
#define DT_ISP2300                      BIT_2
#define DT_ISP2312                      BIT_3
#define DT_ISP2322                      BIT_4
#define DT_ISP6312                      BIT_5
#define DT_ISP6322                      BIT_6
#define DT_ISP2422                      BIT_7
#define DT_ISP2432                      BIT_8
#define DT_ISP5422                      BIT_9
#define DT_ISP5432                      BIT_10
#define DT_ISP2532                      BIT_11
#define DT_ISP8432                      BIT_12
#define DT_ISP8001			BIT_13
#define DT_ISP8021			BIT_14
#define DT_ISP2031			BIT_15
#define DT_ISP8031			BIT_16
#define DT_ISPFX00			BIT_17
#define DT_ISP8044			BIT_18
#define DT_ISP2071			BIT_19
#define DT_ISP2271			BIT_20
#define DT_ISP2261			BIT_21
#define DT_ISP_LAST			(DT_ISP2261 << 1)

	uint32_t	device_type;
#define DT_T10_PI                       BIT_25
#define DT_IIDMA                        BIT_26
#define DT_FWI2                         BIT_27
#define DT_ZIO_SUPPORTED                BIT_28
#define DT_OEM_001                      BIT_29
#define DT_ISP2200A                     BIT_30
#define DT_EXTENDED_IDS                 BIT_31

#define DT_MASK(ha)     ((ha)->isp_type & (DT_ISP_LAST - 1))
#define IS_QLA2100(ha)  (DT_MASK(ha) & DT_ISP2100)
#define IS_QLA2200(ha)  (DT_MASK(ha) & DT_ISP2200)
#define IS_QLA2300(ha)  (DT_MASK(ha) & DT_ISP2300)
#define IS_QLA2312(ha)  (DT_MASK(ha) & DT_ISP2312)
#define IS_QLA2322(ha)  (DT_MASK(ha) & DT_ISP2322)
#define IS_QLA6312(ha)  (DT_MASK(ha) & DT_ISP6312)
#define IS_QLA6322(ha)  (DT_MASK(ha) & DT_ISP6322)
#define IS_QLA2422(ha)  (DT_MASK(ha) & DT_ISP2422)
#define IS_QLA2432(ha)  (DT_MASK(ha) & DT_ISP2432)
#define IS_QLA5422(ha)  (DT_MASK(ha) & DT_ISP5422)
#define IS_QLA5432(ha)  (DT_MASK(ha) & DT_ISP5432)
#define IS_QLA2532(ha)  (DT_MASK(ha) & DT_ISP2532)
#define IS_QLA8432(ha)  (DT_MASK(ha) & DT_ISP8432)
#define IS_QLA8001(ha)	(DT_MASK(ha) & DT_ISP8001)
#define IS_QLA81XX(ha)	(IS_QLA8001(ha))
#define IS_QLA82XX(ha)	(DT_MASK(ha) & DT_ISP8021)
#define IS_QLA8044(ha)  (DT_MASK(ha) & DT_ISP8044)
#define IS_QLA2031(ha)	(DT_MASK(ha) & DT_ISP2031)
#define IS_QLA8031(ha)	(DT_MASK(ha) & DT_ISP8031)
#define IS_QLAFX00(ha)	(DT_MASK(ha) & DT_ISPFX00)
#define IS_QLA2071(ha)	(DT_MASK(ha) & DT_ISP2071)
#define IS_QLA2271(ha)	(DT_MASK(ha) & DT_ISP2271)
#define IS_QLA2261(ha)	(DT_MASK(ha) & DT_ISP2261)

#define IS_QLA23XX(ha)  (IS_QLA2300(ha) || IS_QLA2312(ha) || IS_QLA2322(ha) || \
			IS_QLA6312(ha) || IS_QLA6322(ha))
#define IS_QLA24XX(ha)  (IS_QLA2422(ha) || IS_QLA2432(ha))
#define IS_QLA54XX(ha)  (IS_QLA5422(ha) || IS_QLA5432(ha))
#define IS_QLA25XX(ha)  (IS_QLA2532(ha))
#define IS_QLA83XX(ha)	(IS_QLA2031(ha) || IS_QLA8031(ha))
#define IS_QLA84XX(ha)  (IS_QLA8432(ha))
#define IS_QLA27XX(ha)  (IS_QLA2071(ha) || IS_QLA2271(ha) || IS_QLA2261(ha))
#define IS_QLA24XX_TYPE(ha)     (IS_QLA24XX(ha) || IS_QLA54XX(ha) || \
				IS_QLA84XX(ha))
#define IS_CNA_CAPABLE(ha)	(IS_QLA81XX(ha) || IS_QLA82XX(ha) || \
				IS_QLA8031(ha) || IS_QLA8044(ha))
#define IS_P3P_TYPE(ha)		(IS_QLA82XX(ha) || IS_QLA8044(ha))
#define IS_QLA2XXX_MIDTYPE(ha)	(IS_QLA24XX(ha) || IS_QLA84XX(ha) || \
				IS_QLA25XX(ha) || IS_QLA81XX(ha) || \
				IS_QLA82XX(ha) || IS_QLA83XX(ha) || \
				IS_QLA8044(ha) || IS_QLA27XX(ha))
#define IS_MSIX_NACK_CAPABLE(ha) (IS_QLA81XX(ha) || IS_QLA83XX(ha) || \
				IS_QLA27XX(ha))
#define IS_NOPOLLING_TYPE(ha)	(IS_QLA81XX(ha) && (ha)->flags.msix_enabled)
#define IS_FAC_REQUIRED(ha)	(IS_QLA81XX(ha) || IS_QLA83XX(ha) || \
				IS_QLA27XX(ha))
#define IS_NOCACHE_VPD_TYPE(ha)	(IS_QLA81XX(ha) || IS_QLA83XX(ha) || \
				IS_QLA27XX(ha))
#define IS_ALOGIO_CAPABLE(ha)	(IS_QLA23XX(ha) || IS_FWI2_CAPABLE(ha))

#define IS_T10_PI_CAPABLE(ha)   ((ha)->device_type & DT_T10_PI)
#define IS_IIDMA_CAPABLE(ha)    ((ha)->device_type & DT_IIDMA)
#define IS_FWI2_CAPABLE(ha)     ((ha)->device_type & DT_FWI2)
#define IS_ZIO_SUPPORTED(ha)    ((ha)->device_type & DT_ZIO_SUPPORTED)
#define IS_OEM_001(ha)          ((ha)->device_type & DT_OEM_001)
#define HAS_EXTENDED_IDS(ha)    ((ha)->device_type & DT_EXTENDED_IDS)
#define IS_CT6_SUPPORTED(ha)	((ha)->device_type & DT_CT6_SUPPORTED)
#define IS_MQUE_CAPABLE(ha)	((ha)->mqenable || IS_QLA83XX(ha) || \
				IS_QLA27XX(ha))
#define IS_BIDI_CAPABLE(ha)	((IS_QLA25XX(ha) || IS_QLA2031(ha)))
/* Bit 21 of fw_attributes decides the MCTP capabilities */
#define IS_MCTP_CAPABLE(ha)	(IS_QLA2031(ha) && \
				((ha)->fw_attributes_ext[0] & BIT_0))
#define IS_PI_UNINIT_CAPABLE(ha)	(IS_QLA83XX(ha) || IS_QLA27XX(ha))
#define IS_PI_IPGUARD_CAPABLE(ha)	(IS_QLA83XX(ha) || IS_QLA27XX(ha))
#define IS_PI_DIFB_DIX0_CAPABLE(ha)	(0)
#define IS_PI_SPLIT_DET_CAPABLE_HBA(ha)	(IS_QLA83XX(ha) || IS_QLA27XX(ha))
#define IS_PI_SPLIT_DET_CAPABLE(ha)	(IS_PI_SPLIT_DET_CAPABLE_HBA(ha) && \
    (((ha)->fw_attributes_h << 16 | (ha)->fw_attributes) & BIT_22))
#define IS_ATIO_MSIX_CAPABLE(ha) (IS_QLA83XX(ha) || IS_QLA27XX(ha))
#define IS_TGT_MODE_CAPABLE(ha)	(ha->tgt.atio_q_length)
#define IS_SHADOW_REG_CAPABLE(ha)  (IS_QLA27XX(ha))
#define IS_DPORT_CAPABLE(ha)  (IS_QLA83XX(ha) || IS_QLA27XX(ha))
#define IS_FAWWN_CAPABLE(ha)	(IS_QLA83XX(ha) || IS_QLA27XX(ha))
#define IS_EXCHG_OFFLD_CAPABLE(ha) \
	(IS_QLA81XX(ha) || IS_QLA83XX(ha) || IS_QLA27XX(ha))
#define IS_EXLOGIN_OFFLD_CAPABLE(ha) \
	(IS_QLA25XX(ha) || IS_QLA81XX(ha) || IS_QLA83XX(ha) || IS_QLA27XX(ha))

	/* HBA serial number */
	uint8_t		serial0;
	uint8_t		serial1;
	uint8_t		serial2;

	/* NVRAM configuration data */
#define MAX_NVRAM_SIZE  4096
#define VPD_OFFSET      MAX_NVRAM_SIZE / 2
	uint16_t	nvram_size;
	uint16_t	nvram_base;
	void		*nvram;
	uint16_t	vpd_size;
	uint16_t	vpd_base;
	void		*vpd;

	uint16_t	loop_reset_delay;
	uint8_t		retry_count;
	uint8_t		login_timeout;
	uint16_t	r_a_tov;
	int		port_down_retry_count;
	uint8_t		mbx_count;
	uint8_t		aen_mbx_count;

	uint32_t	login_retry_count;
	/* SNS command interfaces. */
	ms_iocb_entry_t		*ms_iocb;
	dma_addr_t		ms_iocb_dma;
	struct ct_sns_pkt	*ct_sns;
	dma_addr_t		ct_sns_dma;
	/* SNS command interfaces for 2200. */
	struct sns_cmd_pkt	*sns_cmd;
	dma_addr_t		sns_cmd_dma;

#define SFP_DEV_SIZE    512
#define SFP_BLOCK_SIZE  64
	void		*sfp_data;
	dma_addr_t	sfp_data_dma;

#define XGMAC_DATA_SIZE	4096
	void		*xgmac_data;
	dma_addr_t	xgmac_data_dma;

#define DCBX_TLV_DATA_SIZE 4096
	void		*dcbx_tlv;
	dma_addr_t	dcbx_tlv_dma;

	struct task_struct	*dpc_thread;
	uint8_t dpc_active;                  /* DPC routine is active */

	dma_addr_t	gid_list_dma;
	struct gid_list_info *gid_list;
	int		gid_list_info_size;

	/* Small DMA pool allocations -- maximum 256 bytes in length. */
#define DMA_POOL_SIZE   256
	struct dma_pool *s_dma_pool;

	dma_addr_t	init_cb_dma;
	init_cb_t	*init_cb;
	int		init_cb_size;
	dma_addr_t	ex_init_cb_dma;
	struct ex_init_cb_81xx *ex_init_cb;

	void		*async_pd;
	dma_addr_t	async_pd_dma;

#define ENABLE_EXTENDED_LOGIN	BIT_7

	/* Extended Logins  */
	void		*exlogin_buf;
	dma_addr_t	exlogin_buf_dma;
	int		exlogin_size;

#define ENABLE_EXCHANGE_OFFLD	BIT_2

	/* Exchange Offload */
	void		*exchoffld_buf;
	dma_addr_t	exchoffld_buf_dma;
	int		exchoffld_size;
	int 		exchoffld_count;

	void		*swl;

	/* These are used by mailbox operations. */
	uint16_t mailbox_out[MAILBOX_REGISTER_COUNT];
	uint32_t mailbox_out32[MAILBOX_REGISTER_COUNT];
	uint32_t aenmb[AEN_MAILBOX_REGISTER_COUNT_FX00];

	mbx_cmd_t	*mcp;
	struct mbx_cmd_32	*mcp32;

	unsigned long	mbx_cmd_flags;
#define MBX_INTERRUPT		1
#define MBX_INTR_WAIT		2
#define MBX_UPDATE_FLASH_ACTIVE	3

	struct mutex vport_lock;        /* Virtual port synchronization */
	spinlock_t vport_slock; /* order is hardware_lock, then vport_slock */
	struct mutex mq_lock;        /* multi-queue synchronization */
	struct completion mbx_cmd_comp; /* Serialize mbx access */
	struct completion mbx_intr_comp;  /* Used for completion notification */
	struct completion dcbx_comp;	/* For set port config notification */
	struct completion lb_portup_comp; /* Used to wait for link up during
					   * loopback */
#define DCBX_COMP_TIMEOUT	20
#define LB_PORTUP_COMP_TIMEOUT	10

	int notify_dcbx_comp;
	int notify_lb_portup_comp;
	struct mutex selflogin_lock;

	/* Basic firmware related information. */
	uint16_t	fw_major_version;
	uint16_t	fw_minor_version;
	uint16_t	fw_subminor_version;
	uint16_t	fw_attributes;
	uint16_t	fw_attributes_h;
	uint16_t	fw_attributes_ext[2];
	uint32_t	fw_memory_size;
	uint32_t	fw_transfer_size;
	uint32_t	fw_srisc_address;
#define RISC_START_ADDRESS_2100 0x1000
#define RISC_START_ADDRESS_2300 0x800
#define RISC_START_ADDRESS_2400 0x100000

	uint16_t	orig_fw_tgt_xcb_count;
	uint16_t	cur_fw_tgt_xcb_count;
	uint16_t	orig_fw_xcb_count;
	uint16_t	cur_fw_xcb_count;
	uint16_t	orig_fw_iocb_count;
	uint16_t	cur_fw_iocb_count;
	uint16_t	fw_max_fcf_count;

	uint32_t	fw_shared_ram_start;
	uint32_t	fw_shared_ram_end;
	uint32_t	fw_ddr_ram_start;
	uint32_t	fw_ddr_ram_end;

	uint16_t	fw_options[16];         /* slots: 1,2,3,10,11 */
	uint8_t		fw_seriallink_options[4];
	uint16_t	fw_seriallink_options24[4];

	uint8_t		mpi_version[3];
	uint32_t	mpi_capabilities;
	uint8_t		phy_version[3];
	uint8_t		pep_version[3];

	/* Firmware dump template */
	void		*fw_dump_template;
	uint32_t	fw_dump_template_len;
	/* Firmware dump information. */
	struct qla2xxx_fw_dump *fw_dump;
	uint32_t	fw_dump_len;
	int		fw_dumped;
	unsigned long	fw_dump_cap_flags;
#define RISC_PAUSE_CMPL		0
#define DMA_SHUTDOWN_CMPL	1
#define ISP_RESET_CMPL		2
#define RISC_RDY_AFT_RESET	3
#define RISC_SRAM_DUMP_CMPL	4
#define RISC_EXT_MEM_DUMP_CMPL	5
#define ISP_MBX_RDY		6
#define ISP_SOFT_RESET_CMPL	7
	int		fw_dump_reading;
	int		prev_minidump_failed;
	dma_addr_t	eft_dma;
	void		*eft;
/* Current size of mctp dump is 0x086064 bytes */
#define MCTP_DUMP_SIZE  0x086064
	dma_addr_t	mctp_dump_dma;
	void		*mctp_dump;
	int		mctp_dumped;
	int		mctp_dump_reading;
	uint32_t	chain_offset;
	struct dentry *dfs_dir;
	struct dentry *dfs_fce;
	struct dentry *dfs_tgt_counters;
	struct dentry *dfs_fw_resource_cnt;

	dma_addr_t	fce_dma;
	void		*fce;
	uint32_t	fce_bufs;
	uint16_t	fce_mb[8];
	uint64_t	fce_wr, fce_rd;
	struct mutex	fce_mutex;

	uint32_t	pci_attr;
	uint16_t	chip_revision;

	uint16_t	product_id[4];

	uint8_t		model_number[16+1];
#define BINZERO		"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	char		model_desc[80];
	uint8_t		adapter_id[16+1];

	/* Option ROM information. */
	char		*optrom_buffer;
	uint32_t	optrom_size;
	int		optrom_state;
#define QLA_SWAITING	0
#define QLA_SREADING	1
#define QLA_SWRITING	2
	uint32_t	optrom_region_start;
	uint32_t	optrom_region_size;
	struct mutex	optrom_mutex;

/* PCI expansion ROM image information. */
#define ROM_CODE_TYPE_BIOS	0
#define ROM_CODE_TYPE_FCODE	1
#define ROM_CODE_TYPE_EFI	3
	uint8_t 	bios_revision[2];
	uint8_t 	efi_revision[2];
	uint8_t 	fcode_revision[16];
	uint32_t	fw_revision[4];

	uint32_t	gold_fw_version[4];

	/* Offsets for flash/nvram access (set to ~0 if not used). */
	uint32_t	flash_conf_off;
	uint32_t	flash_data_off;
	uint32_t	nvram_conf_off;
	uint32_t	nvram_data_off;

	uint32_t	fdt_wrt_disable;
	uint32_t	fdt_wrt_enable;
	uint32_t	fdt_erase_cmd;
	uint32_t	fdt_block_size;
	uint32_t	fdt_unprotect_sec_cmd;
	uint32_t	fdt_protect_sec_cmd;
	uint32_t	fdt_wrt_sts_reg_cmd;

	uint32_t        flt_region_flt;
	uint32_t        flt_region_fdt;
	uint32_t        flt_region_boot;
	uint32_t        flt_region_boot_sec;
	uint32_t        flt_region_fw;
	uint32_t        flt_region_fw_sec;
	uint32_t        flt_region_vpd_nvram;
	uint32_t        flt_region_vpd;
	uint32_t        flt_region_vpd_sec;
	uint32_t        flt_region_nvram;
	uint32_t        flt_region_npiv_conf;
	uint32_t	flt_region_gold_fw;
	uint32_t	flt_region_fcp_prio;
	uint32_t	flt_region_bootload;
	uint32_t	flt_region_img_status_pri;
	uint32_t	flt_region_img_status_sec;
	uint8_t         active_image;

	/* Needed for BEACON */
	uint16_t        beacon_blink_led;
	uint8_t         beacon_color_state;
#define QLA_LED_GRN_ON		0x01
#define QLA_LED_YLW_ON		0x02
#define QLA_LED_ABR_ON		0x04
#define QLA_LED_ALL_ON		0x07	/* yellow, green, amber. */
					/* ISP2322: red, green, amber. */
	uint16_t        zio_mode;
	uint16_t        zio_timer;

	struct qla_msix_entry *msix_entries;

	struct list_head        vp_list;        /* list of VP */
	unsigned long   vp_idx_map[(MAX_MULTI_ID_FABRIC / 8) /
			sizeof(unsigned long)];
	uint16_t        num_vhosts;     /* number of vports created */
	uint16_t        num_vsans;      /* number of vsan created */
	uint16_t        max_npiv_vports;        /* 63 or 125 per topoloty */
	int             cur_vport_count;

	struct qla_chip_state_84xx *cs84xx;
	struct isp_operations *isp_ops;
	struct workqueue_struct *wq;
	struct qlfc_fw fw_buf;

	/* FCP_CMND priority support */
	struct qla_fcp_prio_cfg *fcp_prio_cfg;

	struct dma_pool *dl_dma_pool;
#define DSD_LIST_DMA_POOL_SIZE  512

	struct dma_pool *fcp_cmnd_dma_pool;
	mempool_t       *ctx_mempool;
#define FCP_CMND_DMA_POOL_SIZE 512

	void __iomem	*nx_pcibase;		/* Base I/O address */
	void __iomem	*nxdb_rd_ptr;		/* Doorbell read pointer */
	void __iomem	*nxdb_wr_ptr;		/* Door bell write pointer */

	uint32_t	crb_win;
	uint32_t	curr_window;
	uint32_t	ddr_mn_window;
	unsigned long	mn_win_crb;
	unsigned long	ms_win_crb;
	int		qdr_sn_window;
	uint32_t	fcoe_dev_init_timeout;
	uint32_t	fcoe_reset_timeout;
	rwlock_t	hw_lock;
	uint16_t	portnum;		/* port number */
	int		link_width;
	struct fw_blob	*hablob;
	struct qla82xx_legacy_intr_set nx_legacy_intr;

	uint16_t	gbl_dsd_inuse;
	uint16_t	gbl_dsd_avail;
	struct list_head gbl_dsd_list;
#define NUM_DSD_CHAIN 4096

	uint8_t fw_type;
	__le32 file_prd_off;	/* File firmware product offset */

	uint32_t	md_template_size;
	void		*md_tmplt_hdr;
	dma_addr_t      md_tmplt_hdr_dma;
	void            *md_dump;
	uint32_t	md_dump_size;

	void		*loop_id_map;

	/* QLA83XX IDC specific fields */
	uint32_t	idc_audit_ts;
	uint32_t	idc_extend_tmo;

	/* DPC low-priority workqueue */
	struct workqueue_struct *dpc_lp_wq;
	struct work_struct idc_aen;
	/* DPC high-priority workqueue */
	struct workqueue_struct *dpc_hp_wq;
	struct work_struct nic_core_reset;
	struct work_struct idc_state_handler;
	struct work_struct nic_core_unrecoverable;
	struct work_struct board_disable;

	struct mr_data_fx00 mr;

	struct qlt_hw_data tgt;
	int	allow_cna_fw_dump;
	uint32_t fw_ability_mask;
	uint16_t min_link_speed;
	uint16_t max_speed_sup;

	atomic_t        nvme_active_aen_cnt;
	uint16_t        nvme_last_rptd_aen;             /* Last recorded aen count */
};

#define FW_ABILITY_MAX_SPEED_MASK	0xFUL
#define FW_ABILITY_MAX_SPEED_16G	0x0
#define FW_ABILITY_MAX_SPEED_32G	0x1
#define FW_ABILITY_MAX_SPEED(ha)	\
	(ha->fw_ability_mask & FW_ABILITY_MAX_SPEED_MASK)

/*
 * Qlogic scsi host structure
 */
typedef struct scsi_qla_host {
	struct list_head list;
	struct list_head vp_fcports;	/* list of fcports */
	struct list_head work_list;
	spinlock_t work_lock;
	struct work_struct iocb_work;

	/* Commonly used flags and state information. */
	struct Scsi_Host *host;
	unsigned long	host_no;
	uint8_t		host_str[16];

	volatile struct {
		uint32_t	init_done		:1;
		uint32_t	online			:1;
		uint32_t	reset_active		:1;

		uint32_t	management_server_logged_in :1;
		uint32_t	process_response_queue	:1;
		uint32_t	difdix_supported:1;
		uint32_t	delete_progress:1;

		uint32_t	fw_tgt_reported:1;
		uint32_t	bbcr_enable:1;
		uint32_t	qpairs_available:1;
		uint32_t	qpairs_req_created:1;
		uint32_t	qpairs_rsp_created:1;
		uint32_t	nvme_enabled:1;
	} flags;

	atomic_t	loop_state;
#define LOOP_TIMEOUT	1
#define LOOP_DOWN	2
#define LOOP_UP		3
#define LOOP_UPDATE	4
#define LOOP_READY	5
#define LOOP_DEAD	6

	unsigned long   relogin_jif;
	unsigned long   dpc_flags;
#define RESET_MARKER_NEEDED	0	/* Send marker to ISP. */
#define RESET_ACTIVE		1
#define ISP_ABORT_NEEDED	2	/* Initiate ISP abort. */
#define ABORT_ISP_ACTIVE	3	/* ISP abort in progress. */
#define LOOP_RESYNC_NEEDED	4	/* Device Resync needed. */
#define LOOP_RESYNC_ACTIVE	5
#define LOCAL_LOOP_UPDATE	6	/* Perform a local loop update. */
#define RSCN_UPDATE		7	/* Perform an RSCN update. */
#define RELOGIN_NEEDED		8
#define REGISTER_FC4_NEEDED	9	/* SNS FC4 registration required. */
#define ISP_ABORT_RETRY		10	/* ISP aborted. */
#define BEACON_BLINK_NEEDED	11
#define REGISTER_FDMI_NEEDED	12
#define FCPORT_UPDATE_NEEDED	13
#define VP_DPC_NEEDED		14	/* wake up for VP dpc handling */
#define UNLOADING		15
#define NPIV_CONFIG_NEEDED	16
#define ISP_UNRECOVERABLE	17
#define FCOE_CTX_RESET_NEEDED	18	/* Initiate FCoE context reset */
#define MPI_RESET_NEEDED	19	/* Initiate MPI FW reset */
#define ISP_QUIESCE_NEEDED	20	/* Driver need some quiescence */
#define FREE_BIT 21
#define PORT_UPDATE_NEEDED	22
#define FX00_RESET_RECOVERY	23
#define FX00_TARGET_SCAN	24
#define FX00_CRITEMP_RECOVERY	25
#define FX00_HOST_INFO_RESEND	26
#define QPAIR_ONLINE_CHECK_NEEDED	27
#define SET_ZIO_THRESHOLD_NEEDED	28
#define DETECT_SFP_CHANGE	29

	unsigned long	pci_flags;
#define PFLG_DISCONNECTED	0	/* PCI device removed */
#define PFLG_DRIVER_REMOVING	1	/* PCI driver .remove */
#define PFLG_DRIVER_PROBING	2	/* PCI driver .probe */

	uint32_t	device_flags;
#define SWITCH_FOUND		BIT_0
#define DFLG_NO_CABLE		BIT_1
#define DFLG_DEV_FAILED		BIT_5

	/* ISP configuration data. */
	uint16_t	loop_id;		/* Host adapter loop id */
	uint16_t        self_login_loop_id;     /* host adapter loop id
						 * get it on self login
						 */
	fc_port_t       bidir_fcport;		/* fcport used for bidir cmnds
						 * no need of allocating it for
						 * each command
						 */

	port_id_t	d_id;			/* Host adapter port id */
	uint8_t		marker_needed;
	uint16_t	mgmt_svr_loop_id;



	/* Timeout timers. */
	uint8_t         loop_down_abort_time;    /* port down timer */
	atomic_t        loop_down_timer;         /* loop down timer */
	uint8_t         link_down_timeout;       /* link down timeout */

	uint32_t        timer_active;
	struct timer_list        timer;

	uint8_t		node_name[WWN_SIZE];
	uint8_t		port_name[WWN_SIZE];
	uint8_t		fabric_node_name[WWN_SIZE];

	struct		nvme_fc_local_port *nvme_local_port;
	struct completion nvme_del_done;
	struct list_head nvme_rport_list;
	atomic_t 	nvme_active_aen_cnt;
	uint16_t	nvme_last_rptd_aen;

	uint16_t	fcoe_vlan_id;
	uint16_t	fcoe_fcf_idx;
	uint8_t		fcoe_vn_port_mac[6];

	/* list of commands waiting on workqueue */
	struct list_head	qla_cmd_list;
	struct list_head	qla_sess_op_cmd_list;
	struct list_head	unknown_atio_list;
	spinlock_t		cmd_list_lock;
	struct delayed_work	unknown_atio_work;

	/* Counter to detect races between ELS and RSCN events */
	atomic_t		generation_tick;
	/* Time when global fcport update has been scheduled */
	int			total_fcport_update_gen;
	/* List of pending LOGOs, protected by tgt_mutex */
	struct list_head	logo_list;
	/* List of pending PLOGI acks, protected by hw lock */
	struct list_head	plogi_ack_list;

	struct list_head	qp_list;

	uint32_t	vp_abort_cnt;

	struct fc_vport	*fc_vport;	/* holds fc_vport * for each vport */
	uint16_t        vp_idx;		/* vport ID */
	struct qla_qpair *qpair;	/* base qpair */

	unsigned long		vp_flags;
#define VP_IDX_ACQUIRED		0	/* bit no 0 */
#define VP_CREATE_NEEDED	1
#define VP_BIND_NEEDED		2
#define VP_DELETE_NEEDED	3
#define VP_SCR_NEEDED		4	/* State Change Request registration */
#define VP_CONFIG_OK		5	/* Flag to cfg VP, if FW is ready */
	atomic_t 		vp_state;
#define VP_OFFLINE		0
#define VP_ACTIVE		1
#define VP_FAILED		2
// #define VP_DISABLE		3
	uint16_t 	vp_err_state;
	uint16_t	vp_prev_err_state;
#define VP_ERR_UNKWN		0
#define VP_ERR_PORTDWN		1
#define VP_ERR_FAB_UNSUPPORTED	2
#define VP_ERR_FAB_NORESOURCES	3
#define VP_ERR_FAB_LOGOUT	4
#define VP_ERR_ADAP_NORESOURCES	5
	struct qla_hw_data *hw;
	struct scsi_qlt_host vha_tgt;
	struct req_que *req;
	int		fw_heartbeat_counter;
	int		seconds_since_last_heartbeat;
	struct fc_host_statistics fc_host_stat;
	struct qla_statistics qla_stats;
	struct bidi_statistics bidi_stats;
	atomic_t	vref_count;
	struct qla8044_reset_template reset_tmplt;
	uint16_t	bbcr;
	struct name_list_extended gnl;
	/* Count of active session/fcport */
	int fcport_count;
	wait_queue_head_t fcport_waitQ;
	wait_queue_head_t vref_waitq;
	uint8_t min_link_speed_feat;
	struct list_head gpnid_list;
} scsi_qla_host_t;

struct qla27xx_image_status {
	uint8_t image_status_mask;
	uint16_t generation_number;
	uint8_t reserved[3];
	uint8_t ver_minor;
	uint8_t ver_major;
	uint32_t checksum;
	uint32_t signature;
} __packed;

#define SET_VP_IDX	1
#define SET_AL_PA	2
#define RESET_VP_IDX	3
#define RESET_AL_PA	4
struct qla_tgt_vp_map {
	uint8_t	idx;
	scsi_qla_host_t *vha;
};

struct qla2_sgx {
	dma_addr_t		dma_addr;	/* OUT */
	uint32_t		dma_len;	/* OUT */

	uint32_t		tot_bytes;	/* IN */
	struct scatterlist	*cur_sg;	/* IN */

	/* for book keeping, bzero on initial invocation */
	uint32_t		bytes_consumed;
	uint32_t		num_bytes;
	uint32_t		tot_partial;

	/* for debugging */
	uint32_t		num_sg;
	srb_t			*sp;
};

#define QLA_FW_STARTED(_ha) {			\
	int i;					\
	_ha->flags.fw_started = 1;		\
	_ha->base_qpair->fw_started = 1;	\
	for (i = 0; i < _ha->max_qpairs; i++) {	\
	if (_ha->queue_pair_map[i])	\
	_ha->queue_pair_map[i]->fw_started = 1;	\
	}					\
}

#define QLA_FW_STOPPED(_ha) {			\
	int i;					\
	_ha->flags.fw_started = 0;		\
	_ha->base_qpair->fw_started = 0;	\
	for (i = 0; i < _ha->max_qpairs; i++) {	\
	if (_ha->queue_pair_map[i])	\
	_ha->queue_pair_map[i]->fw_started = 0;	\
	}					\
}

/*
 * Macros to help code, maintain, etc.
 */
#define LOOP_TRANSITION(ha) \
	(test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags) || \
	 test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags) || \
	 atomic_read(&ha->loop_state) == LOOP_DOWN)

#define STATE_TRANSITION(ha) \
		(test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags) || \
			 test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags))

#define QLA_VHA_MARK_BUSY(__vha, __bail) do {		\
	atomic_inc(&__vha->vref_count);			\
	mb();						\
	if (__vha->flags.delete_progress) {		\
		atomic_dec(&__vha->vref_count);		\
		wake_up(&__vha->vref_waitq);		\
		__bail = 1;				\
	} else {					\
		__bail = 0;				\
	}						\
} while (0)

#define QLA_VHA_MARK_NOT_BUSY(__vha) do {		\
	atomic_dec(&__vha->vref_count);			\
	wake_up(&__vha->vref_waitq);			\
} while (0)						\

#define QLA_QPAIR_MARK_BUSY(__qpair, __bail) do {	\
	atomic_inc(&__qpair->ref_count);		\
	mb();						\
	if (__qpair->delete_in_progress) {		\
		atomic_dec(&__qpair->ref_count);	\
		__bail = 1;				\
	} else {					\
	       __bail = 0;				\
	}						\
} while (0)

#define QLA_QPAIR_MARK_NOT_BUSY(__qpair)		\
	atomic_dec(&__qpair->ref_count);		\


#define QLA_ENA_CONF(_ha) {\
    int i;\
    _ha->base_qpair->enable_explicit_conf = 1;	\
    for (i = 0; i < _ha->max_qpairs; i++) {	\
	if (_ha->queue_pair_map[i])		\
	    _ha->queue_pair_map[i]->enable_explicit_conf = 1; \
    }						\
}

#define QLA_DIS_CONF(_ha) {\
    int i;\
    _ha->base_qpair->enable_explicit_conf = 0;	\
    for (i = 0; i < _ha->max_qpairs; i++) {	\
	if (_ha->queue_pair_map[i])		\
	    _ha->queue_pair_map[i]->enable_explicit_conf = 0; \
    }						\
}

/*
 * qla2x00 local function return status codes
 */
#define MBS_MASK		0x3fff

#define QLA_SUCCESS		(MBS_COMMAND_COMPLETE & MBS_MASK)
#define QLA_INVALID_COMMAND	(MBS_INVALID_COMMAND & MBS_MASK)
#define QLA_INTERFACE_ERROR	(MBS_HOST_INTERFACE_ERROR & MBS_MASK)
#define QLA_TEST_FAILED		(MBS_TEST_FAILED & MBS_MASK)
#define QLA_COMMAND_ERROR	(MBS_COMMAND_ERROR & MBS_MASK)
#define QLA_PARAMETER_ERROR	(MBS_COMMAND_PARAMETER_ERROR & MBS_MASK)
#define QLA_PORT_ID_USED	(MBS_PORT_ID_USED & MBS_MASK)
#define QLA_LOOP_ID_USED	(MBS_LOOP_ID_USED & MBS_MASK)
#define QLA_ALL_IDS_IN_USE	(MBS_ALL_IDS_IN_USE & MBS_MASK)
#define QLA_NOT_LOGGED_IN	(MBS_NOT_LOGGED_IN & MBS_MASK)

#define QLA_FUNCTION_TIMEOUT		0x100
#define QLA_FUNCTION_PARAMETER_ERROR	0x101
#define QLA_FUNCTION_FAILED		0x102
#define QLA_MEMORY_ALLOC_FAILED		0x103
#define QLA_LOCK_TIMEOUT		0x104
#define QLA_ABORTED			0x105
#define QLA_SUSPENDED			0x106
#define QLA_BUSY			0x107
#define QLA_ALREADY_REGISTERED		0x109

#define NVRAM_DELAY()		udelay(10)

/*
 * Flash support definitions
 */
#define OPTROM_SIZE_2300	0x20000
#define OPTROM_SIZE_2322	0x100000
#define OPTROM_SIZE_24XX	0x100000
#define OPTROM_SIZE_25XX	0x200000
#define OPTROM_SIZE_81XX	0x400000
#define OPTROM_SIZE_82XX	0x800000
#define OPTROM_SIZE_83XX	0x1000000

#define OPTROM_BURST_SIZE	0x1000
#define OPTROM_BURST_DWORDS	(OPTROM_BURST_SIZE / 4)

#define	QLA_DSDS_PER_IOCB	37

#define CMD_SP(Cmnd)		((Cmnd)->SCp.ptr)

#define QLA_SG_ALL	1024

enum nexus_wait_type {
	WAIT_HOST = 0,
	WAIT_TARGET,
	WAIT_LUN,
};

/* Refer to SNIA SFF 8247 */
struct sff_8247_a0 {
	u8 txid;	/* transceiver id */
	u8 ext_txid;
	u8 connector;
	/* compliance code */
	u8 eth_infi_cc3;	/* ethernet, inifiband */
	u8 sonet_cc4[2];
	u8 eth_cc6;
	/* link length */
#define FC_LL_VL BIT_7	/* very long */
#define FC_LL_S  BIT_6	/* Short */
#define FC_LL_I  BIT_5	/* Intermidiate*/
#define FC_LL_L  BIT_4	/* Long */
#define FC_LL_M  BIT_3	/* Medium */
#define FC_LL_SA BIT_2	/* ShortWave laser */
#define FC_LL_LC BIT_1	/* LongWave laser */
#define FC_LL_EL BIT_0	/* Electrical inter enclosure */
	u8 fc_ll_cc7;
	/* FC technology */
#define FC_TEC_EL BIT_7	/* Electrical inter enclosure */
#define FC_TEC_SN BIT_6	/* short wave w/o OFC */
#define FC_TEC_SL BIT_5	/* short wave with OFC */
#define FC_TEC_LL BIT_4	/* Longwave Laser */
#define FC_TEC_ACT BIT_3	/* Active cable */
#define FC_TEC_PAS BIT_2	/* Passive cable */
	u8 fc_tec_cc8;
	/* Transmission Media */
#define FC_MED_TW BIT_7	/* Twin Ax */
#define FC_MED_TP BIT_6	/* Twited Pair */
#define FC_MED_MI BIT_5	/* Min Coax */
#define FC_MED_TV BIT_4	/* Video Coax */
#define FC_MED_M6 BIT_3	/* Multimode, 62.5um */
#define FC_MED_M5 BIT_2	/* Multimode, 50um */
#define FC_MED_SM BIT_0	/* Single Mode */
	u8 fc_med_cc9;
	/* speed FC_SP_12: 12*100M = 1200 MB/s */
#define FC_SP_12 BIT_7
#define FC_SP_8  BIT_6
#define FC_SP_16 BIT_5
#define FC_SP_4  BIT_4
#define FC_SP_32 BIT_3
#define FC_SP_2  BIT_2
#define FC_SP_1  BIT_0
	u8 fc_sp_cc10;
	u8 encode;
	u8 bitrate;
	u8 rate_id;
	u8 length_km;		/* offset 14/eh */
	u8 length_100m;
	u8 length_50um_10m;
	u8 length_62um_10m;
	u8 length_om4_10m;
	u8 length_om3_10m;
#define SFF_VEN_NAME_LEN 16
	u8 vendor_name[SFF_VEN_NAME_LEN];	/* offset 20/14h */
	u8 tx_compat;
	u8 vendor_oui[3];
#define SFF_PART_NAME_LEN 16
	u8 vendor_pn[SFF_PART_NAME_LEN];	/* part number */
	u8 vendor_rev[4];
	u8 wavelength[2];
	u8 resv;
	u8 cc_base;
	u8 options[2];	/* offset 64 */
	u8 br_max;
	u8 br_min;
	u8 vendor_sn[16];
	u8 date_code[8];
	u8 diag;
	u8 enh_options;
	u8 sff_revision;
	u8 cc_ext;
	u8 vendor_specific[32];
	u8 resv2[128];
};

#define AUTO_DETECT_SFP_SUPPORT(_vha)\
	(ql2xautodetectsfp && !_vha->vp_idx &&		\
	(IS_QLA25XX(_vha->hw) || IS_QLA81XX(_vha->hw) ||\
	IS_QLA83XX(_vha->hw) || IS_QLA27XX(_vha->hw)))

#define USER_CTRL_IRQ(_ha) (ql2xuctrlirq && QLA_TGT_MODE_ENABLED() && \
	(IS_QLA27XX(_ha) || IS_QLA83XX(_ha)))

#include "qla_target.h"
#include "qla_gbl.h"
#include "qla_dbg.h"
#include "qla_inline.h"
#endif
