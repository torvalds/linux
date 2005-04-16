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
|       COPYRIGHT   I B M   CORPORATION 1997
|       LICENSED MATERIAL  -  PROGRAM PROPERTY OF I B M
+----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------+
| Author:    Tony J. Cerreto
| Component: BSPS
| File:      init_brd.s
| Purpose:   Vesta Evaluation Board initialization subroutines.  The following
|            routines are available:
|              1. INITB_EBIU0:    Initialize EBIU0.
|              2. INITB_CONFIG:   Configure board.
|              3. INITB_HSMC0:    Initialize HSMC0 (SDRAM).
|              4. INITB_HSMC1:    Initialize HSMC1 (SDRAM).
|              5. INITB_CACHE:    Initialize Data and Instruction Cache.
|              6. INITB_DCACHE:   Initialize Data Cache.
|              7. INITB_ICACHE:   Initialize Instruction Cache.
|              8. INITB_GET_CSPD: Get CPU Speed (Bus Speed and Processor Speed)
|
| Changes:
| Date:      Author  Comment:
| ---------  ------  --------
| 01-Mar-00  tjc     Created
| 04-Mar-00  jfh     Modified CIC_SEL3_VAL to support 1284 (Mux3 & GPIO 21-28)
| 04-Mar-00  jfh     Modified XILINIX Reg 0 to support 1284 (Mux3 & GPIO 21-28)
| 04-Mar-00  jfh     Modified XILINIX Reg 1 to support 1284 (Mux3 & GPIO 21-28)
| 04-Mar-00  jfh     Modified XILINIX Reg 4 to support 1284 (Mux3 & GPIO 21-28)
| 19-May-00  rlb     Relcoated HSMC0 to 0x1F000000 to support 32MB of contiguous
|                    SDRAM space.  Changed cache ctl regs to reflect this.
| 22-May-00  tjc     Changed initb_get_cspd interface and eliminated
|                    initb_get_bspd routines.
| 26-May-00  tjc     Added two nop instructions after all mtxxx/mfxxx
|                    instructions due to PPC405 bug.
+----------------------------------------------------------------------------*/
#define VESTA
#include "ppc_40x.h"
#include "stb.h"

/*----------------------------------------------------------------------------+
| BOARD CONFIGURATION DEFINES
+----------------------------------------------------------------------------*/
#define CBS0_CR_VAL          0x00000002          /* CBS control reg value    */
#define CIC0_CR_VAL          0xD0800448          /* CIC control reg value    */
#define CIC0_SEL3_VAL        0x11500000          /* CIC select 3 reg value   */
#define CIC0_VCR_VAL         0x00631700          /* CIC video cntl reg value */

/*----------------------------------------------------------------------------+
| EBIU0 BANK REGISTERS DEFINES
+----------------------------------------------------------------------------*/
#define EBIU0_BRCRH0_VAL     0x00000000          /* BR High 0 (Extension Reg)*/
#define EBIU0_BRCRH1_VAL     0x00000000          /* BR High 1 (Extension Reg)*/
#define EBIU0_BRCRH2_VAL     0x40000000          /* BR High 2 (Extension Reg)*/
#define EBIU0_BRCRH3_VAL     0x40000000          /* BR High 3 (Extension Reg)*/
#define EBIU0_BRCRH4_VAL     0x00000000          /* BR High 4 (Extension Reg)*/
#define EBIU0_BRCRH5_VAL     0x00000000          /* BR High 5 (Extension Reg)*/
#define EBIU0_BRCRH6_VAL     0x00000000          /* BR High 6 (Extension Reg)*/
#define EBIU0_BRCRH7_VAL     0x40000000          /* BR High 7 (Extension Reg)*/

#define EBIU0_BRCR0_VAL      0xFC58BFFE          /* BR 0: 16 bit Flash  4 MB */
#define EBIU0_BRCR1_VAL      0xFF00BFFE          /* BR 1: Ext Connector 1 MB */
#if 1
#define EBIU0_BRCR2_VAL      0x207CFFBE          /* BR 2: Xilinx        8 MB */
                                                 /* twt == 0x3f              */
#else
#define EBIU0_BRCR2_VAL      0x207CCFBE          /* BR 2: Xilinx        8 MB */
                                                 /* twt == 0x0f              */
#endif
#define EBIU0_BRCR3_VAL      0x407CBFBE          /* BR 3: IDE Drive     8 MB */
#define EBIU0_BRCR4_VAL      0xFF00BFFF          /* BR 4: Disabled.     0 MB */
#define EBIU0_BRCR5_VAL      0xFF00BFFF          /* BR 5: Disabled.     0 MB */
#define EBIU0_BRCR6_VAL      0xFF00BFFF          /* BR 6: Disabled.     0 MB */
#define EBIU0_BRCR7_VAL      0xCE3F0003          /* BR 7: Line Mode DMA 2 MB */

/*----------------------------------------------------------------------------+
| GPIO DEFINES
+----------------------------------------------------------------------------*/
#define STB_GPIO0_OUTPUT     (STB_GPIO0_BASE_ADDRESS+ 0x00)
#define STB_GPIO0_TC         (STB_GPIO0_BASE_ADDRESS+ 0x04)
#define STB_GPIO0_OS_0_31    (STB_GPIO0_BASE_ADDRESS+ 0x08)
#define STB_GPIO0_OS_32_63   (STB_GPIO0_BASE_ADDRESS+ 0x0C)
#define STB_GPIO0_TS_0_31    (STB_GPIO0_BASE_ADDRESS+ 0x10)
#define STB_GPIO0_TS_32_63   (STB_GPIO0_BASE_ADDRESS+ 0x14)
#define STB_GPIO0_OD         (STB_GPIO0_BASE_ADDRESS+ 0x18)
#define STB_GPIO0_INPUT      (STB_GPIO0_BASE_ADDRESS+ 0x1C)
#define STB_GPIO0_R1         (STB_GPIO0_BASE_ADDRESS+ 0x20)
#define STB_GPIO0_R2         (STB_GPIO0_BASE_ADDRESS+ 0x24)
#define STB_GPIO0_R3         (STB_GPIO0_BASE_ADDRESS+ 0x28)
#define STB_GPIO0_IS_1_0_31  (STB_GPIO0_BASE_ADDRESS+ 0x30)
#define STB_GPIO0_IS_1_32_63 (STB_GPIO0_BASE_ADDRESS+ 0x34)
#define STB_GPIO0_IS_2_0_31  (STB_GPIO0_BASE_ADDRESS+ 0x38)
#define STB_GPIO0_IS_2_32_63 (STB_GPIO0_BASE_ADDRESS+ 0x3C)
#define STB_GPIO0_IS_3_0_31  (STB_GPIO0_BASE_ADDRESS+ 0x40)
#define STB_GPIO0_IS_3_32_63 (STB_GPIO0_BASE_ADDRESS+ 0x44)
#define STB_GPIO0_SS_1       (STB_GPIO0_BASE_ADDRESS+ 0x50)
#define STB_GPIO0_SS_2       (STB_GPIO0_BASE_ADDRESS+ 0x54)
#define STB_GPIO0_SS_3       (STB_GPIO0_BASE_ADDRESS+ 0x58)

