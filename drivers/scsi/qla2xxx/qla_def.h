/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
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
#include <asm/semaphore.h>

#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_transport_fc.h>

#define QLA2XXX_DRIVER_NAME  "qla2xxx"

/*
 * We have MAILBOX_REGISTER_COUNT sized arrays in a few places,
 * but that's fine as we don't look at the last 24 ones for
 * ISP2100 HBAs.
 */
#define MAILBOX_REGISTER_COUNT_2100	8
#define MAILBOX_REGISTER_COUNT		32

#define QLA2200A_RISC_ROM_VER	4
#define FPM_2300		6
#define FPM_2310		7

#include "qla_settings.h"

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
 * The ISP2312 v2 chip cannot access the FLASH/GPIO registers via MMIO in an
 * 133Mhz slot.
 */
#define RD_REG_WORD_PIO(addr)		(inw((unsigned long)addr))
#define WRT_REG_WORD_PIO(addr, data)	(outw(data,(unsigned long)addr))

/*
 * Fibre Channel device definitions.
 */
#define WWN_SIZE		8	/* Size of WWPN, WWN & WWNN */
#define MAX_FIBRE_DEVICES	512
#define MAX_FIBRE_LUNS  	0xFFFF
#define	MAX_RSCN_COUNT		32
#define	MAX_HOST_COUNT		16

/*
 * Host adapter default definitions.
 */
#define MAX_BUSES		1  /* We only have one bus today */
#define MAX_TARGETS_2100	MAX_FIBRE_DEVICES
#define MAX_TARGETS_2200	MAX_FIBRE_DEVICES
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

#define MAX_CMDSZ	16		/* SCSI maximum CDB size. */
#include "qla_fw.h"

/*
 * Timeout timer counts in seconds
 */
#define PORT_RETRY_TIME			1
#define LOOP_DOWN_TIMEOUT		60
#define LOOP_DOWN_TIME			255	/* 240 */
#define	LOOP_DOWN_RESET			(LOOP_DOWN_TIME - 30)

/* Maximum outstanding commands in ISP queues (1-65535) */
#define MAX_OUTSTANDING_COMMANDS	1024

/* ISP request and response entry counts (37-65535) */
#define REQUEST_ENTRY_CNT_2100		128	/* Number of request entries. */
#define REQUEST_ENTRY_CNT_2200		2048	/* Number of request entries. */
#define REQUEST_ENTRY_CNT_2XXX_EXT_MEM	4096	/* Number of request entries. */
#define REQUEST_ENTRY_CNT_24XX		4096	/* Number of request entries. */
#define RESPONSE_ENTRY_CNT_2100		64	/* Number of response entries.*/
#define RESPONSE_ENTRY_CNT_2300		512	/* Number of response entries.*/

/*
 * SCSI Request Block
 */
typedef struct srb {
	struct scsi_qla_host *ha;	/* HA the SP is queued on */
	struct fc_port *fcport;

	struct scsi_cmnd *cmd;		/* Linux SCSI command pkt */

	uint16_t flags;

	/* Single transfer DMA context */
	dma_addr_t dma_handle;

	uint32_t request_sense_length;
	uint8_t *request_sense_ptr;
} srb_t;

/*
 * SRB flag definitions
 */
#define SRB_TIMEOUT		BIT_0	/* Command timed out */
#define SRB_DMA_VALID		BIT_1	/* Command sent to ISP */
#define SRB_WATCHDOG		BIT_2	/* Command on watchdog list */
#define SRB_ABORT_PENDING	BIT_3	/* Command abort sent to device */

#define SRB_ABORTED		BIT_4	/* Command aborted command already */
#define SRB_RETRY		BIT_5	/* Command needs retrying */
#define SRB_GOT_SENSE		BIT_6	/* Command has sense data */
#define SRB_FAILOVER		BIT_7	/* Command in failover state */

#define SRB_BUSY		BIT_8	/* Command is in busy retry state */
#define SRB_FO_CANCEL		BIT_9	/* Command don't need to do failover */
#define SRB_IOCTL		BIT_10	/* IOCTL command. */
#define SRB_TAPE		BIT_11	/* FCP2 (Tape) command. */

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

