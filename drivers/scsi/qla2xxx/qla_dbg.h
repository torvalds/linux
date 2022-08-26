/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 */

#include "qla_def.h"

/*
 * Firmware Dump structure definition
 */

struct qla2300_fw_dump {
	__be16 hccr;
	__be16 pbiu_reg[8];
	__be16 risc_host_reg[8];
	__be16 mailbox_reg[32];
	__be16 resp_dma_reg[32];
	__be16 dma_reg[48];
	__be16 risc_hdw_reg[16];
	__be16 risc_gp0_reg[16];
	__be16 risc_gp1_reg[16];
	__be16 risc_gp2_reg[16];
	__be16 risc_gp3_reg[16];
	__be16 risc_gp4_reg[16];
	__be16 risc_gp5_reg[16];
	__be16 risc_gp6_reg[16];
	__be16 risc_gp7_reg[16];
	__be16 frame_buf_hdw_reg[64];
	__be16 fpm_b0_reg[64];
	__be16 fpm_b1_reg[64];
	__be16 risc_ram[0xf800];
	__be16 stack_ram[0x1000];
	__be16 data_ram[1];
};

struct qla2100_fw_dump {
	__be16 hccr;
	__be16 pbiu_reg[8];
	__be16 mailbox_reg[32];
	__be16 dma_reg[48];
	__be16 risc_hdw_reg[16];
	__be16 risc_gp0_reg[16];
	__be16 risc_gp1_reg[16];
	__be16 risc_gp2_reg[16];
	__be16 risc_gp3_reg[16];
	__be16 risc_gp4_reg[16];
	__be16 risc_gp5_reg[16];
	__be16 risc_gp6_reg[16];
	__be16 risc_gp7_reg[16];
	__be16 frame_buf_hdw_reg[16];
	__be16 fpm_b0_reg[64];
	__be16 fpm_b1_reg[64];
	__be16 risc_ram[0xf000];
	u8	queue_dump[];
};

struct qla24xx_fw_dump {
	__be32	host_status;
	__be32	host_reg[32];
	__be32	shadow_reg[7];
	__be16	mailbox_reg[32];
	__be32	xseq_gp_reg[128];
	__be32	xseq_0_reg[16];
	__be32	xseq_1_reg[16];
	__be32	rseq_gp_reg[128];
	__be32	rseq_0_reg[16];
	__be32	rseq_1_reg[16];
	__be32	rseq_2_reg[16];
	__be32	cmd_dma_reg[16];
	__be32	req0_dma_reg[15];
	__be32	resp0_dma_reg[15];
	__be32	req1_dma_reg[15];
	__be32	xmt0_dma_reg[32];
	__be32	xmt1_dma_reg[32];
	__be32	xmt2_dma_reg[32];
	__be32	xmt3_dma_reg[32];
	__be32	xmt4_dma_reg[32];
	__be32	xmt_data_dma_reg[16];
	__be32	rcvt0_data_dma_reg[32];
	__be32	rcvt1_data_dma_reg[32];
	__be32	risc_gp_reg[128];
	__be32	lmc_reg[112];
	__be32	fpm_hdw_reg[192];
	__be32	fb_hdw_reg[176];
	__be32	code_ram[0x2000];
	__be32	ext_mem[1];
};

struct qla25xx_fw_dump {
	__be32	host_status;
	__be32	host_risc_reg[32];
	__be32	pcie_regs[4];
	__be32	host_reg[32];
	__be32	shadow_reg[11];
	__be32	risc_io_reg;
	__be16	mailbox_reg[32];
	__be32	xseq_gp_reg[128];
	__be32	xseq_0_reg[48];
	__be32	xseq_1_reg[16];
	__be32	rseq_gp_reg[128];
	__be32	rseq_0_reg[32];
	__be32	rseq_1_reg[16];
	__be32	rseq_2_reg[16];
	__be32	aseq_gp_reg[128];
	__be32	aseq_0_reg[32];
	__be32	aseq_1_reg[16];
	__be32	aseq_2_reg[16];
	__be32	cmd_dma_reg[16];
	__be32	req0_dma_reg[15];
	__be32	resp0_dma_reg[15];
	__be32	req1_dma_reg[15];
	__be32	xmt0_dma_reg[32];
	__be32	xmt1_dma_reg[32];
	__be32	xmt2_dma_reg[32];
	__be32	xmt3_dma_reg[32];
	__be32	xmt4_dma_reg[32];
	__be32	xmt_data_dma_reg[16];
	__be32	rcvt0_data_dma_reg[32];
	__be32	rcvt1_data_dma_reg[32];
	__be32	risc_gp_reg[128];
	__be32	lmc_reg[128];
	__be32	fpm_hdw_reg[192];
	__be32	fb_hdw_reg[192];
	__be32	code_ram[0x2000];
	__be32	ext_mem[1];
};

