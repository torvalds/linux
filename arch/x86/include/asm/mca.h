/* -*- mode: c; c-basic-offset: 8 -*- */

/* Platform specific MCA defines */
#ifndef ASM_X86__MCA_H
#define ASM_X86__MCA_H

/* Maximal number of MCA slots - actually, some machines have less, but
 * they all have sufficient number of POS registers to cover 8.
 */
#define MCA_MAX_SLOT_NR  8

/* Most machines have only one MCA bus.  The only multiple bus machines
 * I know have at most two */
#define MAX_MCA_BUSSES 2

#define MCA_PRIMARY_BUS		0
#define MCA_SECONDARY_BUS	1

/* Dummy slot numbers on primary MCA for integrated functions */
#define MCA_INTEGSCSI	(MCA_MAX_SLOT_NR)
#define MCA_INTEGVIDEO	(MCA_MAX_SLOT_NR+1)
#define MCA_MOTHERBOARD (MCA_MAX_SLOT_NR+2)

/* Dummy POS values for integrated functions */
#define MCA_DUMMY_POS_START	0x10000
#define MCA_INTEGSCSI_POS	(MCA_DUMMY_POS_START+1)
#define MCA_INTEGVIDEO_POS	(MCA_DUMMY_POS_START+2)
#define MCA_MOTHERBOARD_POS	(MCA_DUMMY_POS_START+3)

/* MCA registers */

#define MCA_MOTHERBOARD_SETUP_REG	0x94
#define MCA_ADAPTER_SETUP_REG		0x96
#define MCA_POS_REG(n)			(0x100+(n))

#define MCA_ENABLED	0x01	/* POS 2, set if adapter enabled */

/* Max number of adapters, including both slots and various integrated
 * things.
 */
#define MCA_NUMADAPTERS (MCA_MAX_SLOT_NR+3)

#endif /* ASM_X86__MCA_H */
