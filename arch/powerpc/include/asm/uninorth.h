/*
 * uninorth.h: definitions for using the "UniNorth" host bridge chip
 *             from Apple. This chip is used on "Core99" machines
 *	       This also includes U2 used on more recent MacRISC2/3
 *             machines and U3 (G5) 
 *
 */
#ifdef __KERNEL__
#ifndef __ASM_UNINORTH_H__
#define __ASM_UNINORTH_H__

/*
 * Uni-N and U3 config space reg. definitions
 *
 * (Little endian)
 */

/* Address ranges selection. This one should work with Bandit too */
/* Not U3 */
#define UNI_N_ADDR_SELECT		0x48
#define UNI_N_ADDR_COARSE_MASK		0xffff0000	/* 256Mb regions at *0000000 */
#define UNI_N_ADDR_FINE_MASK		0x0000ffff	/*  16Mb regions at f*000000 */

/* AGP registers */
/* Not U3 */
#define UNI_N_CFG_GART_BASE		0x8c
#define UNI_N_CFG_AGP_BASE		0x90
#define UNI_N_CFG_GART_CTRL		0x94
#define UNI_N_CFG_INTERNAL_STATUS	0x98
#define UNI_N_CFG_GART_DUMMY_PAGE	0xa4

/* UNI_N_CFG_GART_CTRL bits definitions */
#define UNI_N_CFG_GART_INVAL		0x00000001
#define UNI_N_CFG_GART_ENABLE		0x00000100
#define UNI_N_CFG_GART_2xRESET		0x00010000
#define UNI_N_CFG_GART_DISSBADET	0x00020000
/* The following seems to only be used only on U3 <j.glisse@gmail.com> */
#define U3_N_CFG_GART_SYNCMODE		0x00040000
#define U3_N_CFG_GART_PERFRD		0x00080000
#define U3_N_CFG_GART_B2BGNT		0x00200000
#define U3_N_CFG_GART_FASTDDR		0x00400000

/* My understanding of UniNorth AGP as of UniNorth rev 1.0x,
 * revision 1.5 (x4 AGP) may need further changes.
 *
 * AGP_BASE register contains the base address of the AGP aperture on
 * the AGP bus. It doesn't seem to be visible to the CPU as of UniNorth 1.x,
 * even if decoding of this address range is enabled in the address select
 * register. Apparently, the only supported bases are 256Mb multiples
 * (high 4 bits of that register).
 *
 * GART_BASE register appear to contain the physical address of the GART
 * in system memory in the high address bits (page aligned), and the
 * GART size in the low order bits (number of GART pages)
 *
 * The GART format itself is one 32bits word per physical memory page.
 * This word contains, in little-endian format (!!!), the physical address
 * of the page in the high bits, and what appears to be an "enable" bit
 * in the LSB bit (0) that must be set to 1 when the entry is valid.
 *
 * Obviously, the GART is not cache coherent and so any change to it
 * must be flushed to memory (or maybe just make the GART space non
 * cachable). AGP memory itself doesn't seem to be cache coherent neither.
 *
 * In order to invalidate the GART (which is probably necessary to inval
 * the bridge internal TLBs), the following sequence has to be written,
 * in order, to the GART_CTRL register:
 *
 *   UNI_N_CFG_GART_ENABLE | UNI_N_CFG_GART_INVAL
 *   UNI_N_CFG_GART_ENABLE
 *   UNI_N_CFG_GART_ENABLE | UNI_N_CFG_GART_2xRESET
 *   UNI_N_CFG_GART_ENABLE
 *
 * As far as AGP "features" are concerned, it looks like fast write may
 * not be supported but this has to be confirmed.
 *
 * Turning on AGP seem to require a double invalidate operation, one before
 * setting the AGP command register, on after.
 *
 * Turning off AGP seems to require the following sequence: first wait
 * for the AGP to be idle by reading the internal status register, then
 * write in that order to the GART_CTRL register:
 *
 *   UNI_N_CFG_GART_ENABLE | UNI_N_CFG_GART_INVAL
 *   0
 *   UNI_N_CFG_GART_2xRESET
 *   0
 */

