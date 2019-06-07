/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */

#ifndef __QLA_DMP27_H__
#define	__QLA_DMP27_H__

#define IOBASE_ADDR	offsetof(struct device_reg_24xx, iobase_addr)

struct __packed qla27xx_fwdt_template {
	__le32 template_type;
	__le32 entry_offset;
	uint32_t template_size;
	uint32_t count;		/* borrow field for running/residual count */

	__le32 entry_count;
	uint32_t template_version;
	uint32_t capture_timestamp;
	uint32_t template_checksum;

	uint32_t reserved_2;
	uint32_t driver_info[3];

	uint32_t saved_state[16];

	uint32_t reserved_3[8];
	uint32_t firmware_version[5];
};

#define TEMPLATE_TYPE_FWDUMP		99

#define ENTRY_TYPE_NOP			0
#define ENTRY_TYPE_TMP_END		255
#define ENTRY_TYPE_RD_IOB_T1		256
#define ENTRY_TYPE_WR_IOB_T1		257
#define ENTRY_TYPE_RD_IOB_T2		258
#define ENTRY_TYPE_WR_IOB_T2		259
#define ENTRY_TYPE_RD_PCI		260
#define ENTRY_TYPE_WR_PCI		261
#define ENTRY_TYPE_RD_RAM		262
#define ENTRY_TYPE_GET_QUEUE		263
#define ENTRY_TYPE_GET_FCE		264
#define ENTRY_TYPE_PSE_RISC		265
#define ENTRY_TYPE_RST_RISC		266
#define ENTRY_TYPE_DIS_INTR		267
#define ENTRY_TYPE_GET_HBUF		268
#define ENTRY_TYPE_SCRATCH		269
#define ENTRY_TYPE_RDREMREG		270
#define ENTRY_TYPE_WRREMREG		271
#define ENTRY_TYPE_RDREMRAM		272
#define ENTRY_TYPE_PCICFG		273
#define ENTRY_TYPE_GET_SHADOW		274
#define ENTRY_TYPE_WRITE_BUF		275
#define ENTRY_TYPE_CONDITIONAL		276
#define ENTRY_TYPE_RDPEPREG		277
#define ENTRY_TYPE_WRPEPREG		278

#define CAPTURE_FLAG_PHYS_ONLY		BIT_0
#define CAPTURE_FLAG_PHYS_VIRT		BIT_1

#define DRIVER_FLAG_SKIP_ENTRY		BIT_7

struct __packed qla27xx_fwdt_entry {
	struct __packed {
		__le32 type;
		__le32 size;
		uint32_t reserved_1;

		uint8_t  capture_flags;
		uint8_t  reserved_2[2];
		uint8_t  driver_flags;
	} hdr;
	union __packed {
		struct __packed {
		} t0;

		struct __packed {
		} t255;

		struct __packed {
			__le32 base_addr;
			uint8_t  reg_width;
			__le16 reg_count;
			uint8_t  pci_offset;
		} t256;

		struct __packed {
			__le32 base_addr;
			__le32 write_data;
			uint8_t  pci_offset;
			uint8_t  reserved[3];
		} t257;

		struct __packed {
			__le32 base_addr;
			uint8_t  reg_width;
			__le16 reg_count;
			uint8_t  pci_offset;
			uint8_t  banksel_offset;
			uint8_t  reserved[3];
			__le32 bank;
		} t258;

		struct __packed {
			__le32 base_addr;
			__le32 write_data;
			uint8_t  reserved[2];
			uint8_t  pci_offset;
			uint8_t  banksel_offset;
			__le32 bank;
		} t259;

		struct __packed {
			uint8_t pci_offset;
			uint8_t reserved[3];
		} t260;

		struct __packed {
			uint8_t pci_offset;
			uint8_t reserved[3];
			__le32 write_data;
		} t261;

		struct __packed {
			uint8_t  ram_area;
			uint8_t  reserved[3];
			__le32 start_addr;
			__le32 end_addr;
		} t262;

		struct __packed {
			uint32_t num_queues;
			uint8_t  queue_type;
			uint8_t  reserved[3];
		} t263;

		struct __packed {
			uint32_t fce_trace_size;
			uint64_t write_pointer;
			uint64_t base_pointer;
			uint32_t fce_enable_mb0;
			uint32_t fce_enable_mb2;
			uint32_t fce_enable_mb3;
			uint32_t fce_enable_mb4;
			uint32_t fce_enable_mb5;
			uint32_t fce_enable_mb6;
		} t264;

		struct __packed {
		} t265;

		struct __packed {
		} t266;

		struct __packed {
			uint8_t  pci_offset;
			uint8_t  reserved[3];
			__le32 data;
		} t267;

		struct __packed {
			uint8_t  buf_type;
			uint8_t  reserved[3];
			uint32_t buf_size;
			uint64_t start_addr;
		} t268;

		struct __packed {
			uint32_t scratch_size;
		} t269;

		struct __packed {
			__le32 addr;
			__le32 count;
		} t270;

		struct __packed {
			__le32 addr;
			__le32 data;
		} t271;

		struct __packed {
			__le32 addr;
			__le32 count;
		} t272;

		struct __packed {
			__le32 addr;
			__le32 count;
		} t273;

		struct __packed {
			uint32_t num_queues;
			uint8_t  queue_type;
			uint8_t  reserved[3];
		} t274;

		struct __packed {
			__le32 length;
			uint8_t  buffer[];
		} t275;

		struct __packed {
			__le32 cond1;
			__le32 cond2;
		} t276;

		struct __packed {
			__le32 cmd_addr;
			__le32 wr_cmd_data;
			__le32 data_addr;
		} t277;

		struct __packed {
			__le32 cmd_addr;
			__le32 wr_cmd_data;
			__le32 data_addr;
			__le32 wr_data;
		} t278;
	};
};

#define T262_RAM_AREA_CRITICAL_RAM	1
#define T262_RAM_AREA_EXTERNAL_RAM	2
#define T262_RAM_AREA_SHARED_RAM	3
#define T262_RAM_AREA_DDR_RAM		4
#define T262_RAM_AREA_MISC		5

#define T263_QUEUE_TYPE_REQ		1
#define T263_QUEUE_TYPE_RSP		2
#define T263_QUEUE_TYPE_ATIO		3

#define T268_BUF_TYPE_EXTD_TRACE	1
#define T268_BUF_TYPE_EXCH_BUFOFF	2
#define T268_BUF_TYPE_EXTD_LOGIN	3
#define T268_BUF_TYPE_REQ_MIRROR	4
#define T268_BUF_TYPE_RSP_MIRROR	5

#define T274_QUEUE_TYPE_REQ_SHAD	1
#define T274_QUEUE_TYPE_RSP_SHAD	2
#define T274_QUEUE_TYPE_ATIO_SHAD	3

#endif
