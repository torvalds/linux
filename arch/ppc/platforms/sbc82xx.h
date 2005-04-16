/* Board information for the SBCPowerQUICCII, which should be generic for
 * all 8260 boards.  The IMMR is now given to us so the hard define
 * will soon be removed.  All of the clock values are computed from
 * the configuration SCMR and the Power-On-Reset word.
 */

#ifndef __PPC_SBC82xx_H__
#define __PPC_SBC82xx_H__

#include <asm/ppcboot.h>

#define CPM_MAP_ADDR			0xf0000000

#define SBC82xx_TODC_NVRAM_ADDR		0xd0000000

#define SBC82xx_MACADDR_NVRAM_FCC1	0x220000c9	/* JP6B */
#define SBC82xx_MACADDR_NVRAM_SCC1	0x220000cf	/* JP6A */
#define SBC82xx_MACADDR_NVRAM_FCC2	0x220000d5	/* JP7A */
#define SBC82xx_MACADDR_NVRAM_FCC3	0x220000db	/* JP7B */

/* For our show_cpuinfo hooks. */
#define CPUINFO_VENDOR		"Wind River"
#define CPUINFO_MACHINE		"SBC PowerQUICC II"

#define BOOTROM_RESTART_ADDR      ((uint)0x40000104)

#define SBC82xx_PC_IRQA (NR_SIU_INTS+0)
#define SBC82xx_PC_IRQB (NR_SIU_INTS+1)
#define SBC82xx_MPC185_IRQ (NR_SIU_INTS+2)
#define SBC82xx_ATM_IRQ (NR_SIU_INTS+3)
#define SBC82xx_PIRQA (NR_SIU_INTS+4)
#define SBC82xx_PIRQB (NR_SIU_INTS+5)
#define SBC82xx_PIRQC (NR_SIU_INTS+6)
#define SBC82xx_PIRQD (NR_SIU_INTS+7)

#endif /* __PPC_SBC82xx_H__ */