/*
 * Uni-N memory mapped reg. definitions
 *
 * Those registers are Big-Endian !!
 *
 * Their meaning come from either Darwin and/or from experiments I made with
 * the bootrom, I'm not sure about their exact meaning yet
 *
 */

/* Version of the UniNorth chip */
#define UNI_N_VERSION			0x0000		/* Known versions: 3,7 and 8 */

#define UNI_N_VERSION_107		0x0003		/* 1.0.7 */
#define UNI_N_VERSION_10A		0x0007		/* 1.0.10 */
#define UNI_N_VERSION_150		0x0011		/* 1.5 */
#define UNI_N_VERSION_200		0x0024		/* 2.0 */
#define UNI_N_VERSION_PANGEA		0x00C0		/* Integrated U1 + K */
#define UNI_N_VERSION_INTREPID		0x00D2		/* Integrated U2 + K */
#define UNI_N_VERSION_300		0x0030		/* 3.0 (U3 on G5) */

/* This register is used to enable/disable various clocks */
#define UNI_N_CLOCK_CNTL		0x0020
#define UNI_N_CLOCK_CNTL_PCI		0x00000001	/* PCI2 clock control */
#define UNI_N_CLOCK_CNTL_GMAC		0x00000002	/* GMAC clock control */
#define UNI_N_CLOCK_CNTL_FW		0x00000004	/* FireWire clock control */
#define UNI_N_CLOCK_CNTL_ATA100		0x00000010	/* ATA-100 clock control (U2) */

/* Power Management control */
#define UNI_N_POWER_MGT			0x0030
#define UNI_N_POWER_MGT_NORMAL		0x00
#define UNI_N_POWER_MGT_IDLE2		0x01
#define UNI_N_POWER_MGT_SLEEP		0x02

/* This register is configured by Darwin depending on the UniN
 * revision
 */
#define UNI_N_ARB_CTRL			0x0040
#define UNI_N_ARB_CTRL_QACK_DELAY_SHIFT	15
#define UNI_N_ARB_CTRL_QACK_DELAY_MASK	0x0e1f8000
#define UNI_N_ARB_CTRL_QACK_DELAY	0x30
#define UNI_N_ARB_CTRL_QACK_DELAY105	0x00

/* This one _might_ return the CPU number of the CPU reading it;
 * the bootROM decides whether to boot or to sleep/spinloop depending
 * on this register being 0 or not
 */
#define UNI_N_CPU_NUMBER		0x0050

/* This register appear to be read by the bootROM to decide what
 *  to do on a non-recoverable reset (powerup or wakeup)
 */
#define UNI_N_HWINIT_STATE		0x0070
#define UNI_N_HWINIT_STATE_SLEEPING	0x01
#define UNI_N_HWINIT_STATE_RUNNING	0x02
/* This last bit appear to be used by the bootROM to know the second
 * CPU has started and will enter it's sleep loop with IP=0
 */
#define UNI_N_HWINIT_STATE_CPU1_FLAG	0x10000000

/* This register controls AACK delay, which is set when 2004 iBook/PowerBook
 * is in low speed mode.
 */
#define UNI_N_AACK_DELAY		0x0100
#define UNI_N_AACK_DELAY_ENABLE		0x00000001

