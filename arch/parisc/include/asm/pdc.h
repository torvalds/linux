/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PARISC_PDC_H
#define _PARISC_PDC_H

#include <uapi/asm/pdc.h>

#if !defined(__ASSEMBLY__)

extern int parisc_narrow_firmware;

extern int pdc_type;
extern unsigned long parisc_cell_num; /* cell number the CPU runs on (PAT) */
extern unsigned long parisc_cell_loc; /* cell location of CPU (PAT)	   */
extern unsigned long parisc_pat_pdc_cap; /* PDC capabilities (PAT) */

/* Values for pdc_type */
#define PDC_TYPE_ILLEGAL	-1
#define PDC_TYPE_PAT		 0 /* 64-bit PAT-PDC */
#define PDC_TYPE_SYSTEM_MAP	 1 /* 32-bit, but supports PDC_SYSTEM_MAP */
#define PDC_TYPE_SNAKE		 2 /* Doesn't support SYSTEM_MAP */

void pdc_console_init(void);	/* in pdc_console.c */
void pdc_console_restart(void);

void setup_pdc(void);		/* in inventory.c */

/* wrapper-functions from pdc.c */

int pdc_add_valid(unsigned long address);
int pdc_instr(unsigned int *instr);
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

void pdc_pdt_init(void);	/* in pdt.c */
int pdc_mem_pdt_info(struct pdc_mem_retinfo *rinfo);
int pdc_mem_pdt_read_entries(struct pdc_mem_read_pdt *rpdt_read,
		unsigned long *pdt_entries_ptr);
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
