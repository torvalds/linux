/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  This file contains the hardware definitions of the Integrator.
 *
 *  Copyright (C) 1998-1999 ARM Limited.
 */
#ifndef INTEGRATOR_HARDWARE_H
#define INTEGRATOR_HARDWARE_H

/*
 * Where in virtual memory the IO devices (timers, system controllers
 * and so on)
 */
#define IO_BASE			0xF0000000                 // VA of IO
#define IO_SIZE			0x0B000000                 // How much?
#define IO_START		INTEGRATOR_HDR_BASE        // PA of IO

/* macro to get at IO space when running virtually */
#ifdef CONFIG_MMU
#define IO_ADDRESS(x)	(((x) & 0x000fffff) | (((x) >> 4) & 0x0ff00000) | IO_BASE)
#else
#define IO_ADDRESS(x)	(x)
#endif

#define __io_address(n)		((void __iomem *)IO_ADDRESS(n))

/*
 *  Integrator memory map
 */
#define INTEGRATOR_BOOT_ROM_LO          0x00000000
#define INTEGRATOR_BOOT_ROM_HI          0x20000000
#define INTEGRATOR_BOOT_ROM_BASE        INTEGRATOR_BOOT_ROM_HI	 /*  Normal position */
#define INTEGRATOR_BOOT_ROM_SIZE        SZ_512K

/*
 * New Core Modules have different amounts of SSRAM, the amount of SSRAM
 * fitted can be found in HDR_STAT.
 *
 * The symbol INTEGRATOR_SSRAM_SIZE is kept, however this now refers to
 * the minimum amount of SSRAM fitted on any core module.
 *
 * New Core Modules also alias the SSRAM.
 *
 */
#define INTEGRATOR_SSRAM_BASE           0x00000000
#define INTEGRATOR_SSRAM_ALIAS_BASE     0x10800000
#define INTEGRATOR_SSRAM_SIZE           SZ_256K

#define INTEGRATOR_FLASH_BASE           0x24000000
#define INTEGRATOR_FLASH_SIZE           SZ_32M

#define INTEGRATOR_MBRD_SSRAM_BASE      0x28000000
#define INTEGRATOR_MBRD_SSRAM_SIZE      SZ_512K

/*
 *  SDRAM is a SIMM therefore the size is not known.
 */
#define INTEGRATOR_SDRAM_BASE           0x00040000

#define INTEGRATOR_SDRAM_ALIAS_BASE     0x80000000
#define INTEGRATOR_HDR0_SDRAM_BASE      0x80000000
#define INTEGRATOR_HDR1_SDRAM_BASE      0x90000000
#define INTEGRATOR_HDR2_SDRAM_BASE      0xA0000000
#define INTEGRATOR_HDR3_SDRAM_BASE      0xB0000000

/*
 *  Logic expansion modules
 *
 */
#define INTEGRATOR_LOGIC_MODULES_BASE   0xC0000000
#define INTEGRATOR_LOGIC_MODULE0_BASE   0xC0000000
#define INTEGRATOR_LOGIC_MODULE1_BASE   0xD0000000
#define INTEGRATOR_LOGIC_MODULE2_BASE   0xE0000000
#define INTEGRATOR_LOGIC_MODULE3_BASE   0xF0000000

/*
 * Integrator header card registers
 */
#define INTEGRATOR_HDR_ID_OFFSET        0x00
#define INTEGRATOR_HDR_PROC_OFFSET      0x04
#define INTEGRATOR_HDR_OSC_OFFSET       0x08
#define INTEGRATOR_HDR_CTRL_OFFSET      0x0C
#define INTEGRATOR_HDR_STAT_OFFSET      0x10
#define INTEGRATOR_HDR_LOCK_OFFSET      0x14
#define INTEGRATOR_HDR_SDRAM_OFFSET     0x20
#define INTEGRATOR_HDR_INIT_OFFSET      0x24	 /*  CM9x6 */
#define INTEGRATOR_HDR_IC_OFFSET        0x40
#define INTEGRATOR_HDR_SPDBASE_OFFSET   0x100
#define INTEGRATOR_HDR_SPDTOP_OFFSET    0x200

