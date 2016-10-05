
#ifndef __TPM_EVENTLOG_H__
#define __TPM_EVENTLOG_H__

#define TCG_EVENT_NAME_LEN_MAX	255
#define MAX_TEXT_EVENT		1000	/* Max event string length */
#define ACPI_TCPA_SIG		"TCPA"	/* 0x41504354 /'TCPA' */

#ifdef CONFIG_PPC64
#define do_endian_conversion(x) be32_to_cpu(x)
#else
#define do_endian_conversion(x) x
#endif

enum bios_platform_class {
	BIOS_CLIENT = 0x00,
	BIOS_SERVER = 0x01,
};

struct tpm_bios_log {
	void *bios_event_log;
	void *bios_event_log_end;
};

struct tcpa_event {
	u32 pcr_index;
	u32 event_type;
	u8 pcr_value[20];	/* SHA1 */
	u32 event_size;
	u8 event_data[0];
};

enum tcpa_event_types {
	PREBOOT = 0,
	POST_CODE,
	UNUSED,
	NO_ACTION,
	SEPARATOR,
	ACTION,
	EVENT_TAG,
	SCRTM_CONTENTS,
	SCRTM_VERSION,
	CPU_MICROCODE,
	PLATFORM_CONFIG_FLAGS,
	TABLE_OF_DEVICES,
	COMPACT_HASH,
	IPL,
	IPL_PARTITION_DATA,
	NONHOST_CODE,
	NONHOST_CONFIG,
	NONHOST_INFO,
};

struct tcpa_pc_event {
	u32 event_id;
	u32 event_size;
	u8 event_data[0];
};

enum tcpa_pc_event_ids {
	SMBIOS = 1,
	BIS_CERT,
	POST_BIOS_ROM,
	ESCD,
	CMOS,
	NVRAM,
	OPTION_ROM_EXEC,
	OPTION_ROM_CONFIG,
	OPTION_ROM_MICROCODE = 10,
	S_CRTM_VERSION,
	S_CRTM_CONTENTS,
	POST_CONTENTS,
	HOST_TABLE_OF_DEVICES,
};

int read_log(struct tpm_bios_log *log);

#if defined(CONFIG_TCG_IBMVTPM) || defined(CONFIG_TCG_IBMVTPM_MODULE) || \
	defined(CONFIG_ACPI)
extern struct dentry **tpm_bios_log_setup(const char *);
extern void tpm_bios_log_teardown(struct dentry **);
#else
static inline struct dentry **tpm_bios_log_setup(const char *name)
{
	return NULL;
}
static inline void tpm_bios_log_teardown(struct dentry **dir)
{
}
#endif

#endif