#define GPIO0_TC_VAL         0x0C020004          /* three-state control val  */
#define GPIO0_OS_0_31_VAL    0x51A00004          /* output select 0-31  val  */
#define GPIO0_OS_32_63_VAL   0x0000002F          /* output select 32-63 val  */
#define GPIO0_TS_0_31_VAL    0x51A00000          /* three-state sel 0-31  val*/
#define GPIO0_TS_32_63_VAL   0x0000000F          /* three-state sel 32-63 val*/
#define GPIO0_OD_VAL         0xC0000004          /* open drain val           */
#define GPIO0_IS_1_0_31_VAL  0x50000151          /* input select 1 0-31  val */
#define GPIO0_IS_1_32_63_VAL 0x00000000          /* input select 1 32-63 val */
#define GPIO0_IS_2_0_31_VAL  0x00000000          /* input select 2 0-31  val */
#define GPIO0_IS_2_32_63_VAL 0x00000000          /* input select 2 32-63 val */
#define GPIO0_IS_3_0_31_VAL  0x00000440          /* input select 3 0-31  val */
#define GPIO0_IS_3_32_63_VAL 0x00000000          /* input select 3 32-63 val */
#define GPIO0_SS_1_VAL       0x00000000          /* sync select 1 val        */
#define GPIO0_SS_2_VAL       0x00000000          /* sync select 2 val        */
#define GPIO0_SS_3_VAL       0x00000000          /* sync select 3 val        */

/*----------------------------------------------------------------------------+
| XILINX DEFINES
+----------------------------------------------------------------------------*/
#define STB_XILINX_LED       (STB_FPGA_BASE_ADDRESS+ 0x0100)
#define STB_XILINX1_REG0     (STB_FPGA_BASE_ADDRESS+ 0x40000)
#define STB_XILINX1_REG1     (STB_FPGA_BASE_ADDRESS+ 0x40002)
#define STB_XILINX1_REG2     (STB_FPGA_BASE_ADDRESS+ 0x40004)
#define STB_XILINX1_REG3     (STB_FPGA_BASE_ADDRESS+ 0x40006)
#define STB_XILINX1_REG4     (STB_FPGA_BASE_ADDRESS+ 0x40008)
#define STB_XILINX1_REG5     (STB_FPGA_BASE_ADDRESS+ 0x4000A)
#define STB_XILINX1_REG6     (STB_FPGA_BASE_ADDRESS+ 0x4000C)
#define STB_XILINX1_ID       (STB_FPGA_BASE_ADDRESS+ 0x4000E)
#define STB_XILINX1_FLUSH    (STB_FPGA_BASE_ADDRESS+ 0x4000E)
#define STB_XILINX2_REG0     (STB_FPGA_BASE_ADDRESS+ 0x80000)
#define STB_XILINX2_REG1     (STB_FPGA_BASE_ADDRESS+ 0x80002)
#define STB_XILINX2_REG2     (STB_FPGA_BASE_ADDRESS+ 0x80004)

#define XILINX1_R0_VAL       0x2440              /* Xilinx 1 Register 0 Val  */
#define XILINX1_R1_VAL       0x0025              /* Xilinx 1 Register 1 Val  */
#define XILINX1_R2_VAL       0x0441              /* Xilinx 1 Register 2 Val  */
#define XILINX1_R3_VAL       0x0008              /* Xilinx 1 Register 3 Val  */
#define XILINX1_R4_VAL       0x0100              /* Xilinx 1 Register 4 Val  */
#define XILINX1_R5_VAL       0x6810              /* Xilinx 1 Register 5 Val  */
#define XILINX1_R6_VAL       0x0000              /* Xilinx 1 Register 6 Val  */
#if 0
#define XILINX2_R0_VAL       0x0008              /* Xilinx 2 Register 0 Val  */
#define XILINX2_R1_VAL       0x0000              /* Xilinx 2 Register 1 Val  */
#else
#define XILINX2_R0_VAL       0x0018              /* disable IBM IrDA RxD     */
#define XILINX2_R1_VAL       0x0008              /* enable SICC MAX chip     */
#endif
#define XILINX2_R2_VAL       0x0000              /* Xilinx 2 Register 2 Val  */

/*----------------------------------------------------------------------------+
| HSMC BANK REGISTERS DEFINES
+----------------------------------------------------------------------------*/
#ifdef SDRAM16MB
#define HSMC0_BR0_VAL        0x000D2D55          /* 0x1F000000-007FFFFF R/W  */
#define HSMC0_BR1_VAL        0x008D2D55          /* 0x1F800000-1FFFFFFF R/W  */
#else
#define HSMC0_BR0_VAL        0x1F0D2D55          /* 0x1F000000-007FFFFF R/W  */
#define HSMC0_BR1_VAL        0x1F8D2D55          /* 0x1F800000-1FFFFFFF R/W  */
#endif
#define HSMC1_BR0_VAL        0xA00D2D55          /* 0xA0000000-A07FFFFF R/W  */
#define HSMC1_BR1_VAL        0xA08D2D55          /* 0xA0800000-A0FFFFFF R/W  */

/*----------------------------------------------------------------------------+
| CACHE DEFINES
+----------------------------------------------------------------------------*/
#define DCACHE_NLINES               128          /* no. D-cache lines        */
#define DCACHE_NBYTES                32          /* no. bytes/ D-cache line  */
#define ICACHE_NLINES               256          /* no. I-cache lines        */
#define ICACHE_NBYTES                32          /* no. bytes/ I-cache line  */
#ifdef SDRAM16MB
#define DCACHE_ENABLE        0x80000000          /* D-cache regions to enable*/
#define ICACHE_ENABLE        0x80000001          /* I-cache regions to enable*/
#else
#define DCACHE_ENABLE        0x18000000          /* D-cache regions to enable*/
#define ICACHE_ENABLE        0x18000001          /* I-cache regions to enable*/
#endif

/*----------------------------------------------------------------------------+
| CPU CORE SPEED CALCULATION DEFINES
+----------------------------------------------------------------------------*/
#define GCS_LCNT                 500000          /* CPU speed loop count     */
#define GCS_TROW_BYTES                8          /* no. bytes in table row   */
#define GCS_CTICK_TOL               100          /* allowable clock tick tol */
#define GCS_NMULT                     4          /* no. of core speed mults  */

        /*--------------------------------------------------------------------+
        |        No. 13.5Mhz
        |        Clock Ticks
        |        based on a
        |        loop count    Bus
        |        of 100,000    Speed
        +--------------------------------------------------------------------*/
