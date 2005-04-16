/*----------------------------------------------------------------------------+
|       This source code has been made available to you by IBM on an AS-IS
|       basis.  Anyone receiving this source is licensed under IBM
|       copyrights to use it in any way he or she deems fit, including
|       copying it, modifying it, compiling it, and redistributing it either
|       with or without modifications.  No license under IBM patents or
|       patent applications is to be implied by the copyright license.
|
|       Any user of this software should understand that IBM cannot provide
|       technical support for this software and will not be responsible for
|       any consequences resulting from the use of this software.
|
|       Any person who transfers this source code or any derivative work
|       must include the IBM copyright notice, this paragraph, and the
|       preceding two paragraphs in the transferred software.
|
|       COPYRIGHT   I B M   CORPORATION 1999
|       LICENSED MATERIAL  -  PROGRAM PROPERTY OF I B M
+----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------+
| Author: Maciej P. Tyrlik
| Component: Include file.
| File: stb.h
| Purpose: Common Set-tob-box definitions.
| Changes:
| Date:         Comment:
| -----         --------
| 14-Jan-97     Created for ElPaso pass 1                                   MPT
| 13-May-97     Added function prototype and global variables               MPT
| 08-Dec-98     Added RAW IR task information                               MPT
| 19-Jan-99     Port to Romeo                                               MPT
| 19-May-00     Changed SDRAM to 32MB contiguous 0x1F000000 - 0x20FFFFFF    RLB
+----------------------------------------------------------------------------*/

#ifndef _stb_h_
#define _stb_h_

/*----------------------------------------------------------------------------+
| Read/write from I/O macros.
+----------------------------------------------------------------------------*/
#define inbyte(port)            (*((unsigned char volatile *)(port)))
#define outbyte(port,data)      *(unsigned char volatile *)(port)=\
                                (unsigned char)(data)

#define inshort(port)           (*((unsigned short volatile *)(port)))
#define outshort(port,data)     *(unsigned short volatile *)(port)=\
                                (unsigned short)(data)

#define inword(port)            (*((unsigned long volatile *)(port)))
#define outword(port,data)      *(unsigned long volatile *)(port)=\
                                (unsigned long)(data)

/*----------------------------------------------------------------------------+
| STB interrupts.
+----------------------------------------------------------------------------*/
#define STB_XP_TP_INT           0
#define STB_XP_APP_INT          1
#define STB_AUD_INT             2
#define STB_VID_INT             3
#define STB_DMA0_INT            4
#define STB_DMA1_INT            5
#define STB_DMA2_INT            6
#define STB_DMA3_INT            7
#define STB_SCI_INT             8
#define STB_I2C1_INT            9
#define STB_I2C2_INT            10
#define STB_GPT_PWM0            11
#define STB_GPT_PWM1            12
#define STB_SCP_INT             13
#define STB_SSP_INT             14
#define STB_GPT_PWM2            15
#define STB_EXT5_INT            16
#define STB_EXT6_INT            17
#define STB_EXT7_INT            18
#define STB_EXT8_INT            19
#define STB_SCC_INT             20
#define STB_SICC_RECV_INT       21
#define STB_SICC_TRAN_INT       22
#define STB_PPU_INT             23
#define STB_DCRX_INT            24
#define STB_EXT0_INT            25
#define STB_EXT1_INT            26
#define STB_EXT2_INT            27
#define STB_EXT3_INT            28
#define STB_EXT4_INT            29
#define STB_REDWOOD_ENET_INT    STB_EXT1_INT