#define INTEGRATOR_HDR_BASE             0x10000000
#define INTEGRATOR_HDR_ID               (INTEGRATOR_HDR_BASE + INTEGRATOR_HDR_ID_OFFSET)
#define INTEGRATOR_HDR_PROC             (INTEGRATOR_HDR_BASE + INTEGRATOR_HDR_PROC_OFFSET)
#define INTEGRATOR_HDR_OSC              (INTEGRATOR_HDR_BASE + INTEGRATOR_HDR_OSC_OFFSET)
#define INTEGRATOR_HDR_CTRL             (INTEGRATOR_HDR_BASE + INTEGRATOR_HDR_CTRL_OFFSET)
#define INTEGRATOR_HDR_STAT             (INTEGRATOR_HDR_BASE + INTEGRATOR_HDR_STAT_OFFSET)
#define INTEGRATOR_HDR_LOCK             (INTEGRATOR_HDR_BASE + INTEGRATOR_HDR_LOCK_OFFSET)
#define INTEGRATOR_HDR_SDRAM            (INTEGRATOR_HDR_BASE + INTEGRATOR_HDR_SDRAM_OFFSET)
#define INTEGRATOR_HDR_INIT             (INTEGRATOR_HDR_BASE + INTEGRATOR_HDR_INIT_OFFSET)
#define INTEGRATOR_HDR_IC               (INTEGRATOR_HDR_BASE + INTEGRATOR_HDR_IC_OFFSET)
#define INTEGRATOR_HDR_SPDBASE          (INTEGRATOR_HDR_BASE + INTEGRATOR_HDR_SPDBASE_OFFSET)
#define INTEGRATOR_HDR_SPDTOP           (INTEGRATOR_HDR_BASE + INTEGRATOR_HDR_SPDTOP_OFFSET)

#define INTEGRATOR_HDR_CTRL_LED         0x01
#define INTEGRATOR_HDR_CTRL_MBRD_DETECH 0x02
#define INTEGRATOR_HDR_CTRL_REMAP       0x04
#define INTEGRATOR_HDR_CTRL_RESET       0x08
#define INTEGRATOR_HDR_CTRL_HIGHVECTORS 0x10
#define INTEGRATOR_HDR_CTRL_BIG_ENDIAN  0x20
#define INTEGRATOR_HDR_CTRL_FASTBUS     0x40
#define INTEGRATOR_HDR_CTRL_SYNC        0x80

#define INTEGRATOR_HDR_OSC_CORE_10MHz   0x102
#define INTEGRATOR_HDR_OSC_CORE_15MHz   0x107
#define INTEGRATOR_HDR_OSC_CORE_20MHz   0x10C
#define INTEGRATOR_HDR_OSC_CORE_25MHz   0x111
#define INTEGRATOR_HDR_OSC_CORE_30MHz   0x116
#define INTEGRATOR_HDR_OSC_CORE_35MHz   0x11B
#define INTEGRATOR_HDR_OSC_CORE_40MHz   0x120
#define INTEGRATOR_HDR_OSC_CORE_45MHz   0x125
#define INTEGRATOR_HDR_OSC_CORE_50MHz   0x12A
#define INTEGRATOR_HDR_OSC_CORE_55MHz   0x12F
#define INTEGRATOR_HDR_OSC_CORE_60MHz   0x134
#define INTEGRATOR_HDR_OSC_CORE_65MHz   0x139
#define INTEGRATOR_HDR_OSC_CORE_70MHz   0x13E
#define INTEGRATOR_HDR_OSC_CORE_75MHz   0x143
#define INTEGRATOR_HDR_OSC_CORE_80MHz   0x148
#define INTEGRATOR_HDR_OSC_CORE_85MHz   0x14D
#define INTEGRATOR_HDR_OSC_CORE_90MHz   0x152
#define INTEGRATOR_HDR_OSC_CORE_95MHz   0x157
#define INTEGRATOR_HDR_OSC_CORE_100MHz  0x15C
#define INTEGRATOR_HDR_OSC_CORE_105MHz  0x161
#define INTEGRATOR_HDR_OSC_CORE_110MHz  0x166
#define INTEGRATOR_HDR_OSC_CORE_115MHz  0x16B
#define INTEGRATOR_HDR_OSC_CORE_120MHz  0x170
#define INTEGRATOR_HDR_OSC_CORE_125MHz  0x175
#define INTEGRATOR_HDR_OSC_CORE_130MHz  0x17A
#define INTEGRATOR_HDR_OSC_CORE_135MHz  0x17F
#define INTEGRATOR_HDR_OSC_CORE_140MHz  0x184
#define INTEGRATOR_HDR_OSC_CORE_145MHz  0x189
#define INTEGRATOR_HDR_OSC_CORE_150MHz  0x18E
#define INTEGRATOR_HDR_OSC_CORE_155MHz  0x193
#define INTEGRATOR_HDR_OSC_CORE_160MHz  0x198
#define INTEGRATOR_HDR_OSC_CORE_MASK    0x7FF

