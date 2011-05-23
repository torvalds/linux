/*
 * arch/blackfin/lib/ins.S - ins{bwl} using hardware loops
 *
 * Copyright 2004-2008 Analog Devices Inc.
 * Copyright (C) 2005 Bas Vermeulen, BuyWays BV <bas@buyways.nl>
 * Licensed under the GPL-2 or later.
 */

#include <linux/linkage.h>
#include <asm/blackfin.h>

.align 2

#ifdef CONFIG_IPIPE
# define DO_CLI \
	[--sp] = rets; \
	[--sp] = (P5:0); \
	sp += -12; \
	call ___ipipe_disable_root_irqs_hw; \
	sp += 12; \
	(P5:0) = [sp++];
# define CLI_INNER_NOP
#else
# define DO_CLI cli R3;
# define CLI_INNER_NOP nop; nop; nop;
#endif

#ifdef CONFIG_IPIPE
# define DO_STI \
	sp += -12; \
	call ___ipipe_enable_root_irqs_hw; \
	sp += 12; \
2:	rets = [sp++];
#else
# define DO_STI 2: sti R3;
#endif

#ifdef CONFIG_BFIN_INS_LOWOVERHEAD
# define CLI_OUTER DO_CLI;
# define STI_OUTER DO_STI;
# define CLI_INNER 1:
# if ANOMALY_05000416
#  define STI_INNER nop; 2: nop;
# else
#  define STI_INNER 2:
# endif
#else
# define CLI_OUTER
# define STI_OUTER
# define CLI_INNER 1: DO_CLI; CLI_INNER_NOP;
# define STI_INNER DO_STI;
#endif

/*
 * Reads on the Blackfin are speculative. In Blackfin terms, this means they
 * can be interrupted at any time (even after they have been issued on to the
 * external bus), and re-issued after the interrupt occurs.
 *
 * If a FIFO is sitting on the end of the read, it will see two reads,
 * when the core only sees one. The FIFO receives the read which is cancelled,
 * and not delivered to the core.
 *
 * To solve this, interrupts are turned off before reads occur to I/O space.
 * There are 3 versions of all these functions
 *  - turns interrupts off every read (higher overhead, but lower latency)
 *  - turns interrupts off every loop (low overhead, but longer latency)
 *  - DMA version, which do not suffer from this issue. DMA versions have
 *      different name (prefixed by dma_ ), and are located in
 *      ../kernel/bfin_dma_5xx.c
 * Using the dma related functions are recommended for transferring large
 * buffers in/out of FIFOs.
 */

#define COMMON_INS(func, ops) \
ENTRY(_ins##func) \
	P0 = R0;	/* P0 = port */ \
	CLI_OUTER;	/* 3 instructions before first read access */ \
	P1 = R1;	/* P1 = address */ \
	P2 = R2;	/* P2 = count */ \
	SSYNC; \
 \
	LSETUP(1f, 2f) LC0 = P2; \
	CLI_INNER; \
	ops; \
	STI_INNER; \
 \
	STI_OUTER; \
	RTS; \
ENDPROC(_ins##func)

COMMON_INS(l, \
	R0 = [P0]; \
	[P1++] = R0; \
)

COMMON_INS(w, \
	R0 = W[P0]; \
	W[P1++] = R0; \
)

COMMON_INS(w_8, \
	R0 = W[P0]; \
	B[P1++] = R0; \
	R0 = R0 >> 8; \
	B[P1++] = R0; \
)

COMMON_INS(b, \
	R0 = B[P0]; \
	B[P1++] = R0; \
)

COMMON_INS(l_16, \
	R0 = [P0]; \
	W[P1++] = R0; \
	R0 = R0 >> 16; \
	W[P1++] = R0; \
)
