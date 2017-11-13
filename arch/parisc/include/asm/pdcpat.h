#ifndef __PARISC_PATPDC_H
#define __PARISC_PATPDC_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright 2000 (c) Hewlett Packard (Paul Bame <bame()spam.parisc-linux.org>)
 * Copyright 2000,2004 (c) Grant Grundler <grundler()nahspam.parisc-linux.org>
 */


#define PDC_PAT_CELL           	64L   /* Interface for gaining and 
                                         * manipulatin g cell state within PD */
#define PDC_PAT_CELL_GET_NUMBER    0L   /* Return Cell number */
#define PDC_PAT_CELL_GET_INFO      1L   /* Returns info about Cell */
#define PDC_PAT_CELL_MODULE        2L   /* Returns info about Module */
#define PDC_PAT_CELL_SET_ATTENTION 9L   /* Set Cell Attention indicator */
#define PDC_PAT_CELL_NUMBER_TO_LOC 10L   /* Cell Number -> Location */
#define PDC_PAT_CELL_WALK_FABRIC   11L   /* Walk the Fabric */
#define PDC_PAT_CELL_GET_RDT_SIZE  12L   /* Return Route Distance Table Sizes */
#define PDC_PAT_CELL_GET_RDT       13L   /* Return Route Distance Tables */
#define PDC_PAT_CELL_GET_LOCAL_PDH_SZ 14L /* Read Local PDH Buffer Size */
#define PDC_PAT_CELL_SET_LOCAL_PDH    15L  /* Write Local PDH Buffer */
#define PDC_PAT_CELL_GET_REMOTE_PDH_SZ 16L /* Return Remote PDH Buffer Size */
#define PDC_PAT_CELL_GET_REMOTE_PDH 17L /* Read Remote PDH Buffer */
#define PDC_PAT_CELL_GET_DBG_INFO   128L  /* Return DBG Buffer Info */
#define PDC_PAT_CELL_CHANGE_ALIAS   129L  /* Change Non-Equivalent Alias Chacking */


/*
** Arg to PDC_PAT_CELL_MODULE memaddr[4]
**
** Addresses on the Merced Bus != all Runway Bus addresses.
** This is intended for programming SBA/LBA chips range registers.
*/
#define IO_VIEW      0UL
#define PA_VIEW      1UL

/* PDC_PAT_CELL_MODULE entity type values */
#define	PAT_ENTITY_CA	0	/* central agent */
#define	PAT_ENTITY_PROC	1	/* processor */
#define	PAT_ENTITY_MEM	2	/* memory controller */
#define	PAT_ENTITY_SBA	3	/* system bus adapter */
#define	PAT_ENTITY_LBA	4	/* local bus adapter */
#define	PAT_ENTITY_PBC	5	/* processor bus converter */
#define	PAT_ENTITY_XBC	6	/* crossbar fabric connect */
#define	PAT_ENTITY_RC	7	/* fabric interconnect */

/* PDC_PAT_CELL_MODULE address range type values */
#define PAT_PBNUM           0         /* PCI Bus Number */
#define PAT_LMMIO           1         /* < 4G MMIO Space */
#define PAT_GMMIO           2         /* > 4G MMIO Space */
#define PAT_NPIOP           3         /* Non Postable I/O Port Space */
#define PAT_PIOP            4         /* Postable I/O Port Space */
#define PAT_AHPA            5         /* Addional HPA Space */
#define PAT_UFO             6         /* HPA Space (UFO for Mariposa) */
#define PAT_GNIP            7         /* GNI Reserved Space */



/* PDC PAT CHASSIS LOG -- Platform logging & forward progress functions */

#define PDC_PAT_CHASSIS_LOG		65L
#define PDC_PAT_CHASSIS_WRITE_LOG    	0L /* Write Log Entry */
#define PDC_PAT_CHASSIS_READ_LOG     	1L /* Read  Log Entry */


/* PDC PAT CPU  -- CPU configuration within the protection domain */

