#ifndef _PARISC_PDC_H
#define _PARISC_PDC_H

/*
 *	PDC return values ...
 *	All PDC calls return a subset of these errors. 
 */

#define PDC_WARN		  3	/* Call completed with a warning */
#define PDC_REQ_ERR_1		  2	/* See above			 */
#define PDC_REQ_ERR_0		  1	/* Call would generate a requestor error */
#define PDC_OK			  0	/* Call completed successfully	*/
#define PDC_BAD_PROC		 -1	/* Called non-existent procedure*/
#define PDC_BAD_OPTION		 -2	/* Called with non-existent option */
#define PDC_ERROR		 -3	/* Call could not complete without an error */
#define PDC_NE_MOD		 -5	/* Module not found		*/
#define PDC_NE_CELL_MOD		 -7	/* Cell module not found	*/
#define PDC_INVALID_ARG		-10	/* Called with an invalid argument */
#define PDC_BUS_POW_WARN	-12	/* Call could not complete in allowed power budget */
#define PDC_NOT_NARROW		-17	/* Narrow mode not supported	*/

/*
 *	PDC entry points...
 */

#define PDC_POW_FAIL	1		/* perform a power-fail		*/
#define PDC_POW_FAIL_PREPARE	0	/* prepare for powerfail	*/

#define PDC_CHASSIS	2		/* PDC-chassis functions	*/
#define PDC_CHASSIS_DISP	0	/* update chassis display	*/
#define PDC_CHASSIS_WARN	1	/* return chassis warnings	*/
#define PDC_CHASSIS_DISPWARN	2	/* update&return chassis status */
#define PDC_RETURN_CHASSIS_INFO 128	/* HVERSION dependent: return chassis LED/LCD info  */

#define PDC_PIM         3               /* Get PIM data                 */
#define PDC_PIM_HPMC            0       /* Transfer HPMC data           */
#define PDC_PIM_RETURN_SIZE     1       /* Get Max buffer needed for PIM*/
#define PDC_PIM_LPMC            2       /* Transfer HPMC data           */
#define PDC_PIM_SOFT_BOOT       3       /* Transfer Soft Boot data      */
#define PDC_PIM_TOC             4       /* Transfer TOC data            */

#define PDC_MODEL	4		/* PDC model information call	*/
#define PDC_MODEL_INFO		0	/* returns information 		*/
#define PDC_MODEL_BOOTID	1	/* set the BOOT_ID		*/
#define PDC_MODEL_VERSIONS	2	/* returns cpu-internal versions*/
#define PDC_MODEL_SYSMODEL	3	/* return system model info	*/
#define PDC_MODEL_ENSPEC	4	/* enable specific option	*/
#define PDC_MODEL_DISPEC	5	/* disable specific option	*/
#define PDC_MODEL_CPU_ID	6	/* returns cpu-id (only newer machines!) */
#define PDC_MODEL_CAPABILITIES	7	/* returns OS32/OS64-flags	*/
/* Values for PDC_MODEL_CAPABILITIES non-equivalent virtual aliasing support */
#define  PDC_MODEL_IOPDIR_FDC		(1 << 2)
#define  PDC_MODEL_NVA_MASK		(3 << 4)
#define  PDC_MODEL_NVA_SUPPORTED	(0 << 4)
#define  PDC_MODEL_NVA_SLOW		(1 << 4)
#define  PDC_MODEL_NVA_UNSUPPORTED	(3 << 4)
#define PDC_MODEL_GET_BOOT__OP	8	/* returns boot test options	*/
#define PDC_MODEL_SET_BOOT__OP	9	/* set boot test options	*/

#define PA89_INSTRUCTION_SET	0x4	/* capatibilies returned	*/
#define PA90_INSTRUCTION_SET	0x8

#define PDC_CACHE	5		/* return/set cache (& TLB) info*/
#define PDC_CACHE_INFO		0	/* returns information 		*/
#define PDC_CACHE_SET_COH	1	/* set coherence state		*/
#define PDC_CACHE_RET_SPID	2	/* returns space-ID bits	*/

#define PDC_HPA		6		/* return HPA of processor	*/
#define PDC_HPA_PROCESSOR	0
#define PDC_HPA_MODULES		1

#define PDC_COPROC	7		/* Co-Processor (usually FP unit(s)) */
#define PDC_COPROC_CFG		0	/* Co-Processor Cfg (FP unit(s) enabled?) */

