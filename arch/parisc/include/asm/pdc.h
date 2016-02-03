#ifndef _PARISC_PDC_H
#define _PARISC_PDC_H

#include <uapi/asm/pdc.h>

#if !defined(__ASSEMBLY__)

extern int pdc_type;

/* Values for pdc_type */
#define PDC_TYPE_ILLEGAL	-1
#define PDC_TYPE_PAT		 0 /* 64-bit PAT-PDC */
#define PDC_TYPE_SYSTEM_MAP	 1 /* 32-bit, but supports PDC_SYSTEM_MAP */
#define PDC_TYPE_SNAKE		 2 /* Doesn't support SYSTEM_MAP */

struct pdc_chassis_info {       /* for PDC_CHASSIS_INFO */
	unsigned long actcnt;   /* actual number of bytes returned */
	unsigned long maxcnt;   /* maximum number of bytes that could be returned */
};

struct pdc_coproc_cfg {         /* for PDC_COPROC_CFG */
        unsigned long ccr_functional;
        unsigned long ccr_present;
        unsigned long revision;
        unsigned long model;
};

struct pdc_model {		/* for PDC_MODEL */
	unsigned long hversion;
	unsigned long sversion;
	unsigned long hw_id;
	unsigned long boot_id;
	unsigned long sw_id;
	unsigned long sw_cap;
	unsigned long arch_rev;
	unsigned long pot_key;
	unsigned long curr_key;
};

struct pdc_cache_cf {		/* for PDC_CACHE  (I/D-caches) */
    unsigned long
#ifdef CONFIG_64BIT
		cc_padW:32,
#endif
		cc_alias: 4,	/* alias boundaries for virtual addresses   */
		cc_block: 4,	/* to determine most efficient stride */
		cc_line	: 3,	/* maximum amount written back as a result of store (multiple of 16 bytes) */
		cc_shift: 2,	/* how much to shift cc_block left */
		cc_wt	: 1,	/* 0 = WT-Dcache, 1 = WB-Dcache */
		cc_sh	: 2,	/* 0 = separate I/D-cache, else shared I/D-cache */
		cc_cst  : 3,	/* 0 = incoherent D-cache, 1=coherent D-cache */
		cc_pad1 : 10,	/* reserved */
		cc_hv   : 3;	/* hversion dependent */
};

struct pdc_tlb_cf {		/* for PDC_CACHE (I/D-TLB's) */
    unsigned long tc_pad0:12,	/* reserved */
#ifdef CONFIG_64BIT
		tc_padW:32,
#endif
		tc_sh	: 2,	/* 0 = separate I/D-TLB, else shared I/D-TLB */
		tc_hv   : 1,	/* HV */
		tc_page : 1,	/* 0 = 2K page-size-machine, 1 = 4k page size */
		tc_cst  : 3,	/* 0 = incoherent operations, else coherent operations */
		tc_aid  : 5,	/* ITLB: width of access ids of processor (encoded!) */
		tc_sr   : 8;	/* ITLB: width of space-registers (encoded) */
};

struct pdc_cache_info {		/* main-PDC_CACHE-structure (caches & TLB's) */
	/* I-cache */
	unsigned long	ic_size;	/* size in bytes */
	struct pdc_cache_cf ic_conf;	/* configuration */
	unsigned long	ic_base;	/* base-addr */
	unsigned long	ic_stride;
	unsigned long	ic_count;
	unsigned long	ic_loop;
	/* D-cache */
	unsigned long	dc_size;	/* size in bytes */
	struct pdc_cache_cf dc_conf;	/* configuration */
	unsigned long	dc_base;	/* base-addr */
	unsigned long	dc_stride;
	unsigned long	dc_count;
	unsigned long	dc_loop;
	/* Instruction-TLB */
	unsigned long	it_size;	/* number of entries in I-TLB */
	struct pdc_tlb_cf it_conf;	/* I-TLB-configuration */
	unsigned long	it_sp_base;
	unsigned long	it_sp_stride;
	unsigned long	it_sp_count;
	unsigned long	it_off_base;
	unsigned long	it_off_stride;
	unsigned long	it_off_count;
	unsigned long	it_loop;
	/* data-TLB */
	unsigned long	dt_size;	/* number of entries in D-TLB */
	struct pdc_tlb_cf dt_conf;	/* D-TLB-configuration */
	unsigned long	dt_sp_base;
	unsigned long	dt_sp_stride;
	unsigned long	dt_sp_count;
	unsigned long	dt_off_base;
	unsigned long	dt_off_stride;
	unsigned long	dt_off_count;
	unsigned long	dt_loop;
};