typedef union {
		struct device_reg_2xxx isp;
		struct device_reg_24xx isp24;
} device_reg_t;

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
#define MBC_WRITE_RAM_WORD		4	/* Write RAM word. */
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
#define MBC_GET_RETRY_COUNT		0x22	/* Get f/w retry cnt/delay. */
#define MBC_DISABLE_VI			0x24	/* Disable VI operation. */
#define MBC_ENABLE_VI			0x25	/* Enable VI operation. */
#define MBC_GET_FIRMWARE_OPTION		0x28	/* Get Firmware Options. */
#define MBC_SET_FIRMWARE_OPTION		0x38	/* Set Firmware Options. */
#define MBC_LOOP_PORT_BYPASS		0x40	/* Loop Port Bypass. */
#define MBC_LOOP_PORT_ENABLE		0x41	/* Loop Port Enable. */
#define MBC_GET_RESOURCE_COUNTS		0x42	/* Get Resource Counts. */
#define MBC_NON_PARTICIPATE		0x43	/* Non-Participating Mode. */
#define MBC_DIAGNOSTIC_ECHO		0x44	/* Diagnostic echo. */
#define MBC_DIAGNOSTIC_LOOP_BACK	0x45	/* Diagnostic loop back. */
#define MBC_ONLINE_SELF_TEST		0x46	/* Online self-test. */
#define MBC_ENHANCED_GET_PORT_DATABASE	0x47	/* Get port database + login */
#define MBC_RESET_LINK_STATUS		0x52	/* Reset Link Error Status */
#define MBC_IOCB_COMMAND_A64		0x54	/* Execute IOCB command (64) */
#define MBC_SEND_RNID_ELS		0x57	/* Send RNID ELS request */
#define MBC_SET_RNID_PARAMS		0x59	/* Set RNID parameters */
#define MBC_GET_RNID_PARAMS		0x5a	/* Data Rate */
#define MBC_DATA_RATE			0x5d	/* Get RNID parameters */
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
 * ISP24xx mailbox commands
 */
#define MBC_SERDES_PARAMS		0x10	/* Serdes Tx Parameters. */
#define MBC_GET_IOCB_STATUS		0x12	/* Get IOCB status command. */
#define MBC_PORT_PARAMS			0x1A	/* Port iDMA Parameters. */
#define MBC_GET_TIMEOUT_PARAMS		0x22	/* Get FW timeouts. */
#define MBC_TRACE_CONTROL		0x27	/* Trace control command. */
#define MBC_GEN_SYSTEM_ERROR		0x2a	/* Generate System Error. */
#define MBC_READ_SFP			0x31	/* Read SFP Data. */
#define MBC_SET_TIMEOUT_PARAMS		0x32	/* Set FW timeouts. */
#define MBC_MID_INITIALIZE_FIRMWARE	0x48	/* MID Initialize firmware. */
#define MBC_MID_GET_VP_DATABASE		0x49	/* MID Get VP Database. */
#define MBC_MID_GET_VP_ENTRY		0x4a	/* MID Get VP Entry. */
#define MBC_HOST_MEMORY_COPY		0x53	/* Host Memory Copy. */
#define MBC_SEND_RNFT_ELS		0x5e	/* Send RNFT ELS request */
#define MBC_GET_LINK_PRIV_STATS		0x6d	/* Get link & private data. */
#define MBC_SET_VENDOR_ID		0x76	/* Set Vendor ID. */

#define TC_ENABLE			4
#define TC_DISABLE			5

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

typedef struct {
	uint32_t	link_fail_cnt;
	uint32_t	loss_sync_cnt;
	uint32_t	loss_sig_cnt;
	uint32_t	prim_seq_err_cnt;
	uint32_t	inval_xmit_word_cnt;
	uint32_t	inval_crc_cnt;
} link_stat_t;

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
	uint8_t		data[60];
	uint32_t	signature;
#define RESPONSE_PROCESSED	0xDEADDEAD	/* Signature */
} response_t;

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

#define CS_DATA_UNDERRUN	0x15	/* Data Underrun. */
#define CS_QUEUE_FULL		0x1C	/* Queue Full. */
#define CS_PORT_UNAVAILABLE	0x28	/* Port unavailable */
					/* (selection timeout) */
#define CS_PORT_LOGGED_OUT	0x29	/* Port Logged Out */
#define CS_PORT_CONFIG_CHG	0x2A	/* Port Configuration Changed */
#define CS_PORT_BUSY		0x2B	/* Port Busy */
#define CS_COMPLETE_CHKCOND	0x30	/* Error? */
#define CS_BAD_PAYLOAD		0x80	/* Driver defined */
#define CS_UNKNOWN		0x81	/* Driver defined */
#define CS_RETRY		0x82	/* Driver defined */
#define CS_LOOP_DOWN_ABORT	0x83	/* Driver defined */

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

/*
 * ISP request and response queue entry sizes
 */
#define RESPONSE_ENTRY_SIZE	(sizeof(response_t))
#define REQUEST_ENTRY_SIZE	(sizeof(request_t))


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
#elif __LITTLE_ENDIAN
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

/*
 * Switch info gathering structure.
 */
typedef struct {
	port_id_t d_id;
	uint8_t node_name[WWN_SIZE];
	uint8_t port_name[WWN_SIZE];
	uint8_t fabric_port_name[WWN_SIZE];
	uint16_t fp_speed;
} sw_info_t;

/*
 * Fibre channel port type.
 */
 typedef enum {
	FCT_UNKNOWN,
	FCT_RSCN,
	FCT_SWITCH,
	FCT_BROADCAST,
	FCT_INITIATOR,
	FCT_TARGET
} fc_port_type_t;

