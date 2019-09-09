/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Functions for assembling fcx enabled I/O control blocks.
 *
 *    Copyright IBM Corp. 2008
 *    Author(s): Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#ifndef _ASM_S390_FCX_H
#define _ASM_S390_FCX_H

#include <linux/types.h>

#define TCW_FORMAT_DEFAULT		0
#define TCW_TIDAW_FORMAT_DEFAULT	0
#define TCW_FLAGS_INPUT_TIDA		(1 << (23 - 5))
#define TCW_FLAGS_TCCB_TIDA		(1 << (23 - 6))
#define TCW_FLAGS_OUTPUT_TIDA		(1 << (23 - 7))
#define TCW_FLAGS_TIDAW_FORMAT(x)	((x) & 3) << (23 - 9)
#define TCW_FLAGS_GET_TIDAW_FORMAT(x)	(((x) >> (23 - 9)) & 3)

/**
 * struct tcw - Transport Control Word (TCW)
 * @format: TCW format
 * @flags: TCW flags
 * @tccbl: Transport-Command-Control-Block Length
 * @r: Read Operations
 * @w: Write Operations
 * @output: Output-Data Address
 * @input: Input-Data Address
 * @tsb: Transport-Status-Block Address
 * @tccb: Transport-Command-Control-Block Address
 * @output_count: Output Count
 * @input_count: Input Count
 * @intrg: Interrogate TCW Address
 */
struct tcw {
	u32 format:2;
	u32 :6;
	u32 flags:24;
	u32 :8;
	u32 tccbl:6;
	u32 r:1;
	u32 w:1;
	u32 :16;
	u64 output;
	u64 input;
	u64 tsb;
	u64 tccb;
	u32 output_count;
	u32 input_count;
	u32 :32;
	u32 :32;
	u32 :32;
	u32 intrg;
} __attribute__ ((packed, aligned(64)));

#define TIDAW_FLAGS_LAST		(1 << (7 - 0))
#define TIDAW_FLAGS_SKIP		(1 << (7 - 1))
#define TIDAW_FLAGS_DATA_INT		(1 << (7 - 2))
#define TIDAW_FLAGS_TTIC		(1 << (7 - 3))
#define TIDAW_FLAGS_INSERT_CBC		(1 << (7 - 4))

/**
 * struct tidaw - Transport-Indirect-Addressing Word (TIDAW)
 * @flags: TIDAW flags. Can be an arithmetic OR of the following constants:
 * %TIDAW_FLAGS_LAST, %TIDAW_FLAGS_SKIP, %TIDAW_FLAGS_DATA_INT,
 * %TIDAW_FLAGS_TTIC, %TIDAW_FLAGS_INSERT_CBC
 * @count: Count
 * @addr: Address
 */
struct tidaw {
	u32 flags:8;
	u32 :24;
	u32 count;
	u64 addr;
} __attribute__ ((packed, aligned(16)));

/**
 * struct tsa_iostat - I/O-Status Transport-Status Area (IO-Stat TSA)
 * @dev_time: Device Time
 * @def_time: Defer Time
 * @queue_time: Queue Time
 * @dev_busy_time: Device-Busy Time
 * @dev_act_time: Device-Active-Only Time
 * @sense: Sense Data (if present)
 */
struct tsa_iostat {
	u32 dev_time;
	u32 def_time;
	u32 queue_time;
	u32 dev_busy_time;
	u32 dev_act_time;
	u8 sense[32];
} __attribute__ ((packed));

/**
 * struct tsa_ddpcs - Device-Detected-Program-Check Transport-Status Area (DDPC TSA)
 * @rc: Reason Code
 * @rcq: Reason Code Qualifier
 * @sense: Sense Data (if present)
 */
struct tsa_ddpc {
	u32 :24;
	u32 rc:8;
	u8 rcq[16];
	u8 sense[32];
} __attribute__ ((packed));

#define TSA_INTRG_FLAGS_CU_STATE_VALID		(1 << (7 - 0))
#define TSA_INTRG_FLAGS_DEV_STATE_VALID		(1 << (7 - 1))
#define TSA_INTRG_FLAGS_OP_STATE_VALID		(1 << (7 - 2))