#if 0
/* If you start using the next struct, you'll have to adjust it to
 * work with 64-bit firmware I think -PB
 */
struct pdc_iodc {     /* PDC_IODC */
	unsigned char   hversion_model;
	unsigned char 	hversion;
	unsigned char 	spa;
	unsigned char 	type;
	unsigned int	sversion_rev:4;
	unsigned int	sversion_model:19;
	unsigned int	sversion_opt:8;
	unsigned char	rev;
	unsigned char	dep;
	unsigned char	features;
	unsigned char	pad1;
	unsigned int	checksum:16;
	unsigned int	length:16;
	unsigned int    pad[15];
} __attribute__((aligned(8))) ;
#endif

#ifndef CONFIG_PA20
/* no BLTBs in pa2.0 processors */
struct pdc_btlb_info_range {
	__u8 res00;
	__u8 num_i;
	__u8 num_d;
	__u8 num_comb;
};

struct pdc_btlb_info {	/* PDC_BLOCK_TLB, return of PDC_BTLB_INFO */
	unsigned int min_size;	/* minimum size of BTLB in pages */
	unsigned int max_size;	/* maximum size of BTLB in pages */
	struct pdc_btlb_info_range fixed_range_info;
	struct pdc_btlb_info_range variable_range_info;
};

#endif /* !CONFIG_PA20 */

#ifdef CONFIG_64BIT
struct pdc_memory_table_raddr { /* PDC_MEM/PDC_MEM_TABLE (return info) */
	unsigned long entries_returned;
	unsigned long entries_total;
};

struct pdc_memory_table {       /* PDC_MEM/PDC_MEM_TABLE (arguments) */
	unsigned long paddr;
	unsigned int  pages;
	unsigned int  reserved;
};
#endif /* CONFIG_64BIT */

struct pdc_system_map_mod_info { /* PDC_SYSTEM_MAP/FIND_MODULE */
	unsigned long mod_addr;
	unsigned long mod_pgs;
	unsigned long add_addrs;
};

struct pdc_system_map_addr_info { /* PDC_SYSTEM_MAP/FIND_ADDRESS */
	unsigned long mod_addr;
	unsigned long mod_pgs;
};

struct pdc_initiator { /* PDC_INITIATOR */
	int host_id;
	int factor;
	int width;
	int mode;
};

struct hardware_path {
	char  flags;	/* see bit definitions below */
	char  bc[6];	/* Bus Converter routing info to a specific */
			/* I/O adaptor (< 0 means none, > 63 resvd) */
	char  mod;	/* fixed field of specified module */
};

/*
 * Device path specifications used by PDC.
 */
struct pdc_module_path {
	struct hardware_path path;
	unsigned int layers[6]; /* device-specific info (ctlr #, unit # ...) */
};

#ifndef CONFIG_PA20
/* Only used on some pre-PA2.0 boxes */
struct pdc_memory_map {		/* PDC_MEMORY_MAP */
	unsigned long hpa;	/* mod's register set address */
	unsigned long more_pgs;	/* number of additional I/O pgs */
};
#endif

struct pdc_tod {
	unsigned long tod_sec; 
	unsigned long tod_usec;
};

/* architected results from PDC_PIM/transfer hpmc on a PA1.1 machine */

struct pdc_hpmc_pim_11 { /* PDC_PIM */
	__u32 gr[32];
	__u32 cr[32];
	__u32 sr[8];
	__u32 iasq_back;
	__u32 iaoq_back;
	__u32 check_type;
	__u32 cpu_state;
	__u32 rsvd1;
	__u32 cache_check;
	__u32 tlb_check;
	__u32 bus_check;
	__u32 assists_check;
	__u32 rsvd2;
	__u32 assist_state;
	__u32 responder_addr;
	__u32 requestor_addr;
	__u32 path_info;
	__u64 fr[32];
};

/*
 * architected results from PDC_PIM/transfer hpmc on a PA2.0 machine
 *
 * Note that PDC_PIM doesn't care whether or not wide mode was enabled
 * so the results are different on  PA1.1 vs. PA2.0 when in narrow mode.
 *
 * Note also that there are unarchitected results available, which
 * are hversion dependent. Do a "ser pim 0 hpmc" after rebooting, since
 * the firmware is probably the best way of printing hversion dependent
 * data.
 */

