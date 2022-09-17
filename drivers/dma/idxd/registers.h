/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2019 Intel Corporation. All rights rsvd. */
#ifndef _IDXD_REGISTERS_H_
#define _IDXD_REGISTERS_H_

/* PCI Config */
#define PCI_DEVICE_ID_INTEL_DSA_SPR0	0x0b25
#define PCI_DEVICE_ID_INTEL_IAX_SPR0	0x0cfe

#define DEVICE_VERSION_1		0x100
#define DEVICE_VERSION_2		0x200

#define IDXD_MMIO_BAR		0
#define IDXD_WQ_BAR		2
#define IDXD_PORTAL_SIZE	PAGE_SIZE

/* MMIO Device BAR0 Registers */
#define IDXD_VER_OFFSET			0x00
#define IDXD_VER_MAJOR_MASK		0xf0
#define IDXD_VER_MINOR_MASK		0x0f
#define GET_IDXD_VER_MAJOR(x)		(((x) & IDXD_VER_MAJOR_MASK) >> 4)
#define GET_IDXD_VER_MINOR(x)		((x) & IDXD_VER_MINOR_MASK)

union gen_cap_reg {
	struct {
		u64 block_on_fault:1;
		u64 overlap_copy:1;
		u64 cache_control_mem:1;
		u64 cache_control_cache:1;
		u64 cmd_cap:1;
		u64 rsvd:3;
		u64 dest_readback:1;
		u64 drain_readback:1;
		u64 rsvd2:6;
		u64 max_xfer_shift:5;
		u64 max_batch_shift:4;
		u64 max_ims_mult:6;
		u64 config_en:1;
		u64 rsvd3:32;
	};
	u64 bits;
} __packed;
#define IDXD_GENCAP_OFFSET		0x10

union wq_cap_reg {
	struct {
		u64 total_wq_size:16;
		u64 num_wqs:8;
		u64 wqcfg_size:4;
		u64 rsvd:20;
		u64 shared_mode:1;
		u64 dedicated_mode:1;
		u64 wq_ats_support:1;
		u64 priority:1;
		u64 occupancy:1;
		u64 occupancy_int:1;
		u64 op_config:1;
		u64 rsvd3:9;
	};
	u64 bits;
} __packed;
#define IDXD_WQCAP_OFFSET		0x20
#define IDXD_WQCFG_MIN			5

union group_cap_reg {
	struct {
		u64 num_groups:8;
		u64 total_rdbufs:8;	/* formerly total_tokens */
		u64 rdbuf_ctrl:1;	/* formerly token_en */
		u64 rdbuf_limit:1;	/* formerly token_limit */
		u64 progress_limit:1;	/* descriptor and batch descriptor */
		u64 rsvd:45;
	};
	u64 bits;
} __packed;
#define IDXD_GRPCAP_OFFSET		0x30

union engine_cap_reg {
	struct {
		u64 num_engines:8;
		u64 rsvd:56;
	};
	u64 bits;
} __packed;

#define IDXD_ENGCAP_OFFSET		0x38

#define IDXD_OPCAP_NOOP			0x0001
#define IDXD_OPCAP_BATCH			0x0002
#define IDXD_OPCAP_MEMMOVE		0x0008
struct opcap {
	u64 bits[4];
};

#define IDXD_MAX_OPCAP_BITS		256U

#define IDXD_OPCAP_OFFSET		0x40

#define IDXD_TABLE_OFFSET		0x60
union offsets_reg {
	struct {
		u64 grpcfg:16;
		u64 wqcfg:16;
		u64 msix_perm:16;
		u64 ims:16;
		u64 perfmon:16;
		u64 rsvd:48;
	};
	u64 bits[2];
} __packed;

#define IDXD_TABLE_MULT			0x100

#define IDXD_GENCFG_OFFSET		0x80
union gencfg_reg {
	struct {
		u32 rdbuf_limit:8;
		u32 rsvd:4;
		u32 user_int_en:1;
		u32 rsvd2:19;
	};
	u32 bits;
} __packed;

#define IDXD_GENCTRL_OFFSET		0x88
union genctrl_reg {
	struct {
		u32 softerr_int_en:1;
		u32 halt_int_en:1;
		u32 rsvd:30;
	};
	u32 bits;
} __packed;

