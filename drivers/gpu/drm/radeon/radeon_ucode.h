/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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
#ifndef __RADEON_UCODE_H__
#define __RADEON_UCODE_H__

/* CP */
#define R600_PFP_UCODE_SIZE          576
#define R600_PM4_UCODE_SIZE          1792
#define R700_PFP_UCODE_SIZE          848
#define R700_PM4_UCODE_SIZE          1360
#define EVERGREEN_PFP_UCODE_SIZE     1120
#define EVERGREEN_PM4_UCODE_SIZE     1376
#define CAYMAN_PFP_UCODE_SIZE        2176
#define CAYMAN_PM4_UCODE_SIZE        2176
#define SI_PFP_UCODE_SIZE            2144
#define SI_PM4_UCODE_SIZE            2144
#define SI_CE_UCODE_SIZE             2144
#define CIK_PFP_UCODE_SIZE           2144
#define CIK_ME_UCODE_SIZE            2144
#define CIK_CE_UCODE_SIZE            2144

/* MEC */
#define CIK_MEC_UCODE_SIZE           4192

/* RLC */
#define R600_RLC_UCODE_SIZE          768
#define R700_RLC_UCODE_SIZE          1024
#define EVERGREEN_RLC_UCODE_SIZE     768
#define CAYMAN_RLC_UCODE_SIZE        1024
#define ARUBA_RLC_UCODE_SIZE         1536
#define SI_RLC_UCODE_SIZE            2048
#define BONAIRE_RLC_UCODE_SIZE       2048
#define KB_RLC_UCODE_SIZE            2560
#define KV_RLC_UCODE_SIZE            2560
#define ML_RLC_UCODE_SIZE            2560

/* MC */
#define BTC_MC_UCODE_SIZE            6024
#define CAYMAN_MC_UCODE_SIZE         6037
#define SI_MC_UCODE_SIZE             7769
#define TAHITI_MC_UCODE_SIZE         7808
#define PITCAIRN_MC_UCODE_SIZE       7775
#define VERDE_MC_UCODE_SIZE          7875
#define OLAND_MC_UCODE_SIZE          7863
#define BONAIRE_MC_UCODE_SIZE        7866
#define BONAIRE_MC2_UCODE_SIZE       7948
#define HAWAII_MC_UCODE_SIZE         7933
#define HAWAII_MC2_UCODE_SIZE        8091

/* SDMA */
#define CIK_SDMA_UCODE_SIZE          1050
#define CIK_SDMA_UCODE_VERSION       64

/* SMC */
#define RV770_SMC_UCODE_START        0x0100
#define RV770_SMC_UCODE_SIZE         0x410d
#define RV770_SMC_INT_VECTOR_START   0xffc0
#define RV770_SMC_INT_VECTOR_SIZE    0x0040

#define RV730_SMC_UCODE_START        0x0100
#define RV730_SMC_UCODE_SIZE         0x412c
#define RV730_SMC_INT_VECTOR_START   0xffc0
#define RV730_SMC_INT_VECTOR_SIZE    0x0040

#define RV710_SMC_UCODE_START        0x0100
#define RV710_SMC_UCODE_SIZE         0x3f1f
#define RV710_SMC_INT_VECTOR_START   0xffc0
#define RV710_SMC_INT_VECTOR_SIZE    0x0040

#define RV740_SMC_UCODE_START        0x0100
#define RV740_SMC_UCODE_SIZE         0x41c5
#define RV740_SMC_INT_VECTOR_START   0xffc0
#define RV740_SMC_INT_VECTOR_SIZE    0x0040

#define CEDAR_SMC_UCODE_START        0x0100
#define CEDAR_SMC_UCODE_SIZE         0x5d50
#define CEDAR_SMC_INT_VECTOR_START   0xffc0
#define CEDAR_SMC_INT_VECTOR_SIZE    0x0040

#define REDWOOD_SMC_UCODE_START      0x0100
#define REDWOOD_SMC_UCODE_SIZE       0x5f0a
#define REDWOOD_SMC_INT_VECTOR_START 0xffc0
#define REDWOOD_SMC_INT_VECTOR_SIZE  0x0040

#define JUNIPER_SMC_UCODE_START      0x0100
#define JUNIPER_SMC_UCODE_SIZE       0x5f1f
#define JUNIPER_SMC_INT_VECTOR_START 0xffc0
#define JUNIPER_SMC_INT_VECTOR_SIZE  0x0040

#define CYPRESS_SMC_UCODE_START      0x0100
#define CYPRESS_SMC_UCODE_SIZE       0x61f7
#define CYPRESS_SMC_INT_VECTOR_START 0xffc0
#define CYPRESS_SMC_INT_VECTOR_SIZE  0x0040