gcs_lookup_table:
        .int           50000,  54000000          /* 54.0 Mhz                 */
        .int           66667,  40500000          /* 40.5 Mhz                 */
        .int           54545,  49500000          /* 49.5 Mhz                 */
        .int           46154,  58500000          /* 58.5 Mhz                 */
        .int               0,         0          /* end of table flag        */


/*****************************************************************************+
| XXXXXXX  XXX XXX   XXXXXX  XXXXXXX  XXXXXX   XX   XX     XX    XXXX
|  XX   X   XX XX    X XX X   XX   X   XX  XX  XXX  XX    XXXX    XX
|  XX X      XXX       XX     XX X     XX  XX  XXXX XX   XX  XX   XX
|  XXXX       X        XX     XXXX     XXXXX   XX XXXX   XX  XX   XX
|  XX X      XXX       XX     XX X     XX XX   XX  XXX   XXXXXX   XX
|  XX   X   XX XX      XX     XX   X   XX  XX  XX   XX   XX  XX   XX  XX
| XXXXXXX  XXX XXX    XXXX   XXXXXXX  XXX  XX  XX   XX   XX  XX  XXXXXXX
+*****************************************************************************/
/******************************************************************************
|
| Routine:    INITB_EBIU0.
|
| Purpose:    Initialize all the EBIU0 Bank Registers
| Parameters: None.
| Returns:    None.
|
******************************************************************************/
        function_prolog(initb_ebiu0)
        /*--------------------------------------------------------------------+
        |  Set EBIU0 Bank 0
        +--------------------------------------------------------------------*/
        lis     r10,EBIU0_BRCR0_VAL@h
        ori     r10,r10,EBIU0_BRCR0_VAL@l
        mtdcr   ebiu0_brcr0,r10
        lis     r10,EBIU0_BRCRH0_VAL@h
        ori     r10,r10,EBIU0_BRCRH0_VAL@l
        mtdcr   ebiu0_brcrh0,r10

        /*--------------------------------------------------------------------+
        |  Set EBIU0 Bank 1
        +--------------------------------------------------------------------*/
        lis     r10,EBIU0_BRCR1_VAL@h
        ori     r10,r10,EBIU0_BRCR1_VAL@l
        mtdcr   ebiu0_brcr1,r10
        lis     r10,EBIU0_BRCRH1_VAL@h
        ori     r10,r10,EBIU0_BRCRH1_VAL@l
        mtdcr   ebiu0_brcrh1,r10

        /*--------------------------------------------------------------------+
        |  Set EBIU0 Bank 2
        +--------------------------------------------------------------------*/
        lis     r10,EBIU0_BRCR2_VAL@h
        ori     r10,r10,EBIU0_BRCR2_VAL@l
        mtdcr   ebiu0_brcr2,r10
        lis     r10,EBIU0_BRCRH2_VAL@h
        ori     r10,r10,EBIU0_BRCRH2_VAL@l
        mtdcr   ebiu0_brcrh2,r10

        /*--------------------------------------------------------------------+
        |  Set EBIU0 Bank 3
        +--------------------------------------------------------------------*/
        lis     r10,EBIU0_BRCR3_VAL@h
        ori     r10,r10,EBIU0_BRCR3_VAL@l
        mtdcr   ebiu0_brcr3,r10
        lis     r10,EBIU0_BRCRH3_VAL@h
        ori     r10,r10,EBIU0_BRCRH3_VAL@l
        mtdcr   ebiu0_brcrh3,r10

        /*--------------------------------------------------------------------+
        |  Set EBIU0 Bank 4
        +--------------------------------------------------------------------*/
        lis     r10,EBIU0_BRCR4_VAL@h
        ori     r10,r10,EBIU0_BRCR4_VAL@l
        mtdcr   ebiu0_brcr4,r10
        lis     r10,EBIU0_BRCRH4_VAL@h
        ori     r10,r10,EBIU0_BRCRH4_VAL@l
        mtdcr   ebiu0_brcrh4,r10

        /*--------------------------------------------------------------------+
        |  Set EBIU0 Bank 5
        +--------------------------------------------------------------------*/
        lis     r10,EBIU0_BRCR5_VAL@h
        ori     r10,r10,EBIU0_BRCR5_VAL@l
        mtdcr   ebiu0_brcr5,r10
        lis     r10,EBIU0_BRCRH5_VAL@h
        ori     r10,r10,EBIU0_BRCRH5_VAL@l
        mtdcr   ebiu0_brcrh5,r10

        /*--------------------------------------------------------------------+
        |  Set EBIU0 Bank 6
        +--------------------------------------------------------------------*/
        lis     r10,EBIU0_BRCR6_VAL@h
        ori     r10,r10,EBIU0_BRCR6_VAL@l
        mtdcr   ebiu0_brcr6,r10
        lis     r10,EBIU0_BRCRH6_VAL@h
        ori     r10,r10,EBIU0_BRCRH6_VAL@l
        mtdcr   ebiu0_brcrh6,r10

        /*--------------------------------------------------------------------+
        |  Set EBIU0 Bank 7
        +--------------------------------------------------------------------*/
        lis     r10,EBIU0_BRCR7_VAL@h
        ori     r10,r10,EBIU0_BRCR7_VAL@l
        mtdcr   ebiu0_brcr7,r10
        lis     r10,EBIU0_BRCRH7_VAL@h
        ori     r10,r10,EBIU0_BRCRH7_VAL@l
        mtdcr   ebiu0_brcrh7,r10

        blr
        function_epilog(initb_ebiu0)


