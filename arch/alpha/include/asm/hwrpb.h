#ifndef __ALPHA_HWRPB_H
#define __ALPHA_HWRPB_H

#define INIT_HWRPB ((struct hwrpb_struct *) 0x10000000)

/*
 * DEC processor types for Alpha systems.  Found in HWRPB.
 * These values are architected.
 */

#define EV3_CPU                 1       /* EV3                  */
#define EV4_CPU                 2       /* EV4 (21064)          */
#define LCA4_CPU                4       /* LCA4 (21066/21068)   */
#define EV5_CPU                 5       /* EV5 (21164)          */
#define EV45_CPU                6       /* EV4.5 (21064/xxx)    */
#define EV56_CPU		7	/* EV5.6 (21164)	*/
#define EV6_CPU			8	/* EV6 (21264)		*/
#define PCA56_CPU		9	/* PCA56 (21164PC)	*/
#define PCA57_CPU		10	/* PCA57 (notyet)	*/
#define EV67_CPU		11	/* EV67 (21264A)	*/
#define EV68CB_CPU		12	/* EV68CB (21264C)	*/
#define EV68AL_CPU		13	/* EV68AL (21264B)	*/
#define EV68CX_CPU		14	/* EV68CX (21264D)	*/
#define EV7_CPU			15	/* EV7 (21364)		*/
#define EV79_CPU		16	/* EV79 (21364??)	*/
#define EV69_CPU		17	/* EV69 (21264/EV69A)	*/

/*
 * DEC system types for Alpha systems.  Found in HWRPB.
 * These values are architected.
 */

#define ST_ADU			  1	/* Alpha ADU systype	*/
#define ST_DEC_4000		  2	/* Cobra systype	*/
#define ST_DEC_7000		  3	/* Ruby systype		*/
#define ST_DEC_3000_500		  4	/* Flamingo systype	*/
#define ST_DEC_2000_300		  6	/* Jensen systype	*/
#define ST_DEC_3000_300		  7	/* Pelican systype	*/
#define ST_DEC_2100_A500	  9	/* Sable systype	*/
#define ST_DEC_AXPVME_64	 10	/* AXPvme system type	*/
#define ST_DEC_AXPPCI_33	 11	/* NoName system type	*/
#define ST_DEC_TLASER		 12	/* Turbolaser systype	*/
#define ST_DEC_2100_A50		 13	/* Avanti systype	*/
#define ST_DEC_MUSTANG		 14	/* Mustang systype	*/
#define ST_DEC_ALCOR		 15	/* Alcor (EV5) systype	*/
#define ST_DEC_1000		 17	/* Mikasa systype	*/
#define ST_DEC_EB64		 18	/* EB64 systype		*/
#define ST_DEC_EB66		 19	/* EB66 systype		*/
#define ST_DEC_EB64P		 20	/* EB64+ systype	*/
#define ST_DEC_BURNS		 21	/* laptop systype	*/
#define ST_DEC_RAWHIDE		 22	/* Rawhide systype	*/
#define ST_DEC_K2		 23	/* K2 systype		*/
#define ST_DEC_LYNX		 24	/* Lynx systype		*/
#define ST_DEC_XL		 25	/* Alpha XL systype	*/
#define ST_DEC_EB164		 26	/* EB164 systype	*/
#define ST_DEC_NORITAKE		 27	/* Noritake systype	*/
#define ST_DEC_CORTEX		 28	/* Cortex systype	*/
#define ST_DEC_MIATA		 30	/* Miata systype        */
#define ST_DEC_XXM		 31	/* XXM systype		*/
#define ST_DEC_TAKARA		 32	/* Takara systype	*/
#define ST_DEC_YUKON		 33	/* Yukon systype	*/
#define ST_DEC_TSUNAMI		 34	/* Tsunami systype	*/
#define ST_DEC_WILDFIRE		 35	/* Wildfire systype	*/
#define ST_DEC_CUSCO		 36	/* CUSCO systype	*/
#define ST_DEC_EIGER		 37	/* Eiger systype	*/
#define ST_DEC_TITAN		 38	/* Titan systype	*/
#define ST_DEC_MARVEL		 39	/* Marvel systype	*/

/* UNOFFICIAL!!! */
#define ST_UNOFFICIAL_BIAS	100
#define ST_DTI_RUFFIAN		101	/* RUFFIAN systype	*/

/* Alpha Processor, Inc. systems */
#define ST_API_BIAS		200
#define ST_API_NAUTILUS		201	/* UP1000 systype	*/

