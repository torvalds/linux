#ifndef _SPARC_ASI_H
#define _SPARC_ASI_H

/* asi.h:  Address Space Identifier values for the sparc.
 *
 * Copyright (C) 1995,1996 David S. Miller (davem@caip.rutgers.edu)
 *
 * Pioneer work for sun4m: Paul Hatchman (paul@sfe.com.au)
 * Joint edition for sun4c+sun4m: Pete A. Zaitcev <zaitcev@ipmce.su>
 */

/* The first batch are for the sun4c. */

#define ASI_NULL1           0x00
#define ASI_NULL2           0x01

/* sun4c and sun4 control registers and mmu/vac ops */
#define ASI_CONTROL         0x02
#define ASI_SEGMAP          0x03
#define ASI_PTE             0x04
#define ASI_HWFLUSHSEG      0x05
#define ASI_HWFLUSHPAGE     0x06
#define ASI_REGMAP          0x06
#define ASI_HWFLUSHCONTEXT  0x07

#define ASI_USERTXT         0x08
#define ASI_KERNELTXT       0x09
#define ASI_USERDATA        0x0a
#define ASI_KERNELDATA      0x0b

/* VAC Cache flushing on sun4c and sun4 */
#define ASI_FLUSHSEG        0x0c
#define ASI_FLUSHPG         0x0d
#define ASI_FLUSHCTX        0x0e

/* SPARCstation-5: only 6 bits are decoded. */
/* wo = Write Only, rw = Read Write;        */
/* ss = Single Size, as = All Sizes;        */
#define ASI_M_RES00         0x00   /* Don't touch... */
#define ASI_M_UNA01         0x01   /* Same here... */
#define ASI_M_MXCC          0x02   /* Access to TI VIKING MXCC registers */
#define ASI_M_FLUSH_PROBE   0x03   /* Reference MMU Flush/Probe; rw, ss */
#ifndef CONFIG_SPARC_LEON
#define ASI_M_MMUREGS       0x04   /* MMU Registers; rw, ss */
#else
#define ASI_M_MMUREGS       0x19
#endif /* CONFIG_SPARC_LEON */
#define ASI_M_TLBDIAG       0x05   /* MMU TLB only Diagnostics */
#define ASI_M_DIAGS         0x06   /* Reference MMU Diagnostics */
#define ASI_M_IODIAG        0x07   /* MMU I/O TLB only Diagnostics */
#define ASI_M_USERTXT       0x08   /* Same as ASI_USERTXT; rw, as */
#define ASI_M_KERNELTXT     0x09   /* Same as ASI_KERNELTXT; rw, as */
#define ASI_M_USERDATA      0x0A   /* Same as ASI_USERDATA; rw, as */
#define ASI_M_KERNELDATA    0x0B   /* Same as ASI_KERNELDATA; rw, as */
#define ASI_M_TXTC_TAG      0x0C   /* Instruction Cache Tag; rw, ss */
#define ASI_M_TXTC_DATA     0x0D   /* Instruction Cache Data; rw, ss */
#define ASI_M_DATAC_TAG     0x0E   /* Data Cache Tag; rw, ss */
#define ASI_M_DATAC_DATA    0x0F   /* Data Cache Data; rw, ss */

/* The following cache flushing ASIs work only with the 'sta'
 * instruction. Results are unpredictable for 'swap' and 'ldstuba',
 * so don't do it.
 */

/* These ASI flushes affect external caches too. */
#define ASI_M_FLUSH_PAGE    0x10   /* Flush I&D Cache Line (page); wo, ss */
#define ASI_M_FLUSH_SEG     0x11   /* Flush I&D Cache Line (seg); wo, ss */
#define ASI_M_FLUSH_REGION  0x12   /* Flush I&D Cache Line (region); wo, ss */
#define ASI_M_FLUSH_CTX     0x13   /* Flush I&D Cache Line (context); wo, ss */
#define ASI_M_FLUSH_USER    0x14   /* Flush I&D Cache Line (user); wo, ss */

/* Block-copy operations are available only on certain V8 cpus. */
#define ASI_M_BCOPY         0x17   /* Block copy */