#define PDC_IODC	8		/* talk to IODC			*/
#define PDC_IODC_READ		0	/* read IODC entry point	*/
/*      PDC_IODC_RI_			 * INDEX parameter of PDC_IODC_READ */
#define PDC_IODC_RI_DATA_BYTES	0	/* IODC Data Bytes		*/
/*				1, 2	   obsolete - HVERSION dependent*/
#define PDC_IODC_RI_INIT	3	/* Initialize module		*/
#define PDC_IODC_RI_IO		4	/* Module input/output		*/
#define PDC_IODC_RI_SPA		5	/* Module input/output		*/
#define PDC_IODC_RI_CONFIG	6	/* Module input/output		*/
/*				7	  obsolete - HVERSION dependent */
#define PDC_IODC_RI_TEST	8	/* Module input/output		*/
#define PDC_IODC_RI_TLB		9	/* Module input/output		*/
#define PDC_IODC_NINIT		2	/* non-destructive init		*/
#define PDC_IODC_DINIT		3	/* destructive init		*/
#define PDC_IODC_MEMERR		4	/* check for memory errors	*/
#define PDC_IODC_INDEX_DATA	0	/* get first 16 bytes from mod IODC */
#define PDC_IODC_BUS_ERROR	-4	/* bus error return value	*/
#define PDC_IODC_INVALID_INDEX	-5	/* invalid index return value	*/
#define PDC_IODC_COUNT		-6	/* count is too small		*/

#define PDC_TOD		9		/* time-of-day clock (TOD)	*/
#define PDC_TOD_READ		0	/* read TOD			*/
#define PDC_TOD_WRITE		1	/* write TOD			*/


#define PDC_STABLE	10		/* stable storage (sprockets)	*/
#define PDC_STABLE_READ		0
#define PDC_STABLE_WRITE	1
#define PDC_STABLE_RETURN_SIZE	2
#define PDC_STABLE_VERIFY_CONTENTS 3
#define PDC_STABLE_INITIALIZE	4

#define PDC_NVOLATILE	11		/* often not implemented	*/

#define PDC_ADD_VALID	12		/* Memory validation PDC call	*/
#define PDC_ADD_VALID_VERIFY	0	/* Make PDC_ADD_VALID verify region */

#define PDC_INSTR	15		/* get instr to invoke PDCE_CHECK() */

#define PDC_PROC	16		/* (sprockets)			*/

#define PDC_CONFIG	16		/* (sprockets)			*/
#define PDC_CONFIG_DECONFIG	0
#define PDC_CONFIG_DRECONFIG	1
#define PDC_CONFIG_DRETURN_CONFIG 2

#define PDC_BLOCK_TLB	18		/* manage hardware block-TLB	*/
#define PDC_BTLB_INFO		0	/* returns parameter 		*/
#define PDC_BTLB_INSERT		1	/* insert BTLB entry		*/
#define PDC_BTLB_PURGE		2	/* purge BTLB entries 		*/
#define PDC_BTLB_PURGE_ALL	3	/* purge all BTLB entries 	*/

#define PDC_TLB		19		/* manage hardware TLB miss handling */
#define PDC_TLB_INFO		0	/* returns parameter 		*/
#define PDC_TLB_SETUP		1	/* set up miss handling 	*/

#define PDC_MEM		20		/* Manage memory		*/
#define PDC_MEM_MEMINFO		0
#define PDC_MEM_ADD_PAGE	1
#define PDC_MEM_CLEAR_PDT	2
#define PDC_MEM_READ_PDT	3
#define PDC_MEM_RESET_CLEAR	4
#define PDC_MEM_GOODMEM		5
#define PDC_MEM_TABLE		128	/* Non contig mem map (sprockets) */
#define PDC_MEM_RETURN_ADDRESS_TABLE	PDC_MEM_TABLE
#define PDC_MEM_GET_MEMORY_SYSTEM_TABLES_SIZE	131
#define PDC_MEM_GET_MEMORY_SYSTEM_TABLES	132
#define PDC_MEM_GET_PHYSICAL_LOCATION_FROM_MEMORY_ADDRESS 133

#define PDC_MEM_RET_SBE_REPLACED	5	/* PDC_MEM return values */
#define PDC_MEM_RET_DUPLICATE_ENTRY	4
#define PDC_MEM_RET_BUF_SIZE_SMALL	1
#define PDC_MEM_RET_PDT_FULL		-11
#define PDC_MEM_RET_INVALID_PHYSICAL_LOCATION ~0ULL