struct qla81xx_fw_dump {
	__be32	host_status;
	__be32	host_risc_reg[32];
	__be32	pcie_regs[4];
	__be32	host_reg[32];
	__be32	shadow_reg[11];
	__be32	risc_io_reg;
	__be16	mailbox_reg[32];
	__be32	xseq_gp_reg[128];
	__be32	xseq_0_reg[48];
	__be32	xseq_1_reg[16];
	__be32	rseq_gp_reg[128];
	__be32	rseq_0_reg[32];
	__be32	rseq_1_reg[16];
	__be32	rseq_2_reg[16];
	__be32	aseq_gp_reg[128];
	__be32	aseq_0_reg[32];
	__be32	aseq_1_reg[16];
	__be32	aseq_2_reg[16];
	__be32	cmd_dma_reg[16];
	__be32	req0_dma_reg[15];
	__be32	resp0_dma_reg[15];
	__be32	req1_dma_reg[15];
	__be32	xmt0_dma_reg[32];
	__be32	xmt1_dma_reg[32];
	__be32	xmt2_dma_reg[32];
	__be32	xmt3_dma_reg[32];
	__be32	xmt4_dma_reg[32];
	__be32	xmt_data_dma_reg[16];
	__be32	rcvt0_data_dma_reg[32];
	__be32	rcvt1_data_dma_reg[32];
	__be32	risc_gp_reg[128];
	__be32	lmc_reg[128];
	__be32	fpm_hdw_reg[224];
	__be32	fb_hdw_reg[208];
	__be32	code_ram[0x2000];
	__be32	ext_mem[1];
};

struct qla83xx_fw_dump {
	__be32	host_status;
	__be32	host_risc_reg[48];
	__be32	pcie_regs[4];
	__be32	host_reg[32];
	__be32	shadow_reg[11];
	__be32	risc_io_reg;
	__be16	mailbox_reg[32];
	__be32	xseq_gp_reg[256];
	__be32	xseq_0_reg[48];
	__be32	xseq_1_reg[16];
	__be32	xseq_2_reg[16];
	__be32	rseq_gp_reg[256];
	__be32	rseq_0_reg[32];
	__be32	rseq_1_reg[16];
	__be32	rseq_2_reg[16];
	__be32	rseq_3_reg[16];
	__be32	aseq_gp_reg[256];
	__be32	aseq_0_reg[32];
	__be32	aseq_1_reg[16];
	__be32	aseq_2_reg[16];
	__be32	aseq_3_reg[16];
	__be32	cmd_dma_reg[64];
	__be32	req0_dma_reg[15];
	__be32	resp0_dma_reg[15];
	__be32	req1_dma_reg[15];
	__be32	xmt0_dma_reg[32];
	__be32	xmt1_dma_reg[32];
	__be32	xmt2_dma_reg[32];
	__be32	xmt3_dma_reg[32];
	__be32	xmt4_dma_reg[32];
	__be32	xmt_data_dma_reg[16];
	__be32	rcvt0_data_dma_reg[32];
	__be32	rcvt1_data_dma_reg[32];
	__be32	risc_gp_reg[128];
	__be32	lmc_reg[128];
	__be32	fpm_hdw_reg[256];
	__be32	rq0_array_reg[256];
	__be32	rq1_array_reg[256];
	__be32	rp0_array_reg[256];
	__be32	rp1_array_reg[256];
	__be32	queue_control_reg[16];
	__be32	fb_hdw_reg[432];
	__be32	at0_array_reg[128];
	__be32	code_ram[0x2400];
	__be32	ext_mem[1];
};

