/*
 * The Embedded Planet HIOX expansion card definitions.
 * There were a few different versions of these cards, but only
 * the one that escaped real production is defined here.
 *
 * Copyright (c) 2000 Dan Malek (dmalek@jlc.net)
 */
#ifndef __MACH_RPX_HIOX_DEFS
#define __MACH_RPX_HIOX_DEFS

#define HIOX_CSR_ADDR		((uint)0xfac00000)
#define HIOX_CSR_SIZE		((uint)(4 * 1024))
#define HIOX_CSR0_ADDR		HIOX_CSR_ADDR
#define HIOX_CSR4_ADDR		((uint)0xfac00004)

#define HIOX_CSR0_DEFAULT	((uint)0x380f3c00)
#define HIOX_CSR0_ENSCC2	((uint)0x80000000)
#define HIOX_CSR0_ENSMC2	((uint)0x04000000)
#define HIOX_CSR0_ENVDOCLK	((uint)0x02000000)
#define HIOX_CSR0_VDORST_HL	((uint)0x01000000)
#define HIOX_CSR0_RS232SEL	((uint)0x0000c000)
#define HIOX_CSR0_SCC3SEL	((uint)0x0000c000)
#define HIOX_CSR0_SMC1SEL	((uint)0x00008000)
#define HIOX_CSR0_SCC1SEL	((uint)0x00004000)
#define HIOX_CSR0_ENTOUCH	((uint)0x00000080)
#define HIOX_CSR0_PDOWN100	((uint)0x00000060)
#define HIOX_CSR0_PDOWN10	((uint)0x00000040)
#define HIOX_CSR0_PDOWN1	((uint)0x00000020)
#define HIOX_CSR0_TSELSPI	((uint)0x00000010)
#define HIOX_CSR0_TIRQSTAT	((uint)0x00000008)
#define HIOX_CSR4_DEFAULT	((uint)0x00000000)
#define HIOX_CSR4_ENTIRQ2	((uint)0x20000000)
#define HIOX_CSR4_ENTIRQ3	((uint)0x10000000)
#define HIOX_CSR4_ENAUDIO	((uint)0x00000080)
#define HIOX_CSR4_RSTAUDIO	((uint)0x00000040)	/* 0 == reset */
#define HIOX_CSR4_AUDCLKHI	((uint)0x00000020)
#define HIOX_CSR4_AUDSPISEL	((uint)0x00000010)
#define HIOX_CSR4_AUDIRQSTAT	((uint)0x00000008)
#define HIOX_CSR4_AUDCLKSEL	((uint)0x00000007)

#endif
