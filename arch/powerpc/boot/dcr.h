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

#define SDRAM0_READ(offset) ({\
	mtdcr(DCRN_SDRAM0_CFGADDR, offset); \
	mfdcr(DCRN_SDRAM0_CFGDATA); })
#define SDRAM0_WRITE(offset, data) ({\
	mtdcr(DCRN_SDRAM0_CFGADDR, offset); \
	mtdcr(DCRN_SDRAM0_CFGDATA, data); })

#define 	SDRAM0_B0CR				0x40
#define 	SDRAM0_B1CR				0x44
#define 	SDRAM0_B2CR				0x48
#define 	SDRAM0_B3CR				0x4c

static const unsigned long sdram_bxcr[] = { SDRAM0_B0CR, SDRAM0_B1CR,
					    SDRAM0_B2CR, SDRAM0_B3CR };

#define			SDRAM_CONFIG_BANK_ENABLE        0x00000001
#define			SDRAM_CONFIG_SIZE_MASK          0x000e0000
#define			SDRAM_CONFIG_BANK_SIZE(reg)	\
	(0x00400000 << ((reg & SDRAM_CONFIG_SIZE_MASK) >> 17))

/* 440GP External Bus Controller (EBC) */
#define DCRN_EBC0_CFGADDR				0x012
#define DCRN_EBC0_CFGDATA				0x013
#define   EBC_NUM_BANKS					  8
#define   EBC_B0CR					  0x00
#define   EBC_B1CR					  0x01
#define   EBC_B2CR					  0x02
#define   EBC_B3CR					  0x03
#define   EBC_B4CR					  0x04
#define   EBC_B5CR					  0x05
#define   EBC_B6CR					  0x06
#define   EBC_B7CR					  0x07
#define   EBC_BXCR(n)					  (n)
#define	    EBC_BXCR_BAS				    0xfff00000
#define	    EBC_BXCR_BS				  	    0x000e0000
#define	    EBC_BXCR_BANK_SIZE(reg) \
	(0x100000 << (((reg) & EBC_BXCR_BS) >> 17))
#define	    EBC_BXCR_BU				  	    0x00018000
#define	      EBC_BXCR_BU_OFF			  	      0x00000000
#define	      EBC_BXCR_BU_RO			  	      0x00008000
#define	      EBC_BXCR_BU_WO			  	      0x00010000
#define	      EBC_BXCR_BU_RW			  	      0x00018000
#define	    EBC_BXCR_BW				  	    0x00006000
#define   EBC_B0AP					  0x10
#define   EBC_B1AP					  0x11
#define   EBC_B2AP					  0x12
#define   EBC_B3AP					  0x13
#define   EBC_B4AP					  0x14
#define   EBC_B5AP					  0x15
#define   EBC_B6AP					  0x16
#define   EBC_B7AP					  0x17
#define   EBC_BXAP(n)					  (0x10+(n))
#define   EBC_BEAR					  0x20
#define   EBC_BESR					  0x21
#define   EBC_CFG					  0x23
#define   EBC_CID					  0x24

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

#define DCRN_MAL0_CFG					0x180
#define MAL_RESET 0x80000000

/* 440EP Clock/Power-on Reset regs */
#define DCRN_CPR0_ADDR	0xc
#define DCRN_CPR0_DATA	0xd
#define CPR0_PLLD0	0x60
#define CPR0_OPBD0	0xc0
#define CPR0_PERD0	0xe0
#define CPR0_PRIMBD0	0xa0
#define CPR0_SCPID	0x120
#define CPR0_PLLC0	0x40

/* 405GP Clocking/Power Management/Chip Control regs */
#define DCRN_CPC0_PLLMR 0xb0
#define DCRN_405_CPC0_CR0 0xb1
#define DCRN_405_CPC0_CR1 0xb2
#define DCRN_405_CPC0_PSR 0xb4

/* 405EP Clocking/Power Management/Chip Control regs */
#define DCRN_CPC0_PLLMR0  0xf0
#define DCRN_CPC0_PLLMR1  0xf4
#define DCRN_CPC0_UCR     0xf5

/* 440GX/405EX Clock Control reg */
#define DCRN_CPR0_CLKUPD				0x020
#define DCRN_CPR0_PLLC					0x040
#define DCRN_CPR0_PLLD					0x060
#define DCRN_CPR0_PRIMAD				0x080
#define DCRN_CPR0_PRIMBD				0x0a0
#define DCRN_CPR0_OPBD					0x0c0
#define DCRN_CPR0_PERD					0x0e0
#define DCRN_CPR0_MALD					0x100

#define DCRN_SDR0_CONFIG_ADDR 	0xe
#define DCRN_SDR0_CONFIG_DATA	0xf

/* SDR read/write helper macros */
#define SDR0_READ(offset) ({\
	mtdcr(DCRN_SDR0_CONFIG_ADDR, offset); \
	mfdcr(DCRN_SDR0_CONFIG_DATA); })
#define SDR0_WRITE(offset, data) ({\
	mtdcr(DCRN_SDR0_CONFIG_ADDR, offset); \
	mtdcr(DCRN_SDR0_CONFIG_DATA, data); })

#define DCRN_SDR0_UART0		0x0120
#define DCRN_SDR0_UART1		0x0121
#define DCRN_SDR0_UART2		0x0122
#define DCRN_SDR0_UART3		0x0123


/* CPRs read/write helper macros - based off include/asm-ppc/ibm44x.h */

#define DCRN_CPR0_CFGADDR				0xc
#define DCRN_CPR0_CFGDATA				0xd

#define CPR0_READ(offset) ({\
	mtdcr(DCRN_CPR0_CFGADDR, offset); \
	mfdcr(DCRN_CPR0_CFGDATA); })
#define CPR0_WRITE(offset, data) ({\
	mtdcr(DCRN_CPR0_CFGADDR, offset); \
	mtdcr(DCRN_CPR0_CFGDATA, data); })



#endif	/* _PPC_BOOT_DCR_H_ */