#define PDC_PSW		21		/* Get/Set default System Mask  */
#define PDC_PSW_MASK		0	/* Return mask                  */
#define PDC_PSW_GET_DEFAULTS	1	/* Return defaults              */
#define PDC_PSW_SET_DEFAULTS	2	/* Set default                  */
#define PDC_PSW_ENDIAN_BIT	1	/* set for big endian           */
#define PDC_PSW_WIDE_BIT	2	/* set for wide mode            */ 

#define PDC_SYSTEM_MAP	22		/* find system modules		*/
#define PDC_FIND_MODULE 	0
#define PDC_FIND_ADDRESS	1
#define PDC_TRANSLATE_PATH	2

#define PDC_SOFT_POWER	23		/* soft power switch		*/
#define PDC_SOFT_POWER_INFO	0	/* return info about the soft power switch */
#define PDC_SOFT_POWER_ENABLE	1	/* enable/disable soft power switch */


/* HVERSION dependent */

/* The PDC_MEM_MAP calls */
#define PDC_MEM_MAP	128		/* on s700: return page info	*/
#define PDC_MEM_MAP_HPA		0	/* returns hpa of a module	*/

#define PDC_EEPROM	129		/* EEPROM access		*/
#define PDC_EEPROM_READ_WORD	0
#define PDC_EEPROM_WRITE_WORD	1
#define PDC_EEPROM_READ_BYTE	2
#define PDC_EEPROM_WRITE_BYTE	3
#define PDC_EEPROM_EEPROM_PASSWORD -1000

#define PDC_NVM		130		/* NVM (non-volatile memory) access */
#define PDC_NVM_READ_WORD	0
#define PDC_NVM_WRITE_WORD	1
#define PDC_NVM_READ_BYTE	2
#define PDC_NVM_WRITE_BYTE	3

#define PDC_SEED_ERROR	132		/* (sprockets)			*/

#define PDC_IO		135		/* log error info, reset IO system */
#define PDC_IO_READ_AND_CLEAR_ERRORS	0
#define PDC_IO_RESET			1
#define PDC_IO_RESET_DEVICES		2
/* sets bits 6&7 (little endian) of the HcControl Register */
#define PDC_IO_USB_SUSPEND	0xC000000000000000
#define PDC_IO_EEPROM_IO_ERR_TABLE_FULL	-5	/* return value */
#define PDC_IO_NO_SUSPEND		-6	/* return value */

#define PDC_BROADCAST_RESET 136		/* reset all processors		*/
#define PDC_DO_RESET		0	/* option: perform a broadcast reset */
#define PDC_DO_FIRM_TEST_RESET	1	/* Do broadcast reset with bitmap */
#define PDC_BR_RECONFIGURATION	2	/* reset w/reconfiguration	*/
#define PDC_FIRM_TEST_MAGIC	0xab9ec36fUL    /* for this reboot only	*/

#define PDC_LAN_STATION_ID 138		/* Hversion dependent mechanism for */
#define PDC_LAN_STATION_ID_READ	0	/* getting the lan station address  */

#define	PDC_LAN_STATION_ID_SIZE	6

#define PDC_CHECK_RANGES 139		/* (sprockets)			*/

#define PDC_NV_SECTIONS	141		/* (sprockets)			*/

#define PDC_PERFORMANCE	142		/* performance monitoring	*/

#define PDC_SYSTEM_INFO	143		/* system information		*/
#define PDC_SYSINFO_RETURN_INFO_SIZE	0
#define PDC_SYSINFO_RRETURN_SYS_INFO	1
#define PDC_SYSINFO_RRETURN_ERRORS	2
#define PDC_SYSINFO_RRETURN_WARNINGS	3
#define PDC_SYSINFO_RETURN_REVISIONS	4
#define PDC_SYSINFO_RRETURN_DIAGNOSE	5
#define PDC_SYSINFO_RRETURN_HV_DIAGNOSE	1005

#define PDC_RDR		144		/* (sprockets)			*/
#define PDC_RDR_READ_BUFFER	0
#define PDC_RDR_READ_SINGLE	1
#define PDC_RDR_WRITE_SINGLE	2

#define PDC_INTRIGUE	145 		/* (sprockets)			*/
#define PDC_INTRIGUE_WRITE_BUFFER 	 0
#define PDC_INTRIGUE_GET_SCRATCH_BUFSIZE 1
#define PDC_INTRIGUE_START_CPU_COUNTERS	 2
#define PDC_INTRIGUE_STOP_CPU_COUNTERS	 3

