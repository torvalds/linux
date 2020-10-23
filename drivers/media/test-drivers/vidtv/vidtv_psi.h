/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file contains the logic to work with MPEG Program-Specific Information.
 * These are defined both in ISO/IEC 13818-1 (systems) and ETSI EN 300 468.
 * PSI is carried in the form of table structures, and although each table might
 * technically be broken into one or more sections, we do not do this here,
 * hence 'table' and 'section' are interchangeable for vidtv.
 *
 * This code currently supports three tables: PAT, PMT and SDT. These are the
 * bare minimum to get userspace to recognize our MPEG transport stream. It can
 * be extended to support more PSI tables in the future.
 *
 * Copyright (C) 2020 Daniel W. S. Almeida
 */

#ifndef VIDTV_PSI_H
#define VIDTV_PSI_H

#include <linux/types.h>
#include <asm/byteorder.h>

/*
 * all section lengths start immediately after the 'section_length' field
 * see ISO/IEC 13818-1 : 2000 and ETSI EN 300 468 V 1.10.1 for
 * reference
 */
#define PAT_LEN_UNTIL_LAST_SECTION_NUMBER 5
#define PMT_LEN_UNTIL_PROGRAM_INFO_LENGTH 9
#define SDT_LEN_UNTIL_RESERVED_FOR_FUTURE_USE 8
#define MAX_SECTION_LEN 1021
#define VIDTV_PAT_PID 0 /* mandated by the specs */
#define VIDTV_SDT_PID 0x0011 /* mandated by the specs */

enum vidtv_psi_descriptors {
	REGISTRATION_DESCRIPTOR	= 0x05, /* See ISO/IEC 13818-1 section 2.6.8 */
	SERVICE_DESCRIPTOR = 0x48, /* See ETSI EN 300 468 section 6.2.33 */
};

enum vidtv_psi_stream_types {
	STREAM_PRIVATE_DATA = 0x06, /* see ISO/IEC 13818-1 2000 p. 48 */
};

/**
 * struct vidtv_psi_desc - A generic PSI descriptor type.
 * The descriptor length is an 8-bit field specifying the total number of bytes of the data portion
 * of the descriptor following the byte defining the value of this field.
 */
struct vidtv_psi_desc {
	struct vidtv_psi_desc *next;
	u8 type;
	u8 length;
	u8 data[];
} __packed;

/**
 * struct vidtv_psi_desc_service - Service descriptor.
 * See ETSI EN 300 468 section 6.2.33.
 */
struct vidtv_psi_desc_service {
	struct vidtv_psi_desc *next;
	u8 type;
	u8 length;

	u8 service_type;
	u8 provider_name_len;
	char *provider_name;
	u8 service_name_len;
	char *service_name;
} __packed;

/**
 * struct vidtv_psi_desc_registration - A registration descriptor.
 * See ISO/IEC 13818-1 section 2.6.8
 */
struct vidtv_psi_desc_registration {
	struct vidtv_psi_desc *next;
	u8 type;
	u8 length;

	/*
	 * The format_identifier is a 32-bit value obtained from a Registration
	 * Authority as designated by ISO/IEC JTC 1/SC 29.
	 */
	__be32 format_id;
	/*
	 * The meaning of additional_identification_info bytes, if any, are
	 * defined by the assignee of that format_identifier, and once defined
	 * they shall not change.
	 */
	u8 additional_identification_info[];
} __packed;

/**
 * struct vidtv_psi_table_header - A header that is present for all PSI tables.
 */
struct vidtv_psi_table_header {
	u8  table_id;

	__be16 bitfield; /* syntax: 1, zero: 1, one: 2, section_length: 13 */

	__be16 id; /* TS ID */
	u8  current_next:1;
	u8  version:5;
	u8  one2:2;
	u8  section_id;	/* section_number */
	u8  last_section; /* last_section_number */
} __packed;

/**
 * struct vidtv_psi_table_pat_program - A single program in the PAT
 * See ISO/IEC 13818-1 : 2000 p.43
 */
struct vidtv_psi_table_pat_program {
	__be16 service_id;
	__be16 bitfield; /* reserved: 3, program_map_pid/network_pid: 13 */
	struct vidtv_psi_table_pat_program *next;
} __packed;

/**
 * struct vidtv_psi_table_pat - The Program Allocation Table (PAT)
 * See ISO/IEC 13818-1 : 2000 p.43
 */