#define PDC_PAT_CPU                	67L
#define PDC_PAT_CPU_INFO            	0L /* Return CPU config info */
#define PDC_PAT_CPU_DELETE          	1L /* Delete CPU */
#define PDC_PAT_CPU_ADD             	2L /* Add    CPU */
#define PDC_PAT_CPU_GET_NUMBER      	3L /* Return CPU Number */
#define PDC_PAT_CPU_GET_HPA         	4L /* Return CPU HPA */
#define PDC_PAT_CPU_STOP            	5L /* Stop   CPU */
#define PDC_PAT_CPU_RENDEZVOUS      	6L /* Rendezvous CPU */
#define PDC_PAT_CPU_GET_CLOCK_INFO  	7L /* Return CPU Clock info */
#define PDC_PAT_CPU_GET_RENDEZVOUS_STATE 8L /* Return Rendezvous State */
#define PDC_PAT_CPU_PLUNGE_FABRIC 	128L /* Plunge Fabric */
#define PDC_PAT_CPU_UPDATE_CACHE_CLEANSING 129L /* Manipulate Cache 
                                                 * Cleansing Mode */
/*  PDC PAT EVENT -- Platform Events */

#define PDC_PAT_EVENT              	68L
#define PDC_PAT_EVENT_GET_CAPS     	0L /* Get Capabilities */
#define PDC_PAT_EVENT_SET_MODE     	1L /* Set Notification Mode */
#define PDC_PAT_EVENT_SCAN         	2L /* Scan Event */
#define PDC_PAT_EVENT_HANDLE       	3L /* Handle Event */
#define PDC_PAT_EVENT_GET_NB_CALL  	4L /* Get Non-Blocking call Args */

/*  PDC PAT HPMC -- Cause processor to go into spin loop, and wait
 *  			for wake up from Monarch Processor.
 */

#define PDC_PAT_HPMC               70L
#define PDC_PAT_HPMC_RENDEZ_CPU     0L /* go into spin loop */
#define PDC_PAT_HPMC_SET_PARAMS     1L /* Allows OS to specify intr which PDC 
                                        * will use to interrupt OS during
                                        * machine check rendezvous */

/* parameters for PDC_PAT_HPMC_SET_PARAMS: */
#define HPMC_SET_PARAMS_INTR 	    1L /* Rendezvous Interrupt */
#define HPMC_SET_PARAMS_WAKE 	    2L /* Wake up processor */


/*  PDC PAT IO  -- On-line services for I/O modules */

#define PDC_PAT_IO                  71L
#define PDC_PAT_IO_GET_SLOT_STATUS   	5L /* Get Slot Status Info*/
#define PDC_PAT_IO_GET_LOC_FROM_HARDWARE 6L /* Get Physical Location from */
                                            /* Hardware Path */
#define PDC_PAT_IO_GET_HARDWARE_FROM_LOC 7L /* Get Hardware Path from 
                                             * Physical Location */
#define PDC_PAT_IO_GET_PCI_CONFIG_FROM_HW 11L /* Get PCI Configuration
                                               * Address from Hardware Path */
#define PDC_PAT_IO_GET_HW_FROM_PCI_CONFIG 12L /* Get Hardware Path 
                                               * from PCI Configuration Address */
#define PDC_PAT_IO_READ_HOST_BRIDGE_INFO 13L  /* Read Host Bridge State Info */
#define PDC_PAT_IO_CLEAR_HOST_BRIDGE_INFO 14L /* Clear Host Bridge State Info*/
#define PDC_PAT_IO_GET_PCI_ROUTING_TABLE_SIZE 15L /* Get PCI INT Routing Table 
                                                   * Size */
#define PDC_PAT_IO_GET_PCI_ROUTING_TABLE  16L /* Get PCI INT Routing Table */
#define PDC_PAT_IO_GET_HINT_TABLE_SIZE 	17L /* Get Hint Table Size */
#define PDC_PAT_IO_GET_HINT_TABLE   	18L /* Get Hint Table */
#define PDC_PAT_IO_PCI_CONFIG_READ  	19L /* PCI Config Read */
#define PDC_PAT_IO_PCI_CONFIG_WRITE 	20L /* PCI Config Write */
#define PDC_PAT_IO_GET_NUM_IO_SLOTS 	21L /* Get Number of I/O Bay Slots in 
                                       		  * Cabinet */