#define IDXD_GENSTATS_OFFSET		0x90
union gensts_reg {
	struct {
		u32 state:2;
		u32 reset_type:2;
		u32 rsvd:28;
	};
	u32 bits;
} __packed;

enum idxd_device_status_state {
	IDXD_DEVICE_STATE_DISABLED = 0,
	IDXD_DEVICE_STATE_ENABLED,
	IDXD_DEVICE_STATE_DRAIN,
	IDXD_DEVICE_STATE_HALT,
};

enum idxd_device_reset_type {
	IDXD_DEVICE_RESET_SOFTWARE = 0,
	IDXD_DEVICE_RESET_FLR,
	IDXD_DEVICE_RESET_WARM,
	IDXD_DEVICE_RESET_COLD,
};

#define IDXD_INTCAUSE_OFFSET		0x98
#define IDXD_INTC_ERR			0x01
#define IDXD_INTC_CMD			0x02
#define IDXD_INTC_OCCUPY			0x04
#define IDXD_INTC_PERFMON_OVFL		0x08
#define IDXD_INTC_HALT_STATE		0x10
#define IDXD_INTC_INT_HANDLE_REVOKED	0x80000000

#define IDXD_CMD_OFFSET			0xa0
union idxd_command_reg {
	struct {
		u32 operand:20;
		u32 cmd:5;
		u32 rsvd:6;
		u32 int_req:1;
	};
	u32 bits;
} __packed;

enum idxd_cmd {
	IDXD_CMD_ENABLE_DEVICE = 1,
	IDXD_CMD_DISABLE_DEVICE,
	IDXD_CMD_DRAIN_ALL,
	IDXD_CMD_ABORT_ALL,
	IDXD_CMD_RESET_DEVICE,
	IDXD_CMD_ENABLE_WQ,
	IDXD_CMD_DISABLE_WQ,
	IDXD_CMD_DRAIN_WQ,
	IDXD_CMD_ABORT_WQ,
	IDXD_CMD_RESET_WQ,
	IDXD_CMD_DRAIN_PASID,
	IDXD_CMD_ABORT_PASID,
	IDXD_CMD_REQUEST_INT_HANDLE,
	IDXD_CMD_RELEASE_INT_HANDLE,
};

#define CMD_INT_HANDLE_IMS		0x10000

#define IDXD_CMDSTS_OFFSET		0xa8
union cmdsts_reg {
	struct {
		u8 err;
		u16 result;
		u8 rsvd:7;
		u8 active:1;
	};
	u32 bits;
} __packed;
#define IDXD_CMDSTS_ACTIVE		0x80000000
#define IDXD_CMDSTS_ERR_MASK		0xff
#define IDXD_CMDSTS_RES_SHIFT		8

enum idxd_cmdsts_err {
	IDXD_CMDSTS_SUCCESS = 0,
	IDXD_CMDSTS_INVAL_CMD,
	IDXD_CMDSTS_INVAL_WQIDX,
	IDXD_CMDSTS_HW_ERR,
	/* enable device errors */
	IDXD_CMDSTS_ERR_DEV_ENABLED = 0x10,
	IDXD_CMDSTS_ERR_CONFIG,
	IDXD_CMDSTS_ERR_BUSMASTER_EN,
	IDXD_CMDSTS_ERR_PASID_INVAL,
	IDXD_CMDSTS_ERR_WQ_SIZE_ERANGE,
	IDXD_CMDSTS_ERR_GRP_CONFIG,
	IDXD_CMDSTS_ERR_GRP_CONFIG2,
	IDXD_CMDSTS_ERR_GRP_CONFIG3,
	IDXD_CMDSTS_ERR_GRP_CONFIG4,
	/* enable wq errors */
	IDXD_CMDSTS_ERR_DEV_NOTEN = 0x20,
	IDXD_CMDSTS_ERR_WQ_ENABLED,
	IDXD_CMDSTS_ERR_WQ_SIZE,
	IDXD_CMDSTS_ERR_WQ_PRIOR,
	IDXD_CMDSTS_ERR_WQ_MODE,
	IDXD_CMDSTS_ERR_BOF_EN,
	IDXD_CMDSTS_ERR_PASID_EN,
	IDXD_CMDSTS_ERR_MAX_BATCH_SIZE,
	IDXD_CMDSTS_ERR_MAX_XFER_SIZE,
	/* disable device errors */
	IDXD_CMDSTS_ERR_DIS_DEV_EN = 0x31,
	/* disable WQ, drain WQ, abort WQ, reset WQ */
	IDXD_CMDSTS_ERR_DEV_NOT_EN,
	/* request interrupt handle */
	IDXD_CMDSTS_ERR_INVAL_INT_IDX = 0x41,
	IDXD_CMDSTS_ERR_NO_HANDLE,
};

