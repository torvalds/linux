/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef __ICP_QAT_UCLO_H__
#define __ICP_QAT_UCLO_H__

#define ICP_QAT_AC_895XCC_DEV_TYPE 0x00400000
#define ICP_QAT_AC_C62X_DEV_TYPE   0x01000000
#define ICP_QAT_AC_C3XXX_DEV_TYPE  0x02000000
#define ICP_QAT_AC_4XXX_A_DEV_TYPE 0x08000000
#define ICP_QAT_UCLO_MAX_AE       17
#define ICP_QAT_UCLO_MAX_CTX      8
#define ICP_QAT_UCLO_MAX_UIMAGE   (ICP_QAT_UCLO_MAX_AE * ICP_QAT_UCLO_MAX_CTX)
#define ICP_QAT_UCLO_MAX_USTORE   0x4000
#define ICP_QAT_UCLO_MAX_XFER_REG 128
#define ICP_QAT_UCLO_MAX_GPR_REG  128
#define ICP_QAT_UCLO_MAX_LMEM_REG 1024
#define ICP_QAT_UCLO_MAX_LMEM_REG_2X 1280
#define ICP_QAT_UCLO_AE_ALL_CTX   0xff
#define ICP_QAT_UOF_OBJID_LEN     8
#define ICP_QAT_UOF_FID 0xc6c2
#define ICP_QAT_UOF_MAJVER 0x4
#define ICP_QAT_UOF_MINVER 0x11
#define ICP_QAT_UOF_OBJS        "UOF_OBJS"
#define ICP_QAT_UOF_STRT        "UOF_STRT"
#define ICP_QAT_UOF_IMAG        "UOF_IMAG"
#define ICP_QAT_UOF_IMEM        "UOF_IMEM"
#define ICP_QAT_UOF_LOCAL_SCOPE     1
#define ICP_QAT_UOF_INIT_EXPR               0
#define ICP_QAT_UOF_INIT_REG                1
#define ICP_QAT_UOF_INIT_REG_CTX            2
#define ICP_QAT_UOF_INIT_EXPR_ENDIAN_SWAP   3
#define ICP_QAT_SUOF_OBJ_ID_LEN             8
#define ICP_QAT_SUOF_FID  0x53554f46
#define ICP_QAT_SUOF_MAJVER 0x0
#define ICP_QAT_SUOF_MINVER 0x1
#define ICP_QAT_SUOF_OBJ_NAME_LEN 128
#define ICP_QAT_MOF_OBJ_ID_LEN 8
#define ICP_QAT_MOF_OBJ_CHUNKID_LEN 8
#define ICP_QAT_MOF_FID 0x00666f6d
#define ICP_QAT_MOF_MAJVER 0x0
#define ICP_QAT_MOF_MINVER 0x1
#define ICP_QAT_MOF_SYM_OBJS "SYM_OBJS"
#define ICP_QAT_SUOF_OBJS "SUF_OBJS"
#define ICP_QAT_SUOF_IMAG "SUF_IMAG"
#define ICP_QAT_SIMG_AE_INIT_SEQ_LEN    (50 * sizeof(unsigned long long))
#define ICP_QAT_SIMG_AE_INSTS_LEN       (0x4000 * sizeof(unsigned long long))

#define DSS_FWSK_MODULUS_LEN    384 /* RSA3K */
#define DSS_FWSK_EXPONENT_LEN   4
#define DSS_FWSK_PADDING_LEN    380
#define DSS_SIGNATURE_LEN       384 /* RSA3K */

#define CSS_FWSK_MODULUS_LEN    256 /* RSA2K */
#define CSS_FWSK_EXPONENT_LEN   4
#define CSS_FWSK_PADDING_LEN    252
#define CSS_SIGNATURE_LEN       256 /* RSA2K */

#define ICP_QAT_CSS_FWSK_MODULUS_LEN(handle)	((handle)->chip_info->css_3k ? \
						DSS_FWSK_MODULUS_LEN  : \
						CSS_FWSK_MODULUS_LEN)

#define ICP_QAT_CSS_FWSK_EXPONENT_LEN(handle)	((handle)->chip_info->css_3k ? \
						DSS_FWSK_EXPONENT_LEN : \
						CSS_FWSK_EXPONENT_LEN)

#define ICP_QAT_CSS_FWSK_PAD_LEN(handle)	((handle)->chip_info->css_3k ? \
						DSS_FWSK_PADDING_LEN : \
						CSS_FWSK_PADDING_LEN)

#define ICP_QAT_CSS_FWSK_PUB_LEN(handle)	(ICP_QAT_CSS_FWSK_MODULUS_LEN(handle) + \
						ICP_QAT_CSS_FWSK_EXPONENT_LEN(handle) + \
						ICP_QAT_CSS_FWSK_PAD_LEN(handle))