/*
 * Fibre channel port structure.
 */
typedef struct fc_port {
	struct list_head list;
	struct scsi_qla_host *ha;

	uint8_t node_name[WWN_SIZE];
	uint8_t port_name[WWN_SIZE];
	port_id_t d_id;
	uint16_t loop_id;
	uint16_t old_loop_id;

	uint8_t fabric_port_name[WWN_SIZE];
	uint16_t fp_speed;

	fc_port_type_t port_type;

	atomic_t state;
	uint32_t flags;

	unsigned int os_target_id;

	int port_login_retry_count;
	int login_retry;
	atomic_t port_down_timer;

	spinlock_t rport_lock;
	struct fc_rport *rport, *drport;
	u32 supported_classes;

	unsigned long last_queue_full;
	unsigned long last_ramp_up;

	struct list_head vp_fcport;
	uint16_t vp_idx;
} fc_port_t;

/*
 * Fibre channel port/lun states.
 */
#define FCS_UNCONFIGURED	1
#define FCS_DEVICE_DEAD		2
#define FCS_DEVICE_LOST		3
#define FCS_ONLINE		4
#define FCS_NOT_SUPPORTED	5
#define FCS_FAILOVER		6
#define FCS_FAILOVER_FAILED	7

/*
 * FC port flags.
 */
#define FCF_FABRIC_DEVICE	BIT_0
#define FCF_LOGIN_NEEDED	BIT_1
#define FCF_FO_MASKED		BIT_2
#define FCF_FAILOVER_NEEDED	BIT_3
#define FCF_RESET_NEEDED	BIT_4
#define FCF_PERSISTENT_BOUND	BIT_5
#define FCF_TAPE_PRESENT	BIT_6
#define FCF_FARP_DONE		BIT_7
#define FCF_FARP_FAILED		BIT_8
#define FCF_FARP_REPLY_NEEDED	BIT_9
#define FCF_AUTH_REQ		BIT_10
#define FCF_SEND_AUTH_REQ	BIT_11
#define FCF_RECEIVE_AUTH_REQ	BIT_12
#define FCF_AUTH_SUCCESS	BIT_13
#define FCF_RLC_SUPPORT		BIT_14
#define FCF_CONFIG		BIT_15	/* Needed? */
#define FCF_RESCAN_NEEDED	BIT_16
#define FCF_XP_DEVICE		BIT_17
#define FCF_MSA_DEVICE		BIT_18
#define FCF_EVA_DEVICE		BIT_19
#define FCF_MSA_PORT_ACTIVE	BIT_20
#define FCF_FAILBACK_DISABLE	BIT_21
#define FCF_FAILOVER_DISABLE	BIT_22
#define FCF_DSXXX_DEVICE	BIT_23
#define FCF_AA_EVA_DEVICE	BIT_24
#define FCF_AA_MSA_DEVICE	BIT_25

/* No loop ID flag. */
#define FC_NO_LOOP_ID		0x1000

/*
 * FC-CT interface
 *
 * NOTE: All structures are big-endian in form.
 */

#define CT_REJECT_RESPONSE	0x8001
#define CT_ACCEPT_RESPONSE	0x8002
#define CT_REASON_INVALID_COMMAND_CODE	0x01
#define CT_REASON_CANNOT_PERFORM	0x09
#define CT_EXPL_ALREADY_REGISTERED	0x10

#define NS_N_PORT_TYPE	0x01
#define NS_NL_PORT_TYPE	0x02
#define NS_NX_PORT_TYPE	0x7F

#define	GA_NXT_CMD	0x100
#define	GA_NXT_REQ_SIZE	(16 + 4)
#define	GA_NXT_RSP_SIZE	(16 + 620)

#define	GID_PT_CMD	0x1A1
#define	GID_PT_REQ_SIZE	(16 + 4)
#define	GID_PT_RSP_SIZE	(16 + (MAX_FIBRE_DEVICES * 4))

#define	GPN_ID_CMD	0x112
#define	GPN_ID_REQ_SIZE	(16 + 4)
#define	GPN_ID_RSP_SIZE	(16 + 8)

#define	GNN_ID_CMD	0x113
#define	GNN_ID_REQ_SIZE	(16 + 4)
#define	GNN_ID_RSP_SIZE	(16 + 8)

#define	GFT_ID_CMD	0x117
#define	GFT_ID_REQ_SIZE	(16 + 4)
#define	GFT_ID_RSP_SIZE	(16 + 32)

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


/*
 * HBA attribute types.
 */