#define PDC_PAT_IO_GET_LOC_IO_SLOTS 	22L /* Get Physical Location of I/O */
                                   		     /* Bay Slots in Cabinet */
#define PDC_PAT_IO_BAY_STATUS_INFO  	28L /* Get I/O Bay Slot Status Info */
#define PDC_PAT_IO_GET_PROC_VIEW        29L /* Get Processor view of IO address */
#define PDC_PAT_IO_PROG_SBA_DIR_RANGE   30L /* Program directed range */


/* PDC PAT MEM  -- Manage memory page deallocation */

#define PDC_PAT_MEM            72L
#define PDC_PAT_MEM_PD_INFO     	0L /* Return PDT info for PD       */
#define PDC_PAT_MEM_PD_CLEAR    	1L /* Clear PDT for PD             */
#define PDC_PAT_MEM_PD_READ     	2L /* Read PDT entries for PD      */
#define PDC_PAT_MEM_PD_RESET    	3L /* Reset clear bit for PD       */
#define PDC_PAT_MEM_CELL_INFO   	5L /* Return PDT info For Cell     */
#define PDC_PAT_MEM_CELL_CLEAR  	6L /* Clear PDT For Cell           */
#define PDC_PAT_MEM_CELL_READ   	7L /* Read PDT entries For Cell    */
#define PDC_PAT_MEM_CELL_RESET  	8L /* Reset clear bit For Cell     */
#define PDC_PAT_MEM_SETGM		9L /* Set Good Memory value        */
#define PDC_PAT_MEM_ADD_PAGE		10L /* ADDs a page to the cell      */
#define PDC_PAT_MEM_ADDRESS		11L /* Get Physical Location From   */
					    /* Memory Address               */
#define PDC_PAT_MEM_GET_TXT_SIZE   	12L /* Get Formatted Text Size   */
#define PDC_PAT_MEM_GET_PD_TXT     	13L /* Get PD Formatted Text     */
#define PDC_PAT_MEM_GET_CELL_TXT   	14L /* Get Cell Formatted Text   */
#define PDC_PAT_MEM_RD_STATE_INFO  	15L /* Read Mem Module State Info*/
#define PDC_PAT_MEM_CLR_STATE_INFO 	16L /*Clear Mem Module State Info*/
#define PDC_PAT_MEM_CLEAN_RANGE    	128L /*Clean Mem in specific range*/
#define PDC_PAT_MEM_GET_TBL_SIZE   	131L /* Get Memory Table Size     */
#define PDC_PAT_MEM_GET_TBL        	132L /* Get Memory Table          */


/* PDC PAT NVOLATILE  --  Access Non-Volatile Memory */

#define PDC_PAT_NVOLATILE	73L
#define PDC_PAT_NVOLATILE_READ		0L /* Read Non-Volatile Memory   */
#define PDC_PAT_NVOLATILE_WRITE		1L /* Write Non-Volatile Memory  */
#define PDC_PAT_NVOLATILE_GET_SIZE	2L /* Return size of NVM         */
#define PDC_PAT_NVOLATILE_VERIFY	3L /* Verify contents of NVM     */
#define PDC_PAT_NVOLATILE_INIT		4L /* Initialize NVM             */

/* PDC PAT PD */
#define PDC_PAT_PD		74L         /* Protection Domain Info   */
#define PDC_PAT_PD_GET_ADDR_MAP		0L  /* Get Address Map          */

/* PDC_PAT_PD_GET_ADDR_MAP entry types */
#define PAT_MEMORY_DESCRIPTOR		1   

/* PDC_PAT_PD_GET_ADDR_MAP memory types */
#define PAT_MEMTYPE_MEMORY		0
#define PAT_MEMTYPE_FIRMWARE		4