struct pcb_struct {
	unsigned long ksp;
	unsigned long usp;
	unsigned long ptbr;
	unsigned int pcc;
	unsigned int asn;
	unsigned long unique;
	unsigned long flags;
	unsigned long res1, res2;
};

struct percpu_struct {
	unsigned long hwpcb[16];
	unsigned long flags;
	unsigned long pal_mem_size;
	unsigned long pal_scratch_size;
	unsigned long pal_mem_pa;
	unsigned long pal_scratch_pa;
	unsigned long pal_revision;
	unsigned long type;
	unsigned long variation;
	unsigned long revision;
	unsigned long serial_no[2];
	unsigned long logout_area_pa;
	unsigned long logout_area_len;
	unsigned long halt_PCBB;
	unsigned long halt_PC;
	unsigned long halt_PS;
	unsigned long halt_arg;
	unsigned long halt_ra;
	unsigned long halt_pv;
	unsigned long halt_reason;
	unsigned long res;
	unsigned long ipc_buffer[21];
	unsigned long palcode_avail[16];
	unsigned long compatibility;
	unsigned long console_data_log_pa;
	unsigned long console_data_log_length;
	unsigned long bcache_info;
};

struct procdesc_struct {
	unsigned long weird_vms_stuff;
	unsigned long address;
};

struct vf_map_struct {
	unsigned long va;
	unsigned long pa;
	unsigned long count;
};

struct crb_struct {
	struct procdesc_struct * dispatch_va;
	struct procdesc_struct * dispatch_pa;
	struct procdesc_struct * fixup_va;
	struct procdesc_struct * fixup_pa;
	/* virtual->physical map */
	unsigned long map_entries;
	unsigned long map_pages;
	struct vf_map_struct map[1];
};

struct memclust_struct {
	unsigned long start_pfn;
	unsigned long numpages;
	unsigned long numtested;
	unsigned long bitmap_va;
	unsigned long bitmap_pa;
	unsigned long bitmap_chksum;
	unsigned long usage;
};

struct memdesc_struct {
	unsigned long chksum;
	unsigned long optional_pa;
	unsigned long numclusters;
	struct memclust_struct cluster[0];
};

struct dsr_struct {
	long smm;			/* SMM nubber used by LMF       */
	unsigned long  lurt_off;	/* offset to LURT table         */
	unsigned long  sysname_off;	/* offset to sysname char count */
};

struct hwrpb_struct {
	unsigned long phys_addr;	/* check: physical address of the hwrpb */
	unsigned long id;		/* check: "HWRPB\0\0\0" */
	unsigned long revision;	
	unsigned long size;		/* size of hwrpb */
	unsigned long cpuid;
	unsigned long pagesize;		/* 8192, I hope */
	unsigned long pa_bits;		/* number of physical address bits */
	unsigned long max_asn;
	unsigned char ssn[16];		/* system serial number: big bother is watching */
	unsigned long sys_type;
	unsigned long sys_variation;
	unsigned long sys_revision;
	unsigned long intr_freq;	/* interval clock frequency * 4096 */
	unsigned long cycle_freq;	/* cycle counter frequency */
	unsigned long vptb;		/* Virtual Page Table Base address */
	unsigned long res1;
	unsigned long tbhb_offset;	/* Translation Buffer Hint Block */
	unsigned long nr_processors;
	unsigned long processor_size;
	unsigned long processor_offset;
	unsigned long ctb_nr;
	unsigned long ctb_size;		/* console terminal block size */
	unsigned long ctbt_offset;	/* console terminal block table offset */
	unsigned long crb_offset;	/* console callback routine block */
	unsigned long mddt_offset;	/* memory data descriptor table */
	unsigned long cdb_offset;	/* configuration data block (or NULL) */
	unsigned long frut_offset;	/* FRU table (or NULL) */
	void (*save_terminal)(unsigned long);
	unsigned long save_terminal_data;
	void (*restore_terminal)(unsigned long);
	unsigned long restore_terminal_data;
	void (*CPU_restart)(unsigned long);
	unsigned long CPU_restart_data;
	unsigned long res2;
	unsigned long res3;
	unsigned long chksum;
	unsigned long rxrdy;
	unsigned long txrdy;
	unsigned long dsr_offset;	/* "Dynamic System Recognition Data Block Table" */
};

#ifdef __KERNEL__

extern struct hwrpb_struct *hwrpb;

static inline void
hwrpb_update_checksum(struct hwrpb_struct *h)
{
	unsigned long sum = 0, *l;
        for (l = (unsigned long *) h; l < (unsigned long *) &h->chksum; ++l)
                sum += *l;
        h->chksum = sum;
}

#endif /* __KERNEL__ */

#endif /* __ALPHA_HWRPB_H */
