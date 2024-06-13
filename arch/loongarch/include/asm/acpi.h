/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Jianmin Lv <lvjianmin@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#ifndef _ASM_LOONGARCH_ACPI_H
#define _ASM_LOONGARCH_ACPI_H

#ifdef CONFIG_ACPI
extern int acpi_strict;
extern int acpi_disabled;
extern int acpi_pci_disabled;
extern int acpi_noirq;

#define acpi_os_ioremap acpi_os_ioremap
void __iomem *acpi_os_ioremap(acpi_physical_address phys, acpi_size size);

static inline void disable_acpi(void)
{
	acpi_disabled = 1;
	acpi_pci_disabled = 1;
	acpi_noirq = 1;
}

static inline bool acpi_has_cpu_in_madt(void)
{
	return true;
}

extern struct list_head acpi_wakeup_device_list;

/*
 * Temporary definitions until the core ACPICA code gets updated (see
 * 1656837932-18257-1-git-send-email-lvjianmin@loongson.cn and its
 * follow-ups for the "rationale").
 *
 * Once the "legal reasons" are cleared and that the code is merged,
 * this can be dropped entierely.
 */
#if (ACPI_CA_VERSION == 0x20220331 && !defined(LOONGARCH_ACPICA_EXT))

#define LOONGARCH_ACPICA_EXT	1

#define	ACPI_MADT_TYPE_CORE_PIC		17
#define	ACPI_MADT_TYPE_LIO_PIC		18
#define	ACPI_MADT_TYPE_HT_PIC		19
#define	ACPI_MADT_TYPE_EIO_PIC		20
#define	ACPI_MADT_TYPE_MSI_PIC		21
#define	ACPI_MADT_TYPE_BIO_PIC		22
#define	ACPI_MADT_TYPE_LPC_PIC		23

/* Values for Version field above */

enum acpi_madt_core_pic_version {
	ACPI_MADT_CORE_PIC_VERSION_NONE = 0,
	ACPI_MADT_CORE_PIC_VERSION_V1 = 1,
	ACPI_MADT_CORE_PIC_VERSION_RESERVED = 2	/* 2 and greater are reserved */
};

enum acpi_madt_lio_pic_version {
	ACPI_MADT_LIO_PIC_VERSION_NONE = 0,
	ACPI_MADT_LIO_PIC_VERSION_V1 = 1,
	ACPI_MADT_LIO_PIC_VERSION_RESERVED = 2	/* 2 and greater are reserved */
};

enum acpi_madt_eio_pic_version {
	ACPI_MADT_EIO_PIC_VERSION_NONE = 0,
	ACPI_MADT_EIO_PIC_VERSION_V1 = 1,
	ACPI_MADT_EIO_PIC_VERSION_RESERVED = 2	/* 2 and greater are reserved */
};

enum acpi_madt_ht_pic_version {
	ACPI_MADT_HT_PIC_VERSION_NONE = 0,
	ACPI_MADT_HT_PIC_VERSION_V1 = 1,
	ACPI_MADT_HT_PIC_VERSION_RESERVED = 2	/* 2 and greater are reserved */
};

enum acpi_madt_bio_pic_version {
	ACPI_MADT_BIO_PIC_VERSION_NONE = 0,
	ACPI_MADT_BIO_PIC_VERSION_V1 = 1,
	ACPI_MADT_BIO_PIC_VERSION_RESERVED = 2	/* 2 and greater are reserved */
};

enum acpi_madt_msi_pic_version {
	ACPI_MADT_MSI_PIC_VERSION_NONE = 0,
	ACPI_MADT_MSI_PIC_VERSION_V1 = 1,
	ACPI_MADT_MSI_PIC_VERSION_RESERVED = 2	/* 2 and greater are reserved */
};

enum acpi_madt_lpc_pic_version {
	ACPI_MADT_LPC_PIC_VERSION_NONE = 0,
	ACPI_MADT_LPC_PIC_VERSION_V1 = 1,
	ACPI_MADT_LPC_PIC_VERSION_RESERVED = 2	/* 2 and greater are reserved */
};

#pragma pack(1)

/* Core Interrupt Controller */

struct acpi_madt_core_pic {
	struct acpi_subtable_header header;
	u8 version;
	u32 processor_id;
	u32 core_id;
	u32 flags;
};

/* Legacy I/O Interrupt Controller */

struct acpi_madt_lio_pic {
	struct acpi_subtable_header header;
	u8 version;
	u64 address;
	u16 size;
	u8 cascade[2];
	u32 cascade_map[2];
};

/* Extend I/O Interrupt Controller */

struct acpi_madt_eio_pic {
	struct acpi_subtable_header header;
	u8 version;
	u8 cascade;
	u8 node;
	u64 node_map;
};

/* HT Interrupt Controller */

struct acpi_madt_ht_pic {
	struct acpi_subtable_header header;
	u8 version;
	u64 address;
	u16 size;
	u8 cascade[8];
};

/* Bridge I/O Interrupt Controller */

struct acpi_madt_bio_pic {
	struct acpi_subtable_header header;
	u8 version;
	u64 address;
	u16 size;
	u16 id;
	u16 gsi_base;
};

/* MSI Interrupt Controller */

struct acpi_madt_msi_pic {
	struct acpi_subtable_header header;
	u8 version;
	u64 msg_address;
	u32 start;
	u32 count;
};

/* LPC Interrupt Controller */

struct acpi_madt_lpc_pic {
	struct acpi_subtable_header header;
	u8 version;
	u64 address;
	u16 size;
	u8 cascade;
};

#pragma pack()

#endif

#endif /* !CONFIG_ACPI */

#define ACPI_TABLE_UPGRADE_MAX_PHYS ARCH_LOW_ADDRESS_LIMIT

#endif /* _ASM_LOONGARCH_ACPI_H */