#define ICP_QAT_CSS_SIGNATURE_LEN(handle)	((handle)->chip_info->css_3k ? \
						DSS_SIGNATURE_LEN : \
						CSS_SIGNATURE_LEN)

#define ICP_QAT_CSS_AE_IMG_LEN     (sizeof(struct icp_qat_simg_ae_mode) + \
				    ICP_QAT_SIMG_AE_INIT_SEQ_LEN +         \
				    ICP_QAT_SIMG_AE_INSTS_LEN)
#define ICP_QAT_CSS_AE_SIMG_LEN(handle) (sizeof(struct icp_qat_css_hdr) + \
					ICP_QAT_CSS_FWSK_PUB_LEN(handle) + \
					ICP_QAT_CSS_SIGNATURE_LEN(handle) + \
					ICP_QAT_CSS_AE_IMG_LEN)
#define ICP_QAT_AE_IMG_OFFSET(handle) (sizeof(struct icp_qat_css_hdr) + \
					ICP_QAT_CSS_FWSK_MODULUS_LEN(handle) + \
					ICP_QAT_CSS_FWSK_EXPONENT_LEN(handle) + \
					ICP_QAT_CSS_SIGNATURE_LEN(handle))
#define ICP_QAT_CSS_RSA4K_MAX_IMAGE_LEN    0x40000
#define ICP_QAT_CSS_RSA3K_MAX_IMAGE_LEN    0x30000

#define ICP_QAT_CTX_MODE(ae_mode) ((ae_mode) & 0xf)
#define ICP_QAT_NN_MODE(ae_mode) (((ae_mode) >> 0x4) & 0xf)
#define ICP_QAT_SHARED_USTORE_MODE(ae_mode) (((ae_mode) >> 0xb) & 0x1)
#define RELOADABLE_CTX_SHARED_MODE(ae_mode) (((ae_mode) >> 0xc) & 0x1)

#define ICP_QAT_LOC_MEM0_MODE(ae_mode) (((ae_mode) >> 0x8) & 0x1)
#define ICP_QAT_LOC_MEM1_MODE(ae_mode) (((ae_mode) >> 0x9) & 0x1)
#define ICP_QAT_LOC_MEM2_MODE(ae_mode) (((ae_mode) >> 0x6) & 0x1)
#define ICP_QAT_LOC_MEM3_MODE(ae_mode) (((ae_mode) >> 0x7) & 0x1)
#define ICP_QAT_LOC_TINDEX_MODE(ae_mode) (((ae_mode) >> 0xe) & 0x1)

enum icp_qat_uof_mem_region {
	ICP_QAT_UOF_SRAM_REGION = 0x0,
	ICP_QAT_UOF_LMEM_REGION = 0x3,
	ICP_QAT_UOF_UMEM_REGION = 0x5
};

enum icp_qat_uof_regtype {
	ICP_NO_DEST	= 0,
	ICP_GPA_REL	= 1,
	ICP_GPA_ABS	= 2,
	ICP_GPB_REL	= 3,
	ICP_GPB_ABS	= 4,
	ICP_SR_REL	= 5,
	ICP_SR_RD_REL	= 6,
	ICP_SR_WR_REL	= 7,
	ICP_SR_ABS	= 8,
	ICP_SR_RD_ABS	= 9,
	ICP_SR_WR_ABS	= 10,
	ICP_DR_REL	= 19,
	ICP_DR_RD_REL	= 20,
	ICP_DR_WR_REL	= 21,
	ICP_DR_ABS	= 22,
	ICP_DR_RD_ABS	= 23,
	ICP_DR_WR_ABS	= 24,
	ICP_LMEM	= 26,
	ICP_LMEM0	= 27,
	ICP_LMEM1	= 28,
	ICP_NEIGH_REL	= 31,
	ICP_LMEM2	= 61,
	ICP_LMEM3	= 62,
};

