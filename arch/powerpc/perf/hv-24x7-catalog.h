#ifndef LINUX_POWERPC_PERF_HV_24X7_CATALOG_H_
#define LINUX_POWERPC_PERF_HV_24X7_CATALOG_H_

#include <linux/types.h>

/* From document "24x7 Event and Group Catalog Formats Proposal" v0.15 */

struct hv_24x7_catalog_page_0 {
#define HV_24X7_CATALOG_MAGIC 0x32347837 /* "24x7" in ASCII */
	__be32 magic;
	__be32 length; /* In 4096 byte pages */
	__be64 version; /* XXX: arbitrary? what's the meaning/useage/purpose? */
	__u8 build_time_stamp[16]; /* "YYYYMMDDHHMMSS\0\0" */
	__u8 reserved2[32];
	__be16 schema_data_offs; /* in 4096 byte pages */
	__be16 schema_data_len;  /* in 4096 byte pages */
	__be16 schema_entry_count;
	__u8 reserved3[2];
	__be16 event_data_offs;
	__be16 event_data_len;
	__be16 event_entry_count;
	__u8 reserved4[2];
	__be16 group_data_offs; /* in 4096 byte pages */
	__be16 group_data_len;  /* in 4096 byte pages */
	__be16 group_entry_count;
	__u8 reserved5[2];
	__be16 formula_data_offs; /* in 4096 byte pages */
	__be16 formula_data_len;  /* in 4096 byte pages */
	__be16 formula_entry_count;
	__u8 reserved6[2];
} __packed;

#endif