#define INTEGRATOR_HDR_OSC_MEM_10MHz    0x10C000
#define INTEGRATOR_HDR_OSC_MEM_15MHz    0x116000
#define INTEGRATOR_HDR_OSC_MEM_20MHz    0x120000
#define INTEGRATOR_HDR_OSC_MEM_25MHz    0x12A000
#define INTEGRATOR_HDR_OSC_MEM_30MHz    0x134000
#define INTEGRATOR_HDR_OSC_MEM_33MHz    0x13A000
#define INTEGRATOR_HDR_OSC_MEM_40MHz    0x148000
#define INTEGRATOR_HDR_OSC_MEM_50MHz    0x15C000
#define INTEGRATOR_HDR_OSC_MEM_60MHz    0x170000
#define INTEGRATOR_HDR_OSC_MEM_66MHz    0x17C000
#define INTEGRATOR_HDR_OSC_MEM_MASK     0x7FF000

#define INTEGRATOR_HDR_OSC_BUS_MODE_CM7x0  0x0
#define INTEGRATOR_HDR_OSC_BUS_MODE_CM9x0  0x0800000
#define INTEGRATOR_HDR_OSC_BUS_MODE_CM9x6  0x1000000
#define INTEGRATOR_HDR_OSC_BUS_MODE_CM10x00  0x1800000
#define INTEGRATOR_HDR_OSC_BUS_MODE_MASK  0x1800000

#define INTEGRATOR_HDR_SDRAM_SPD_OK     (1 << 5)

/*
 * Integrator system registers
 */

/*
 *  System Controller
 */
#define INTEGRATOR_SC_ID_OFFSET         0x00
#define INTEGRATOR_SC_OSC_OFFSET        0x04
#define INTEGRATOR_SC_CTRLS_OFFSET      0x08
#define INTEGRATOR_SC_CTRLC_OFFSET      0x0C
#define INTEGRATOR_SC_DEC_OFFSET        0x10
#define INTEGRATOR_SC_ARB_OFFSET        0x14
#define INTEGRATOR_SC_LOCK_OFFSET       0x1C

#define INTEGRATOR_SC_BASE              0x11000000
#define INTEGRATOR_SC_ID                (INTEGRATOR_SC_BASE + INTEGRATOR_SC_ID_OFFSET)
#define INTEGRATOR_SC_OSC               (INTEGRATOR_SC_BASE + INTEGRATOR_SC_OSC_OFFSET)
#define INTEGRATOR_SC_CTRLS             (INTEGRATOR_SC_BASE + INTEGRATOR_SC_CTRLS_OFFSET)
#define INTEGRATOR_SC_CTRLC             (INTEGRATOR_SC_BASE + INTEGRATOR_SC_CTRLC_OFFSET)
#define INTEGRATOR_SC_DEC               (INTEGRATOR_SC_BASE + INTEGRATOR_SC_DEC_OFFSET)
#define INTEGRATOR_SC_ARB               (INTEGRATOR_SC_BASE + INTEGRATOR_SC_ARB_OFFSET)
#define INTEGRATOR_SC_PCIENABLE         (INTEGRATOR_SC_BASE + INTEGRATOR_SC_PCIENABLE_OFFSET)
#define INTEGRATOR_SC_LOCK              (INTEGRATOR_SC_BASE + INTEGRATOR_SC_LOCK_OFFSET)

