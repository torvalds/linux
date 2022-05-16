/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _DISCOVERY_H_
#define _DISCOVERY_H_

#define PSP_HEADER_SIZE                 256
#define BINARY_SIGNATURE                0x28211407
#define DISCOVERY_TABLE_SIGNATURE       0x53445049

typedef enum
{
	IP_DISCOVERY = 0,
	GC,
	HARVEST_INFO,
	TABLE_4,
	RESERVED_1,
	RESERVED_2,
	TOTAL_TABLES = 6
} table;

#pragma pack(1)

typedef struct table_info
{
	uint16_t offset;   /* Byte offset */
	uint16_t checksum; /* Byte sum of the table */
	uint16_t size;     /* Table size */
	uint16_t padding;
} table_info;

typedef struct binary_header
{
	/* psp structure should go at the top of this structure */
	uint32_t binary_signature; /* 0x7, 0x14, 0x21, 0x28 */
	uint16_t version_major;
	uint16_t version_minor;
	uint16_t binary_checksum;  /* Byte sum of the binary after this field */
	uint16_t binary_size;      /* Binary Size*/
	table_info table_list[TOTAL_TABLES];
} binary_header;

typedef struct die_info
{
	uint16_t die_id;
	uint16_t die_offset; /* Points to the corresponding die_header structure */
} die_info;


typedef struct ip_discovery_header
{
	uint32_t signature;    /* Table Signature */
	uint16_t version;      /* Table Version */
	uint16_t size;         /* Table Size */
	uint32_t id;           /* Table ID */
	uint16_t num_dies;     /* Number of Dies */
	die_info die_info[16]; /* list die information for up to 16 dies */
	uint16_t padding[1];   /* padding */
} ip_discovery_header;

typedef struct ip
{
	uint16_t hw_id;           /* Hardware ID */
	uint8_t number_instance;  /* instance of the IP */
	uint8_t num_base_address; /* Number of Base Addresses */
	uint8_t major;            /* HCID Major */
	uint8_t minor;            /* HCID Minor */
	uint8_t revision;         /* HCID Revision */
#if defined(__BIG_ENDIAN)
	uint8_t reserved : 4;     /* Placeholder field */
	uint8_t harvest : 4;      /* Harvest */
#else
	uint8_t harvest : 4;      /* Harvest */
	uint8_t reserved : 4;     /* Placeholder field */
#endif
	uint32_t base_address[1]; /* variable number of Addresses */
} ip;

typedef struct die_header
{
	uint16_t die_id;
	uint16_t num_ips;
} die_header;

typedef struct ip_structure
{
	ip_discovery_header* header;
	struct die
	{
		die_header *die_header;
		ip *ip_list;
	} die;
} ip_structure;

struct gpu_info_header {
	uint32_t table_id;      /* table ID */
	uint16_t version_major; /* table version */
	uint16_t version_minor; /* table version */
	uint32_t size;          /* size of the entire header+data in bytes */
};

struct gc_info_v1_0 {
	struct gpu_info_header header;

	uint32_t gc_num_se;
	uint32_t gc_num_wgp0_per_sa;
	uint32_t gc_num_wgp1_per_sa;
	uint32_t gc_num_rb_per_se;
	uint32_t gc_num_gl2c;
	uint32_t gc_num_gprs;
	uint32_t gc_num_max_gs_thds;
	uint32_t gc_gs_table_depth;
	uint32_t gc_gsprim_buff_depth;
	uint32_t gc_parameter_cache_depth;
	uint32_t gc_double_offchip_lds_buffer;
	uint32_t gc_wave_size;
	uint32_t gc_max_waves_per_simd;
	uint32_t gc_max_scratch_slots_per_cu;
	uint32_t gc_lds_size;
	uint32_t gc_num_sc_per_se;
	uint32_t gc_num_sa_per_se;
	uint32_t gc_num_packer_per_sc;
	uint32_t gc_num_gl2a;
};

struct gc_info_v1_1 {
	struct gpu_info_header header;

	uint32_t gc_num_se;
	uint32_t gc_num_wgp0_per_sa;
	uint32_t gc_num_wgp1_per_sa;
	uint32_t gc_num_rb_per_se;
	uint32_t gc_num_gl2c;
	uint32_t gc_num_gprs;
	uint32_t gc_num_max_gs_thds;
	uint32_t gc_gs_table_depth;
	uint32_t gc_gsprim_buff_depth;
	uint32_t gc_parameter_cache_depth;
	uint32_t gc_double_offchip_lds_buffer;
	uint32_t gc_wave_size;
	uint32_t gc_max_waves_per_simd;
	uint32_t gc_max_scratch_slots_per_cu;
	uint32_t gc_lds_size;
	uint32_t gc_num_sc_per_se;
	uint32_t gc_num_sa_per_se;
	uint32_t gc_num_packer_per_sc;
	uint32_t gc_num_gl2a;
	uint32_t gc_num_tcp_per_sa;
	uint32_t gc_num_sdp_interface;
	uint32_t gc_num_tcps;
};

struct gc_info_v2_0 {
	struct gpu_info_header header;

	uint32_t gc_num_se;
	uint32_t gc_num_cu_per_sh;
	uint32_t gc_num_sh_per_se;
	uint32_t gc_num_rb_per_se;
	uint32_t gc_num_tccs;
	uint32_t gc_num_gprs;
	uint32_t gc_num_max_gs_thds;
	uint32_t gc_gs_table_depth;
	uint32_t gc_gsprim_buff_depth;
	uint32_t gc_parameter_cache_depth;
	uint32_t gc_double_offchip_lds_buffer;
	uint32_t gc_wave_size;
	uint32_t gc_max_waves_per_simd;
	uint32_t gc_max_scratch_slots_per_cu;
	uint32_t gc_lds_size;
	uint32_t gc_num_sc_per_se;
	uint32_t gc_num_packer_per_sc;
};

typedef struct harvest_info_header {
	uint32_t signature; /* Table Signature */
	uint32_t version;   /* Table Version */
} harvest_info_header;

typedef struct harvest_info {
	uint16_t hw_id;          /* Hardware ID */
	uint8_t number_instance; /* Instance of the IP */
	uint8_t reserved;        /* Reserved for alignment */
} harvest_info;

typedef struct harvest_table {
	harvest_info_header header;
	harvest_info list[32];
} harvest_table;

#pragma pack()

#endif