#define PDC_STI		146 		/* STI access			*/
/* same as PDC_PCI_XXX values (see below) */

/* Legacy PDC definitions for same stuff */
#define PDC_PCI_INDEX	147
#define PDC_PCI_INTERFACE_INFO		0
#define PDC_PCI_SLOT_INFO		1
#define PDC_PCI_INFLIGHT_BYTES		2
#define PDC_PCI_READ_CONFIG		3
#define PDC_PCI_WRITE_CONFIG		4
#define PDC_PCI_READ_PCI_IO		5
#define PDC_PCI_WRITE_PCI_IO		6
#define PDC_PCI_READ_CONFIG_DELAY	7
#define PDC_PCI_UPDATE_CONFIG_DELAY	8
#define PDC_PCI_PCI_PATH_TO_PCI_HPA	9
#define PDC_PCI_PCI_HPA_TO_PCI_PATH	10
#define PDC_PCI_PCI_PATH_TO_PCI_BUS	11
#define PDC_PCI_PCI_RESERVED		12
#define PDC_PCI_PCI_INT_ROUTE_SIZE	13
#define PDC_PCI_GET_INT_TBL_SIZE	PDC_PCI_PCI_INT_ROUTE_SIZE
#define PDC_PCI_PCI_INT_ROUTE		14
#define PDC_PCI_GET_INT_TBL		PDC_PCI_PCI_INT_ROUTE 
#define PDC_PCI_READ_MON_TYPE		15
#define PDC_PCI_WRITE_MON_TYPE		16


/* Get SCSI Interface Card info:  SDTR, SCSI ID, mode (SE vs LVD) */
#define PDC_INITIATOR	163
#define PDC_GET_INITIATOR	0
#define PDC_SET_INITIATOR	1
#define PDC_DELETE_INITIATOR	2
#define PDC_RETURN_TABLE_SIZE	3
#define PDC_RETURN_TABLE	4

#define PDC_LINK	165 		/* (sprockets)			*/
#define PDC_LINK_PCI_ENTRY_POINTS	0  /* list (Arg1) = 0 */
#define PDC_LINK_USB_ENTRY_POINTS	1  /* list (Arg1) = 1 */

/* cl_class
 * page 3-33 of IO-Firmware ARS
 * IODC ENTRY_INIT(Search first) RET[1]
 */
#define	CL_NULL		0	/* invalid */
#define	CL_RANDOM	1	/* random access (as disk) */
#define	CL_SEQU		2	/* sequential access (as tape) */
#define	CL_DUPLEX	7	/* full-duplex point-to-point (RS-232, Net) */
#define	CL_KEYBD	8	/* half-duplex console (HIL Keyboard) */
#define	CL_DISPL	9	/* half-duplex console (display) */
#define	CL_FC		10	/* FiberChannel access media */

/* IODC ENTRY_INIT() */
#define ENTRY_INIT_SRCH_FRST	2
#define ENTRY_INIT_SRCH_NEXT	3
#define ENTRY_INIT_MOD_DEV	4
#define ENTRY_INIT_DEV		5
#define ENTRY_INIT_MOD		6
#define ENTRY_INIT_MSG		9

/* IODC ENTRY_IO() */
#define ENTRY_IO_BOOTIN		0
#define ENTRY_IO_BOOTOUT	1
#define ENTRY_IO_CIN		2
#define ENTRY_IO_COUT		3
#define ENTRY_IO_CLOSE		4
#define ENTRY_IO_GETMSG		9
#define ENTRY_IO_BBLOCK_IN	16
#define ENTRY_IO_BBLOCK_OUT	17

/* IODC ENTRY_SPA() */

/* IODC ENTRY_CONFIG() */

/* IODC ENTRY_TEST() */

/* IODC ENTRY_TLB() */

/* constants for OS (NVM...) */
#define OS_ID_NONE		0	/* Undefined OS ID	*/
#define OS_ID_HPUX		1	/* HP-UX OS		*/
#define OS_ID_MPEXL		2	/* MPE XL OS		*/
#define OS_ID_OSF		3	/* OSF OS		*/
#define OS_ID_HPRT		4	/* HP-RT OS		*/
#define OS_ID_NOVEL		5	/* NOVELL OS		*/
#define OS_ID_LINUX		6	/* Linux		*/