/* PDC_PAT_PD_GET_ADDR_MAP memory usage */
#define PAT_MEMUSE_GENERAL		0
#define PAT_MEMUSE_GI			128
#define PAT_MEMUSE_GNI			129


#ifndef __ASSEMBLY__
#include <linux/types.h>

#ifdef CONFIG_64BIT
#define is_pdc_pat()	(PDC_TYPE_PAT == pdc_type)
extern int pdc_pat_get_irt_size(unsigned long *num_entries, unsigned long cell_num);
extern int pdc_pat_get_irt(void *r_addr, unsigned long cell_num);
#else	/* ! CONFIG_64BIT */
/* No PAT support for 32-bit kernels...sorry */
#define is_pdc_pat()	(0)
#define pdc_pat_get_irt_size(num_entries, cell_numn)	PDC_BAD_PROC
#define pdc_pat_get_irt(r_addr, cell_num)		PDC_BAD_PROC
#endif	/* ! CONFIG_64BIT */


struct pdc_pat_cell_num {
	unsigned long cell_num;
	unsigned long cell_loc;
};

struct pdc_pat_cpu_num {
	unsigned long cpu_num;
	unsigned long cpu_loc;
};

struct pdc_pat_mem_retinfo { /* PDC_PAT_MEM/PDC_PAT_MEM_PD_INFO (return info) */
	unsigned int ke;	/* bit 0: memory inside good memory? */
	unsigned int current_pdt_entries:16;
	unsigned int max_pdt_entries:16;
	unsigned long Cs_bitmap;
	unsigned long Ic_bitmap;
	unsigned long good_mem;
	unsigned long first_dbe_loc; /* first location of double bit error */
	unsigned long clear_time; /* last PDT clear time (since Jan 1970) */
};

struct pdc_pat_mem_cell_pdt_retinfo { /* PDC_PAT_MEM/PDC_PAT_MEM_CELL_INFO */
	u64 reserved:32;
	u64 cs:1;		/* clear status: cleared since the last call? */
	u64 current_pdt_entries:15;
	u64 ic:1;		/* interleaving had to be changed ? */
	u64 max_pdt_entries:15;
	unsigned long good_mem;
	unsigned long first_dbe_loc; /* first location of double bit error */
	unsigned long clear_time; /* last PDT clear time (since Jan 1970) */
};


struct pdc_pat_mem_read_pd_retinfo { /* PDC_PAT_MEM/PDC_PAT_MEM_PD_READ */
	unsigned long actual_count_bytes;
	unsigned long pdt_entries;
};

struct pdc_pat_mem_phys_mem_location { /* PDC_PAT_MEM/PDC_PAT_MEM_ADDRESS */
	u64 cabinet:8;
	u64 ign1:8;
	u64 ign2:8;
	u64 cell_slot:8;
	u64 ign3:8;
	u64 dimm_slot:8; /* DIMM slot, e.g. 0x1A, 0x2B, show user hex value! */
	u64 ign4:8;
	u64 source:4; /* for mem: always 0x07 */
	u64 source_detail:4; /* for mem: always 0x04 (SIMM or DIMM) */
};

struct pdc_pat_pd_addr_map_entry {
	unsigned char entry_type;       /* 1 = Memory Descriptor Entry Type */
	unsigned char reserve1[5];
	unsigned char memory_type;
	unsigned char memory_usage;
	unsigned long paddr;
	unsigned int  pages;            /* Length in 4K pages */
	unsigned int  reserve2;
	unsigned long cell_map;
};

/********************************************************************
* PDC_PAT_CELL[Return Cell Module] memaddr[0] conf_base_addr
* ----------------------------------------------------------
* Bit  0 to 51 - conf_base_addr
* Bit 52 to 62 - reserved
* Bit       63 - endianess bit
********************************************************************/
#define PAT_GET_CBA(value) ((value) & 0xfffffffffffff000UL)