/* These affect only the ICACHE and are Ross HyperSparc and TurboSparc specific. */
#define ASI_M_IFLUSH_PAGE   0x18   /* Flush I Cache Line (page); wo, ss */
#define ASI_M_IFLUSH_SEG    0x19   /* Flush I Cache Line (seg); wo, ss */
#define ASI_M_IFLUSH_REGION 0x1A   /* Flush I Cache Line (region); wo, ss */
#define ASI_M_IFLUSH_CTX    0x1B   /* Flush I Cache Line (context); wo, ss */
#define ASI_M_IFLUSH_USER   0x1C   /* Flush I Cache Line (user); wo, ss */

/* Block-fill operations are available on certain V8 cpus */
#define ASI_M_BFILL         0x1F

/* This allows direct access to main memory, actually 0x20 to 0x2f are
 * the available ASI's for physical ram pass-through, but I don't have
 * any idea what the other ones do....
 */

#define ASI_M_BYPASS       0x20   /* Reference MMU bypass; rw, as */
#define ASI_M_FBMEM        0x29   /* Graphics card frame buffer access */
#define ASI_M_VMEUS        0x2A   /* VME user 16-bit access */
#define ASI_M_VMEPS        0x2B   /* VME priv 16-bit access */
#define ASI_M_VMEUT        0x2C   /* VME user 32-bit access */
#define ASI_M_VMEPT        0x2D   /* VME priv 32-bit access */
#define ASI_M_SBUS         0x2E   /* Direct SBus access */
#define ASI_M_CTL          0x2F   /* Control Space (ECC and MXCC are here) */


/* This is ROSS HyperSparc only. */
#define ASI_M_FLUSH_IWHOLE 0x31   /* Flush entire ICACHE; wo, ss */

/* Tsunami/Viking/TurboSparc i/d cache flash clear. */
#define ASI_M_IC_FLCLEAR   0x36
#define ASI_M_DC_FLCLEAR   0x37

#define ASI_M_DCDR         0x39   /* Data Cache Diagnostics Register rw, ss */

#define ASI_M_VIKING_TMP1  0x40	  /* Emulation temporary 1 on Viking */
/* only available on SuperSparc I */
/* #define ASI_M_VIKING_TMP2  0x41 */  /* Emulation temporary 2 on Viking */

#define ASI_M_ACTION       0x4c   /* Breakpoint Action Register (GNU/Viking) */

/* V9 Architecture mandary ASIs. */
#define ASI_N			0x04 /* Nucleus				*/
#define ASI_NL			0x0c /* Nucleus, little endian		*/
#define ASI_AIUP		0x10 /* Primary, user			*/
#define ASI_AIUS		0x11 /* Secondary, user			*/
#define ASI_AIUPL		0x18 /* Primary, user, little endian	*/
#define ASI_AIUSL		0x19 /* Secondary, user, little endian	*/
#define ASI_P			0x80 /* Primary, implicit		*/
#define ASI_S			0x81 /* Secondary, implicit		*/
#define ASI_PNF			0x82 /* Primary, no fault		*/
#define ASI_SNF			0x83 /* Secondary, no fault		*/
#define ASI_PL			0x88 /* Primary, implicit, l-endian	*/
#define ASI_SL			0x89 /* Secondary, implicit, l-endian	*/
#define ASI_PNFL		0x8a /* Primary, no fault, l-endian	*/
#define ASI_SNFL		0x8b /* Secondary, no fault, l-endian	*/

/* SpitFire and later extended ASIs.  The "(III)" marker designates
 * UltraSparc-III and later specific ASIs.  The "(CMT)" marker designates
 * Chip Multi Threading specific ASIs.  "(NG)" designates Niagara specific
 * ASIs, "(4V)" designates SUN4V specific ASIs.
 */