/*----------------------------------------------------------------------------+
| STB tasks, task stack sizes, and task priorities.  The actual task priority
| is 1 more than the specified number since priority 0 is reserved (system
| internaly adds 1 to supplied priority number).
+----------------------------------------------------------------------------*/
#define STB_IDLE_TASK_SS        (5* 1024)
#define STB_IDLE_TASK_PRIO      0
#define STB_LEDTEST_SS          (2* 1024)
#define STB_LEDTEST_PRIO        0
#define STB_CURSOR_TASK_SS      (10* 1024)
#define STB_CURSOR_TASK_PRIO    7
#define STB_MPEG_TASK_SS        (10* 1024)
#define STB_MPEG_TASK_PRIO      9
#define STB_DEMUX_TASK_SS       (10* 1024)
#define STB_DEMUX_TASK_PRIO     20
#define RAW_STB_IR_TASK_SS      (10* 1024)
#define RAW_STB_IR_TASK_PRIO    20

#define STB_SERIAL_ER_TASK_SS   (10* 1024)
#define STB_SERIAL_ER_TASK_PRIO 1
#define STB_CA_TASK_SS          (10* 1024)
#define STB_CA_TASK_PRIO        8

#define INIT_DEFAULT_VIDEO_SS   (10* 1024)
#define INIT_DEFAULT_VIDEO_PRIO 8
#define INIT_DEFAULT_SERVI_SS   (10* 1024)
#define INIT_DEFAULT_SERVI_PRIO 8
#define INIT_DEFAULT_POST_SS    (10* 1024)
#define INIT_DEFAULT_POST_PRIO  8
#define INIT_DEFAULT_INTER_SS   (10* 1024)
#define INIT_DEFAULT_INTER_PRIO 8
#define INIT_DEFAULT_BR_SS      (10* 1024)
#define INIT_DEFAULT_BR_PRIO    8
#define INITIAL_TASK_STACK_SIZE (32* 1024)

#ifdef VESTA
/*----------------------------------------------------------------------------+
| Vesta Overall Address Map (all addresses are double mapped, bit 0 of the
| address is not decoded.  Numbers below are dependent on board configuration.
| FLASH, SDRAM, DRAM numbers can be affected by actual board setup.
|
|    FFE0,0000 - FFFF,FFFF        FLASH
|    F200,0000 - F210,FFFF        FPGA logic
|                                   Ethernet       = F200,0000
|                                   LED Display    = F200,0100
|                                   Xilinx #1 Regs = F204,0000
|                                   Xilinx #2 Regs = F208,0000
|                                   Spare          = F20C,0000
|                                   IDE CS0        = F210,0000
|    F410,0000 - F410,FFFF        IDE CS1
|    C000,0000 - C7FF,FFFF        OBP
|    C000,0000 - C000,0014        SICC  (16550 + infra red)
|    C001,0000 - C001,0018        PPU   (Parallel Port)
|    C002,0000 - C002,001B        SC0   (Smart Card 0)
|    C003,0000 - C003,000F        I2C0
|    C004,0000 - C004,0009        SCC   (16550 UART)
|    C005,0000 - C005,0124        GPT   (Timers)
|    C006,0000 - C006,0058        GPIO0
|    C007,0000 - C007,001b        SC1   (Smart Card 1)
|    C008,0000 - C008,FFFF        Unused
|    C009,0000 - C009,FFFF        Unused
|    C00A,0000 - C00A,FFFF        Unused
|    C00B,0000 - C00B,000F        I2C1
|    C00C,0000 - C00C,0006        SCP
|    C00D,0000 - C00D,0010        SSP
|    A000,0000 - A0FF,FFFF        SDRAM1  (16M)
|    0000,0000 - 00FF,FFFF        SDRAM0  (16M)
+----------------------------------------------------------------------------*/
#define STB_FLASH_BASE_ADDRESS  0xFFE00000
#define STB_FPGA_BASE_ADDRESS   0xF2000000
#define STB_SICC_BASE_ADDRESS   0xC0000000
#define STB_PPU_BASE_ADDR       0xC0010000
#define STB_SC0_BASE_ADDRESS    0xC0020000
#define STB_I2C1_BASE_ADDRESS   0xC0030000
#define STB_SCC_BASE_ADDRESS    0xC0040000
#define STB_TIMERS_BASE_ADDRESS 0xC0050000
#define STB_GPIO0_BASE_ADDRESS  0xC0060000
#define STB_SC1_BASE_ADDRESS    0xC0070000
#define STB_I2C2_BASE_ADDRESS   0xC00B0000
#define STB_SCP_BASE_ADDRESS    0xC00C0000
#define STB_SSP_BASE_ADDRESS    0xC00D0000
/*----------------------------------------------------------------------------+
|The following are used by the IBM RTOS SW.
|15-May-00 Changed these values to reflect movement of base addresses in
|order to support 32MB of contiguous SDRAM space.
|Points to the cacheable region since these values are used in IBM RTOS
|to establish the vector address.
+----------------------------------------------------------------------------*/
#define STB_SDRAM1_BASE_ADDRESS 0x20000000
#define STB_SDRAM1_SIZE         0x01000000
#define STB_SDRAM0_BASE_ADDRESS 0x1F000000
#define STB_SDRAM0_SIZE         0x01000000