#define BARTS_SMC_UCODE_START        0x0100
#define BARTS_SMC_UCODE_SIZE         0x6107
#define BARTS_SMC_INT_VECTOR_START   0xffc0
#define BARTS_SMC_INT_VECTOR_SIZE    0x0040

#define TURKS_SMC_UCODE_START        0x0100
#define TURKS_SMC_UCODE_SIZE         0x605b
#define TURKS_SMC_INT_VECTOR_START   0xffc0
#define TURKS_SMC_INT_VECTOR_SIZE    0x0040

#define CAICOS_SMC_UCODE_START       0x0100
#define CAICOS_SMC_UCODE_SIZE        0x5fbd
#define CAICOS_SMC_INT_VECTOR_START  0xffc0
#define CAICOS_SMC_INT_VECTOR_SIZE   0x0040

#define CAYMAN_SMC_UCODE_START       0x0100
#define CAYMAN_SMC_UCODE_SIZE        0x79ec
#define CAYMAN_SMC_INT_VECTOR_START  0xffc0
#define CAYMAN_SMC_INT_VECTOR_SIZE   0x0040

#define TAHITI_SMC_UCODE_START       0x10000
#define TAHITI_SMC_UCODE_SIZE        0xf458

#define PITCAIRN_SMC_UCODE_START     0x10000
#define PITCAIRN_SMC_UCODE_SIZE      0xe9f4

#define VERDE_SMC_UCODE_START        0x10000
#define VERDE_SMC_UCODE_SIZE         0xebe4

#define OLAND_SMC_UCODE_START        0x10000
#define OLAND_SMC_UCODE_SIZE         0xe7b4

#define HAINAN_SMC_UCODE_START       0x10000
#define HAINAN_SMC_UCODE_SIZE        0xe67C

#define BONAIRE_SMC_UCODE_START      0x20000
#define BONAIRE_SMC_UCODE_SIZE       0x1FDEC

#define HAWAII_SMC_UCODE_START       0x20000
#define HAWAII_SMC_UCODE_SIZE        0x1FDEC

struct common_firmware_header {
	uint32_t size_bytes; /* size of the entire header+image(s) in bytes */
	uint32_t header_size_bytes; /* size of just the header in bytes */
	uint16_t header_version_major; /* header version */
	uint16_t header_version_minor; /* header version */
	uint16_t ip_version_major; /* IP version */
	uint16_t ip_version_minor; /* IP version */
	uint32_t ucode_version;
	uint32_t ucode_size_bytes; /* size of ucode in bytes */
	uint32_t ucode_array_offset_bytes; /* payload offset from the start of the header */
	uint32_t crc32;  /* crc32 checksum of the payload */
};

/* version_major=1, version_minor=0 */
struct mc_firmware_header_v1_0 {
	struct common_firmware_header header;
	uint32_t io_debug_size_bytes; /* size of debug array in dwords */
	uint32_t io_debug_array_offset_bytes; /* payload offset from the start of the header */
};

/* version_major=1, version_minor=0 */
struct smc_firmware_header_v1_0 {
	struct common_firmware_header header;
	uint32_t ucode_start_addr;
};

/* version_major=1, version_minor=0 */
struct gfx_firmware_header_v1_0 {
	struct common_firmware_header header;
	uint32_t ucode_feature_version;
	uint32_t jt_offset; /* jt location */
	uint32_t jt_size;  /* size of jt */
};

/* version_major=1, version_minor=0 */
struct rlc_firmware_header_v1_0 {
	struct common_firmware_header header;
	uint32_t ucode_feature_version;
	uint32_t save_and_restore_offset;
	uint32_t clear_state_descriptor_offset;
	uint32_t avail_scratch_ram_locations;
	uint32_t master_pkt_description_offset;
};

/* version_major=1, version_minor=0 */
struct sdma_firmware_header_v1_0 {
	struct common_firmware_header header;
	uint32_t ucode_feature_version;
	uint32_t ucode_change_version;
	uint32_t jt_offset; /* jt location */
	uint32_t jt_size; /* size of jt */
};

/* header is fixed size */
union radeon_firmware_header {
	struct common_firmware_header common;
	struct mc_firmware_header_v1_0 mc;
	struct smc_firmware_header_v1_0 smc;
	struct gfx_firmware_header_v1_0 gfx;
	struct rlc_firmware_header_v1_0 rlc;
	struct sdma_firmware_header_v1_0 sdma;
	uint8_t raw[0x100];
};

void radeon_ucode_print_mc_hdr(const struct common_firmware_header *hdr);
void radeon_ucode_print_smc_hdr(const struct common_firmware_header *hdr);
void radeon_ucode_print_gfx_hdr(const struct common_firmware_header *hdr);
void radeon_ucode_print_rlc_hdr(const struct common_firmware_header *hdr);
void radeon_ucode_print_sdma_hdr(const struct common_firmware_header *hdr);
int radeon_ucode_validate(const struct firmware *fw);

#endif