struct vidtv_psi_table_pat {
	struct vidtv_psi_table_header header;
	u16 programs; /* Included by libdvbv5, not part of the table and not actually serialized */
	struct vidtv_psi_table_pat_program *program;
} __packed;

/**
 * struct vidtv_psi_table_sdt_service - Represents a service in the SDT.
 * see ETSI EN 300 468 v1.15.1 section 5.2.3.
 */
struct vidtv_psi_table_sdt_service {
	__be16 service_id;
	u8 EIT_present_following:1;
	u8 EIT_schedule:1;
	u8 reserved:6;
	__be16 bitfield; /* running_status: 3, free_ca:1, desc_loop_len:12 */
	struct vidtv_psi_desc *descriptor;
	struct vidtv_psi_table_sdt_service *next;
} __packed;

/**
 * struct vidtv_psi_table_sdt - Represents the Service Description Table
 * see ETSI EN 300 468 v1.15.1 section 5.2.3.
 */

struct vidtv_psi_table_sdt {
	struct vidtv_psi_table_header header;
	__be16 network_id; /* original_network_id */
	u8  reserved;
	struct vidtv_psi_table_sdt_service *service;
} __packed;

/**
 * enum service_running_status - Status of a SDT service.
 * see ETSI EN 300 468 v1.15.1 section 5.2.3 table 6.
 */
enum service_running_status {
	RUNNING = 0x4,
};

/**
 * enum service_type - The type of a SDT service.
 * see ETSI EN 300 468 v1.15.1 section 6.2.33, table 81.
 */
enum service_type {
	/* see ETSI EN 300 468 v1.15.1 p. 77 */
	DIGITAL_TELEVISION_SERVICE = 0x1,
};

/**
 * struct vidtv_psi_table_pmt_stream - A single stream in the PMT.
 * See ISO/IEC 13818-1 : 2000 p.46.
 */
struct vidtv_psi_table_pmt_stream {
	u8 type;
	__be16 bitfield; /* reserved: 3, elementary_pid: 13 */
	__be16 bitfield2; /*reserved: 4, zero: 2, desc_length: 10 */
	struct vidtv_psi_desc *descriptor;
	struct vidtv_psi_table_pmt_stream *next;
} __packed;

/**
 * struct vidtv_psi_table_pmt - The Program Map Table (PMT).
 * See ISO/IEC 13818-1 : 2000 p.46.
 */
struct vidtv_psi_table_pmt {
	struct vidtv_psi_table_header header;
	__be16 bitfield; /* reserved:3, pcr_pid: 13 */
	__be16 bitfield2; /* reserved: 4, zero: 2, desc_len: 10 */
	struct vidtv_psi_desc *descriptor;
	struct vidtv_psi_table_pmt_stream *stream;
} __packed;

/**
 * struct psi_write_args - Arguments for the PSI packetizer.
 * @dest_buf: The buffer to write into.
 * @from: PSI data to be copied.
 * @len: How much to write.
 * @dest_offset: where to start writing in the dest_buffer.
 * @pid: TS packet ID.
 * @new_psi_section: Set when starting a table section.
 * @continuity_counter: Incremented on every new packet.
 * @is_crc: Set when writing the CRC at the end.
 * @dest_buf_sz: The size of the dest_buffer
 * @crc: a pointer to store the crc for this chunk
 */
struct psi_write_args {
	void *dest_buf;
	void *from;
	size_t len;
	u32 dest_offset;
	u16 pid;
	bool new_psi_section;
	u8 *continuity_counter;
	bool is_crc;
	u32 dest_buf_sz;
	u32 *crc;
};

/**
 * struct desc_write_args - Arguments in order to write a descriptor.
 * @dest_buf: The buffer to write into.
 * @dest_offset: where to start writing in the dest_buffer.
 * @desc: A pointer to the descriptor
 * @pid: TS packet ID.
 * @continuity_counter: Incremented on every new packet.
 * @dest_buf_sz: The size of the dest_buffer
 * @crc: a pointer to store the crc for this chunk
 */
struct desc_write_args {
	void *dest_buf;
	u32 dest_offset;
	struct vidtv_psi_desc *desc;
	u16 pid;
	u8 *continuity_counter;
	u32 dest_buf_sz;
	u32 *crc;
};