#define IDXD_CMDCAP_OFFSET		0xb0

#define IDXD_SWERR_OFFSET		0xc0
#define IDXD_SWERR_VALID		0x00000001
#define IDXD_SWERR_OVERFLOW		0x00000002
#define IDXD_SWERR_ACK			(IDXD_SWERR_VALID | IDXD_SWERR_OVERFLOW)
union sw_err_reg {
	struct {
		u64 valid:1;
		u64 overflow:1;
		u64 desc_valid:1;
		u64 wq_idx_valid:1;
		u64 batch:1;
		u64 fault_rw:1;
		u64 priv:1;
		u64 rsvd:1;
		u64 error:8;
		u64 wq_idx:8;
		u64 rsvd2:8;
		u64 operation:8;
		u64 pasid:20;
		u64 rsvd3:4;

		u64 batch_idx:16;
		u64 rsvd4:16;
		u64 invalid_flags:32;

		u64 fault_addr;

		u64 rsvd5;
	};
	u64 bits[4];
} __packed;

union msix_perm {
	struct {
		u32 rsvd:2;
		u32 ignore:1;
		u32 pasid_en:1;
		u32 rsvd2:8;
		u32 pasid:20;
	};
	u32 bits;
} __packed;

union group_flags {
	struct {
		u64 tc_a:3;
		u64 tc_b:3;
		u64 rsvd:1;
		u64 use_rdbuf_limit:1;
		u64 rdbufs_reserved:8;
		u64 rsvd2:4;
		u64 rdbufs_allowed:8;
		u64 rsvd3:4;
		u64 desc_progress_limit:2;
		u64 rsvd4:30;
	};
	u64 bits;
} __packed;

struct grpcfg {
	u64 wqs[4];
	u64 engines;
	union group_flags flags;
} __packed;

union wqcfg {
	struct {
		/* bytes 0-3 */
		u16 wq_size;
		u16 rsvd;

		/* bytes 4-7 */
		u16 wq_thresh;
		u16 rsvd1;

		/* bytes 8-11 */
		u32 mode:1;	/* shared or dedicated */
		u32 bof:1;	/* block on fault */
		u32 wq_ats_disable:1;
		u32 rsvd2:1;
		u32 priority:4;
		u32 pasid:20;
		u32 pasid_en:1;
		u32 priv:1;
		u32 rsvd3:2;

		/* bytes 12-15 */
		u32 max_xfer_shift:5;
		u32 max_batch_shift:4;
		u32 rsvd4:23;

		/* bytes 16-19 */
		u16 occupancy_inth;
		u16 occupancy_table_sel:1;
		u16 rsvd5:15;

		/* bytes 20-23 */
		u16 occupancy_limit;
		u16 occupancy_int_en:1;
		u16 rsvd6:15;

		/* bytes 24-27 */
		u16 occupancy;
		u16 occupancy_int:1;
		u16 rsvd7:12;
		u16 mode_support:1;
		u16 wq_state:2;

		/* bytes 28-31 */
		u32 rsvd8;

		/* bytes 32-63 */
		u64 op_config[4];
	};
	u32 bits[16];
} __packed;

#define WQCFG_PASID_IDX                2
#define WQCFG_PRIVL_IDX		2
#define WQCFG_OCCUP_IDX		6

#define WQCFG_OCCUP_MASK	0xffff

/*
 * This macro calculates the offset into the WQCFG register
 * idxd - struct idxd *
 * n - wq id
 * ofs - the index of the 32b dword for the config register
 *
 * The WQCFG register block is divided into groups per each wq. The n index
 * allows us to move to the register group that's for that particular wq.
 * Each register is 32bits. The ofs gives us the number of register to access.
 */