struct pdc_hpmc_pim_20 { /* PDC_PIM */
	__u64 gr[32];
	__u64 cr[32];
	__u64 sr[8];
	__u64 iasq_back;
	__u64 iaoq_back;
	__u32 check_type;
	__u32 cpu_state;
	__u32 cache_check;
	__u32 tlb_check;
	__u32 bus_check;
	__u32 assists_check;
	__u32 assist_state;
	__u32 path_info;
	__u64 responder_addr;
	__u64 requestor_addr;
	__u64 fr[32];
};

void pdc_console_init(void);	/* in pdc_console.c */
void pdc_console_restart(void);

void setup_pdc(void);		/* in inventory.c */

/* wrapper-functions from pdc.c */

int pdc_add_valid(unsigned long address);
int pdc_chassis_info(struct pdc_chassis_info *chassis_info, void *led_info, unsigned long len);
int pdc_chassis_disp(unsigned long disp);
int pdc_chassis_warn(unsigned long *warn);
int pdc_coproc_cfg(struct pdc_coproc_cfg *pdc_coproc_info);
int pdc_coproc_cfg_unlocked(struct pdc_coproc_cfg *pdc_coproc_info);
int pdc_iodc_read(unsigned long *actcnt, unsigned long hpa, unsigned int index,
		  void *iodc_data, unsigned int iodc_data_size);
int pdc_system_map_find_mods(struct pdc_system_map_mod_info *pdc_mod_info,
			     struct pdc_module_path *mod_path, long mod_index);
int pdc_system_map_find_addrs(struct pdc_system_map_addr_info *pdc_addr_info,
			      long mod_index, long addr_index);
int pdc_model_info(struct pdc_model *model);
int pdc_model_sysmodel(char *name);
int pdc_model_cpuid(unsigned long *cpu_id);
int pdc_model_versions(unsigned long *versions, int id);
int pdc_model_capabilities(unsigned long *capabilities);
int pdc_cache_info(struct pdc_cache_info *cache);
int pdc_spaceid_bits(unsigned long *space_bits);
#ifndef CONFIG_PA20
int pdc_btlb_info(struct pdc_btlb_info *btlb);
int pdc_mem_map_hpa(struct pdc_memory_map *r_addr, struct pdc_module_path *mod_path);
#endif /* !CONFIG_PA20 */
int pdc_lan_station_id(char *lan_addr, unsigned long net_hpa);

int pdc_stable_read(unsigned long staddr, void *memaddr, unsigned long count);
int pdc_stable_write(unsigned long staddr, void *memaddr, unsigned long count);
int pdc_stable_get_size(unsigned long *size);
int pdc_stable_verify_contents(void);
int pdc_stable_initialize(void);

int pdc_pci_irt_size(unsigned long *num_entries, unsigned long hpa);
int pdc_pci_irt(unsigned long num_entries, unsigned long hpa, void *tbl);

int pdc_get_initiator(struct hardware_path *, struct pdc_initiator *);
int pdc_tod_read(struct pdc_tod *tod);
int pdc_tod_set(unsigned long sec, unsigned long usec);

#ifdef CONFIG_64BIT
int pdc_mem_mem_table(struct pdc_memory_table_raddr *r_addr,
		struct pdc_memory_table *tbl, unsigned long entries);
#endif

void set_firmware_width(void);
void set_firmware_width_unlocked(void);
int pdc_do_firm_test_reset(unsigned long ftc_bitmap);
int pdc_do_reset(void);
int pdc_soft_power_info(unsigned long *power_reg);
int pdc_soft_power_button(int sw_control);
void pdc_io_reset(void);
void pdc_io_reset_devices(void);
int pdc_iodc_getc(void);
int pdc_iodc_print(const unsigned char *str, unsigned count);

void pdc_emergency_unlock(void);
int pdc_sti_call(unsigned long func, unsigned long flags,
                 unsigned long inptr, unsigned long outputr,
                 unsigned long glob_cfg);

static inline char * os_id_to_string(u16 os_id) {
	switch(os_id) {
	case OS_ID_NONE:	return "No OS";
	case OS_ID_HPUX:	return "HP-UX";
	case OS_ID_MPEXL:	return "MPE-iX";
	case OS_ID_OSF:		return "OSF";
	case OS_ID_HPRT:	return "HP-RT";
	case OS_ID_NOVEL:	return "Novell Netware";
	case OS_ID_LINUX:	return "Linux";
	default:	return "Unknown";
	}
}

#endif /* !defined(__ASSEMBLY__) */
#endif /* _PARISC_PDC_H */