/**
 * struct tsa_intrg - Interrogate Transport-Status Area (Intrg. TSA)
 * @format: Format
 * @flags: Flags. Can be an arithmetic OR of the following constants:
 * %TSA_INTRG_FLAGS_CU_STATE_VALID, %TSA_INTRG_FLAGS_DEV_STATE_VALID,
 * %TSA_INTRG_FLAGS_OP_STATE_VALID
 * @cu_state: Controle-Unit State
 * @dev_state: Device State
 * @op_state: Operation State
 * @sd_info: State-Dependent Information
 * @dl_id: Device-Level Identifier
 * @dd_data: Device-Dependent Data
 */
struct tsa_intrg {
	u32 format:8;
	u32 flags:8;
	u32 cu_state:8;
	u32 dev_state:8;
	u32 op_state:8;
	u32 :24;
	u8 sd_info[12];
	u32 dl_id;
	u8 dd_data[28];
} __attribute__ ((packed));

#define TSB_FORMAT_NONE		0
#define TSB_FORMAT_IOSTAT	1
#define TSB_FORMAT_DDPC		2
#define TSB_FORMAT_INTRG	3

#define TSB_FLAGS_DCW_OFFSET_VALID	(1 << (7 - 0))
#define TSB_FLAGS_COUNT_VALID		(1 << (7 - 1))
#define TSB_FLAGS_CACHE_MISS		(1 << (7 - 2))
#define TSB_FLAGS_TIME_VALID		(1 << (7 - 3))
#define TSB_FLAGS_FORMAT(x)		((x) & 7)
#define TSB_FORMAT(t)			((t)->flags & 7)

/**
 * struct tsb - Transport-Status Block (TSB)
 * @length: Length
 * @flags: Flags. Can be an arithmetic OR of the following constants:
 * %TSB_FLAGS_DCW_OFFSET_VALID, %TSB_FLAGS_COUNT_VALID, %TSB_FLAGS_CACHE_MISS,
 * %TSB_FLAGS_TIME_VALID
 * @dcw_offset: DCW Offset
 * @count: Count
 * @tsa: Transport-Status-Area
 */
struct tsb {
	u32 length:8;
	u32 flags:8;
	u32 dcw_offset:16;
	u32 count;
	u32 :32;
	union {
		struct tsa_iostat iostat;
		struct tsa_ddpc ddpc;
		struct tsa_intrg intrg;
	} __attribute__ ((packed)) tsa;
} __attribute__ ((packed, aligned(8)));

#define DCW_INTRG_FORMAT_DEFAULT	0

#define DCW_INTRG_RC_UNSPECIFIED	0
#define DCW_INTRG_RC_TIMEOUT		1

#define DCW_INTRG_RCQ_UNSPECIFIED	0
#define DCW_INTRG_RCQ_PRIMARY		1
#define DCW_INTRG_RCQ_SECONDARY		2

#define DCW_INTRG_FLAGS_MPM		(1 << (7 - 0))
#define DCW_INTRG_FLAGS_PPR		(1 << (7 - 1))
#define DCW_INTRG_FLAGS_CRIT		(1 << (7 - 2))

/**
 * struct dcw_intrg_data - Interrogate DCW data
 * @format: Format. Should be %DCW_INTRG_FORMAT_DEFAULT
 * @rc: Reason Code. Can be one of %DCW_INTRG_RC_UNSPECIFIED,
 * %DCW_INTRG_RC_TIMEOUT
 * @rcq: Reason Code Qualifier: Can be one of %DCW_INTRG_RCQ_UNSPECIFIED,
 * %DCW_INTRG_RCQ_PRIMARY, %DCW_INTRG_RCQ_SECONDARY
 * @lpm: Logical-Path Mask
 * @pam: Path-Available Mask
 * @pim: Path-Installed Mask
 * @timeout: Timeout
 * @flags: Flags. Can be an arithmetic OR of %DCW_INTRG_FLAGS_MPM,
 * %DCW_INTRG_FLAGS_PPR, %DCW_INTRG_FLAGS_CRIT
 * @time: Time
 * @prog_id: Program Identifier
 * @prog_data: Program-Dependent Data
 */