/* constants for PDC_CHASSIS */
#define OSTAT_OFF		0
#define OSTAT_FLT		1 
#define OSTAT_TEST		2
#define OSTAT_INIT		3
#define OSTAT_SHUT		4
#define OSTAT_WARN		5
#define OSTAT_RUN		6
#define OSTAT_ON		7

/* Page Zero constant offsets used by the HPMC handler */
#define BOOT_CONSOLE_HPA_OFFSET  0x3c0
#define BOOT_CONSOLE_SPA_OFFSET  0x3c4
#define BOOT_CONSOLE_PATH_OFFSET 0x3a8

/* size of the pdc_result buffer for firmware.c */
#define NUM_PDC_RESULT	32

#if !defined(__ASSEMBLY__)
#ifdef __KERNEL__

#include <linux/types.h>

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
		tc_pad1 : 8;	/* ITLB: width of space-registers (encoded) */
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

#endif /* __KERNEL__ */

#define PAGE0   ((struct zeropage *)__PAGE_OFFSET)

/* DEFINITION OF THE ZERO-PAGE (PAG0) */
/* based on work by Jason Eckhardt (jason@equator.com) */

/* flags of the device_path */
#define	PF_AUTOBOOT	0x80
#define	PF_AUTOSEARCH	0x40
#define	PF_TIMER	0x0F

struct device_path {		/* page 1-69 */
	unsigned char flags;	/* flags see above! */
	unsigned char bc[6];	/* bus converter routing info */
	unsigned char mod;
	unsigned int  layers[6];/* device-specific layer-info */
} __attribute__((aligned(8))) ;

struct pz_device {
	struct	device_path dp;	/* see above */
	/* struct	iomod *hpa; */
	unsigned int hpa;	/* HPA base address */
	/* char	*spa; */
	unsigned int spa;	/* SPA base address */
	/* int	(*iodc_io)(struct iomod*, ...); */
	unsigned int iodc_io;	/* device entry point */
	short	pad;		/* reserved */
	unsigned short cl_class;/* see below */
} __attribute__((aligned(8))) ;

struct zeropage {
	/* [0x000] initialize vectors (VEC) */
	unsigned int	vec_special;		/* must be zero */
	/* int	(*vec_pow_fail)(void);*/
	unsigned int	vec_pow_fail; /* power failure handler */
	/* int	(*vec_toc)(void); */
	unsigned int	vec_toc;
	unsigned int	vec_toclen;
	/* int	(*vec_rendz)(void); */
	unsigned int vec_rendz;
	int	vec_pow_fail_flen;
	int	vec_pad[10];		
	
	/* [0x040] reserved processor dependent */
	int	pad0[112];

	/* [0x200] reserved */
	int	pad1[84];

	/* [0x350] memory configuration (MC) */
	int	memc_cont;		/* contiguous mem size (bytes) */
	int	memc_phsize;		/* physical memory size */
	int	memc_adsize;		/* additional mem size, bytes of SPA space used by PDC */
	unsigned int mem_pdc_hi;	/* used for 64-bit */

	/* [0x360] various parameters for the boot-CPU */
	/* unsigned int *mem_booterr[8]; */
	unsigned int mem_booterr[8];	/* ptr to boot errors */
	unsigned int mem_free;		/* first location, where OS can be loaded */
	/* struct iomod *mem_hpa; */
	unsigned int mem_hpa;		/* HPA of the boot-CPU */
	/* int (*mem_pdc)(int, ...); */
	unsigned int mem_pdc;		/* PDC entry point */
	unsigned int mem_10msec;	/* number of clock ticks in 10msec */

	/* [0x390] initial memory module (IMM) */
	/* struct iomod *imm_hpa; */
	unsigned int imm_hpa;		/* HPA of the IMM */
	int	imm_soft_boot;		/* 0 = was hard boot, 1 = was soft boot */
	unsigned int	imm_spa_size;		/* SPA size of the IMM in bytes */
	unsigned int	imm_max_mem;		/* bytes of mem in IMM */

	/* [0x3A0] boot console, display device and keyboard */
	struct pz_device mem_cons;	/* description of console device */
	struct pz_device mem_boot;	/* description of boot device */
	struct pz_device mem_kbd;	/* description of keyboard device */

	/* [0x430] reserved */
	int	pad430[116];

	/* [0x600] processor dependent */
	__u32	pad600[1];
	__u32	proc_sti;		/* pointer to STI ROM */
	__u32	pad608[126];
};

#endif /* !defined(__ASSEMBLY__) */

#endif /* _PARISC_PDC_H */