#define WQCFG_OFFSET(_idxd_dev, n, ofs) \
({\
	typeof(_idxd_dev) __idxd_dev = (_idxd_dev);	\
	(__idxd_dev)->wqcfg_offset + (n) * (__idxd_dev)->wqcfg_size + sizeof(u32) * (ofs);	\
})

#define WQCFG_STRIDES(_idxd_dev) ((_idxd_dev)->wqcfg_size / sizeof(u32))

#define GRPCFG_SIZE		64
#define GRPWQCFG_STRIDES	4

/*
 * This macro calculates the offset into the GRPCFG register
 * idxd - struct idxd *
 * n - wq id
 * ofs - the index of the 32b dword for the config register
 *
 * The WQCFG register block is divided into groups per each wq. The n index
 * allows us to move to the register group that's for that particular wq.
 * Each register is 32bits. The ofs gives us the number of register to access.
 */
#define GRPWQCFG_OFFSET(idxd_dev, n, ofs) ((idxd_dev)->grpcfg_offset +\
					   (n) * GRPCFG_SIZE + sizeof(u64) * (ofs))
#define GRPENGCFG_OFFSET(idxd_dev, n) ((idxd_dev)->grpcfg_offset + (n) * GRPCFG_SIZE + 32)
#define GRPFLGCFG_OFFSET(idxd_dev, n) ((idxd_dev)->grpcfg_offset + (n) * GRPCFG_SIZE + 40)

/* Following is performance monitor registers */
#define IDXD_PERFCAP_OFFSET		0x0
union idxd_perfcap {
	struct {
		u64 num_perf_counter:6;
		u64 rsvd1:2;
		u64 counter_width:8;
		u64 num_event_category:4;
		u64 global_event_category:16;
		u64 filter:8;
		u64 rsvd2:8;
		u64 cap_per_counter:1;
		u64 writeable_counter:1;
		u64 counter_freeze:1;
		u64 overflow_interrupt:1;
		u64 rsvd3:8;
	};
	u64 bits;
} __packed;

#define IDXD_EVNTCAP_OFFSET		0x80
union idxd_evntcap {
	struct {
		u64 events:28;
		u64 rsvd:36;
	};
	u64 bits;
} __packed;

struct idxd_event {
	union {
		struct {
			u32 event_category:4;
			u32 events:28;
		};
		u32 val;
	};
} __packed;

#define IDXD_CNTRCAP_OFFSET		0x800
struct idxd_cntrcap {
	union {
		struct {
			u32 counter_width:8;
			u32 rsvd:20;
			u32 num_events:4;
		};
		u32 val;
	};
	struct idxd_event events[];
} __packed;

#define IDXD_PERFRST_OFFSET		0x10
union idxd_perfrst {
	struct {
		u32 perfrst_config:1;
		u32 perfrst_counter:1;
		u32 rsvd:30;
	};
	u32 val;
} __packed;

#define IDXD_OVFSTATUS_OFFSET		0x30
#define IDXD_PERFFRZ_OFFSET		0x20
#define IDXD_CNTRCFG_OFFSET		0x100
union idxd_cntrcfg {
	struct {
		u64 enable:1;
		u64 interrupt_ovf:1;
		u64 global_freeze_ovf:1;
		u64 rsvd1:5;
		u64 event_category:4;
		u64 rsvd2:20;
		u64 events:28;
		u64 rsvd3:4;
	};
	u64 val;
} __packed;

#define IDXD_FLTCFG_OFFSET		0x300

#define IDXD_CNTRDATA_OFFSET		0x200
union idxd_cntrdata {
	struct {
		u64 event_count_value;
	};
	u64 val;
} __packed;

union event_cfg {
	struct {
		u64 event_cat:4;
		u64 event_enc:28;
	};
	u64 val;
} __packed;

union filter_cfg {
	struct {
		u64 wq:32;
		u64 tc:8;
		u64 pg_sz:4;
		u64 xfer_sz:8;
		u64 eng:8;
	};
	u64 val;
} __packed;

#endif
