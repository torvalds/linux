/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2013 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */

#ifndef __QLA_DMP27_H__
#define	__QLA_DMP27_H__

#define IOBASE_ADDR	offsetof(struct device_reg_24xx, iobase_addr)

struct __packed qla27xx_fwdt_template {
	uint32_t template_type;
	uint32_t entry_offset;
	uint32_t template_size;
	uint32_t reserved_1;

	uint32_t entry_count;
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

#define CAPTURE_FLAG_PHYS_ONLY		BIT_0
#define CAPTURE_FLAG_PHYS_VIRT		BIT_1

#define DRIVER_FLAG_SKIP_ENTRY		BIT_7

struct __packed qla27xx_fwdt_entry {
	struct __packed {
		uint32_t entry_type;
		uint32_t entry_size;
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
			uint32_t base_addr;
			uint8_t  reg_width;
			uint16_t reg_count;
			uint8_t  pci_offset;
		} t256;

		struct __packed {
			uint32_t base_addr;
			uint32_t write_data;
			uint8_t  pci_offset;
			uint8_t  reserved[3];
		} t257;

		struct __packed {
			uint32_t base_addr;
			uint8_t  reg_width;
			uint16_t reg_count;
			uint8_t  pci_offset;
			uint8_t  banksel_offset;
			uint8_t  reserved[3];
			uint32_t bank;
		} t258;

		struct __packed {
			uint32_t base_addr;
			uint32_t write_data;
			uint8_t  reserved[2];
			uint8_t  pci_offset;
			uint8_t  banksel_offset;
			uint32_t bank;
		} t259;

		struct __packed {
			uint8_t pci_addr;
			uint8_t reserved[3];
		} t260;

		struct __packed {
			uint8_t pci_addr;
			uint8_t reserved[3];
			uint32_t write_data;
		} t261;

		struct __packed {
			uint8_t  ram_area;
			uint8_t  reserved[3];
			uint32_t start_addr;
			uint32_t end_addr;
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
			uint32_t data;
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
			uint32_t addr;
			uint32_t count;
		} t270;

		struct __packed {
			uint32_t addr;
			uint32_t data;
		} t271;

		struct __packed {
			uint32_t addr;
			uint32_t count;
		} t272;

		struct __packed {
			uint32_t addr;
			uint32_t count;
		} t273;
	};
};

#define T262_RAM_AREA_CRITICAL_RAM	1
#define T262_RAM_AREA_EXTERNAL_RAM	2
#define T262_RAM_AREA_SHARED_RAM	3
#define T262_RAM_AREA_DDR_RAM		4

#define T263_QUEUE_TYPE_REQ		1
#define T263_QUEUE_TYPE_RSP		2
#define T263_QUEUE_TYPE_ATIO		3

#define T268_BUF_TYPE_EXTD_TRACE	1
#define T268_BUF_TYPE_EXCH_BUFOFF	2
#define T268_BUF_TYPE_EXTD_LOGIN	3

#endif