#define FDMI_HBA_ATTR_COUNT			9
#define FDMI_HBA_NODE_NAME			1
#define FDMI_HBA_MANUFACTURER			2
#define FDMI_HBA_SERIAL_NUMBER			3
#define FDMI_HBA_MODEL				4
#define FDMI_HBA_MODEL_DESCRIPTION		5
#define FDMI_HBA_HARDWARE_VERSION		6
#define FDMI_HBA_DRIVER_VERSION			7
#define FDMI_HBA_OPTION_ROM_VERSION		8
#define FDMI_HBA_FIRMWARE_VERSION		9
#define FDMI_HBA_OS_NAME_AND_VERSION		0xa
#define FDMI_HBA_MAXIMUM_CT_PAYLOAD_LENGTH	0xb

struct ct_fdmi_hba_attr {
	uint16_t type;
	uint16_t len;
	union {
		uint8_t node_name[WWN_SIZE];
		uint8_t manufacturer[32];
		uint8_t serial_num[8];
		uint8_t model[16];
		uint8_t model_desc[80];
		uint8_t hw_version[16];
		uint8_t driver_version[32];
		uint8_t orom_version[16];
		uint8_t fw_version[16];
		uint8_t os_version[128];
		uint8_t max_ct_len[4];
	} a;
};

struct ct_fdmi_hba_attributes {
	uint32_t count;
	struct ct_fdmi_hba_attr entry[FDMI_HBA_ATTR_COUNT];
};

/*
 * Port attribute types.
 */
#define FDMI_PORT_ATTR_COUNT		6
#define FDMI_PORT_FC4_TYPES		1
#define FDMI_PORT_SUPPORT_SPEED		2
#define FDMI_PORT_CURRENT_SPEED		3
#define FDMI_PORT_MAX_FRAME_SIZE	4
#define FDMI_PORT_OS_DEVICE_NAME	5
#define FDMI_PORT_HOST_NAME		6

#define FDMI_PORT_SPEED_1GB		0x1
#define FDMI_PORT_SPEED_2GB		0x2
#define FDMI_PORT_SPEED_10GB		0x4
#define FDMI_PORT_SPEED_4GB		0x8
#define FDMI_PORT_SPEED_8GB		0x10
#define FDMI_PORT_SPEED_16GB		0x20
#define FDMI_PORT_SPEED_UNKNOWN		0x8000

struct ct_fdmi_port_attr {
	uint16_t type;
	uint16_t len;
	union {
		uint8_t fc4_types[32];
		uint32_t sup_speed;
		uint32_t cur_speed;
		uint32_t max_frame_size;
		uint8_t os_dev_name[32];
		uint8_t host_name[32];
	} a;
};

/*
 * Port Attribute Block.
 */
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
			uint8_t hba_indentifier[8];
		} ghat;

		struct {
			uint8_t hba_identifier[8];
			uint32_t entry_count;
			uint8_t port_name[8];
			struct ct_fdmi_hba_attributes attrs;
		} rhba;

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
			struct ct_sns_gid_pt_data entries[MAX_FIBRE_DEVICES];
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
	} rsp;
};

struct ct_sns_pkt {
	union {
		struct ct_sns_req req;
		struct ct_sns_rsp rsp;
	} p;
};

/*
 * SNS command structures -- for 2200 compatability.
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
#define	GID_PT_SNS_DATA_SIZE	(MAX_FIBRE_DEVICES * 4 + 16)

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
#define GID_LIST_SIZE (sizeof(struct gid_list_info) * MAX_FIBRE_DEVICES)

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

#define to_qla_parent(x) (((x)->parent) ? (x)->parent : (x))

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
	char * (*fw_version_str) (struct scsi_qla_host *, char *);

	irq_handler_t intr_handler;
	void (*enable_intrs) (struct scsi_qla_host *);
	void (*disable_intrs) (struct scsi_qla_host *);

	int (*abort_command) (struct scsi_qla_host *, srb_t *);
	int (*abort_target) (struct fc_port *);
	int (*fabric_login) (struct scsi_qla_host *, uint16_t, uint8_t,
		uint8_t, uint8_t, uint16_t *, uint8_t);
	int (*fabric_logout) (struct scsi_qla_host *, uint16_t, uint8_t,
	    uint8_t, uint8_t);

	uint16_t (*calc_req_entries) (uint16_t);
	void (*build_iocbs) (srb_t *, cmd_entry_t *, uint16_t);
	void * (*prep_ms_iocb) (struct scsi_qla_host *, uint32_t, uint32_t);
	void * (*prep_ms_fdmi_iocb) (struct scsi_qla_host *, uint32_t,
	    uint32_t);

	uint8_t * (*read_nvram) (struct scsi_qla_host *, uint8_t *,
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
};

/* MSI-X Support *************************************************************/

#define QLA_MSIX_CHIP_REV_24XX	3
#define QLA_MSIX_FW_MODE(m)	(((m) & (BIT_7|BIT_8|BIT_9)) >> 7)
#define QLA_MSIX_FW_MODE_1(m)	(QLA_MSIX_FW_MODE(m) == 1)