/**
 * struct crc32_write_args - Arguments in order to write the CRC at the end of
 * the PSI tables.
 * @dest_buf: The buffer to write into.
 * @dest_offset: where to start writing in the dest_buffer.
 * @crc: the CRC value to write
 * @pid: TS packet ID.
 * @continuity_counter: Incremented on every new packet.
 * @dest_buf_sz: The size of the dest_buffer
 */
struct crc32_write_args {
	void *dest_buf;
	u32 dest_offset;
	__be32 crc;
	u16 pid;
	u8 *continuity_counter;
	u32 dest_buf_sz;
};

/**
 * struct header_write_args - Arguments in order to write the common table
 * header
 * @dest_buf: The buffer to write into.
 * @dest_offset: where to start writing in the dest_buffer.
 * @h: a pointer to the header.
 * @pid: TS packet ID.
 * @continuity_counter: Incremented on every new packet.
 * @dest_buf_sz: The size of the dest_buffer
 * @crc: a pointer to store the crc for this chunk
 */
struct header_write_args {
	void *dest_buf;
	u32 dest_offset;
	struct vidtv_psi_table_header *h;
	u16 pid;
	u8 *continuity_counter;
	u32 dest_buf_sz;
	u32 *crc;
};

struct vidtv_psi_desc_service *vidtv_psi_service_desc_init(struct vidtv_psi_desc *head,
							   enum service_type service_type,
							   char *service_name,
							   char *provider_name);

struct vidtv_psi_desc_registration
*vidtv_psi_registration_desc_init(struct vidtv_psi_desc *head,
				  __be32 format_id,
				  u8 *additional_ident_info,
				  u32 additional_info_len);

struct vidtv_psi_table_pat_program
*vidtv_psi_pat_program_init(struct vidtv_psi_table_pat_program *head,
			    u16 service_id,
			    u16 program_map_pid);

struct vidtv_psi_table_pmt_stream*
vidtv_psi_pmt_stream_init(struct vidtv_psi_table_pmt_stream *head,
			  enum vidtv_psi_stream_types stream_type,
			  u16 es_pid);

struct vidtv_psi_table_pat *vidtv_psi_pat_table_init(u16 transport_stream_id);

struct vidtv_psi_table_pmt *vidtv_psi_pmt_table_init(u16 program_number,
						     u16 pcr_pid);

struct vidtv_psi_table_sdt *vidtv_psi_sdt_table_init(u16 transport_stream_id);

struct vidtv_psi_table_sdt_service*
vidtv_psi_sdt_service_init(struct vidtv_psi_table_sdt_service *head,
			   u16 service_id);

void
vidtv_psi_desc_destroy(struct vidtv_psi_desc *desc);

void
vidtv_psi_pat_program_destroy(struct vidtv_psi_table_pat_program *p);

void
vidtv_psi_pat_table_destroy(struct vidtv_psi_table_pat *p);

void
vidtv_psi_pmt_stream_destroy(struct vidtv_psi_table_pmt_stream *s);

void
vidtv_psi_pmt_table_destroy(struct vidtv_psi_table_pmt *pmt);

void
vidtv_psi_sdt_table_destroy(struct vidtv_psi_table_sdt *sdt);

void
vidtv_psi_sdt_service_destroy(struct vidtv_psi_table_sdt_service *service);

/**
 * vidtv_psi_sdt_service_assign - Assigns the service loop to the SDT.
 * @sdt: The SDT to assign to.
 * @service: The service loop (one or more services)
 *
 * This will free the previous service loop in the table.
 * This will assign ownership of the service loop to the table, i.e. the table
 * will free this service loop when a call to its destroy function is made.
 */
void
vidtv_psi_sdt_service_assign(struct vidtv_psi_table_sdt *sdt,
			     struct vidtv_psi_table_sdt_service *service);

/**
 * vidtv_psi_desc_assign - Assigns a descriptor loop at some point
 * @to: Where to assign this descriptor loop to
 * @desc: The descriptor loop that will be assigned.
 *
 * This will free the loop in 'to', if any.
 */
void vidtv_psi_desc_assign(struct vidtv_psi_desc **to,
			   struct vidtv_psi_desc *desc);

/**
 * vidtv_psi_pmt_desc_assign - Assigns a descriptor loop at some point in a PMT section.
 * @pmt: The PMT section that will contain the descriptor loop
 * @to: Where in the PMT to assign this descriptor loop to
 * @desc: The descriptor loop that will be assigned.
 *
 * This will free the loop in 'to', if any.
 * This will assign ownership of the loop to the table, i.e. the table
 * will free this loop when a call to its destroy function is made.
 */