#define INTEGRATOR_SC_OSC_SYS_10MHz     0x20
#define INTEGRATOR_SC_OSC_SYS_15MHz     0x34
#define INTEGRATOR_SC_OSC_SYS_20MHz     0x48
#define INTEGRATOR_SC_OSC_SYS_25MHz     0x5C
#define INTEGRATOR_SC_OSC_SYS_33MHz     0x7C
#define INTEGRATOR_SC_OSC_SYS_MASK      0xFF

#define INTEGRATOR_SC_OSC_PCI_25MHz     0x100
#define INTEGRATOR_SC_OSC_PCI_33MHz     0x0
#define INTEGRATOR_SC_OSC_PCI_MASK      0x100

#define INTEGRATOR_SC_CTRL_SOFTRST      (1 << 0)
#define INTEGRATOR_SC_CTRL_nFLVPPEN     (1 << 1)
#define INTEGRATOR_SC_CTRL_nFLWP        (1 << 2)
#define INTEGRATOR_SC_CTRL_URTS0        (1 << 4)
#define INTEGRATOR_SC_CTRL_UDTR0        (1 << 5)
#define INTEGRATOR_SC_CTRL_URTS1        (1 << 6)
#define INTEGRATOR_SC_CTRL_UDTR1        (1 << 7)

/*
 *  External Bus Interface
 */
#define INTEGRATOR_EBI_BASE             0x12000000

#define INTEGRATOR_EBI_CSR0_OFFSET      0x00
#define INTEGRATOR_EBI_CSR1_OFFSET      0x04
#define INTEGRATOR_EBI_CSR2_OFFSET      0x08
#define INTEGRATOR_EBI_CSR3_OFFSET      0x0C
#define INTEGRATOR_EBI_LOCK_OFFSET      0x20

#define INTEGRATOR_EBI_CSR0             (INTEGRATOR_EBI_BASE + INTEGRATOR_EBI_CSR0_OFFSET)
#define INTEGRATOR_EBI_CSR1             (INTEGRATOR_EBI_BASE + INTEGRATOR_EBI_CSR1_OFFSET)
#define INTEGRATOR_EBI_CSR2             (INTEGRATOR_EBI_BASE + INTEGRATOR_EBI_CSR2_OFFSET)
#define INTEGRATOR_EBI_CSR3             (INTEGRATOR_EBI_BASE + INTEGRATOR_EBI_CSR3_OFFSET)
#define INTEGRATOR_EBI_LOCK             (INTEGRATOR_EBI_BASE + INTEGRATOR_EBI_LOCK_OFFSET)

#define INTEGRATOR_EBI_8_BIT            0x00
#define INTEGRATOR_EBI_16_BIT           0x01
#define INTEGRATOR_EBI_32_BIT           0x02
#define INTEGRATOR_EBI_WRITE_ENABLE     0x04
#define INTEGRATOR_EBI_SYNC             0x08
#define INTEGRATOR_EBI_WS_2             0x00
#define INTEGRATOR_EBI_WS_3             0x10
#define INTEGRATOR_EBI_WS_4             0x20
#define INTEGRATOR_EBI_WS_5             0x30
#define INTEGRATOR_EBI_WS_6             0x40
#define INTEGRATOR_EBI_WS_7             0x50
#define INTEGRATOR_EBI_WS_8             0x60
#define INTEGRATOR_EBI_WS_9             0x70
#define INTEGRATOR_EBI_WS_10            0x80
#define INTEGRATOR_EBI_WS_11            0x90
#define INTEGRATOR_EBI_WS_12            0xA0
#define INTEGRATOR_EBI_WS_13            0xB0
#define INTEGRATOR_EBI_WS_14            0xC0
#define INTEGRATOR_EBI_WS_15            0xD0
#define INTEGRATOR_EBI_WS_16            0xE0
#define INTEGRATOR_EBI_WS_17            0xF0