#define QLA_MSIX_DEFAULT	0x00
#define QLA_MSIX_RSP_Q		0x01

#define QLA_MSIX_ENTRIES	2
#define QLA_MIDX_DEFAULT	0
#define QLA_MIDX_RSP_Q		1

struct scsi_qla_host;

struct qla_msix_entry {
	int have_irq;
	uint16_t msix_vector;
	uint16_t msix_entry;
};

#define	WATCH_INTERVAL		1       /* number of seconds */

/* NPIV */
#define MAX_MULTI_ID_LOOP                     126
#define MAX_MULTI_ID_FABRIC                    64
#define MAX_NUM_VPORT_LOOP                      (MAX_MULTI_ID_LOOP - 1)
#define MAX_NUM_VPORT_FABRIC                    (MAX_MULTI_ID_FABRIC - 1)
#define MAX_NUM_VHBA_LOOP                       (MAX_MULTI_ID_LOOP - 1)
#define MAX_NUM_VHBA_FABRIC                     (MAX_MULTI_ID_FABRIC - 1)

/*
 * Linux Host Adapter structure
 */
typedef struct scsi_qla_host {
	struct list_head list;

	/* Commonly used flags and state information. */
	struct Scsi_Host *host;
	struct pci_dev	*pdev;

	unsigned long	host_no;
	unsigned long	instance;

	volatile struct {
		uint32_t	init_done		:1;
		uint32_t	online			:1;
		uint32_t	mbox_int		:1;
		uint32_t	mbox_busy		:1;
		uint32_t	rscn_queue_overflow	:1;
		uint32_t	reset_active		:1;

		uint32_t	management_server_logged_in :1;
                uint32_t	process_response_queue	:1;

		uint32_t	disable_risc_code_load	:1;
		uint32_t	enable_64bit_addressing	:1;
		uint32_t	enable_lip_reset	:1;
		uint32_t	enable_lip_full_login	:1;
		uint32_t	enable_target_reset	:1;
		uint32_t	enable_led_scheme	:1;
		uint32_t	inta_enabled		:1;
		uint32_t	msi_enabled		:1;
		uint32_t	msix_enabled		:1;
		uint32_t	disable_serdes		:1;
		uint32_t	gpsc_supported		:1;
		uint32_t        vsan_enabled            :1;
		uint32_t	npiv_supported		:1;
	} flags;

	atomic_t	loop_state;
#define LOOP_TIMEOUT	1
#define LOOP_DOWN	2
#define LOOP_UP		3
#define LOOP_UPDATE	4
#define LOOP_READY	5
#define LOOP_DEAD	6

	unsigned long   dpc_flags;
#define	RESET_MARKER_NEEDED	0	/* Send marker to ISP. */
#define	RESET_ACTIVE		1
#define	ISP_ABORT_NEEDED	2	/* Initiate ISP abort. */
#define	ABORT_ISP_ACTIVE	3	/* ISP abort in progress. */
#define	LOOP_RESYNC_NEEDED	4	/* Device Resync needed. */
#define	LOOP_RESYNC_ACTIVE	5
#define LOCAL_LOOP_UPDATE       6	/* Perform a local loop update. */
#define RSCN_UPDATE             7	/* Perform an RSCN update. */
#define MAILBOX_RETRY           8
#define ISP_RESET_NEEDED        9	/* Initiate a ISP reset. */
#define FAILOVER_EVENT_NEEDED   10
#define FAILOVER_EVENT		11
#define FAILOVER_NEEDED   	12
#define SCSI_RESTART_NEEDED	13	/* Processes SCSI retry queue. */
#define PORT_RESTART_NEEDED	14	/* Processes Retry queue. */
#define RESTART_QUEUES_NEEDED	15	/* Restarts the Lun queue. */
#define ABORT_QUEUES_NEEDED	16
#define RELOGIN_NEEDED	        17
#define LOGIN_RETRY_NEEDED	18	/* Initiate required fabric logins. */
#define REGISTER_FC4_NEEDED	19	/* SNS FC4 registration required. */
#define ISP_ABORT_RETRY         20      /* ISP aborted. */
#define FCPORT_RESCAN_NEEDED	21      /* IO descriptor processing needed */
#define IODESC_PROCESS_NEEDED	22      /* IO descriptor processing needed */
#define IOCTL_ERROR_RECOVERY	23
#define LOOP_RESET_NEEDED	24
#define BEACON_BLINK_NEEDED	25
#define REGISTER_FDMI_NEEDED	26
#define FCPORT_UPDATE_NEEDED	27
#define VP_DPC_NEEDED		28	/* wake up for VP dpc handling */

	uint32_t	device_flags;
#define DFLG_LOCAL_DEVICES		BIT_0
#define DFLG_RETRY_LOCAL_DEVICES	BIT_1
#define DFLG_FABRIC_DEVICES		BIT_2
#define	SWITCH_FOUND			BIT_3
#define	DFLG_NO_CABLE			BIT_4

#define PCI_DEVICE_ID_QLOGIC_ISP2532	0x2532
	uint32_t	device_type;
#define DT_ISP2100			BIT_0
#define DT_ISP2200			BIT_1
#define DT_ISP2300			BIT_2
#define DT_ISP2312			BIT_3
#define DT_ISP2322			BIT_4
#define DT_ISP6312			BIT_5
#define DT_ISP6322			BIT_6
#define DT_ISP2422			BIT_7
#define DT_ISP2432			BIT_8
#define DT_ISP5422			BIT_9
#define DT_ISP5432			BIT_10
#define DT_ISP2532			BIT_11
#define DT_ISP_LAST			(DT_ISP2532 << 1)

#define DT_IIDMA			BIT_26
#define DT_FWI2				BIT_27
#define DT_ZIO_SUPPORTED		BIT_28
#define DT_OEM_001			BIT_29
#define DT_ISP2200A			BIT_30
#define DT_EXTENDED_IDS			BIT_31

#define DT_MASK(ha)	((ha)->device_type & (DT_ISP_LAST - 1))
#define IS_QLA2100(ha)	(DT_MASK(ha) & DT_ISP2100)
#define IS_QLA2200(ha)	(DT_MASK(ha) & DT_ISP2200)
#define IS_QLA2300(ha)	(DT_MASK(ha) & DT_ISP2300)
#define IS_QLA2312(ha)	(DT_MASK(ha) & DT_ISP2312)
#define IS_QLA2322(ha)	(DT_MASK(ha) & DT_ISP2322)
#define IS_QLA6312(ha)	(DT_MASK(ha) & DT_ISP6312)
#define IS_QLA6322(ha)	(DT_MASK(ha) & DT_ISP6322)
#define IS_QLA2422(ha)	(DT_MASK(ha) & DT_ISP2422)
#define IS_QLA2432(ha)	(DT_MASK(ha) & DT_ISP2432)
#define IS_QLA5422(ha)	(DT_MASK(ha) & DT_ISP5422)
#define IS_QLA5432(ha)	(DT_MASK(ha) & DT_ISP5432)
#define IS_QLA2532(ha)	(DT_MASK(ha) & DT_ISP2532)

#define IS_QLA23XX(ha)	(IS_QLA2300(ha) || IS_QLA2312(ha) || IS_QLA2322(ha) || \
    			 IS_QLA6312(ha) || IS_QLA6322(ha))