enum icp_qat_css_fwtype {
	CSS_AE_FIRMWARE = 0,
	CSS_MMP_FIRMWARE = 1
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
	struct icp_qat_uclo_region *region;
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
	u64 micro_words;
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
	u64 strings;
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
	u64 *uword_buf;
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
	unsigned int ac_dev_type;
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
	unsigned int ac_dev_type;
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

struct icp_qat_suof_img_hdr {
	char          *simg_buf;
	unsigned long simg_len;
	char          *css_header;
	char          *css_key;
	char          *css_signature;
	char          *css_simg;
	unsigned long simg_size;
	unsigned int  ae_num;
	unsigned int  ae_mask;
	unsigned int  fw_type;
	unsigned long simg_name;
	unsigned long appmeta_data;
};

struct icp_qat_suof_img_tbl {
	unsigned int num_simgs;
	struct icp_qat_suof_img_hdr *simg_hdr;
};

struct icp_qat_suof_handle {
	unsigned int  file_id;
	unsigned int  check_sum;
	char          min_ver;
	char          maj_ver;
	char          fw_type;
	char          *suof_buf;
	unsigned int  suof_size;
	char          *sym_str;
	unsigned int  sym_size;
	struct icp_qat_suof_img_tbl img_table;
};

struct icp_qat_fw_auth_desc {
	unsigned int   img_len;
	unsigned int   ae_mask;
	unsigned int   css_hdr_high;
	unsigned int   css_hdr_low;
	unsigned int   img_high;
	unsigned int   img_low;
	unsigned int   signature_high;
	unsigned int   signature_low;
	unsigned int   fwsk_pub_high;
	unsigned int   fwsk_pub_low;
	unsigned int   img_ae_mode_data_high;
	unsigned int   img_ae_mode_data_low;
	unsigned int   img_ae_init_data_high;
	unsigned int   img_ae_init_data_low;
	unsigned int   img_ae_insts_high;
	unsigned int   img_ae_insts_low;
};

struct icp_qat_auth_chunk {
	struct icp_qat_fw_auth_desc fw_auth_desc;
	u64 chunk_size;
	u64 chunk_bus_addr;
};

struct icp_qat_css_hdr {
	unsigned int module_type;
	unsigned int header_len;
	unsigned int header_ver;
	unsigned int module_id;
	unsigned int module_vendor;
	unsigned int date;
	unsigned int size;
	unsigned int key_size;
	unsigned int module_size;
	unsigned int exponent_size;
	unsigned int fw_type;
	unsigned int reserved[21];
};

struct icp_qat_simg_ae_mode {
	unsigned int     file_id;
	unsigned short   maj_ver;
	unsigned short   min_ver;
	unsigned int     dev_type;
	unsigned short   devmax_ver;
	unsigned short   devmin_ver;
	unsigned int     ae_mask;
	unsigned int     ctx_enables;
	char             fw_type;
	char             ctx_mode;
	char             nn_mode;
	char             lm0_mode;
	char             lm1_mode;
	char             scs_mode;
	char             lm2_mode;
	char             lm3_mode;
	char             tindex_mode;
	unsigned char    reserved[7];
	char             simg_name[256];
	char             appmeta_data[256];
};

struct icp_qat_suof_filehdr {
	unsigned int     file_id;
	unsigned int     check_sum;
	char             min_ver;
	char             maj_ver;
	char             fw_type;
	char             reserved;
	unsigned short   max_chunks;
	unsigned short   num_chunks;
};

struct icp_qat_suof_chunk_hdr {
	char chunk_id[ICP_QAT_SUOF_OBJ_ID_LEN];
	u64 offset;
	u64 size;
};

struct icp_qat_suof_strtable {
	unsigned int tab_length;
	unsigned int strings;
};

struct icp_qat_suof_objhdr {
	unsigned int img_length;
	unsigned int reserved;
};

struct icp_qat_mof_file_hdr {
	unsigned int file_id;
	unsigned int checksum;
	char min_ver;
	char maj_ver;
	unsigned short reserved;
	unsigned short max_chunks;
	unsigned short num_chunks;
};

struct icp_qat_mof_chunkhdr {
	char chunk_id[ICP_QAT_MOF_OBJ_ID_LEN];
	u64 offset;
	u64 size;
};

struct icp_qat_mof_str_table {
	unsigned int tab_len;
	unsigned int strings;
};

struct icp_qat_mof_obj_hdr {
	unsigned short max_chunks;
	unsigned short num_chunks;
	unsigned int reserved;
};

struct icp_qat_mof_obj_chunkhdr {
	char chunk_id[ICP_QAT_MOF_OBJ_CHUNKID_LEN];
	u64 offset;
	u64 size;
	unsigned int name;
	unsigned int reserved;
};

struct icp_qat_mof_objhdr {
	char *obj_name;
	char *obj_buf;
	unsigned int obj_size;
};

struct icp_qat_mof_table {
	unsigned int num_objs;
	struct icp_qat_mof_objhdr *obj_hdr;
};

struct icp_qat_mof_handle {
	unsigned int file_id;
	unsigned int checksum;
	char min_ver;
	char maj_ver;
	char *mof_buf;
	u32 mof_size;
	char *sym_str;
	unsigned int sym_size;
	char *uobjs_hdr;
	char *sobjs_hdr;
	struct icp_qat_mof_table obj_table;
};
#endif
