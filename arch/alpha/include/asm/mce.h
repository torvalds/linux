#ifndef __ALPHA_MCE_H
#define __ALPHA_MCE_H

/*
 * This is the logout header that should be common to all platforms
 * (assuming they are running OSF/1 PALcode, I guess).
 */
struct el_common {
	unsigned int	size;		/* size in bytes of logout area */
	unsigned int	sbz1	: 30;	/* should be zero */
	unsigned int	err2	:  1;	/* second error */
	unsigned int	retry	:  1;	/* retry flag */
	unsigned int	proc_offset;	/* processor-specific offset */
	unsigned int	sys_offset;	/* system-specific offset */
	unsigned int	code;		/* machine check code */
	unsigned int	frame_rev;	/* frame revision */
};

/* Machine Check Frame for uncorrectable errors (Large format)
 *      --- This is used to log uncorrectable errors such as
 *          double bit ECC errors.
 *      --- These errors are detected by both processor and systems.
 */
struct el_common_EV5_uncorrectable_mcheck {
        unsigned long   shadow[8];        /* Shadow reg. 8-14, 25           */
        unsigned long   paltemp[24];      /* PAL TEMP REGS.                 */
        unsigned long   exc_addr;         /* Address of excepting instruction*/
        unsigned long   exc_sum;          /* Summary of arithmetic traps.   */
        unsigned long   exc_mask;         /* Exception mask (from exc_sum). */
        unsigned long   pal_base;         /* Base address for PALcode.      */
        unsigned long   isr;              /* Interrupt Status Reg.          */
        unsigned long   icsr;             /* CURRENT SETUP OF EV5 IBOX      */
        unsigned long   ic_perr_stat;     /* I-CACHE Reg. <11> set Data parity
                                                         <12> set TAG parity*/
        unsigned long   dc_perr_stat;     /* D-CACHE error Reg. Bits set to 1:
                                                     <2> Data error in bank 0
                                                     <3> Data error in bank 1
                                                     <4> Tag error in bank 0
                                                     <5> Tag error in bank 1 */
        unsigned long   va;               /* Effective VA of fault or miss. */
        unsigned long   mm_stat;          /* Holds the reason for D-stream 
                                             fault or D-cache parity errors */
        unsigned long   sc_addr;          /* Address that was being accessed
                                             when EV5 detected Secondary cache
                                             failure.                 */
        unsigned long   sc_stat;          /* Helps determine if the error was
                                             TAG/Data parity(Secondary Cache)*/
        unsigned long   bc_tag_addr;      /* Contents of EV5 BC_TAG_ADDR    */
        unsigned long   ei_addr;          /* Physical address of any transfer
                                             that is logged in EV5 EI_STAT */
        unsigned long   fill_syndrome;    /* For correcting ECC errors.     */
        unsigned long   ei_stat;          /* Helps identify reason of any 
                                             processor uncorrectable error
                                             at its external interface.     */
        unsigned long   ld_lock;          /* Contents of EV5 LD_LOCK register*/
};

struct el_common_EV6_mcheck {
	unsigned int FrameSize;		/* Bytes, including this field */
	unsigned int FrameFlags;	/* <31> = Retry, <30> = Second Error */
	unsigned int CpuOffset;		/* Offset to CPU-specific info */
	unsigned int SystemOffset;	/* Offset to system-specific info */
	unsigned int MCHK_Code;
	unsigned int MCHK_Frame_Rev;
	unsigned long I_STAT;		/* EV6 Internal Processor Registers */
	unsigned long DC_STAT;		/* (See the 21264 Spec) */
	unsigned long C_ADDR;
	unsigned long DC1_SYNDROME;
	unsigned long DC0_SYNDROME;
	unsigned long C_STAT;
	unsigned long C_STS;
	unsigned long MM_STAT;
	unsigned long EXC_ADDR;
	unsigned long IER_CM;
	unsigned long ISUM;
	unsigned long RESERVED0;
	unsigned long PAL_BASE;
	unsigned long I_CTL;
	unsigned long PCTX;
};


#endif /* __ALPHA_MCE_H */
