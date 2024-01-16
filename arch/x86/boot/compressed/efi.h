/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BOOT_COMPRESSED_EFI_H
#define BOOT_COMPRESSED_EFI_H

#if defined(_LINUX_EFI_H) || defined(_ASM_X86_EFI_H)
#error Please do not include kernel proper namespace headers
#endif

typedef guid_t efi_guid_t __aligned(__alignof__(u32));

#define EFI_GUID(a, b, c, d...) (efi_guid_t){ {					\
	(a) & 0xff, ((a) >> 8) & 0xff, ((a) >> 16) & 0xff, ((a) >> 24) & 0xff,	\
	(b) & 0xff, ((b) >> 8) & 0xff,						\
	(c) & 0xff, ((c) >> 8) & 0xff, d } }

#define ACPI_TABLE_GUID				EFI_GUID(0xeb9d2d30, 0x2d88, 0x11d3,  0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d)
#define ACPI_20_TABLE_GUID			EFI_GUID(0x8868e871, 0xe4f1, 0x11d3,  0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81)
#define EFI_CC_BLOB_GUID			EFI_GUID(0x067b1f5f, 0xcf26, 0x44c5, 0x85, 0x54, 0x93, 0xd7, 0x77, 0x91, 0x2d, 0x42)

#define EFI32_LOADER_SIGNATURE	"EL32"
#define EFI64_LOADER_SIGNATURE	"EL64"

/*
 * Generic EFI table header
 */
typedef	struct {
	u64 signature;
	u32 revision;
	u32 headersize;
	u32 crc32;
	u32 reserved;
} efi_table_hdr_t;

#define EFI_CONVENTIONAL_MEMORY		 7

#define EFI_MEMORY_MORE_RELIABLE \
				((u64)0x0000000000010000ULL)	/* higher reliability */
#define EFI_MEMORY_SP		((u64)0x0000000000040000ULL)	/* soft reserved */

#define EFI_PAGE_SHIFT		12

typedef struct {
	u32 type;
	u32 pad;
	u64 phys_addr;
	u64 virt_addr;
	u64 num_pages;
	u64 attribute;
} efi_memory_desc_t;

#define efi_early_memdesc_ptr(map, desc_size, n)			\
	(efi_memory_desc_t *)((void *)(map) + ((n) * (desc_size)))

typedef struct {
	efi_guid_t guid;
	u64 table;
} efi_config_table_64_t;

typedef struct {
	efi_guid_t guid;
	u32 table;
} efi_config_table_32_t;

typedef struct {
	efi_table_hdr_t hdr;
	u64 fw_vendor;	/* physical addr of CHAR16 vendor string */
	u32 fw_revision;
	u32 __pad1;
	u64 con_in_handle;
	u64 con_in;
	u64 con_out_handle;
	u64 con_out;
	u64 stderr_handle;
	u64 stderr;
	u64 runtime;
	u64 boottime;
	u32 nr_tables;
	u32 __pad2;
	u64 tables;
} efi_system_table_64_t;

typedef struct {
	efi_table_hdr_t hdr;
	u32 fw_vendor;	/* physical addr of CHAR16 vendor string */
	u32 fw_revision;
	u32 con_in_handle;
	u32 con_in;
	u32 con_out_handle;
	u32 con_out;
	u32 stderr_handle;
	u32 stderr;
	u32 runtime;
	u32 boottime;
	u32 nr_tables;
	u32 tables;
} efi_system_table_32_t;

/* kexec external ABI */
struct efi_setup_data {
	u64 fw_vendor;
	u64 __unused;
	u64 tables;
	u64 smbios;
	u64 reserved[8];
};

static inline int efi_guidcmp (efi_guid_t left, efi_guid_t right)
{
	return memcmp(&left, &right, sizeof (efi_guid_t));
}

#ifdef CONFIG_EFI
bool __pure __efi_soft_reserve_enabled(void);

static inline bool __pure efi_soft_reserve_enabled(void)
{
	return IS_ENABLED(CONFIG_EFI_SOFT_RESERVE)
		&& __efi_soft_reserve_enabled();
}
#else
static inline bool efi_soft_reserve_enabled(void)
{
	return false;
}
#endif /* CONFIG_EFI */
#endif /* BOOT_COMPRESSED_EFI_H */