/******************************************************************************
|
| Routine:    INITB_CONFIG
|
| Purpose:    Configure the Vesta Evaluation Board.  The following items
|             will be configured:
|               1.  Cross-Bar Switch.
|               2.  Chip Interconnect.
|               3.  Clear/reset key PPC registers.
|               4.  Xilinx and GPIO Registers.
|
| Returns:    None.
|
******************************************************************************/
        function_prolog(initb_config)
        /*--------------------------------------------------------------------+
        |  Init CROSS-BAR SWITCH
        +--------------------------------------------------------------------*/
        lis     r10,CBS0_CR_VAL@h                /* r10 <- CBS Cntl Reg val  */
        ori     r10,r10,CBS0_CR_VAL@l
        mtdcr   cbs0_cr,r10

        /*--------------------------------------------------------------------+
        |  Init Chip-Interconnect (CIC) Registers
        +--------------------------------------------------------------------*/
        lis     r10,CIC0_CR_VAL@h                /* r10 <- CIC Cntl Reg val  */
        ori     r10,r10,CIC0_CR_VAL@l
        mtdcr   cic0_cr,r10

        lis     r10,CIC0_SEL3_VAL@h              /* r10 <- CIC SEL3 Reg val  */
        ori     r10,r10,CIC0_SEL3_VAL@l
        mtdcr   cic0_sel3,r10

        lis     r10,CIC0_VCR_VAL@h               /* r10 <- CIC Vid C-Reg val */
        ori     r10,r10,CIC0_VCR_VAL@l
        mtdcr   cic0_vcr,r10

        /*--------------------------------------------------------------------+
        | Clear SGR and DCWR
        +--------------------------------------------------------------------*/
        li      r10,0x0000
        mtspr   sgr,r10
        mtspr   dcwr,r10

        /*--------------------------------------------------------------------+
        | Clear/set up some machine state registers.
        +--------------------------------------------------------------------*/
        li      r10,0x0000                       /* r10 <- 0                 */
        mtdcr   ebiu0_besr,r10                   /* clr Bus Err Syndrome Reg */
        mtspr   esr,r10                          /* clr Exceptn Syndrome Reg */
        mttcr   r10                              /* timer control register   */

        mtdcr   uic0_er,r10                      /* disable all interrupts   */

	/* UIC_IIC0 | UIC_IIC1 | UIC_U0 | UIC_IR_RCV | UIC_IR_XMIT */
	lis	r10,    0x00600e00@h
	ori	r10,r10,0x00600e00@l
	mtdcr	uic0_pr,r10

	li	r10,0x00000020			/* UIC_EIR1 */
	mtdcr	uic0_tr,r10

        lis     r10,0xFFFF                       /* r10 <- 0xFFFFFFFF        */
        ori     r10,r10,0xFFFF                   /*                          */
        mtdbsr  r10                              /* clear/reset the dbsr     */
        mtdcr   uic0_sr,r10                      /* clear pending interrupts */

        li      r10,0x1000                       /* set Machine Exception bit*/
        oris    r10,r10,0x2                      /* set Criticl Exception bit*/
        mtmsr   r10                              /* change MSR               */

        /*--------------------------------------------------------------------+
        |  Clear XER.
        +--------------------------------------------------------------------*/
        li      r10,0x0000
        mtxer   r10

        /*--------------------------------------------------------------------+
        |  Init GPIO0 Registers
        +--------------------------------------------------------------------*/
        lis     r10,    STB_GPIO0_TC@h           /* Three-state control      */
        ori     r10,r10,STB_GPIO0_TC@l
        lis     r11,    GPIO0_TC_VAL@h
        ori     r11,r11,GPIO0_TC_VAL@l
        stw     r11,0(r10)

        lis     r10,    STB_GPIO0_OS_0_31@h      /* output select 0-31       */
        ori     r10,r10,STB_GPIO0_OS_0_31@l
        lis     r11,    GPIO0_OS_0_31_VAL@h
        ori     r11,r11,GPIO0_OS_0_31_VAL@l
        stw     r11,0(r10)

        lis     r10,    STB_GPIO0_OS_32_63@h     /* output select 32-63      */
        ori     r10,r10,STB_GPIO0_OS_32_63@l
        lis     r11,    GPIO0_OS_32_63_VAL@h
        ori     r11,r11,GPIO0_OS_32_63_VAL@l
        stw     r11,0(r10)

        lis     r10,    STB_GPIO0_TS_0_31@h      /* three-state select 0-31  */
        ori     r10,r10,STB_GPIO0_TS_0_31@l
        lis     r11,    GPIO0_TS_0_31_VAL@h
        ori     r11,r11,GPIO0_TS_0_31_VAL@l
        stw     r11,0(r10)

        lis     r10,    STB_GPIO0_TS_32_63@h     /* three-state select 32-63 */
        ori     r10,r10,STB_GPIO0_TS_32_63@l
        lis     r11,    GPIO0_TS_32_63_VAL@h
        ori     r11,r11,GPIO0_TS_32_63_VAL@l
        stw     r11,0(r10)

        lis     r10,    STB_GPIO0_OD@h           /* open drain               */
        ori     r10,r10,STB_GPIO0_OD@l
        lis     r11,    GPIO0_OD_VAL@h
        ori     r11,r11,GPIO0_OD_VAL@l
        stw     r11,0(r10)

        lis     r10,    STB_GPIO0_IS_1_0_31@h    /* input select 1, 0-31     */
        ori     r10,r10,STB_GPIO0_IS_1_0_31@l
        lis     r11,    GPIO0_IS_1_0_31_VAL@h
        ori     r11,r11,GPIO0_IS_1_0_31_VAL@l
        stw     r11,0(r10)

        lis     r10,    STB_GPIO0_IS_1_32_63@h   /* input select 1, 32-63    */
        ori     r10,r10,STB_GPIO0_IS_1_32_63@l
        lis     r11,    GPIO0_IS_1_32_63_VAL@h
        ori     r11,r11,GPIO0_IS_1_32_63_VAL@l
        stw     r11,0(r10)

        lis     r10,    STB_GPIO0_IS_2_0_31@h    /* input select 2, 0-31     */
        ori     r10,r10,STB_GPIO0_IS_2_0_31@l
        lis     r11,    GPIO0_IS_2_0_31_VAL@h
        ori     r11,r11,GPIO0_IS_2_0_31_VAL@l
        stw     r11,0(r10)

        lis     r10,    STB_GPIO0_IS_2_32_63@h   /* input select 2, 32-63    */
        ori     r10,r10,STB_GPIO0_IS_2_32_63@l
        lis     r11,    GPIO0_IS_2_32_63_VAL@h
        ori     r11,r11,GPIO0_IS_2_32_63_VAL@l
        stw     r11,0(r10)

        lis     r10,    STB_GPIO0_IS_3_0_31@h    /* input select 3, 0-31     */
        ori     r10,r10,STB_GPIO0_IS_3_0_31@l
        lis     r11,    GPIO0_IS_3_0_31_VAL@h
        ori     r11,r11,GPIO0_IS_3_0_31_VAL@l
        stw     r11,0(r10)

        lis     r10,    STB_GPIO0_IS_3_32_63@h   /* input select 3, 32-63    */
        ori     r10,r10,STB_GPIO0_IS_3_32_63@l
        lis     r11,    GPIO0_IS_3_32_63_VAL@h
        ori     r11,r11,GPIO0_IS_3_32_63_VAL@l
        stw     r11,0(r10)

        lis     r10,    STB_GPIO0_SS_1@h         /* sync select 1            */
        ori     r10,r10,STB_GPIO0_SS_1@l
        lis     r11,    GPIO0_SS_1_VAL@h
        ori     r11,r11,GPIO0_SS_1_VAL@l
        stw     r11,0(r10)

        lis     r10,    STB_GPIO0_SS_2@h         /* sync select 2            */
        ori     r10,r10,STB_GPIO0_SS_2@l
        lis     r11,    GPIO0_SS_2_VAL@h
        ori     r11,r11,GPIO0_SS_2_VAL@l
        stw     r11,0(r10)

        lis     r10,    STB_GPIO0_SS_3@h         /* sync select 3            */
        ori     r10,r10,STB_GPIO0_SS_3@l
        lis     r11,    GPIO0_SS_3_VAL@h
        ori     r11,r11,GPIO0_SS_3_VAL@l
        stw     r11,0(r10)

        /*--------------------------------------------------------------------+
        |  Init Xilinx #1 Registers
        +--------------------------------------------------------------------*/
        lis     r10,    STB_XILINX1_REG0@h       /* init Xilinx1 Reg 0       */
        ori     r10,r10,STB_XILINX1_REG0@l
        li      r11,XILINX1_R0_VAL
        sth     r11,0(r10)

        lis     r10,    STB_XILINX1_REG1@h       /* init Xilinx1 Reg 1       */
        ori     r10,r10,STB_XILINX1_REG1@l
        li      r11,XILINX1_R1_VAL
        sth     r11,0(r10)

        lis     r10,    STB_XILINX1_REG2@h       /* init Xilinx1 Reg 2       */
        ori     r10,r10,STB_XILINX1_REG2@l
        li      r11,XILINX1_R2_VAL
        sth     r11,0(r10)

        lis     r10,    STB_XILINX1_REG3@h       /* init Xilinx1 Reg 3       */
        ori     r10,r10,STB_XILINX1_REG3@l
        li      r11,XILINX1_R3_VAL
        sth     r11,0(r10)

        lis     r10,    STB_XILINX1_REG4@h       /* init Xilinx1 Reg 4       */
        ori     r10,r10,STB_XILINX1_REG4@l
        li      r11,XILINX1_R4_VAL
        sth     r11,0(r10)

        lis     r10,    STB_XILINX1_REG5@h       /* init Xilinx1 Reg 5       */
        ori     r10,r10,STB_XILINX1_REG5@l
        li      r11,XILINX1_R5_VAL
        sth     r11,0(r10)

        lis     r10,    STB_XILINX1_REG6@h       /* init Xilinx1 Reg 6       */
        ori     r10,r10,STB_XILINX1_REG6@l
        li      r11,XILINX1_R6_VAL
        sth     r11,0(r10)

        lis     r10,    STB_XILINX1_FLUSH@h      /* latch registers in Xilinx*/
        ori     r10,r10,STB_XILINX1_FLUSH@l
        li      r11,0x0000
        sth     r11,0(r10)

        /*--------------------------------------------------------------------+
        |  Init Xilinx #2 Registers
        +--------------------------------------------------------------------*/
        lis     r10,    STB_XILINX2_REG0@h       /* init Xilinx2 Reg 0       */
        ori     r10,r10,STB_XILINX2_REG0@l
        li      r11,XILINX2_R0_VAL
        sth     r11,0(r10)

        lis     r10,    STB_XILINX2_REG1@h       /* init Xilinx2 Reg 1       */
        ori     r10,r10,STB_XILINX2_REG1@l
        li      r11,XILINX2_R1_VAL
        sth     r11,0(r10)

        lis     r10,    STB_XILINX2_REG2@h       /* init Xilinx2 Reg 2       */
        ori     r10,r10,STB_XILINX2_REG2@l
        li      r11,XILINX2_R2_VAL
        sth     r11,0(r10)

        blr
        function_epilog(initb_config)