#define EFT_NUM_BUFFERS		4
#define EFT_BYTES_PER_BUFFER	0x4000
#define EFT_SIZE		((EFT_BYTES_PER_BUFFER) * (EFT_NUM_BUFFERS))

#define FCE_NUM_BUFFERS		64
#define FCE_BYTES_PER_BUFFER	0x400
#define FCE_SIZE		((FCE_BYTES_PER_BUFFER) * (FCE_NUM_BUFFERS))
#define fce_calc_size(b)	((FCE_BYTES_PER_BUFFER) * (b))

struct qla2xxx_fce_chain {
	__be32	type;
	__be32	chain_size;

	__be32	size;
	__be32	addr_l;
	__be32	addr_h;
	__be32	eregs[8];
};

/* used by exchange off load and extended login offload */
struct qla2xxx_offld_chain {
	__be32	type;
	__be32	chain_size;

	__be32	size;
	__be32	reserved;
	__be64	addr;
};

struct qla2xxx_mq_chain {
	__be32	type;
	__be32	chain_size;

	__be32	count;
	__be32	qregs[4 * QLA_MQ_SIZE];
};

struct qla2xxx_mqueue_header {
	__be32	queue;
#define TYPE_REQUEST_QUEUE	0x1
#define TYPE_RESPONSE_QUEUE	0x2
#define TYPE_ATIO_QUEUE		0x3
	__be32	number;
	__be32	size;
};

struct qla2xxx_mqueue_chain {
	__be32	type;
	__be32	chain_size;
};

#define DUMP_CHAIN_VARIANT	0x80000000
#define DUMP_CHAIN_FCE		0x7FFFFAF0
#define DUMP_CHAIN_MQ		0x7FFFFAF1
#define DUMP_CHAIN_QUEUE	0x7FFFFAF2
#define DUMP_CHAIN_EXLOGIN	0x7FFFFAF3
#define DUMP_CHAIN_EXCHG	0x7FFFFAF4
#define DUMP_CHAIN_LAST		0x80000000

struct qla2xxx_fw_dump {
	uint8_t signature[4];
	__be32	version;

	__be32 fw_major_version;
	__be32 fw_minor_version;
	__be32 fw_subminor_version;
	__be32 fw_attributes;

	__be32 vendor;
	__be32 device;
	__be32 subsystem_vendor;
	__be32 subsystem_device;

	__be32	fixed_size;
	__be32	mem_size;
	__be32	req_q_size;
	__be32	rsp_q_size;

	__be32	eft_size;
	__be32	eft_addr_l;
	__be32	eft_addr_h;

	__be32	header_size;

	union {
		struct qla2100_fw_dump isp21;
		struct qla2300_fw_dump isp23;
		struct qla24xx_fw_dump isp24;
		struct qla25xx_fw_dump isp25;
		struct qla81xx_fw_dump isp81;
		struct qla83xx_fw_dump isp83;
	} isp;
};

#define QL_MSGHDR "qla2xxx"
#define QL_DBG_DEFAULT1_MASK    0x1e600000

#define ql_log_fatal		0 /* display fatal errors */
#define ql_log_warn		1 /* display critical errors */
#define ql_log_info		2 /* display all recovered errors */
#define ql_log_all		3 /* This value is only used by ql_errlev.
				   * No messages will use this value.
				   * This should be always highest value
				   * as compared to other log levels.
				   */

extern uint ql_errlev;

void __attribute__((format (printf, 4, 5)))
ql_dbg(uint, scsi_qla_host_t *vha, uint, const char *fmt, ...);
void __attribute__((format (printf, 4, 5)))
ql_dbg_pci(uint, struct pci_dev *pdev, uint, const char *fmt, ...);
void __attribute__((format (printf, 4, 5)))
ql_dbg_qp(uint32_t, struct qla_qpair *, int32_t, const char *fmt, ...);


void __attribute__((format (printf, 4, 5)))
ql_log(uint, scsi_qla_host_t *vha, uint, const char *fmt, ...);
void __attribute__((format (printf, 4, 5)))
ql_log_pci(uint, struct pci_dev *pdev, uint, const char *fmt, ...);

void __attribute__((format (printf, 4, 5)))
ql_log_qp(uint32_t, struct qla_qpair *, int32_t, const char *fmt, ...);