#define IS_QLA24XX(ha)	(IS_QLA2422(ha) || IS_QLA2432(ha))
#define IS_QLA54XX(ha)	(IS_QLA5422(ha) || IS_QLA5432(ha))
#define IS_QLA25XX(ha)	(IS_QLA2532(ha))

#define IS_IIDMA_CAPABLE(ha)	((ha)->device_type & DT_IIDMA)
#define IS_FWI2_CAPABLE(ha)	((ha)->device_type & DT_FWI2)
#define IS_ZIO_SUPPORTED(ha)	((ha)->device_type & DT_ZIO_SUPPORTED)
#define IS_OEM_001(ha)		((ha)->device_type & DT_OEM_001)
#define HAS_EXTENDED_IDS(ha)	((ha)->device_type & DT_EXTENDED_IDS)

	/* SRB cache. */
#define SRB_MIN_REQ	128
	mempool_t	*srb_mempool;

	/* This spinlock is used to protect "io transactions", you must
	 * acquire it before doing any IO to the card, eg with RD_REG*() and
	 * WRT_REG*() for the duration of your entire commandtransaction.
	 *
	 * This spinlock is of lower priority than the io request lock.
	 */

	spinlock_t		hardware_lock ____cacheline_aligned;

	int		bars;
	device_reg_t __iomem *iobase;		/* Base I/O address */
	unsigned long	pio_address;
	unsigned long	pio_length;
#define MIN_IOBASE_LEN		0x100

	/* ISP ring lock, rings, and indexes */
	dma_addr_t	request_dma;        /* Physical address. */
	request_t       *request_ring;      /* Base virtual address */
	request_t       *request_ring_ptr;  /* Current address. */
	uint16_t        req_ring_index;     /* Current index. */
	uint16_t        req_q_cnt;          /* Number of available entries. */
	uint16_t	request_q_length;

	dma_addr_t	response_dma;       /* Physical address. */
	response_t      *response_ring;     /* Base virtual address */
	response_t      *response_ring_ptr; /* Current address. */
	uint16_t        rsp_ring_index;     /* Current index. */
	uint16_t	response_q_length;

	struct isp_operations *isp_ops;

	/* Outstandings ISP commands. */
	srb_t		*outstanding_cmds[MAX_OUTSTANDING_COMMANDS];
	uint32_t	current_outstanding_cmd;
	srb_t		*status_srb;	/* Status continuation entry. */

	/* ISP configuration data. */
	uint16_t	loop_id;		/* Host adapter loop id */
	uint16_t	switch_cap;