/******************************************************************************
|
| Routine:    INITB_HSMC0.
|
| Purpose:    Initialize the HSMC0 Registers for SDRAM
| Parameters: None.
| Returns:    R3 =  0: Successful
|                = -1: Unsuccessful, SDRAM did not reset properly.
|
******************************************************************************/
        function_prolog(initb_hsmc0)
        mflr    r0                               /* Save return addr         */

        /*--------------------------------------------------------------------+
        |  Set Global SDRAM Controller to recommended default
        +--------------------------------------------------------------------*/
        lis     r10,0x6C00
        ori     r10,r10,0x0000
        mtdcr   hsmc0_gr,r10

        /*--------------------------------------------------------------------+
        |  Set HSMC0 Data Register to recommended default
        +--------------------------------------------------------------------*/
        lis     r10,0x0037
        ori     r10,r10,0x0000
        mtdcr   hsmc0_data,r10

        /*--------------------------------------------------------------------+
        |  Init HSMC0 Bank Register 0
        +--------------------------------------------------------------------*/
        lis     r10,HSMC0_BR0_VAL@h
        ori     r10,r10,HSMC0_BR0_VAL@l
        mtdcr   hsmc0_br0,r10

        /*--------------------------------------------------------------------+
        |  Init HSMC0 Bank Register 1
        +--------------------------------------------------------------------*/
        lis     r10,HSMC0_BR1_VAL@h
        ori     r10,r10,HSMC0_BR1_VAL@l
        mtdcr   hsmc0_br1,r10

        /*--------------------------------------------------------------------+
        |  Set HSMC0 Control Reg 0
        +--------------------------------------------------------------------*/
        lis     r10,0x8077                       /* PRECHARGE ALL DEVICE BKS */
        ori     r10,r10,0x0000
        mtdcr   hsmc0_cr0,r10
        li      r3,0x0000
        bl      hsmc_cr_wait                     /* wait for op completion   */
        cmpwi   cr0,r3,0x0000
        bne     cr0,hsmc0_err

        lis     r10,0x8078                       /* AUTO-REFRESH             */
        ori     r10,r10,0x0000
        mtdcr   hsmc0_cr0,r10
        li      r3,0x0000
        bl      hsmc_cr_wait                     /* wait for op completion   */
        cmpwi   cr0,r3,0x0000
        bne     cr0,hsmc0_err

        lis     r10,0x8070                       /* PROG MODE W/DATA REG VAL */
        ori     r10,r10,0x8000
        mtdcr   hsmc0_cr0,r10
        li      r3,0x0000
        bl      hsmc_cr_wait                     /* wait for op completion   */
        cmpwi   cr0,r3,0x0000
        bne     hsmc0_err

        /*--------------------------------------------------------------------+
        |  Set HSMC0 Control Reg 1
        +--------------------------------------------------------------------*/
        lis     r10,0x8077                       /* PRECHARGE ALL DEVICE BKS */
        ori     r10,r10,0x0000
        mtdcr   hsmc0_cr1,r10
        li      r3,0x0001
        bl      hsmc_cr_wait                     /* wait for op completion   */
        cmpwi   cr0,r3,0x0000
        bne     cr0,hsmc0_err

        lis     r10,0x8078                       /* AUTO-REFRESH             */
        ori     r10,r10,0x0000
        mtdcr   hsmc0_cr1,r10
        li      r3,0x0001
        bl      hsmc_cr_wait                     /* wait for op completion   */
        cmpwi   cr0,r3,0x0000
        bne     cr0,hsmc0_err

        lis     r10,0x8070                       /* PROG MODE W/DATA REG VAL */
        ori     r10,r10,0x8000
        mtdcr   hsmc0_cr1,r10
        li      r3,0x0001
        bl      hsmc_cr_wait                     /* wait for op completion   */
        cmpwi   cr0,r3,0x0000
        bne     cr0,hsmc0_err

        /*--------------------------------------------------------------------+
        |  Set HSMC0 Refresh Register
        +--------------------------------------------------------------------*/
        lis     r10,0x0FE1
        ori     r10,r10,0x0000
        mtdcr   hsmc0_crr,r10
        li      r3,0