void vidtv_pmt_desc_assign(struct vidtv_psi_table_pmt *pmt,
			   struct vidtv_psi_desc **to,
			   struct vidtv_psi_desc *desc);

/**
 * vidtv_psi_sdt_desc_assign - Assigns a descriptor loop at some point in a SDT.
 * @sdt: The SDT that will contain the descriptor loop
 * @to: Where in the PMT to assign this descriptor loop to
 * @desc: The descriptor loop that will be assigned.
 *
 * This will free the loop in 'to', if any.
 * This will assign ownership of the loop to the table, i.e. the table
 * will free this loop when a call to its destroy function is made.
 */
void vidtv_sdt_desc_assign(struct vidtv_psi_table_sdt *sdt,
			   struct vidtv_psi_desc **to,
			   struct vidtv_psi_desc *desc);

/**
 * vidtv_psi_pat_program_assign - Assigns the program loop to the PAT.
 * @pat: The PAT to assign to.
 * @p: The program loop (one or more programs)
 *
 * This will free the previous program loop in the table.
 * This will assign ownership of the program loop to the table, i.e. the table
 * will free this program loop when a call to its destroy function is made.
 */
void vidtv_psi_pat_program_assign(struct vidtv_psi_table_pat *pat,
				  struct vidtv_psi_table_pat_program *p);

/**
 * vidtv_psi_pmt_stream_assign - Assigns the stream loop to the PAT.
 * @pmt: The PMT to assign to.
 * @s: The stream loop (one or more streams)
 *
 * This will free the previous stream loop in the table.
 * This will assign ownership of the stream loop to the table, i.e. the table
 * will free this stream loop when a call to its destroy function is made.
 */
void vidtv_psi_pmt_stream_assign(struct vidtv_psi_table_pmt *pmt,
				 struct vidtv_psi_table_pmt_stream *s);

struct vidtv_psi_desc *vidtv_psi_desc_clone(struct vidtv_psi_desc *desc);

/**
 * vidtv_psi_create_sec_for_each_pat_entry - Create a PMT section for each
 * program found in the PAT
 * @pat: The PAT to look for programs.
 * @s: The stream loop (one or more streams)
 * @pcr_pid: packet ID for the PCR to be used for the program described in this
 * PMT section
 */
struct vidtv_psi_table_pmt**
vidtv_psi_pmt_create_sec_for_each_pat_entry(struct vidtv_psi_table_pat *pat, u16 pcr_pid);

/**
 * vidtv_psi_pmt_get_pid - Get the TS PID for a PMT section.
 * @section: The PMT section whose PID we want to retrieve.
 * @pat: The PAT table to look into.
 *
 * Returns: the TS PID for 'section'
 */
u16 vidtv_psi_pmt_get_pid(struct vidtv_psi_table_pmt *section,
			  struct vidtv_psi_table_pat *pat);

/**
 * vidtv_psi_pat_table_update_sec_len - Recompute and update the PAT section length.
 * @pat: The PAT whose length is to be updated.
 *
 * This will traverse the table and accumulate the length of its components,
 * which is then used to replace the 'section_length' field.
 *
 * If section_length > MAX_SECTION_LEN, the operation fails.
 */
void vidtv_psi_pat_table_update_sec_len(struct vidtv_psi_table_pat *pat);

/**
 * vidtv_psi_pmt_table_update_sec_len - Recompute and update the PMT section length.
 * @pmt: The PMT whose length is to be updated.
 *
 * This will traverse the table and accumulate the length of its components,
 * which is then used to replace the 'section_length' field.
 *
 * If section_length > MAX_SECTION_LEN, the operation fails.
 */
void vidtv_psi_pmt_table_update_sec_len(struct vidtv_psi_table_pmt *pmt);

/**
 * vidtv_psi_sdt_table_update_sec_len - Recompute and update the SDT section length.
 * @sdt: The SDT whose length is to be updated.
 *
 * This will traverse the table and accumulate the length of its components,
 * which is then used to replace the 'section_length' field.
 *
 * If section_length > MAX_SECTION_LEN, the operation fails.
 */
void vidtv_psi_sdt_table_update_sec_len(struct vidtv_psi_table_sdt *sdt);