#define ASI_PHYS_USE_EC		0x14 /* PADDR, E-cachable		*/
#define ASI_PHYS_BYPASS_EC_E	0x15 /* PADDR, E-bit			*/
#define ASI_BLK_AIUP_4V		0x16 /* (4V) Prim, user, block ld/st	*/
#define ASI_BLK_AIUS_4V		0x17 /* (4V) Sec, user, block ld/st	*/
#define ASI_PHYS_USE_EC_L	0x1c /* PADDR, E-cachable, little endian*/
#define ASI_PHYS_BYPASS_EC_E_L	0x1d /* PADDR, E-bit, little endian	*/
#define ASI_BLK_AIUP_L_4V	0x1e /* (4V) Prim, user, block, l-endian*/
#define ASI_BLK_AIUS_L_4V	0x1f /* (4V) Sec, user, block, l-endian	*/
#define ASI_SCRATCHPAD		0x20 /* (4V) Scratch Pad Registers	*/
#define ASI_MMU			0x21 /* (4V) MMU Context Registers	*/
#define ASI_BLK_INIT_QUAD_LDD_AIUS 0x23 /* (NG) init-store, twin load,
					 * secondary, user
					 */
#define ASI_NUCLEUS_QUAD_LDD	0x24 /* Cachable, qword load		*/
#define ASI_QUEUE		0x25 /* (4V) Interrupt Queue Registers	*/
#define ASI_QUAD_LDD_PHYS_4V	0x26 /* (4V) Physical, qword load	*/
#define ASI_NUCLEUS_QUAD_LDD_L	0x2c /* Cachable, qword load, l-endian 	*/
#define ASI_QUAD_LDD_PHYS_L_4V	0x2e /* (4V) Phys, qword load, l-endian	*/
#define ASI_PCACHE_DATA_STATUS	0x30 /* (III) PCache data stat RAM diag	*/
#define ASI_PCACHE_DATA		0x31 /* (III) PCache data RAM diag	*/
#define ASI_PCACHE_TAG		0x32 /* (III) PCache tag RAM diag	*/
#define ASI_PCACHE_SNOOP_TAG	0x33 /* (III) PCache snoop tag RAM diag	*/
#define ASI_QUAD_LDD_PHYS	0x34 /* (III+) PADDR, qword load	*/
#define ASI_WCACHE_VALID_BITS	0x38 /* (III) WCache Valid Bits diag	*/
#define ASI_WCACHE_DATA		0x39 /* (III) WCache data RAM diag	*/
#define ASI_WCACHE_TAG		0x3a /* (III) WCache tag RAM diag	*/
#define ASI_WCACHE_SNOOP_TAG	0x3b /* (III) WCache snoop tag RAM diag	*/
#define ASI_QUAD_LDD_PHYS_L	0x3c /* (III+) PADDR, qw-load, l-endian	*/
#define ASI_SRAM_FAST_INIT	0x40 /* (III+) Fast SRAM init		*/
#define ASI_CORE_AVAILABLE	0x41 /* (CMT) LP Available		*/
#define ASI_CORE_ENABLE_STAT	0x41 /* (CMT) LP Enable Status		*/
#define ASI_CORE_ENABLE		0x41 /* (CMT) LP Enable RW		*/
#define ASI_XIR_STEERING	0x41 /* (CMT) XIR Steering RW		*/
#define ASI_CORE_RUNNING_RW	0x41 /* (CMT) LP Running RW		*/
#define ASI_CORE_RUNNING_W1S	0x41 /* (CMT) LP Running Write-One Set	*/
#define ASI_CORE_RUNNING_W1C	0x41 /* (CMT) LP Running Write-One Clr	*/
#define ASI_CORE_RUNNING_STAT	0x41 /* (CMT) LP Running Status		*/
#define ASI_CMT_ERROR_STEERING	0x41 /* (CMT) Error Steering RW		*/
#define ASI_DCACHE_INVALIDATE	0x42 /* (III) DCache Invalidate diag	*/
#define ASI_DCACHE_UTAG		0x43 /* (III) DCache uTag diag		*/
#define ASI_DCACHE_SNOOP_TAG	0x44 /* (III) DCache snoop tag RAM diag	*/
#define ASI_LSU_CONTROL		0x45 /* Load-store control unit		*/
#define ASI_DCU_CONTROL_REG	0x45 /* (III) DCache Unit Control reg	*/
#define ASI_DCACHE_DATA		0x46 /* DCache data-ram diag access	*/
#define ASI_DCACHE_TAG		0x47 /* Dcache tag/valid ram diag access*/
#define ASI_INTR_DISPATCH_STAT	0x48 /* IRQ vector dispatch status	*/
#define ASI_INTR_RECEIVE	0x49 /* IRQ vector receive status	*/
#define ASI_UPA_CONFIG		0x4a /* UPA config space		*/
#define ASI_JBUS_CONFIG		0x4a /* (IIIi) JBUS Config Register	*/
#define ASI_SAFARI_CONFIG	0x4a /* (III) Safari Config Register	*/
#define ASI_SAFARI_ADDRESS	0x4a /* (III) Safari Address Register	*/
#define ASI_ESTATE_ERROR_EN	0x4b /* E-cache error enable space	*/
#define ASI_AFSR		0x4c /* Async fault status register	*/
#define ASI_AFAR		0x4d /* Async fault address register	*/
#define ASI_EC_TAG_DATA		0x4e /* E-cache tag/valid ram diag acc	*/
#define ASI_IMMU		0x50 /* Insn-MMU main register space	*/
#define ASI_IMMU_TSB_8KB_PTR	0x51 /* Insn-MMU 8KB TSB pointer reg	*/
#define ASI_IMMU_TSB_64KB_PTR	0x52 /* Insn-MMU 64KB TSB pointer reg	*/
#define ASI_ITLB_DATA_IN	0x54 /* Insn-MMU TLB data in reg	*/
#define ASI_ITLB_DATA_ACCESS	0x55 /* Insn-MMU TLB data access reg	*/
#define ASI_ITLB_TAG_READ	0x56 /* Insn-MMU TLB tag read reg	*/
#define ASI_IMMU_DEMAP		0x57 /* Insn-MMU TLB demap		*/
#define ASI_DMMU		0x58 /* Data-MMU main register space	*/
#define ASI_DMMU_TSB_8KB_PTR	0x59 /* Data-MMU 8KB TSB pointer reg	*/
#define ASI_DMMU_TSB_64KB_PTR	0x5a /* Data-MMU 16KB TSB pointer reg	*/
#define ASI_DMMU_TSB_DIRECT_PTR	0x5b /* Data-MMU TSB direct pointer reg	*/
#define ASI_DTLB_DATA_IN	0x5c /* Data-MMU TLB data in reg	*/
#define ASI_DTLB_DATA_ACCESS	0x5d /* Data-MMU TLB data access reg	*/
#define ASI_DTLB_TAG_READ	0x5e /* Data-MMU TLB tag read reg	*/
#define ASI_DMMU_DEMAP		0x5f /* Data-MMU TLB demap		*/
#define ASI_IIU_INST_TRAP	0x60 /* (III) Instruction Breakpoint	*/
#define ASI_INTR_ID		0x63 /* (CMT) Interrupt ID register	*/
#define ASI_CORE_ID		0x63 /* (CMT) LP ID register		*/
#define ASI_CESR_ID		0x63 /* (CMT) CESR ID register		*/
#define ASI_IC_INSTR		0x66 /* Insn cache instrucion ram diag	*/
#define ASI_IC_TAG		0x67 /* Insn cache tag/valid ram diag 	*/
#define ASI_IC_STAG		0x68 /* (III) Insn cache snoop tag ram	*/
#define ASI_IC_PRE_DECODE	0x6e /* Insn cache pre-decode ram diag	*/
#define ASI_IC_NEXT_FIELD	0x6f /* Insn cache next-field ram diag	*/
#define ASI_BRPRED_ARRAY	0x6f /* (III) Branch Prediction RAM diag*/
#define ASI_BLK_AIUP		0x70 /* Primary, user, block load/store	*/
#define ASI_BLK_AIUS		0x71 /* Secondary, user, block ld/st	*/
#define ASI_MCU_CTRL_REG	0x72 /* (III) Memory controller regs	*/
#define ASI_EC_DATA		0x74 /* (III) E-cache data staging reg	*/
#define ASI_EC_CTRL		0x75 /* (III) E-cache control reg	*/
#define ASI_EC_W		0x76 /* E-cache diag write access	*/
#define ASI_UDB_ERROR_W		0x77 /* External UDB error regs W	*/
#define ASI_UDB_CONTROL_W	0x77 /* External UDB control regs W	*/
#define ASI_INTR_W		0x77 /* IRQ vector dispatch write	*/
#define ASI_INTR_DATAN_W	0x77 /* (III) Out irq vector data reg N	*/
#define ASI_INTR_DISPATCH_W	0x77 /* (III) Interrupt vector dispatch	*/
#define ASI_BLK_AIUPL		0x78 /* Primary, user, little, blk ld/st*/
#define ASI_BLK_AIUSL		0x79 /* Secondary, user, little, blk ld/st*/
#define ASI_EC_R		0x7e /* E-cache diag read access	*/
#define ASI_UDBH_ERROR_R	0x7f /* External UDB error regs rd hi	*/
#define ASI_UDBL_ERROR_R	0x7f /* External UDB error regs rd low	*/
#define ASI_UDBH_CONTROL_R	0x7f /* External UDB control regs rd hi	*/
#define ASI_UDBL_CONTROL_R	0x7f /* External UDB control regs rd low*/
#define ASI_INTR_R		0x7f /* IRQ vector dispatch read	*/
#define ASI_INTR_DATAN_R	0x7f /* (III) In irq vector data reg N	*/
#define ASI_PST8_P		0xc0 /* Primary, 8 8-bit, partial	*/
#define ASI_PST8_S		0xc1 /* Secondary, 8 8-bit, partial	*/
#define ASI_PST16_P		0xc2 /* Primary, 4 16-bit, partial	*/
#define ASI_PST16_S		0xc3 /* Secondary, 4 16-bit, partial	*/
#define ASI_PST32_P		0xc4 /* Primary, 2 32-bit, partial	*/
#define ASI_PST32_S		0xc5 /* Secondary, 2 32-bit, partial	*/
#define ASI_PST8_PL		0xc8 /* Primary, 8 8-bit, partial, L	*/
#define ASI_PST8_SL		0xc9 /* Secondary, 8 8-bit, partial, L	*/
#define ASI_PST16_PL		0xca /* Primary, 4 16-bit, partial, L	*/
#define ASI_PST16_SL		0xcb /* Secondary, 4 16-bit, partial, L	*/
#define ASI_PST32_PL		0xcc /* Primary, 2 32-bit, partial, L	*/
#define ASI_PST32_SL		0xcd /* Secondary, 2 32-bit, partial, L	*/
#define ASI_FL8_P		0xd0 /* Primary, 1 8-bit, fpu ld/st	*/
#define ASI_FL8_S		0xd1 /* Secondary, 1 8-bit, fpu ld/st	*/
#define ASI_FL16_P		0xd2 /* Primary, 1 16-bit, fpu ld/st	*/
#define ASI_FL16_S		0xd3 /* Secondary, 1 16-bit, fpu ld/st	*/
#define ASI_FL8_PL		0xd8 /* Primary, 1 8-bit, fpu ld/st, L	*/
#define ASI_FL8_SL		0xd9 /* Secondary, 1 8-bit, fpu ld/st, L*/
#define ASI_FL16_PL		0xda /* Primary, 1 16-bit, fpu ld/st, L	*/
#define ASI_FL16_SL		0xdb /* Secondary, 1 16-bit, fpu ld/st,L*/
#define ASI_BLK_COMMIT_P	0xe0 /* Primary, blk store commit	*/
#define ASI_BLK_COMMIT_S	0xe1 /* Secondary, blk store commit	*/
#define ASI_BLK_INIT_QUAD_LDD_P	0xe2 /* (NG) init-store, twin load,
				      * primary, implicit
				      */
#define ASI_BLK_P		0xf0 /* Primary, blk ld/st		*/
#define ASI_BLK_S		0xf1 /* Secondary, blk ld/st		*/
#define ASI_BLK_PL		0xf8 /* Primary, blk ld/st, little	*/
#define ASI_BLK_SL		0xf9 /* Secondary, blk ld/st, little	*/

#endif /* _SPARC_ASI_H */