hsmc0_err:
        mtlr    r0
        blr
        function_epilog(initb_hsmc0)


/******************************************************************************
|
| Routine:    INITB_HSMC1.
|
| Purpose:    Initialize the HSMC1 Registers for SDRAM
| Parameters: None.
| Returns:    R3 =  0: Successful
|                = -1: Unsuccessful, SDRAM did not reset properly.
|
******************************************************************************/
        function_prolog(initb_hsmc1)
        mflr    r0                               /* Save return addr         */

        /*--------------------------------------------------------------------+
        |  Set Global SDRAM Controller to recommended default
        +--------------------------------------------------------------------*/
        lis     r10,0x6C00
        ori     r10,r10,0x0000
        mtdcr   hsmc1_gr,r10

        /*--------------------------------------------------------------------+
        |  Set HSMC1 Data Register to recommended default
        +--------------------------------------------------------------------*/
        lis     r10,0x0037
        ori     r10,r10,0x0000
        mtdcr   hsmc1_data,r10

        /*--------------------------------------------------------------------+
        |  Init HSMC1 Bank Register 0
        +--------------------------------------------------------------------*/
        lis     r10,HSMC1_BR0_VAL@h
        ori     r10,r10,HSMC1_BR0_VAL@l
        mtdcr   hsmc1_br0,r10

        /*--------------------------------------------------------------------+
        |  Init HSMC1 Bank Register 1
        +--------------------------------------------------------------------*/
        lis     r10,HSMC1_BR1_VAL@h
        ori     r10,r10,HSMC1_BR1_VAL@l
        mtdcr   hsmc1_br1,r10

        /*--------------------------------------------------------------------+
        |  Set HSMC1 Control Reg 0
        +--------------------------------------------------------------------*/
        lis     r10,0x8077                       /* PRECHARGE ALL DEVICE BANKS    */
        ori     r10,r10,0x0000
        mtdcr   hsmc1_cr0,r10
        li      r3,0x0002
        bl      hsmc_cr_wait                     /* wait for operation completion */
        cmpwi   cr0,r3,0x0000
        bne     hsmc1_err

        lis     r10,0x8078                       /* AUTO-REFRESH                  */
        ori     r10,r10,0x0000
        mtdcr   hsmc1_cr0,r10
        li      r3,0x0002
        bl      hsmc_cr_wait                     /* wait for operation completion */
        cmpwi   cr0,r3,0x0000
        bne     hsmc1_err

        lis     r10,0x8070                       /* PROGRAM MODE W/DATA REG VALUE */
        ori     r10,r10,0x8000
        mtdcr   hsmc1_cr0,r10
        li      r3,0x0002
        bl      hsmc_cr_wait                     /* wait for operation completion */
        cmpwi   cr0,r3,0x0000
        bne     hsmc1_err

        /*--------------------------------------------------------------------+
        |  Set HSMC1 Control Reg 1
        +--------------------------------------------------------------------*/
        lis     r10,0x8077                       /* PRECHARGE ALL DEVICE BKS */
        ori     r10,r10,0x0000
        mtdcr   hsmc1_cr1,r10
        li      r3,0x0003
        bl      hsmc_cr_wait                     /* wait for op completion   */
        cmpwi   cr0,r3,0x0000
        bne     hsmc1_err

        lis     r10,0x8078                       /* AUTO-REFRESH             */
        ori     r10,r10,0x0000
        mtdcr   hsmc1_cr1,r10
        li      r3,0x0003
        bl      hsmc_cr_wait                     /* wait for op completion   */
        cmpwi   cr0,r3,0x0000
        bne     hsmc1_err

        lis     r10,0x8070                       /* PROG MODE W/DATA REG VAL */
        ori     r10,r10,0x8000
        mtdcr   hsmc1_cr1,r10
        li      r3,0x0003
        bl      hsmc_cr_wait                     /* wait for op completion   */
        cmpwi   cr0,r3,0x0000
        bne     hsmc1_err

        /*--------------------------------------------------------------------+
        |  Set HSMC1 Refresh Register
        +--------------------------------------------------------------------*/
        lis     r10,0x0FE1
        ori     r10,r10,0x0000
        mtdcr   hsmc1_crr,r10
        xor     r3,r3,r3

hsmc1_err:
        mtlr    r0
        blr
        function_epilog(initb_hsmc1)


/******************************************************************************
|
| Routine:    INITB_CACHE
|
| Purpose:    This routine will enable Data and Instruction Cache.
|             The Data Cache is an 8K two-way set associative and the
|             Instruction Cache is an 16K two-way set associative cache.
|
| Parameters: None.
|
| Returns:    None.
|
******************************************************************************/
        function_prolog(initb_cache)
        mflr    r0                               /* Save return addr         */

        bl      initb_Dcache                     /* enable D-Cache           */
        bl      initb_Icache                     /* enable I-Cache           */

        mtlr    r0
        blr
       function_epilog(initb_cache)


