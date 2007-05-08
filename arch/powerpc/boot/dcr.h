#ifndef _PPC_BOOT_DCR_H_
#define _PPC_BOOT_DCR_H_

#define mfdcr(rn) \
	({	\
		unsigned long rval; \
		asm volatile("mfdcr %0,%1" : "=r"(rval) : "i"(rn)); \
		rval; \
	})
#define mtdcr(rn, val) \
	asm volatile("mtdcr %0,%1" : : "i"(rn), "r"(val))

/* 440GP/440GX SDRAM controller DCRs */
#define DCRN_SDRAM0_CFGADDR				0x010
#define DCRN_SDRAM0_CFGDATA				0x011

#define 	SDRAM0_B0CR				0x40
#define 	SDRAM0_B1CR				0x44
#define 	SDRAM0_B2CR				0x48
#define 	SDRAM0_B3CR				0x4c

static const unsigned long sdram_bxcr[] = { SDRAM0_B0CR, SDRAM0_B1CR, SDRAM0_B2CR, SDRAM0_B3CR };

#define			SDRAM_CONFIG_BANK_ENABLE        0x00000001
#define			SDRAM_CONFIG_SIZE_MASK          0x000e0000
#define			SDRAM_CONFIG_BANK_SIZE(reg)	\
	(0x00400000 << ((reg & SDRAM_CONFIG_SIZE_MASK) >> 17))

/* 440GP Clock, PM, chip control */
#define DCRN_CPC0_SR					0x0b0
#define DCRN_CPC0_ER					0x0b1
#define DCRN_CPC0_FR					0x0b2
#define DCRN_CPC0_SYS0					0x0e0
#define	  CPC0_SYS0_TUNE				  0xffc00000
#define	  CPC0_SYS0_FBDV_MASK				  0x003c0000
#define	  CPC0_SYS0_FWDVA_MASK				  0x00038000
#define	  CPC0_SYS0_FWDVB_MASK				  0x00007000
#define	  CPC0_SYS0_OPDV_MASK				  0x00000c00
#define	  CPC0_SYS0_EPDV_MASK				  0x00000300
/* Helper macros to compute the actual clock divider values from the
 * encodings in the CPC0 register */
#define	  CPC0_SYS0_FBDV(reg) \
		((((((reg) & CPC0_SYS0_FBDV_MASK) >> 18) - 1) & 0xf) + 1)
#define	  CPC0_SYS0_FWDVA(reg) \
		(8 - (((reg) & CPC0_SYS0_FWDVA_MASK) >> 15))
#define	  CPC0_SYS0_FWDVB(reg) \
		(8 - (((reg) & CPC0_SYS0_FWDVB_MASK) >> 12))
#define	  CPC0_SYS0_OPDV(reg) \
		((((reg) & CPC0_SYS0_OPDV_MASK) >> 10) + 1)
#define	  CPC0_SYS0_EPDV(reg) \
		((((reg) & CPC0_SYS0_EPDV_MASK) >> 8) + 1)
#define	  CPC0_SYS0_EXTSL				  0x00000080
#define	  CPC0_SYS0_RW_MASK				  0x00000060
#define	  CPC0_SYS0_RL					  0x00000010
#define	  CPC0_SYS0_ZMIISL_MASK				  0x0000000c
#define	  CPC0_SYS0_BYPASS				  0x00000002
#define	  CPC0_SYS0_NTO1				  0x00000001
#define DCRN_CPC0_SYS1					0x0e1
#define DCRN_CPC0_CUST0					0x0e2
#define DCRN_CPC0_CUST1					0x0e3
#define DCRN_CPC0_STRP0					0x0e4
#define DCRN_CPC0_STRP1					0x0e5
#define DCRN_CPC0_STRP2					0x0e6
#define DCRN_CPC0_STRP3					0x0e7
#define DCRN_CPC0_GPIO					0x0e8
#define DCRN_CPC0_PLB					0x0e9
#define DCRN_CPC0_CR1					0x0ea
#define DCRN_CPC0_CR0					0x0eb
#define	  CPC0_CR0_SWE					  0x80000000
#define	  CPC0_CR0_CETE					  0x40000000
#define	  CPC0_CR0_U1FCS				  0x20000000
#define	  CPC0_CR0_U0DTE				  0x10000000
#define	  CPC0_CR0_U0DRE				  0x08000000
#define	  CPC0_CR0_U0DC					  0x04000000
#define	  CPC0_CR0_U1DTE				  0x02000000
#define	  CPC0_CR0_U1DRE				  0x01000000
#define	  CPC0_CR0_U1DC					  0x00800000
#define	  CPC0_CR0_U0EC					  0x00400000
#define	  CPC0_CR0_U1EC					  0x00200000
#define	  CPC0_CR0_UDIV_MASK				  0x001f0000
#define	  CPC0_CR0_UDIV(reg) \
		((((reg) & CPC0_CR0_UDIV_MASK) >> 16) + 1)
#define DCRN_CPC0_MIRQ0					0x0ec
#define DCRN_CPC0_MIRQ1					0x0ed
#define DCRN_CPC0_JTAGID				0x0ef

#endif	/* _PPC_BOOT_DCR_H_ */
