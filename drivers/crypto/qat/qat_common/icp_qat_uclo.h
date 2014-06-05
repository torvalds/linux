/*
  This file is provided under a dual BSD/GPLv2 license.  When using or
  redistributing this file, you may do so under either license.

  GPL LICENSE SUMMARY
  Copyright(c) 2014 Intel Corporation.
  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  Contact Information:
  qat-linux@intel.com

  BSD LICENSE
  Copyright(c) 2014 Intel Corporation.
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#ifndef __ICP_QAT_UCLO_H__
#define __ICP_QAT_UCLO_H__

#define ICP_QAT_AC_C_CPU_TYPE     0x00400000
#define ICP_QAT_UCLO_MAX_AE       12
#define ICP_QAT_UCLO_MAX_CTX      8
#define ICP_QAT_UCLO_MAX_UIMAGE   (ICP_QAT_UCLO_MAX_AE * ICP_QAT_UCLO_MAX_CTX)
#define ICP_QAT_UCLO_MAX_USTORE   0x4000
#define ICP_QAT_UCLO_MAX_XFER_REG 128
#define ICP_QAT_UCLO_MAX_GPR_REG  128
#define ICP_QAT_UCLO_MAX_NN_REG   128
#define ICP_QAT_UCLO_MAX_LMEM_REG 1024
#define ICP_QAT_UCLO_AE_ALL_CTX   0xff
#define ICP_QAT_UOF_OBJID_LEN     8
#define ICP_QAT_UOF_FID 0xc6c2
#define ICP_QAT_UOF_MAJVER 0x4
#define ICP_QAT_UOF_MINVER 0x11
#define ICP_QAT_UOF_NN_MODE_NOTCARE   0xff
#define ICP_QAT_UOF_OBJS        "UOF_OBJS"
#define ICP_QAT_UOF_STRT        "UOF_STRT"
#define ICP_QAT_UOF_GTID        "UOF_GTID"
#define ICP_QAT_UOF_IMAG        "UOF_IMAG"
#define ICP_QAT_UOF_IMEM        "UOF_IMEM"
#define ICP_QAT_UOF_MSEG        "UOF_MSEG"
#define ICP_QAT_UOF_LOCAL_SCOPE     1
#define ICP_QAT_UOF_INIT_EXPR               0
#define ICP_QAT_UOF_INIT_REG                1
#define ICP_QAT_UOF_INIT_REG_CTX            2
#define ICP_QAT_UOF_INIT_EXPR_ENDIAN_SWAP   3

#define ICP_QAT_CTX_MODE(ae_mode) ((ae_mode) & 0xf)
#define ICP_QAT_NN_MODE(ae_mode) (((ae_mode) >> 0x4) & 0xf)
#define ICP_QAT_SHARED_USTORE_MODE(ae_mode) (((ae_mode) >> 0xb) & 0x1)
#define RELOADABLE_CTX_SHARED_MODE(ae_mode) (((ae_mode) >> 0xc) & 0x1)

#define ICP_QAT_LOC_MEM0_MODE(ae_mode) (((ae_mode) >> 0x8) & 0x1)
#define ICP_QAT_LOC_MEM1_MODE(ae_mode) (((ae_mode) >> 0x9) & 0x1)

enum icp_qat_uof_mem_region {
	ICP_QAT_UOF_SRAM_REGION = 0x0,
	ICP_QAT_UOF_LMEM_REGION = 0x3,
	ICP_QAT_UOF_UMEM_REGION = 0x5
};

enum icp_qat_uof_regtype {
	ICP_NO_DEST,
	ICP_GPA_REL,
	ICP_GPA_ABS,
	ICP_GPB_REL,
	ICP_GPB_ABS,
	ICP_SR_REL,
	ICP_SR_RD_REL,
	ICP_SR_WR_REL,
	ICP_SR_ABS,
	ICP_SR_RD_ABS,
	ICP_SR_WR_ABS,
	ICP_DR_REL,
	ICP_DR_RD_REL,
	ICP_DR_WR_REL,
	ICP_DR_ABS,
	ICP_DR_RD_ABS,
	ICP_DR_WR_ABS,
	ICP_LMEM,
	ICP_LMEM0,
	ICP_LMEM1,
	ICP_NEIGH_REL,
};

struct icp_qat_uclo_page {
	struct icp_qat_uclo_encap_page *encap_page;
	struct icp_qat_uclo_region *region;
	unsigned int flags;
};

struct icp_qat_uclo_region {
	struct icp_qat_uclo_page *loaded;
	struct icp_qat_uclo_page *page;
};

struct icp_qat_uclo_aeslice {
	struct icp_qat_uclo_region *regions;
	struct icp_qat_uclo_page *page;
	struct icp_qat_uclo_page *cur_page[ICP_QAT_UCLO_MAX_CTX];
	struct icp_qat_uclo_encapme *encap_image;
	unsigned int ctx_mask_assigned;
	unsigned int new_uaddr[ICP_QAT_UCLO_MAX_CTX];
};

struct icp_qat_uclo_aedata {
	unsigned int slice_num;
	unsigned int eff_ustore_size;
	struct icp_qat_uclo_aeslice ae_slices[ICP_QAT_UCLO_MAX_CTX];
};

struct icp_qat_uof_encap_obj {
	char *beg_uof;
	struct icp_qat_uof_objhdr *obj_hdr;
	struct icp_qat_uof_chunkhdr *chunk_hdr;
	struct icp_qat_uof_varmem_seg *var_mem_seg;
};

struct icp_qat_uclo_encap_uwblock {
	unsigned int start_addr;
	unsigned int words_num;
	uint64_t micro_words;
};

struct icp_qat_uclo_encap_page {
	unsigned int def_page;
	unsigned int page_region;
	unsigned int beg_addr_v;
	unsigned int beg_addr_p;
	unsigned int micro_words_num;
	unsigned int uwblock_num;
	struct icp_qat_uclo_encap_uwblock *uwblock;
};

struct icp_qat_uclo_encapme {
	struct icp_qat_uof_image *img_ptr;
	struct icp_qat_uclo_encap_page *page;
	unsigned int ae_reg_num;
	struct icp_qat_uof_ae_reg *ae_reg;
	unsigned int init_regsym_num;
	struct icp_qat_uof_init_regsym *init_regsym;
	unsigned int sbreak_num;
	struct icp_qat_uof_sbreak *sbreak;
	unsigned int uwords_num;
};

struct icp_qat_uclo_init_mem_table {
	unsigned int entry_num;
	struct icp_qat_uof_initmem *init_mem;
};

struct icp_qat_uclo_objhdr {
	char *file_buff;
	unsigned int checksum;
	unsigned int size;
};

struct icp_qat_uof_strtable {
	unsigned int table_len;
	unsigned int reserved;
	uint64_t strings;
};

struct icp_qat_uclo_objhandle {
	unsigned int prod_type;
	unsigned int prod_rev;
	struct icp_qat_uclo_objhdr *obj_hdr;
	struct icp_qat_uof_encap_obj encap_uof_obj;
	struct icp_qat_uof_strtable str_table;
	struct icp_qat_uclo_encapme ae_uimage[ICP_QAT_UCLO_MAX_UIMAGE];
	struct icp_qat_uclo_aedata ae_data[ICP_QAT_UCLO_MAX_AE];
	struct icp_qat_uclo_init_mem_table init_mem_tab;
	struct icp_qat_uof_batch_init *lm_init_tab[ICP_QAT_UCLO_MAX_AE];
	struct icp_qat_uof_batch_init *umem_init_tab[ICP_QAT_UCLO_MAX_AE];
	int uimage_num;
	int uword_in_bytes;
	int global_inited;
	unsigned int ae_num;
	unsigned int ustore_phy_size;
	void *obj_buf;
	uint64_t *uword_buf;
};

struct icp_qat_uof_uword_block {
	unsigned int start_addr;
	unsigned int words_num;
	unsigned int uword_offset;
	unsigned int reserved;
};

struct icp_qat_uof_filehdr {
	unsigned short file_id;
	unsigned short reserved1;
	char min_ver;
	char maj_ver;
	unsigned short reserved2;
	unsigned short max_chunks;
	unsigned short num_chunks;
};

struct icp_qat_uof_filechunkhdr {
	char chunk_id[ICP_QAT_UOF_OBJID_LEN];
	unsigned int checksum;
	unsigned int offset;
	unsigned int size;
};

struct icp_qat_uof_objhdr {
	unsigned int cpu_type;
	unsigned short min_cpu_ver;
	unsigned short max_cpu_ver;
	short max_chunks;
	short num_chunks;
	unsigned int reserved1;
	unsigned int reserved2;
};

struct icp_qat_uof_chunkhdr {
	char chunk_id[ICP_QAT_UOF_OBJID_LEN];
	unsigned int offset;
	unsigned int size;
};

struct icp_qat_uof_memvar_attr {
	unsigned int offset_in_byte;
	unsigned int value;
};

struct icp_qat_uof_initmem {
	unsigned int sym_name;
	char region;
	char scope;
	unsigned short reserved1;
	unsigned int addr;
	unsigned int num_in_bytes;
	unsigned int val_attr_num;
};

struct icp_qat_uof_init_regsym {
	unsigned int sym_name;
	char init_type;
	char value_type;
	char reg_type;
	unsigned char ctx;
	unsigned int reg_addr;
	unsigned int value;
};

struct icp_qat_uof_varmem_seg {
	unsigned int sram_base;
	unsigned int sram_size;
	unsigned int sram_alignment;
	unsigned int sdram_base;
	unsigned int sdram_size;
	unsigned int sdram_alignment;
	unsigned int sdram1_base;
	unsigned int sdram1_size;
	unsigned int sdram1_alignment;
	unsigned int scratch_base;
	unsigned int scratch_size;
	unsigned int scratch_alignment;
};

struct icp_qat_uof_gtid {
	char tool_id[ICP_QAT_UOF_OBJID_LEN];
	int tool_ver;
	unsigned int reserved1;
	unsigned int reserved2;
};

struct icp_qat_uof_sbreak {
	unsigned int page_num;
	unsigned int virt_uaddr;
	unsigned char sbreak_type;
	unsigned char reg_type;
	unsigned short reserved1;
	unsigned int addr_offset;
	unsigned int reg_addr;
};

struct icp_qat_uof_code_page {
	unsigned int page_region;
	unsigned int page_num;
	unsigned char def_page;
	unsigned char reserved2;
	unsigned short reserved1;
	unsigned int beg_addr_v;
	unsigned int beg_addr_p;
	unsigned int neigh_reg_tab_offset;
	unsigned int uc_var_tab_offset;
	unsigned int imp_var_tab_offset;
	unsigned int imp_expr_tab_offset;
	unsigned int code_area_offset;
};

struct icp_qat_uof_image {
	unsigned int img_name;
	unsigned int ae_assigned;
	unsigned int ctx_assigned;
	unsigned int cpu_type;
	unsigned int entry_address;
	unsigned int fill_pattern[2];
	unsigned int reloadable_size;
	unsigned char sensitivity;
	unsigned char reserved;
	unsigned short ae_mode;
	unsigned short max_ver;
	unsigned short min_ver;
	unsigned short image_attrib;
	unsigned short reserved2;
	unsigned short page_region_num;
	unsigned short numpages;
	unsigned int reg_tab_offset;
	unsigned int init_reg_sym_tab;
	unsigned int sbreak_tab;
	unsigned int app_metadata;
};

struct icp_qat_uof_objtable {
	unsigned int entry_num;
};

struct icp_qat_uof_ae_reg {
	unsigned int name;
	unsigned int vis_name;
	unsigned short type;
	unsigned short addr;
	unsigned short access_mode;
	unsigned char visible;
	unsigned char reserved1;
	unsigned short ref_count;
	unsigned short reserved2;
	unsigned int xo_id;
};

struct icp_qat_uof_code_area {
	unsigned int micro_words_num;
	unsigned int uword_block_tab;
};

struct icp_qat_uof_batch_init {
	unsigned int ae;
	unsigned int addr;
	unsigned int *value;
	unsigned int size;
	struct icp_qat_uof_batch_init *next;
};
#endif