/******************************************************************************
|
| Routine:    INITB_DCACHE
|
| Purpose:    This routine will invalidate all data in the Data Cache and
|             then enable D-Cache.  If cache is enabled already, the D-Cache
|             will be flushed before the data is invalidated.
|
| Parameters: None.
|
| Returns:    None.
|
******************************************************************************/
        function_prolog(initb_Dcache)
        /*--------------------------------------------------------------------+
        |  Flush Data Cache if enabled
        +--------------------------------------------------------------------*/
        mfdccr  r10                              /* r10 <- DCCR              */
        isync                                    /* ensure prev insts done   */
        cmpwi   r10,0x00
        beq     ic_dcinv                         /* D-cache off, invalidate  */

        /*--------------------------------------------------------------------+
        |  Data Cache enabled, force known memory addresses to be Cached
        +--------------------------------------------------------------------*/
        lis     r10,HSMC0_BR0_VAL@h              /* r10 <- first memory loc  */
        andis.  r10,r10,0xFFF0
        li      r11,DCACHE_NLINES                /* r11 <- # A-way addresses */
        addi    r11,r11,DCACHE_NLINES            /* r11 <- # B-way addresses */
        mtctr   r11                              /* set loop counter         */

ic_dcload:
        lwz     r12,0(r10)                       /* force cache of address   */
        addi    r10,r10,DCACHE_NBYTES            /* r10 <- next memory loc   */
        bdnz    ic_dcload
        sync                                     /* ensure prev insts done   */
        isync

        /*--------------------------------------------------------------------+
        |  Flush the known memory addresses from Cache
        +--------------------------------------------------------------------*/
        lis     r10,HSMC0_BR0_VAL@h              /* r10 <- first memory loc  */
        andis.  r10,r10,0xFFF0
        mtctr   r11                              /* set loop counter         */

ic_dcflush:
        dcbf    0,r10                            /* flush D-cache line       */
        addi    r10,r10,DCACHE_NBYTES            /* r10 <- next memory loc   */
        bdnz    ic_dcflush
        sync                                     /* ensure prev insts done   */
        isync

        /*--------------------------------------------------------------------+
        |  Disable then invalidate Data Cache
        +--------------------------------------------------------------------*/
        li      r10,0                            /* r10 <- 0                 */
        mtdccr  r10                              /* disable the D-Cache      */
        isync                                    /* ensure prev insts done   */

ic_dcinv:
        li      r10,0                            /* r10 <- line address      */
        li      r11,DCACHE_NLINES                /* r11 <- # lines in cache  */
        mtctr   r11                              /* set loop counter         */

ic_dcloop:
        dccci   0,r10                            /* invalidate A/B cache lns */
        addi    r10,r10,DCACHE_NBYTES            /* bump to next line        */
        bdnz    ic_dcloop
        sync                                     /* ensure prev insts done   */
        isync

        /*--------------------------------------------------------------------+
        |  Enable Data Cache
        +--------------------------------------------------------------------*/
        lis     r10,DCACHE_ENABLE@h              /* r10 <- D-cache enable msk*/
        ori     r10,r10,DCACHE_ENABLE@l
        mtdccr  r10
        sync                                     /* ensure prev insts done   */
        isync

        blr
        function_epilog(initb_Dcache)


/******************************************************************************
|
| Routine:    INITB_ICACHE
|
| Purpose:    This routine will invalidate all data in the Instruction
|             Cache then enable I-Cache.
|
| Parameters: None.
|
| Returns:    None.
|
******************************************************************************/
        function_prolog(initb_Icache)
        /*--------------------------------------------------------------------+
        |  Invalidate Instruction Cache
        +--------------------------------------------------------------------*/
        li      r10,0                            /* r10 <- lines address     */
        iccci   0,r10                            /* invalidate all I-cache   */
        sync                                     /* ensure prev insts done   */
        isync

        /*--------------------------------------------------------------------+
        |  Enable Instruction Cache
        +--------------------------------------------------------------------*/
        lis     r10,ICACHE_ENABLE@h              /* r10 <- I-cache enable msk*/
        ori     r10,r10,ICACHE_ENABLE@l
        mticcr  r10
        sync                                     /* ensure prev insts done   */
        isync

        blr
        function_epilog(initb_Icache)

#if 0
/******************************************************************************
|
| Routine:    INITB_GET_CSPD
|
| Purpose:    Determine the CPU Core Speed.  The 13.5 Mhz Time Base
|             Counter (TBC) is used to measure a conditional branch
|             instruction.
|
| Parameters: R3 = Address of Bus Speed
|             R4 = Address of Core Speed
|
| Returns:    (R3) = >0: Bus Speed.
|                     0: Bus Speed not found in Look-Up Table.
|             (R4) = >0: Core Speed.
|                     0: Core Speed not found in Look-Up Table.
|
| Note:       1. This routine assumes the bdnz branch instruction takes
|                two instruction cycles to complete.
|             2. This routine must be called before interrupts are enabled.
|
******************************************************************************/
        function_prolog(initb_get_cspd)
        mflr    r0                               /* Save return address      */
        /*--------------------------------------------------------------------+
        |  Set-up timed loop
        +--------------------------------------------------------------------*/
        lis     r9,gcs_time_loop@h               /* r9  <- addr loop instr   */
        ori     r9,r9,gcs_time_loop@l
        lis     r10,GCS_LCNT@h                   /* r10 <- loop count        */
        ori     r10,r10,GCS_LCNT@l
        mtctr   r10                              /* ctr <- loop count        */
        lis     r11,STB_TIMERS_TBC@h             /* r11 <- TBC register addr */
        ori     r11,r11,STB_TIMERS_TBC@l
        li      r12,0                            /* r12 <- 0                 */

        /*--------------------------------------------------------------------+
        |  Cache timed-loop instruction
        +--------------------------------------------------------------------*/
        icbt    0,r9
        sync
        isync

        /*--------------------------------------------------------------------+
        |  Get number of 13.5 Mhz cycles to execute time-loop
        +--------------------------------------------------------------------*/
        stw     r12,0(r11)                       /* reset TBC                */
gcs_time_loop:
        bdnz+   gcs_time_loop                    /* force branch pred taken  */
        lwz     r5,0(r11)                        /* r5 <- num 13.5 Mhz ticks */
        li      r6,5                             /* LUT based on 1/5th the...*/
        divw    r5,r5,r6                         /*..loop count used         */
        sync
        isync

        /*--------------------------------------------------------------------+
        |  Look-up core speed based on TBC value
        +--------------------------------------------------------------------*/
        lis     r6,gcs_lookup_table@h            /* r6 <- pts at core spd LUT*/
        ori     r6,r6,gcs_lookup_table@l
        bl      gcs_cspd_lookup                  /* find core speed in LUT   */

        mtlr    r0                               /* set return address       */
        blr
        function_epilog(initb_get_cspd)