#else
/*----------------------------------------------------------------------------+
| ElPaso Overall Address Map (all addresses are double mapped, bit 0 of the
| address is not decoded.  Numbers below are dependent on board configuration.
| FLASH, SDRAM, DRAM numbers can be affected by actual board setup.  OPB
| devices are inside the ElPaso chip.
|    FFE0,0000 - FFFF,FFFF        FLASH
|    F144,0000 - F104,FFFF        FPGA logic
|    F140,0000 - F100,0000        ethernet (through FPGA logic)
|    C000,0000 - C7FF,FFFF        OBP
|    C000,0000 - C000,0014        SICC (16550+ infra red)
|    C001,0000 - C001,0016        PPU (parallel port)
|    C002,0000 - C002,001B        SC (smart card)
|    C003,0000 - C003,000F        I2C 1
|    C004,0000 - C004,0009        SCC (16550 UART)
|    C005,0000 - C005,0124        Timers
|    C006,0000 - C006,0058        GPIO0
|    C007,0000 - C007,0058        GPIO1
|    C008,0000 - C008,0058        GPIO2
|    C009,0000 - C009,0058        GPIO3
|    C00A,0000 - C00A,0058        GPIO4
|    C00B,0000 - C00B,000F        I2C 2
|    C00C,0000 - C00C,0006        SCP
|    C00D,0000 - C00D,0006        SSP
|    A000,0000 - A0FF,FFFF        SDRAM 16M
|    0000,0000 - 00FF,FFFF        DRAM 16M
+----------------------------------------------------------------------------*/
#define STB_FLASH_BASE_ADDRESS  0xFFE00000
#define STB_FPGA_BASE_ADDRESS   0xF1440000
#define STB_ENET_BASE_ADDRESS   0xF1400000
#define STB_SICC_BASE_ADDRESS   0xC0000000
#define STB_PPU_BASE_ADDR       0xC0010000
#define STB_SC_BASE_ADDRESS     0xC0020000
#define STB_I2C1_BASE_ADDRESS   0xC0030000
#define STB_SCC_BASE_ADDRESS    0xC0040000
#define STB_TIMERS_BASE_ADDRESS 0xC0050000
#define STB_GPIO0_BASE_ADDRESS  0xC0060000
#define STB_GPIO1_BASE_ADDRESS  0xC0070000
#define STB_GPIO2_BASE_ADDRESS  0xC0080000
#define STB_GPIO3_BASE_ADDRESS  0xC0090000
#define STB_GPIO4_BASE_ADDRESS  0xC00A0000
#define STB_I2C2_BASE_ADDRESS   0xC00B0000
#define STB_SCP_BASE_ADDRESS    0xC00C0000
#define STB_SSP_BASE_ADDRESS    0xC00D0000
#define STB_SDRAM_BASE_ADDRESS  0xA0000000
#endif

/*----------------------------------------------------------------------------+
| Other common defines.
+----------------------------------------------------------------------------*/
#ifndef TRUE
#define TRUE    1
#endif

#ifndef FALSE
#define FALSE   0
#endif

#endif /* _stb_h_ */