#define FLOGI_SEQ_DEL		BIT_8
#define FLOGI_MID_SUPPORT	BIT_10
#define FLOGI_VSAN_SUPPORT	BIT_12
#define FLOGI_SP_SUPPORT	BIT_13
	uint16_t	fb_rev;

	port_id_t	d_id;			/* Host adapter port id */
	uint16_t	max_public_loop_ids;
	uint16_t	min_external_loopid;	/* First external loop Id */

#define PORT_SPEED_UNKNOWN 0xFFFF
#define PORT_SPEED_1GB	0x00
#define PORT_SPEED_2GB	0x01
#define PORT_SPEED_4GB	0x03
#define PORT_SPEED_8GB	0x04
	uint16_t	link_data_rate;		/* F/W operating speed */

	uint8_t		current_topology;
	uint8_t		prev_topology;
#define ISP_CFG_NL	1
#define ISP_CFG_N	2
#define ISP_CFG_FL	4
#define ISP_CFG_F	8

	uint8_t		operating_mode;		/* F/W operating mode */
#define LOOP      0
#define P2P       1
#define LOOP_P2P  2
#define P2P_LOOP  3

        uint8_t		marker_needed;

	uint8_t		interrupts_on;

	/* HBA serial number */
	uint8_t		serial0;
	uint8_t		serial1;
	uint8_t		serial2;

	/* NVRAM configuration data */
#define MAX_NVRAM_SIZE	4096
#define VPD_OFFSET	MAX_NVRAM_SIZE / 2
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
	uint16_t	last_loop_id;
	uint16_t	mgmt_svr_loop_id;

        uint32_t	login_retry_count;
	int		max_q_depth;

	/* Fibre Channel Device List. */
	struct list_head	fcports;

	/* RSCN queue. */
	uint32_t rscn_queue[MAX_RSCN_COUNT];
	uint8_t rscn_in_ptr;
	uint8_t rscn_out_ptr;

	/* SNS command interfaces. */
	ms_iocb_entry_t		*ms_iocb;
	dma_addr_t		ms_iocb_dma;
	struct ct_sns_pkt	*ct_sns;
	dma_addr_t		ct_sns_dma;
	/* SNS command interfaces for 2200. */
	struct sns_cmd_pkt	*sns_cmd;
	dma_addr_t		sns_cmd_dma;

#define SFP_DEV_SIZE	256
#define SFP_BLOCK_SIZE	64
	void			*sfp_data;
	dma_addr_t		sfp_data_dma;

	struct task_struct	*dpc_thread;
	uint8_t dpc_active;                  /* DPC routine is active */

	/* Timeout timers. */
	uint8_t         loop_down_abort_time;    /* port down timer */
	atomic_t        loop_down_timer;         /* loop down timer */
	uint8_t         link_down_timeout;       /* link down timeout */

	uint32_t        timer_active;
	struct timer_list        timer;

	dma_addr_t	gid_list_dma;
	struct gid_list_info *gid_list;
	int		gid_list_info_size;

	/* Small DMA pool allocations -- maximum 256 bytes in length. */
#define DMA_POOL_SIZE	256
	struct dma_pool *s_dma_pool;

	dma_addr_t	init_cb_dma;
	init_cb_t	*init_cb;
	int		init_cb_size;

	/* These are used by mailbox operations. */
	volatile uint16_t mailbox_out[MAILBOX_REGISTER_COUNT];

	mbx_cmd_t	*mcp;
	unsigned long	mbx_cmd_flags;
#define MBX_INTERRUPT	1
#define MBX_INTR_WAIT	2
#define MBX_UPDATE_FLASH_ACTIVE	3

	struct semaphore mbx_cmd_sem;	/* Serialialize mbx access */
	struct semaphore vport_sem;	/* Virtual port synchronization */
	struct semaphore mbx_intr_sem;  /* Used for completion notification */

	uint32_t	mbx_flags;
#define  MBX_IN_PROGRESS	BIT_0
#define  MBX_BUSY		BIT_1	/* Got the Access */
#define  MBX_SLEEPING_ON_SEM	BIT_2
#define  MBX_POLLING_FOR_COMP	BIT_3
#define  MBX_COMPLETED		BIT_4
#define  MBX_TIMEDOUT		BIT_5
#define  MBX_ACCESS_TIMEDOUT	BIT_6

	mbx_cmd_t 	mc;

	/* Basic firmware related information. */
	uint16_t	fw_major_version;
	uint16_t	fw_minor_version;
	uint16_t	fw_subminor_version;
	uint16_t	fw_attributes;
	uint32_t	fw_memory_size;
	uint32_t	fw_transfer_size;
	uint32_t	fw_srisc_address;