/********************************************************************
* PDC_PAT_CELL[Return Cell Module] memaddr[1] mod_info
* ----------------------------------------------------
* Bit  0 to  7 - entity type
*    0 = central agent,            1 = processor,
*    2 = memory controller,        3 = system bus adapter,
*    4 = local bus adapter,        5 = processor bus converter,
*    6 = crossbar fabric connect,  7 = fabric interconnect,
*    8 to 254 reserved,            255 = unknown.
* Bit  8 to 15 - DVI
* Bit 16 to 23 - IOC functions
* Bit 24 to 39 - reserved
* Bit 40 to 63 - mod_pages
*    number of 4K pages a module occupies starting at conf_base_addr
********************************************************************/
#define PAT_GET_ENTITY(value)	(((value) >> 56) & 0xffUL)
#define PAT_GET_DVI(value)	(((value) >> 48) & 0xffUL)
#define PAT_GET_IOC(value)	(((value) >> 40) & 0xffUL)
#define PAT_GET_MOD_PAGES(value) ((value) & 0xffffffUL)


/*
** PDC_PAT_CELL_GET_INFO return block
*/
typedef struct pdc_pat_cell_info_rtn_block {
	unsigned long cpu_info;
	unsigned long cell_info;
	unsigned long cell_location;
	unsigned long reo_location;
	unsigned long mem_size;
	unsigned long dimm_status;
	unsigned long pdc_rev;
	unsigned long fabric_info0;
	unsigned long fabric_info1;
	unsigned long fabric_info2;
	unsigned long fabric_info3;
	unsigned long reserved[21];
} pdc_pat_cell_info_rtn_block_t;


/* FIXME: mod[508] should really be a union of the various mod components */
struct pdc_pat_cell_mod_maddr_block {	/* PDC_PAT_CELL_MODULE */
	unsigned long cba;		/* func 0 cfg space address */
	unsigned long mod_info;		/* module information */
	unsigned long mod_location;	/* physical location of the module */
	struct hardware_path mod_path;	/* module path (device path - layers) */
	unsigned long mod[508];		/* PAT cell module components */
} __attribute__((aligned(8))) ;

typedef struct pdc_pat_cell_mod_maddr_block pdc_pat_cell_mod_maddr_block_t;


extern int pdc_pat_chassis_send_log(unsigned long status, unsigned long data);
extern int pdc_pat_cell_get_number(struct pdc_pat_cell_num *cell_info);
extern int pdc_pat_cell_module(unsigned long *actcnt, unsigned long ploc, unsigned long mod, unsigned long view_type, void *mem_addr);
extern int pdc_pat_cell_num_to_loc(void *, unsigned long);

extern int pdc_pat_cpu_get_number(struct pdc_pat_cpu_num *cpu_info, unsigned long hpa);

extern int pdc_pat_pd_get_addr_map(unsigned long *actual_len, void *mem_addr, unsigned long count, unsigned long offset);

extern int pdc_pat_io_pci_cfg_read(unsigned long pci_addr, int pci_size, u32 *val); 
extern int pdc_pat_io_pci_cfg_write(unsigned long pci_addr, int pci_size, u32 val); 

extern int pdc_pat_mem_pdt_info(struct pdc_pat_mem_retinfo *rinfo);
extern int pdc_pat_mem_pdt_cell_info(struct pdc_pat_mem_cell_pdt_retinfo *rinfo,
		unsigned long cell);
extern int pdc_pat_mem_read_cell_pdt(struct pdc_pat_mem_read_pd_retinfo *pret,
		unsigned long *pdt_entries_ptr, unsigned long max_entries);
extern int pdc_pat_mem_read_pd_pdt(struct pdc_pat_mem_read_pd_retinfo *pret,
		unsigned long *pdt_entries_ptr, unsigned long count,
		unsigned long offset);
extern int pdc_pat_mem_get_dimm_phys_location(
                struct pdc_pat_mem_phys_mem_location *pret,
                unsigned long phys_addr);

#endif /* __ASSEMBLY__ */

#endif /* ! __PARISC_PATPDC_H */