#endif
/*****************************************************************************+
| XXXX   XX   XX   XXXXXX  XXXXXXX  XXXXXX   XX   XX     XX    XXXX
|  XX    XXX  XX   X XX X   XX   X   XX  XX  XXX  XX    XXXX    XX
|  XX    XXXX XX     XX     XX X     XX  XX  XXXX XX   XX  XX   XX
|  XX    XX XXXX     XX     XXXX     XXXXX   XX XXXX   XX  XX   XX
|  XX    XX  XXX     XX     XX X     XX XX   XX  XXX   XXXXXX   XX
|  XX    XX   XX     XX     XX   X   XX  XX  XX   XX   XX  XX   XX  XX
| XXXX   XX   XX    XXXX   XXXXXXX  XXX  XX  XX   XX   XX  XX  XXXXXXX
+*****************************************************************************/
/******************************************************************************
|
| Routine:    HSMC_CR_WAIT
|
| Purpose:    Wait for the HSMC Control Register (bits 12-16) to be reset
|             after an auto-refresh, pre-charge or program mode register
|             command execution.
|
| Parameters: R3 = HSMC Control Register ID.
|                  0: HSMC0 CR0
|                  1: HSMC0 CR1
|                  2: HSMC1 CR0
|                  3: HSMC1 CR1
|
| Returns:    R3 = 0: Successful
|                 -1: Unsuccessful
|
******************************************************************************/
hsmc_cr_wait:

        li      r11,10                           /* r11 <- retry counter     */
        mtctr   r11                              /* set retry counter        */
        mr      r11,r3                           /* r11 <- HSMC CR reg id    */

hsmc_cr_rep:
        bdz     hsmc_cr_err                      /* branch if max retries hit*/

        /*--------------------------------------------------------------------+
        |  GET HSMCx_CRx value based on HSMC Control Register ID
        +--------------------------------------------------------------------*/
try_hsmc0_cr0:                                   /* CHECK IF ID=HSMC0 CR0 REG*/
        cmpwi   cr0,r11,0x0000
        bne     cr0,try_hsmc0_cr1
        mfdcr   r10,hsmc0_cr0                    /* r11 <- HSMC0 CR0 value   */
        b       hsmc_cr_read

try_hsmc0_cr1:                                   /* CHECK IF ID=HSMC0 CR1 REG*/
        cmpwi   cr0,r11,0x0001
        bne     cr0,try_hsmc1_cr0
        mfdcr   r10,hsmc0_cr1                    /* r10 <- HSMC0 CR1 value   */
        b       hsmc_cr_read

try_hsmc1_cr0:                                   /* CHECK IF ID=HSMC1 CR0 REG*/
        cmpwi   cr0,r11,0x0002
        bne     cr0,try_hsmc1_cr1
        mfdcr   r10,hsmc1_cr0                    /* r10 <- HSMC1 CR0 value   */
        b       hsmc_cr_read

try_hsmc1_cr1:                                   /* CHECK IF ID=HSMC1 CR1 REG*/
        cmpwi   cr0,r11,0x0003
        bne     cr0,hsmc_cr_err
        mfdcr   r10,hsmc1_cr1                    /* r10 <- HSMC1 CR1 value   */

        /*--------------------------------------------------------------------+
        |  Check if HSMC CR register was reset after command execution
        +--------------------------------------------------------------------*/
hsmc_cr_read:
        lis     r12,0x000F                       /* create "AND" mask        */
        ori     r12,r12,0x8000
        and.    r10,r10,r12                      /* r10 <- HSMC CR bits 12-16*/
        bne     cr0,hsmc_cr_rep                  /* wait for bits to reset   */
        li      r3,0                             /* set return code = success*/
        b       hsmc_cr_done

hsmc_cr_err:                                     /* ERROR: SDRAM didn't reset*/
        li      r3,-1                            /* set RC=unsuccessful      */

hsmc_cr_done:
        blr

#if 0
/******************************************************************************
|
| Routine:    GCS_CSPD_LOOKUP
|
| Purpose:    Uses the number of 13.5 Mhz clock ticks found after executing
|             the branch instruction time loop to look-up the CPU Core Speed
|             in the Core Speed Look-up Table.
|
| Parameters: R3 = Address of Bus Speed
|             R4 = Address of Core Speed
|             R5 = Number of 13.5 Mhz clock ticks found in time loop.
|             R6 = Pointer to Core-Speed Look-Up Table
|
| Returns:    (R3) = >0: Bus Speed.
|                     0: Bus Speed not found in Look-Up Table.
|             (R4) = >0: Core Speed.
|                     0: Core Speed not found in Look-Up Table.
|
| Note:       Core Speed = Bus Speed * Mult Factor (1-4x).
|
******************************************************************************/
gcs_cspd_lookup:

        li      r9,1                             /* r9 <- core speed mult    */
        /*--------------------------------------------------------------------+
        |  Get theoritical number 13.5 Mhz ticks for a given Bus Speed from
        |  Look-up Table.  Check all mult factors to determine if calculated
        |  value matches theoretical value (within a tolerance).
        +--------------------------------------------------------------------*/
gcs_cspd_loop:
        lwz     r10,0(r6)                        /* r10 <- no. ticks from LUT*/
        divw    r10,r10,r9                       /* r10 <- div mult (1-4x)   */
        subi    r11,r10,GCS_CTICK_TOL            /* r11 <- no. tks low range */
        addi    r12,r10,GCS_CTICK_TOL            /* r12 <- no. tks high range*/

        cmpw    cr0,r5,r11                       /* calc value within range? */
        blt     gcs_cspd_retry                   /* less than low range      */
        cmpw    cr0,r5,r12
        bgt     gcs_cspd_retry                   /* greater than high range  */
        b       gcs_cspd_fnd                     /* calc value within range  */

        /*--------------------------------------------------------------------+
        |  SO FAR CORE SPEED NOT FOUND: Check next mult factor
        +--------------------------------------------------------------------*/
gcs_cspd_retry:
        addi    r9,r9,1                          /* bump mult factor (1-4x)  */
        cmpwi   cr0,r9,GCS_NMULT
        ble     gcs_cspd_loop

        /*--------------------------------------------------------------------+
        |  SO FAR CORE SPEED NOT FOUND: Point at next Bus Speed in LUT
        +--------------------------------------------------------------------*/
        li      r9,1                             /* reset mult factor        */
        addi    r6,r6,GCS_TROW_BYTES             /* point at next table entry*/
        lwz     r10,0(r6)
        cmpwi   cr0,r10,0                        /* check for EOT flag       */
        bne     gcs_cspd_loop

        /*--------------------------------------------------------------------+
        |  COMPUTE CORE SPEED AND GET BUS SPEED FROM LOOK-UP TABLE
        +--------------------------------------------------------------------*/
gcs_cspd_fnd:
        lwz     r5,4(r6)                         /*  r5  <- Bus Speed in LUT */
        mullw   r6,r5,r9                         /*  r6  <- Core speed       */
        stw     r5,0(r3)                         /* (r3) <- Bus Speed        */
        stw     r6,0(r4)                         /* (r4) <- Core Speed       */

        blr
#endif