/**
 * struct vidtv_psi_pat_write_args - Arguments for writing a PAT table
 * @buf: The destination buffer.
 * @offset: The offset into the destination buffer.
 * @pat: A pointer to the PAT.
 * @buf_sz: The size of the destination buffer.
 * @continuity_counter: A pointer to the CC. Incremented on every new packet.
 *
 */
struct vidtv_psi_pat_write_args {
	char *buf;
	u32 offset;
	struct vidtv_psi_table_pat *pat;
	u32 buf_sz;
	u8 *continuity_counter;
};

/**
 * vidtv_psi_pat_write_into - Write PAT as MPEG-TS packets into a buffer.
 * @args: An instance of struct vidtv_psi_pat_write_args
 *
 * This function writes the MPEG TS packets for a PAT table into a buffer.
 * Calling code will usually generate the PAT via a call to its init function
 * and thus is responsible for freeing it.
 *
 * Return: The number of bytes written into the buffer. This is NOT
 * equal to the size of the PAT, since more space is needed for TS headers during TS
 * encapsulation.
 */
u32 vidtv_psi_pat_write_into(struct vidtv_psi_pat_write_args args);

/**
 * struct vidtv_psi_sdt_write_args - Arguments for writing a SDT table
 * @buf: The destination buffer.
 * @offset: The offset into the destination buffer.
 * @sdt: A pointer to the SDT.
 * @buf_sz: The size of the destination buffer.
 * @continuity_counter: A pointer to the CC. Incremented on every new packet.
 *
 */

struct vidtv_psi_sdt_write_args {
	char *buf;
	u32 offset;
	struct vidtv_psi_table_sdt *sdt;
	u32 buf_sz;
	u8 *continuity_counter;
};

/**
 * vidtv_psi_sdt_write_into - Write SDT as MPEG-TS packets into a buffer.
 * @args: an instance of struct vidtv_psi_sdt_write_args
 *
 * This function writes the MPEG TS packets for a SDT table into a buffer.
 * Calling code will usually generate the SDT via a call to its init function
 * and thus is responsible for freeing it.
 *
 * Return: The number of bytes written into the buffer. This is NOT
 * equal to the size of the SDT, since more space is needed for TS headers during TS
 * encapsulation.
 */
u32 vidtv_psi_sdt_write_into(struct vidtv_psi_sdt_write_args args);

/**
 * struct vidtv_psi_pmt_write_args - Arguments for writing a PMT section
 * @buf: The destination buffer.
 * @offset: The offset into the destination buffer.
 * @pmt: A pointer to the PMT.
 * @buf_sz: The size of the destination buffer.
 * @continuity_counter: A pointer to the CC. Incremented on every new packet.
 *
 */
struct vidtv_psi_pmt_write_args {
	char *buf;
	u32 offset;
	struct vidtv_psi_table_pmt *pmt;
	u16 pid;
	u32 buf_sz;
	u8 *continuity_counter;
	u16 pcr_pid;
};

/**
 * vidtv_psi_pmt_write_into - Write PMT as MPEG-TS packets into a buffer.
 * @args: an instance of struct vidtv_psi_pmt_write_args
 *
 * This function writes the MPEG TS packets for a PMT section into a buffer.
 * Calling code will usually generate the PMT section via a call to its init function
 * and thus is responsible for freeing it.
 *
 * Return: The number of bytes written into the buffer. This is NOT
 * equal to the size of the PMT section, since more space is needed for TS headers
 * during TS encapsulation.
 */
u32 vidtv_psi_pmt_write_into(struct vidtv_psi_pmt_write_args args);

/**
 * vidtv_psi_find_pmt_sec - Finds the PMT section for 'program_num'
 * @pmt_sections: The sections to look into.
 * @nsections: The number of sections.
 * @program_num: The 'program_num' from PAT pointing to a PMT section.
 *
 * Return: A pointer to the PMT, if found, or NULL.
 */
struct vidtv_psi_table_pmt *vidtv_psi_find_pmt_sec(struct vidtv_psi_table_pmt **pmt_sections,
						   u16 nsections,
						   u16 program_num);

u16 vidtv_psi_get_pat_program_pid(struct vidtv_psi_table_pat_program *p);
u16 vidtv_psi_pmt_stream_get_elem_pid(struct vidtv_psi_table_pmt_stream *s);

#endif // VIDTV_PSI_H
