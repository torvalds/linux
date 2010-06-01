/*
 * This table is used by some tools to extract hardware parameters.
 * The table should be included in the kernel and the decompressor.
 * Don't forget to update the tools if you change this table.
 *
 * Copyright (C) 2001 Axis Communications AB
 *
 * Authors:  Mikael Starvik (starvik@axis.com)
 */

#define PA_SET_VALUE ((CONFIG_ETRAX_DEF_R_PORT_PA_DIR << 8) | \
		(CONFIG_ETRAX_DEF_R_PORT_PA_DATA))
#define PB_SET_VALUE ((CONFIG_ETRAX_DEF_R_PORT_PB_CONFIG << 16) | \
		(CONFIG_ETRAX_DEF_R_PORT_PB_DIR << 8) | \
		(CONFIG_ETRAX_DEF_R_PORT_PB_DATA))

	.ascii "HW_PARAM_MAGIC" ; Magic number
	.dword 0xc0004000	; Kernel start address

	; Debug port
#ifdef CONFIG_ETRAX_DEBUG_PORT0
	.dword 0
#elif defined(CONFIG_ETRAX_DEBUG_PORT1)
	.dword 1
#elif defined(CONFIG_ETRAX_DEBUG_PORT2)
	.dword 2
#elif defined(CONFIG_ETRAX_DEBUG_PORT3)
	.dword 3
#else
	.dword 4 ; No debug
#endif

	; SDRAM or EDO DRAM?
#ifdef CONFIG_ETRAX_SDRAM
	.dword 1
#else
	.dword 0
#endif

	; Register values
	.dword R_WAITSTATES
	.dword CONFIG_ETRAX_DEF_R_WAITSTATES
	.dword R_BUS_CONFIG
	.dword CONFIG_ETRAX_DEF_R_BUS_CONFIG
#ifdef CONFIG_ETRAX_SDRAM
	.dword R_SDRAM_CONFIG
	.dword CONFIG_ETRAX_DEF_R_SDRAM_CONFIG
	.dword R_SDRAM_TIMING
	.dword CONFIG_ETRAX_DEF_R_SDRAM_TIMING
#else
	.dword R_DRAM_CONFIG
	.dword CONFIG_ETRAX_DEF_R_DRAM_CONFIG
	.dword R_DRAM_TIMING
	.dword CONFIG_ETRAX_DEF_R_DRAM_TIMING
#endif
	.dword R_PORT_PA_SET
	.dword PA_SET_VALUE
	.dword R_PORT_PB_SET
	.dword PB_SET_VALUE
	.dword 0 ; No more register values