#define RISC_START_ADDRESS_2100 0x1000
#define RISC_START_ADDRESS_2300 0x800
#define RISC_START_ADDRESS_2400 0x100000

	uint16_t	fw_options[16];		/* slots: 1,2,3,10,11 */
	uint8_t		fw_seriallink_options[4];
	uint16_t	fw_seriallink_options24[4];

	/* Firmware dump information. */
	struct qla2xxx_fw_dump *fw_dump;
	uint32_t	fw_dump_len;
	int		fw_dumped;
	int		fw_dump_reading;
	dma_addr_t	eft_dma;
	void		*eft;

	uint8_t		host_str[16];
	uint32_t	pci_attr;
	uint16_t	chip_revision;

	uint16_t	product_id[4];

	uint8_t		model_number[16+1];
#define BINZERO		"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	char		*model_desc;
	uint8_t		adapter_id[16+1];

	uint8_t		*node_name;
	uint8_t		*port_name;
	uint8_t		fabric_node_name[WWN_SIZE];
	uint32_t    isp_abort_cnt;

	/* Option ROM information. */
	char		*optrom_buffer;
	uint32_t	optrom_size;
	int		optrom_state;
#define QLA_SWAITING	0
#define QLA_SREADING	1
#define QLA_SWRITING	2
	uint32_t	optrom_region_start;
	uint32_t	optrom_region_size;

        /* PCI expansion ROM image information. */
#define ROM_CODE_TYPE_BIOS	0
#define ROM_CODE_TYPE_FCODE	1
#define ROM_CODE_TYPE_EFI	3
	uint8_t		bios_revision[2];
	uint8_t		efi_revision[2];
	uint8_t		fcode_revision[16];
	uint32_t	fw_revision[4];

	/* Needed for BEACON */
	uint16_t	beacon_blink_led;
	uint8_t		beacon_color_state;
#define QLA_LED_GRN_ON		0x01
#define QLA_LED_YLW_ON		0x02
#define QLA_LED_ABR_ON		0x04
#define QLA_LED_ALL_ON		0x07	/* yellow, green, amber. */
					/* ISP2322: red, green, amber. */

	uint16_t	zio_mode;
	uint16_t	zio_timer;
	struct fc_host_statistics fc_host_stat;

	struct qla_msix_entry msix_entries[QLA_MSIX_ENTRIES];

	struct list_head	vp_list;	/* list of VP */
	struct fc_vport	*fc_vport;	/* holds fc_vport * for each vport */
	uint8_t		vp_idx_map[16];
	uint16_t        num_vhosts;	/* number of vports created */
	uint16_t        num_vsans;	/* number of vsan created */
	uint16_t        vp_idx;		/* vport ID */

	struct scsi_qla_host	*parent;	/* holds pport */
	unsigned long		vp_flags;
	struct list_head	vp_fcports;	/* list of fcports */
#define VP_IDX_ACQUIRED		0	/* bit no 0 */
#define VP_CREATE_NEEDED	1
#define VP_BIND_NEEDED		2
#define VP_DELETE_NEEDED	3
#define VP_SCR_NEEDED		4	/* State Change Request registration */
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
	uint16_t	max_npiv_vports;	/* 63 or 125 per topoloty */
	int		cur_vport_count;
} scsi_qla_host_t;


/*
 * Macros to help code, maintain, etc.
 */
#define LOOP_TRANSITION(ha) \
	(test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags) || \
	 test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags) || \
	 atomic_read(&ha->loop_state) == LOOP_DOWN)

#define qla_printk(level, ha, format, arg...) \
	dev_printk(level , &((ha)->pdev->dev) , format , ## arg)

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
#define QLA_RSCNS_HANDLED		0x108
#define QLA_ALREADY_REGISTERED		0x109

#define NVRAM_DELAY()		udelay(10)

#define INVALID_HANDLE	(MAX_OUTSTANDING_COMMANDS+1)

/*
 * Flash support definitions
 */
#define OPTROM_SIZE_2300	0x20000
#define OPTROM_SIZE_2322	0x100000
#define OPTROM_SIZE_24XX	0x100000
#define OPTROM_SIZE_25XX	0x200000

#include "qla_gbl.h"
#include "qla_dbg.h"
#include "qla_inline.h"

#define CMD_SP(Cmnd)		((Cmnd)->SCp.ptr)
#define CMD_COMPL_STATUS(Cmnd)  ((Cmnd)->SCp.this_residual)
#define CMD_RESID_LEN(Cmnd)	((Cmnd)->SCp.buffers_residual)
#define CMD_SCSI_STATUS(Cmnd)	((Cmnd)->SCp.Status)
#define CMD_ACTUAL_SNSLEN(Cmnd)	((Cmnd)->SCp.Message)
#define CMD_ENTRY_STATUS(Cmnd)	((Cmnd)->SCp.have_data_in)

#endif
