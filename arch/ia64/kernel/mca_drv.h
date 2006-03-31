/*
 * File:	mca_drv.h
 * Purpose:	Define helpers for Generic MCA handling
 *
 * Copyright (C) 2004 FUJITSU LIMITED
 * Copyright (C) Hidetoshi Seto (seto.hidetoshi@jp.fujitsu.com)
 */
/*
 * Processor error section:
 *
 *  +-sal_log_processor_info_t *info-------------+
 *  | sal_log_section_hdr_t header;              |
 *  | ...                                        |
 *  | sal_log_mod_error_info_t info[0];          |
 *  +-+----------------+-------------------------+
 *    | CACHE_CHECK    |  ^ num_cache_check v
 *    +----------------+
 *    | TLB_CHECK      |  ^ num_tlb_check v
 *    +----------------+
 *    | BUS_CHECK      |  ^ num_bus_check v
 *    +----------------+
 *    | REG_FILE_CHECK |  ^ num_reg_file_check v
 *    +----------------+
 *    | MS_CHECK       |  ^ num_ms_check v
 *  +-struct cpuid_info *id----------------------+
 *  | regs[5];                                   |
 *  | reserved;                                  |
 *  +-sal_processor_static_info_t *regs----------+
 *  | valid;                                     |
 *  | ...                                        |
 *  | fr[128];                                   |
 *  +--------------------------------------------+
 */

/* peidx: index of processor error section */
typedef struct peidx_table {
	sal_log_processor_info_t        *info;
	struct sal_cpuid_info           *id;
	sal_processor_static_info_t     *regs;
} peidx_table_t;

#define peidx_head(p)   (((p)->info))
#define peidx_mid(p)    (((p)->id))
#define peidx_bottom(p) (((p)->regs))

#define peidx_psp(p)           (&(peidx_head(p)->proc_state_parameter))
#define peidx_field_valid(p)   (&(peidx_head(p)->valid))
#define peidx_minstate_area(p) (&(peidx_bottom(p)->min_state_area))

#define peidx_cache_check_num(p)    (peidx_head(p)->valid.num_cache_check)
#define peidx_tlb_check_num(p)      (peidx_head(p)->valid.num_tlb_check)
#define peidx_bus_check_num(p)      (peidx_head(p)->valid.num_bus_check)
#define peidx_reg_file_check_num(p) (peidx_head(p)->valid.num_reg_file_check)
#define peidx_ms_check_num(p)       (peidx_head(p)->valid.num_ms_check)

#define peidx_cache_check_idx(p, n)    (n)
#define peidx_tlb_check_idx(p, n)      (peidx_cache_check_idx(p, peidx_cache_check_num(p)) + n)
#define peidx_bus_check_idx(p, n)      (peidx_tlb_check_idx(p, peidx_tlb_check_num(p)) + n)
#define peidx_reg_file_check_idx(p, n) (peidx_bus_check_idx(p, peidx_bus_check_num(p)) + n)
#define peidx_ms_check_idx(p, n)       (peidx_reg_file_check_idx(p, peidx_reg_file_check_num(p)) + n)

#define peidx_mod_error_info(p, name, n) \
({	int __idx = peidx_##name##_idx(p, n); \
	sal_log_mod_error_info_t *__ret = NULL; \
	if (peidx_##name##_num(p) > n) /*BUG*/ \
		__ret = &(peidx_head(p)->info[__idx]); \
	__ret; })

#define peidx_cache_check(p, n)    peidx_mod_error_info(p, cache_check, n)
#define peidx_tlb_check(p, n)      peidx_mod_error_info(p, tlb_check, n)
#define peidx_bus_check(p, n)      peidx_mod_error_info(p, bus_check, n)
#define peidx_reg_file_check(p, n) peidx_mod_error_info(p, reg_file_check, n)
#define peidx_ms_check(p, n)       peidx_mod_error_info(p, ms_check, n)

#define peidx_check_info(proc, name, n) \
({ \
	sal_log_mod_error_info_t *__info = peidx_mod_error_info(proc, name, n);\
	u64 __temp = __info && __info->valid.check_info \
		? __info->check_info : 0; \
	__temp; })

/* slidx: index of SAL log error record */

typedef struct slidx_list {
	struct list_head list;
	sal_log_section_hdr_t *hdr;
} slidx_list_t;

typedef struct slidx_table {
	sal_log_record_header_t *header;
	int n_sections;			/* # of section headers */
	struct list_head proc_err;
	struct list_head mem_dev_err;
	struct list_head sel_dev_err;
	struct list_head pci_bus_err;
	struct list_head smbios_dev_err;
	struct list_head pci_comp_err;
	struct list_head plat_specific_err;
	struct list_head host_ctlr_err;
	struct list_head plat_bus_err;
	struct list_head unsupported;	/* list of unsupported sections */
} slidx_table_t;

#define slidx_foreach_entry(pos, head) \
	list_for_each_entry(pos, head, list)
#define slidx_first_entry(head) \
	(((head)->next != (head)) ? list_entry((head)->next, typeof(slidx_list_t), list) : NULL)
#define slidx_count(slidx, sec) \
({	int __count = 0; \
	slidx_list_t *__pos; \
	slidx_foreach_entry(__pos, &((slidx)->sec)) { __count++; }\
	__count; })

struct mca_table_entry {
	int start_addr;	/* location-relative starting address of MCA recoverable range */
	int end_addr;	/* location-relative ending address of MCA recoverable range */
};

extern const struct mca_table_entry *search_mca_tables (unsigned long addr);
extern int mca_recover_range(unsigned long);