#define INTEGRATOR_CT_BASE              0x13000000	 /*  Counter/Timers */
#define INTEGRATOR_IC_BASE              0x14000000	 /*  Interrupt Controller */
#define INTEGRATOR_RTC_BASE             0x15000000	 /*  Real Time Clock */
#define INTEGRATOR_UART0_BASE           0x16000000	 /*  UART 0 */
#define INTEGRATOR_UART1_BASE           0x17000000	 /*  UART 1 */
#define INTEGRATOR_KBD_BASE             0x18000000	 /*  Keyboard */
#define INTEGRATOR_MOUSE_BASE           0x19000000	 /*  Mouse */

/*
 *  LED's & Switches
 */
#define INTEGRATOR_DBG_ALPHA_OFFSET     0x00
#define INTEGRATOR_DBG_LEDS_OFFSET      0x04
#define INTEGRATOR_DBG_SWITCH_OFFSET    0x08

#define INTEGRATOR_DBG_BASE             0x1A000000
#define INTEGRATOR_DBG_ALPHA            (INTEGRATOR_DBG_BASE + INTEGRATOR_DBG_ALPHA_OFFSET)
#define INTEGRATOR_DBG_LEDS             (INTEGRATOR_DBG_BASE + INTEGRATOR_DBG_LEDS_OFFSET)
#define INTEGRATOR_DBG_SWITCH           (INTEGRATOR_DBG_BASE + INTEGRATOR_DBG_SWITCH_OFFSET)

#define INTEGRATOR_AP_GPIO_BASE		0x1B000000	/* GPIO */

#define INTEGRATOR_CP_MMC_BASE		0x1C000000	/* MMC */
#define INTEGRATOR_CP_AACI_BASE		0x1D000000	/* AACI */
#define INTEGRATOR_CP_ETH_BASE		0xC8000000	/* Ethernet */
#define INTEGRATOR_CP_GPIO_BASE		0xC9000000	/* GPIO */
#define INTEGRATOR_CP_SIC_BASE		0xCA000000	/* SIC */
#define INTEGRATOR_CP_CTL_BASE		0xCB000000	/* CP system control */

/* PS2 Keyboard interface */
#define KMI0_BASE                       INTEGRATOR_KBD_BASE

/* PS2 Mouse interface */
#define KMI1_BASE                       INTEGRATOR_MOUSE_BASE

/*
 * Integrator Interrupt Controllers
 *
 *
 * Offsets from interrupt controller base
 *
 * System Controller interrupt controller base is
 *
 * 	INTEGRATOR_IC_BASE + (header_number << 6)
 *
 * Core Module interrupt controller base is
 *
 * 	INTEGRATOR_HDR_IC
 */
#define IRQ_STATUS                      0
#define IRQ_RAW_STATUS                  0x04
#define IRQ_ENABLE                      0x08
#define IRQ_ENABLE_SET                  0x08
#define IRQ_ENABLE_CLEAR                0x0C

#define INT_SOFT_SET                    0x10
#define INT_SOFT_CLEAR                  0x14

#define FIQ_STATUS                      0x20
#define FIQ_RAW_STATUS                  0x24
#define FIQ_ENABLE                      0x28
#define FIQ_ENABLE_SET                  0x28
#define FIQ_ENABLE_CLEAR                0x2C


/*
 * LED's
 */
#define GREEN_LED                       0x01
#define YELLOW_LED                      0x02
#define RED_LED                         0x04
#define GREEN_LED_2                     0x08
#define ALL_LEDS                        0x0F

#define LED_BANK                        INTEGRATOR_DBG_LEDS

/*
 *  Timer definitions
 *
 *  Only use timer 1 & 2
 *  (both run at 24MHz and will need the clock divider set to 16).
 *
 *  Timer 0 runs at bus frequency
 */
#define INTEGRATOR_TIMER0_BASE          INTEGRATOR_CT_BASE
#define INTEGRATOR_TIMER1_BASE          (INTEGRATOR_CT_BASE + 0x100)
#define INTEGRATOR_TIMER2_BASE          (INTEGRATOR_CT_BASE + 0x200)

#define INTEGRATOR_CSR_BASE             0x10000000
#define INTEGRATOR_CSR_SIZE             0x10000000

#endif /* INTEGRATOR_HARDWARE_H */