/* Clock status for Intrepid */
#define UNI_N_CLOCK_STOP_STATUS0	0x0150
#define UNI_N_CLOCK_STOPPED_EXTAGP	0x00200000
#define UNI_N_CLOCK_STOPPED_AGPDEL	0x00100000
#define UNI_N_CLOCK_STOPPED_I2S0_45_49	0x00080000
#define UNI_N_CLOCK_STOPPED_I2S0_18	0x00040000
#define UNI_N_CLOCK_STOPPED_I2S1_45_49	0x00020000
#define UNI_N_CLOCK_STOPPED_I2S1_18	0x00010000
#define UNI_N_CLOCK_STOPPED_TIMER	0x00008000
#define UNI_N_CLOCK_STOPPED_SCC_RTCLK18	0x00004000
#define UNI_N_CLOCK_STOPPED_SCC_RTCLK32	0x00002000
#define UNI_N_CLOCK_STOPPED_SCC_VIA32	0x00001000
#define UNI_N_CLOCK_STOPPED_SCC_SLOT0	0x00000800
#define UNI_N_CLOCK_STOPPED_SCC_SLOT1	0x00000400
#define UNI_N_CLOCK_STOPPED_SCC_SLOT2	0x00000200
#define UNI_N_CLOCK_STOPPED_PCI_FBCLKO	0x00000100
#define UNI_N_CLOCK_STOPPED_VEO0	0x00000080
#define UNI_N_CLOCK_STOPPED_VEO1	0x00000040
#define UNI_N_CLOCK_STOPPED_USB0	0x00000020
#define UNI_N_CLOCK_STOPPED_USB1	0x00000010
#define UNI_N_CLOCK_STOPPED_USB2	0x00000008
#define UNI_N_CLOCK_STOPPED_32		0x00000004
#define UNI_N_CLOCK_STOPPED_45		0x00000002
#define UNI_N_CLOCK_STOPPED_49		0x00000001

#define UNI_N_CLOCK_STOP_STATUS1	0x0160
#define UNI_N_CLOCK_STOPPED_PLL4REF	0x00080000
#define UNI_N_CLOCK_STOPPED_CPUDEL	0x00040000
#define UNI_N_CLOCK_STOPPED_CPU		0x00020000
#define UNI_N_CLOCK_STOPPED_BUF_REFCKO	0x00010000
#define UNI_N_CLOCK_STOPPED_PCI2	0x00008000
#define UNI_N_CLOCK_STOPPED_FW		0x00004000
#define UNI_N_CLOCK_STOPPED_GB		0x00002000
#define UNI_N_CLOCK_STOPPED_ATA66	0x00001000
#define UNI_N_CLOCK_STOPPED_ATA100	0x00000800
#define UNI_N_CLOCK_STOPPED_MAX		0x00000400
#define UNI_N_CLOCK_STOPPED_PCI1	0x00000200
#define UNI_N_CLOCK_STOPPED_KLPCI	0x00000100
#define UNI_N_CLOCK_STOPPED_USB0PCI	0x00000080
#define UNI_N_CLOCK_STOPPED_USB1PCI	0x00000040
#define UNI_N_CLOCK_STOPPED_USB2PCI	0x00000020
#define UNI_N_CLOCK_STOPPED_7PCI1	0x00000008
#define UNI_N_CLOCK_STOPPED_AGP		0x00000004
#define UNI_N_CLOCK_STOPPED_PCI0	0x00000002
#define UNI_N_CLOCK_STOPPED_18		0x00000001

/* Intrepid registe to OF do-platform-clockspreading */
#define UNI_N_CLOCK_SPREADING		0x190

/* Uninorth 1.5 rev. has additional perf. monitor registers at 0xf00-0xf50 */


/*
 * U3 specific registers
 */


/* U3 Toggle */
#define U3_TOGGLE_REG			0x00e0
#define U3_PMC_START_STOP		0x0001
#define U3_MPIC_RESET			0x0002
#define U3_MPIC_OUTPUT_ENABLE		0x0004

/* U3 API PHY Config 1 */
#define U3_API_PHY_CONFIG_1		0x23030

/* U3 HyperTransport registers */
#define U3_HT_CONFIG_BASE      		0x70000
#define U3_HT_LINK_COMMAND		0x100
#define U3_HT_LINK_CONFIG		0x110
#define U3_HT_LINK_FREQ			0x120

#endif /* __ASM_UNINORTH_H__ */
#endif /* __KERNEL__ */