/* Debug Levels */
/* The 0x40000000 is the max value any debug level can have
 * as ql2xextended_error_logging is of type signed int
 */
#define ql_dbg_init	0x40000000 /* Init Debug */
#define ql_dbg_mbx	0x20000000 /* MBX Debug */
#define ql_dbg_disc	0x10000000 /* Device Discovery Debug */
#define ql_dbg_io	0x08000000 /* IO Tracing Debug */
#define ql_dbg_dpc	0x04000000 /* DPC Thead Debug */
#define ql_dbg_async	0x02000000 /* Async events Debug */
#define ql_dbg_timer	0x01000000 /* Timer Debug */
#define ql_dbg_user	0x00800000 /* User Space Interations Debug */
#define ql_dbg_taskm	0x00400000 /* Task Management Debug */
#define ql_dbg_aer	0x00200000 /* AER/EEH Debug */
#define ql_dbg_multiq	0x00100000 /* MultiQ Debug */
#define ql_dbg_p3p	0x00080000 /* P3P specific Debug */
#define ql_dbg_vport	0x00040000 /* Virtual Port Debug */
#define ql_dbg_buffer	0x00020000 /* For dumping the buffer/regs */
#define ql_dbg_misc	0x00010000 /* For dumping everything that is not
				    * not covered by upper categories
				    */
#define ql_dbg_verbose	0x00008000 /* More verbosity for each level
				    * This is to be used with other levels where
				    * more verbosity is required. It might not
				    * be applicable to all the levels.
				    */
#define ql_dbg_tgt	0x00004000 /* Target mode */
#define ql_dbg_tgt_mgt	0x00002000 /* Target mode management */
#define ql_dbg_tgt_tmr	0x00001000 /* Target mode task management */
#define ql_dbg_tgt_dif  0x00000800 /* Target mode dif */
#define ql_dbg_edif	0x00000400 /* edif and purex debug */

extern int qla27xx_dump_mpi_ram(struct qla_hw_data *, uint32_t, uint32_t *,
	uint32_t, void **);
extern int qla24xx_dump_ram(struct qla_hw_data *, uint32_t, __be32 *,
	uint32_t, void **);
extern void qla24xx_pause_risc(struct device_reg_24xx __iomem *,
	struct qla_hw_data *);
extern int qla24xx_soft_reset(struct qla_hw_data *);

static inline int
ql_mask_match(uint level)
{
	if (ql2xextended_error_logging == 1)
		ql2xextended_error_logging = QL_DBG_DEFAULT1_MASK;

	return level && ((level & ql2xextended_error_logging) == level);
}

static inline int
ql_mask_match_ext(uint level, int *log_tunable)
{
	if (*log_tunable == 1)
		*log_tunable = QL_DBG_DEFAULT1_MASK;

	return (level & *log_tunable) == level;
}

/* Assumes local variable pbuf and pbuf_ready present. */
#define ql_ktrace(dbg_msg, level, pbuf, pdev, vha, id, fmt) do {	\
	struct va_format _vaf;						\
	va_list _va;							\
	u32 dbg_off = dbg_msg ? ql_dbg_offset : 0;			\
									\
	pbuf[0] = 0;							\
	if (!trace_ql_dbg_log_enabled())				\
		break;							\
									\
	if (dbg_msg && !ql_mask_match_ext(level,			\
				&ql2xextended_error_logging_ktrace))	\
		break;							\
									\
	ql_dbg_prefix(pbuf, ARRAY_SIZE(pbuf), pdev, vha, id + dbg_off);	\
									\
	va_start(_va, fmt);						\
	_vaf.fmt = fmt;							\
	_vaf.va = &_va;							\
									\
	trace_ql_dbg_log(pbuf, &_vaf);					\
									\
	va_end(_va);							\
} while (0)

#define QLA_ENABLE_KERNEL_TRACING

#ifdef QLA_ENABLE_KERNEL_TRACING
#define QLA_TRACE_ENABLE(_tr) \
	trace_array_set_clr_event(_tr, "qla", NULL, true)
#else /* QLA_ENABLE_KERNEL_TRACING */
#define QLA_TRACE_ENABLE(_tr)
#endif /* QLA_ENABLE_KERNEL_TRACING */