struct dcw_intrg_data {
	u32 format:8;
	u32 rc:8;
	u32 rcq:8;
	u32 lpm:8;
	u32 pam:8;
	u32 pim:8;
	u32 timeout:16;
	u32 flags:8;
	u32 :24;
	u32 :32;
	u64 time;
	u64 prog_id;
	u8  prog_data[0];
} __attribute__ ((packed));

#define DCW_FLAGS_CC		(1 << (7 - 1))

#define DCW_CMD_WRITE		0x01
#define DCW_CMD_READ		0x02
#define DCW_CMD_CONTROL		0x03
#define DCW_CMD_SENSE		0x04
#define DCW_CMD_SENSE_ID	0xe4
#define DCW_CMD_INTRG		0x40

/**
 * struct dcw - Device-Command Word (DCW)
 * @cmd: Command Code. Can be one of %DCW_CMD_WRITE, %DCW_CMD_READ,
 * %DCW_CMD_CONTROL, %DCW_CMD_SENSE, %DCW_CMD_SENSE_ID, %DCW_CMD_INTRG
 * @flags: Flags. Can be an arithmetic OR of %DCW_FLAGS_CC
 * @cd_count: Control-Data Count
 * @count: Count
 * @cd: Control Data
 */
struct dcw {
	u32 cmd:8;
	u32 flags:8;
	u32 :8;
	u32 cd_count:8;
	u32 count;
	u8 cd[0];
} __attribute__ ((packed));

#define TCCB_FORMAT_DEFAULT	0x7f
#define TCCB_MAX_DCW		30
#define TCCB_MAX_SIZE		(sizeof(struct tccb_tcah) + \
				 TCCB_MAX_DCW * sizeof(struct dcw) + \
				 sizeof(struct tccb_tcat))
#define TCCB_SAC_DEFAULT	0x1ffe
#define TCCB_SAC_INTRG		0x1fff

/**
 * struct tccb_tcah - Transport-Command-Area Header (TCAH)
 * @format: Format. Should be %TCCB_FORMAT_DEFAULT
 * @tcal: Transport-Command-Area Length
 * @sac: Service-Action Code. Can be one of %TCCB_SAC_DEFAULT, %TCCB_SAC_INTRG
 * @prio: Priority
 */
struct tccb_tcah {
	u32 format:8;
	u32 :24;
	u32 :24;
	u32 tcal:8;
	u32 sac:16;
	u32 :8;
	u32 prio:8;
	u32 :32;
} __attribute__ ((packed));

/**
 * struct tccb_tcat - Transport-Command-Area Trailer (TCAT)
 * @count: Transport Count
 */
struct tccb_tcat {
	u32 :32;
	u32 count;
} __attribute__ ((packed));

/**
 * struct tccb - (partial) Transport-Command-Control Block (TCCB)
 * @tcah: TCAH
 * @tca: Transport-Command Area
 */
struct tccb {
	struct tccb_tcah tcah;
	u8 tca[0];
} __attribute__ ((packed, aligned(8)));

struct tcw *tcw_get_intrg(struct tcw *tcw);
void *tcw_get_data(struct tcw *tcw);
struct tccb *tcw_get_tccb(struct tcw *tcw);
struct tsb *tcw_get_tsb(struct tcw *tcw);

void tcw_init(struct tcw *tcw, int r, int w);
void tcw_finalize(struct tcw *tcw, int num_tidaws);

void tcw_set_intrg(struct tcw *tcw, struct tcw *intrg_tcw);
void tcw_set_data(struct tcw *tcw, void *data, int use_tidal);
void tcw_set_tccb(struct tcw *tcw, struct tccb *tccb);
void tcw_set_tsb(struct tcw *tcw, struct tsb *tsb);

void tccb_init(struct tccb *tccb, size_t tccb_size, u32 sac);
void tsb_init(struct tsb *tsb);
struct dcw *tccb_add_dcw(struct tccb *tccb, size_t tccb_size, u8 cmd, u8 flags,
			 void *cd, u8 cd_count, u32 count);
struct tidaw *tcw_add_tidaw(struct tcw *tcw, int num_tidaws, u8 flags,
			    void *addr, u32 count);

#endif /* _ASM_S390_FCX_H */
