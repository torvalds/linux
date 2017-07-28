/*
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the Clear BSD license or the GPL-2 (or later)
 */

#ifndef _DEF_BF60X_H
#define _DEF_BF60X_H


/* ************************************************************** */
/*   SYSTEM & MMR ADDRESS DEFINITIONS COMMON TO ALL ADSP-BF60x    */
/* ************************************************************** */


/* =========================
        CNT Registers
   ========================= */

/* =========================
        CNT0
   ========================= */
#define CNT_CONFIG                 0xFFC00400         /* CNT0 Configuration Register */
#define CNT_IMASK                  0xFFC00404         /* CNT0 Interrupt Mask Register */
#define CNT_STATUS                 0xFFC00408         /* CNT0 Status Register */
#define CNT_COMMAND                0xFFC0040C         /* CNT0 Command Register */
#define CNT_DEBOUNCE               0xFFC00410         /* CNT0 Debounce Register */
#define CNT_COUNTER                0xFFC00414         /* CNT0 Counter Register */
#define CNT_MAX                    0xFFC00418         /* CNT0 Maximum Count Register */
#define CNT_MIN                    0xFFC0041C         /* CNT0 Minimum Count Register */


/* =========================
        RSI Registers
   ========================= */

#define RSI_CLK_CONTROL            0xFFC00604         /* RSI0 Clock Control Register */
#define RSI_ARGUMENT               0xFFC00608         /* RSI0 Argument Register */
#define RSI_COMMAND                0xFFC0060C         /* RSI0 Command Register */
#define RSI_RESP_CMD               0xFFC00610         /* RSI0 Response Command Register */
#define RSI_RESPONSE0              0xFFC00614         /* RSI0 Response 0 Register */
#define RSI_RESPONSE1              0xFFC00618         /* RSI0 Response 1 Register */
#define RSI_RESPONSE2              0xFFC0061C         /* RSI0 Response 2 Register */
#define RSI_RESPONSE3              0xFFC00620         /* RSI0 Response 3 Register */
#define RSI_DATA_TIMER             0xFFC00624         /* RSI0 Data Timer Register */
#define RSI_DATA_LGTH              0xFFC00628         /* RSI0 Data Length Register */
#define RSI_DATA_CONTROL           0xFFC0062C         /* RSI0 Data Control Register */
#define RSI_DATA_CNT               0xFFC00630         /* RSI0 Data Count Register */
#define RSI_STATUS                 0xFFC00634         /* RSI0 Status Register */
#define RSI_STATUSCL               0xFFC00638         /* RSI0 Status Clear Register */
#define RSI_MASK0                  0xFFC0063C         /* RSI0 Interrupt 0 Mask Register */
#define RSI_MASK1                  0xFFC00640         /* RSI0 Interrupt 1 Mask Register */
#define RSI_FIFO_CNT               0xFFC00648         /* RSI0 FIFO Counter Register */
#define RSI_CEATA_CONTROL          0xFFC0064C         /* RSI0 This register contains bit to dis CCS gen */
#define RSI_BOOT_TCNTR             0xFFC00650         /* RSI0 Boot Timing Counter Register */
#define RSI_BACK_TOUT              0xFFC00654         /* RSI0 Boot Acknowledge Timeout Register */
#define RSI_SLP_WKUP_TOUT          0xFFC00658         /* RSI0 Sleep Wakeup Timeout Register */
#define RSI_BLKSZ                  0xFFC0065C         /* RSI0 Block Size Register */
#define RSI_FIFO                   0xFFC00680         /* RSI0 Data FIFO Register */
#define RSI_ESTAT                  0xFFC006C0         /* RSI0 Exception Status Register */
#define RSI_EMASK                  0xFFC006C4         /* RSI0 Exception Mask Register */
#define RSI_CONFIG                 0xFFC006C8         /* RSI0 Configuration Register */
#define RSI_RD_WAIT_EN             0xFFC006CC         /* RSI0 Read Wait Enable Register */
#define RSI_PID0                   0xFFC006D0         /* RSI0 Peripheral Identification Register */
#define RSI_PID1                   0xFFC006D4         /* RSI0 Peripheral Identification Register */
#define RSI_PID2                   0xFFC006D8         /* RSI0 Peripheral Identification Register */
#define RSI_PID3                   0xFFC006DC         /* RSI0 Peripheral Identification Register */

/* =========================
        CAN Registers
   ========================= */

/* =========================
        CAN0
   ========================= */
#define CAN0_MC1                    0xFFC00A00         /* CAN0 Mailbox Configuration Register 1 */
#define CAN0_MD1                    0xFFC00A04         /* CAN0 Mailbox Direction Register 1 */
#define CAN0_TRS1                   0xFFC00A08         /* CAN0 Transmission Request Set Register 1 */
#define CAN0_TRR1                   0xFFC00A0C         /* CAN0 Transmission Request Reset Register 1 */
#define CAN0_TA1                    0xFFC00A10         /* CAN0 Transmission Acknowledge Register 1 */
#define CAN0_AA1                    0xFFC00A14         /* CAN0 Abort Acknowledge Register 1 */
#define CAN0_RMP1                   0xFFC00A18         /* CAN0 Receive Message Pending Register 1 */
#define CAN0_RML1                   0xFFC00A1C         /* CAN0 Receive Message Lost Register 1 */
#define CAN0_MBTIF1                 0xFFC00A20         /* CAN0 Mailbox Transmit Interrupt Flag Register 1 */
#define CAN0_MBRIF1                 0xFFC00A24         /* CAN0 Mailbox Receive Interrupt Flag Register 1 */
#define CAN0_MBIM1                  0xFFC00A28         /* CAN0 Mailbox Interrupt Mask Register 1 */
#define CAN0_RFH1                   0xFFC00A2C         /* CAN0 Remote Frame Handling Register 1 */
#define CAN0_OPSS1                  0xFFC00A30         /* CAN0 Overwrite Protection/Single Shot Transmission Register 1 */
#define CAN0_MC2                    0xFFC00A40         /* CAN0 Mailbox Configuration Register 2 */
#define CAN0_MD2                    0xFFC00A44         /* CAN0 Mailbox Direction Register 2 */
#define CAN0_TRS2                   0xFFC00A48         /* CAN0 Transmission Request Set Register 2 */
#define CAN0_TRR2                   0xFFC00A4C         /* CAN0 Transmission Request Reset Register 2 */
#define CAN0_TA2                    0xFFC00A50         /* CAN0 Transmission Acknowledge Register 2 */
#define CAN0_AA2                    0xFFC00A54         /* CAN0 Abort Acknowledge Register 2 */
#define CAN0_RMP2                   0xFFC00A58         /* CAN0 Receive Message Pending Register 2 */
#define CAN0_RML2                   0xFFC00A5C         /* CAN0 Receive Message Lost Register 2 */
#define CAN0_MBTIF2                 0xFFC00A60         /* CAN0 Mailbox Transmit Interrupt Flag Register 2 */
#define CAN0_MBRIF2                 0xFFC00A64         /* CAN0 Mailbox Receive Interrupt Flag Register 2 */
#define CAN0_MBIM2                  0xFFC00A68         /* CAN0 Mailbox Interrupt Mask Register 2 */
#define CAN0_RFH2                   0xFFC00A6C         /* CAN0 Remote Frame Handling Register 2 */
#define CAN0_OPSS2                  0xFFC00A70         /* CAN0 Overwrite Protection/Single Shot Transmission Register 2 */
#define CAN0_CLOCK                    0xFFC00A80         /* CAN0 Clock Register */
#define CAN0_TIMING                 0xFFC00A84         /* CAN0 Timing Register */
#define CAN0_DEBUG                    0xFFC00A88         /* CAN0 Debug Register */
#define CAN0_STATUS                   0xFFC00A8C         /* CAN0 Status Register */
#define CAN0_CEC                    0xFFC00A90         /* CAN0 Error Counter Register */
#define CAN0_GIS                    0xFFC00A94         /* CAN0 Global CAN Interrupt Status */
#define CAN0_GIM                    0xFFC00A98         /* CAN0 Global CAN Interrupt Mask */
#define CAN0_GIF                    0xFFC00A9C         /* CAN0 Global CAN Interrupt Flag */
#define CAN0_CONTROL                    0xFFC00AA0         /* CAN0 CAN Master Control Register */
#define CAN0_INTR                    0xFFC00AA4         /* CAN0 Interrupt Pending Register */
#define CAN0_MBTD                   0xFFC00AAC         /* CAN0 Temporary Mailbox Disable Register */
#define CAN0_EWR                    0xFFC00AB0         /* CAN0 Error Counter Warning Level Register */
#define CAN0_ESR                    0xFFC00AB4         /* CAN0 Error Status Register */
#define CAN0_UCCNT                  0xFFC00AC4         /* CAN0 Universal Counter Register */
#define CAN0_UCRC                   0xFFC00AC8         /* CAN0 Universal Counter Reload/Capture Register */
#define CAN0_UCCNF                  0xFFC00ACC         /* CAN0 Universal Counter Configuration Mode Register */
#define CAN0_AM00L                  0xFFC00B00         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM01L                  0xFFC00B08         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM02L                  0xFFC00B10         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM03L                  0xFFC00B18         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM04L                  0xFFC00B20         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM05L                  0xFFC00B28         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM06L                  0xFFC00B30         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM07L                  0xFFC00B38         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM08L                  0xFFC00B40         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM09L                  0xFFC00B48         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM10L                  0xFFC00B50         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM11L                  0xFFC00B58         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM12L                  0xFFC00B60         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM13L                  0xFFC00B68         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM14L                  0xFFC00B70         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM15L                  0xFFC00B78         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM16L                  0xFFC00B80         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM17L                  0xFFC00B88         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM18L                  0xFFC00B90         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM19L                  0xFFC00B98         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM20L                  0xFFC00BA0         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM21L                  0xFFC00BA8         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM22L                  0xFFC00BB0         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM23L                  0xFFC00BB8         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM24L                  0xFFC00BC0         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM25L                  0xFFC00BC8         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM26L                  0xFFC00BD0         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM27L                  0xFFC00BD8         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM28L                  0xFFC00BE0         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM29L                  0xFFC00BE8         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM30L                  0xFFC00BF0         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM31L                  0xFFC00BF8         /* CAN0 Acceptance Mask Register (L) */
#define CAN0_AM00H                  0xFFC00B04         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM01H                  0xFFC00B0C         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM02H                  0xFFC00B14         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM03H                  0xFFC00B1C         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM04H                  0xFFC00B24         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM05H                  0xFFC00B2C         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM06H                  0xFFC00B34         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM07H                  0xFFC00B3C         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM08H                  0xFFC00B44         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM09H                  0xFFC00B4C         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM10H                  0xFFC00B54         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM11H                  0xFFC00B5C         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM12H                  0xFFC00B64         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM13H                  0xFFC00B6C         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM14H                  0xFFC00B74         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM15H                  0xFFC00B7C         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM16H                  0xFFC00B84         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM17H                  0xFFC00B8C         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM18H                  0xFFC00B94         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM19H                  0xFFC00B9C         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM20H                  0xFFC00BA4         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM21H                  0xFFC00BAC         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM22H                  0xFFC00BB4         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM23H                  0xFFC00BBC         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM24H                  0xFFC00BC4         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM25H                  0xFFC00BCC         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM26H                  0xFFC00BD4         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM27H                  0xFFC00BDC         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM28H                  0xFFC00BE4         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM29H                  0xFFC00BEC         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM30H                  0xFFC00BF4         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_AM31H                  0xFFC00BFC         /* CAN0 Acceptance Mask Register (H) */
#define CAN0_MB00_DATA0             0xFFC00C00         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB01_DATA0             0xFFC00C20         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB02_DATA0             0xFFC00C40         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB03_DATA0             0xFFC00C60         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB04_DATA0             0xFFC00C80         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB05_DATA0             0xFFC00CA0         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB06_DATA0             0xFFC00CC0         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB07_DATA0             0xFFC00CE0         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB08_DATA0             0xFFC00D00         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB09_DATA0             0xFFC00D20         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB10_DATA0             0xFFC00D40         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB11_DATA0             0xFFC00D60         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB12_DATA0             0xFFC00D80         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB13_DATA0             0xFFC00DA0         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB14_DATA0             0xFFC00DC0         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB15_DATA0             0xFFC00DE0         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB16_DATA0             0xFFC00E00         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB17_DATA0             0xFFC00E20         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB18_DATA0             0xFFC00E40         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB19_DATA0             0xFFC00E60         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB20_DATA0             0xFFC00E80         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB21_DATA0             0xFFC00EA0         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB22_DATA0             0xFFC00EC0         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB23_DATA0             0xFFC00EE0         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB24_DATA0             0xFFC00F00         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB25_DATA0             0xFFC00F20         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB26_DATA0             0xFFC00F40         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB27_DATA0             0xFFC00F60         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB28_DATA0             0xFFC00F80         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB29_DATA0             0xFFC00FA0         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB30_DATA0             0xFFC00FC0         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB31_DATA0             0xFFC00FE0         /* CAN0 Mailbox Word 0 Register */
#define CAN0_MB00_DATA1             0xFFC00C04         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB01_DATA1             0xFFC00C24         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB02_DATA1             0xFFC00C44         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB03_DATA1             0xFFC00C64         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB04_DATA1             0xFFC00C84         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB05_DATA1             0xFFC00CA4         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB06_DATA1             0xFFC00CC4         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB07_DATA1             0xFFC00CE4         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB08_DATA1             0xFFC00D04         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB09_DATA1             0xFFC00D24         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB10_DATA1             0xFFC00D44         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB11_DATA1             0xFFC00D64         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB12_DATA1             0xFFC00D84         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB13_DATA1             0xFFC00DA4         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB14_DATA1             0xFFC00DC4         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB15_DATA1             0xFFC00DE4         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB16_DATA1             0xFFC00E04         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB17_DATA1             0xFFC00E24         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB18_DATA1             0xFFC00E44         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB19_DATA1             0xFFC00E64         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB20_DATA1             0xFFC00E84         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB21_DATA1             0xFFC00EA4         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB22_DATA1             0xFFC00EC4         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB23_DATA1             0xFFC00EE4         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB24_DATA1             0xFFC00F04         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB25_DATA1             0xFFC00F24         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB26_DATA1             0xFFC00F44         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB27_DATA1             0xFFC00F64         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB28_DATA1             0xFFC00F84         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB29_DATA1             0xFFC00FA4         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB30_DATA1             0xFFC00FC4         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB31_DATA1             0xFFC00FE4         /* CAN0 Mailbox Word 1 Register */
#define CAN0_MB00_DATA2             0xFFC00C08         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB01_DATA2             0xFFC00C28         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB02_DATA2             0xFFC00C48         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB03_DATA2             0xFFC00C68         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB04_DATA2             0xFFC00C88         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB05_DATA2             0xFFC00CA8         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB06_DATA2             0xFFC00CC8         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB07_DATA2             0xFFC00CE8         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB08_DATA2             0xFFC00D08         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB09_DATA2             0xFFC00D28         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB10_DATA2             0xFFC00D48         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB11_DATA2             0xFFC00D68         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB12_DATA2             0xFFC00D88         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB13_DATA2             0xFFC00DA8         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB14_DATA2             0xFFC00DC8         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB15_DATA2             0xFFC00DE8         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB16_DATA2             0xFFC00E08         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB17_DATA2             0xFFC00E28         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB18_DATA2             0xFFC00E48         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB19_DATA2             0xFFC00E68         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB20_DATA2             0xFFC00E88         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB21_DATA2             0xFFC00EA8         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB22_DATA2             0xFFC00EC8         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB23_DATA2             0xFFC00EE8         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB24_DATA2             0xFFC00F08         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB25_DATA2             0xFFC00F28         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB26_DATA2             0xFFC00F48         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB27_DATA2             0xFFC00F68         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB28_DATA2             0xFFC00F88         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB29_DATA2             0xFFC00FA8         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB30_DATA2             0xFFC00FC8         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB31_DATA2             0xFFC00FE8         /* CAN0 Mailbox Word 2 Register */
#define CAN0_MB00_DATA3             0xFFC00C0C         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB01_DATA3             0xFFC00C2C         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB02_DATA3             0xFFC00C4C         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB03_DATA3             0xFFC00C6C         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB04_DATA3             0xFFC00C8C         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB05_DATA3             0xFFC00CAC         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB06_DATA3             0xFFC00CCC         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB07_DATA3             0xFFC00CEC         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB08_DATA3             0xFFC00D0C         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB09_DATA3             0xFFC00D2C         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB10_DATA3             0xFFC00D4C         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB11_DATA3             0xFFC00D6C         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB12_DATA3             0xFFC00D8C         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB13_DATA3             0xFFC00DAC         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB14_DATA3             0xFFC00DCC         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB15_DATA3             0xFFC00DEC         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB16_DATA3             0xFFC00E0C         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB17_DATA3             0xFFC00E2C         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB18_DATA3             0xFFC00E4C         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB19_DATA3             0xFFC00E6C         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB20_DATA3             0xFFC00E8C         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB21_DATA3             0xFFC00EAC         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB22_DATA3             0xFFC00ECC         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB23_DATA3             0xFFC00EEC         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB24_DATA3             0xFFC00F0C         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB25_DATA3             0xFFC00F2C         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB26_DATA3             0xFFC00F4C         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB27_DATA3             0xFFC00F6C         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB28_DATA3             0xFFC00F8C         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB29_DATA3             0xFFC00FAC         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB30_DATA3             0xFFC00FCC         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB31_DATA3             0xFFC00FEC         /* CAN0 Mailbox Word 3 Register */
#define CAN0_MB00_LENGTH            0xFFC00C10         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB01_LENGTH            0xFFC00C30         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB02_LENGTH            0xFFC00C50         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB03_LENGTH            0xFFC00C70         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB04_LENGTH            0xFFC00C90         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB05_LENGTH            0xFFC00CB0         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB06_LENGTH            0xFFC00CD0         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB07_LENGTH            0xFFC00CF0         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB08_LENGTH            0xFFC00D10         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB09_LENGTH            0xFFC00D30         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB10_LENGTH            0xFFC00D50         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB11_LENGTH            0xFFC00D70         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB12_LENGTH            0xFFC00D90         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB13_LENGTH            0xFFC00DB0         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB14_LENGTH            0xFFC00DD0         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB15_LENGTH            0xFFC00DF0         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB16_LENGTH            0xFFC00E10         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB17_LENGTH            0xFFC00E30         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB18_LENGTH            0xFFC00E50         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB19_LENGTH            0xFFC00E70         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB20_LENGTH            0xFFC00E90         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB21_LENGTH            0xFFC00EB0         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB22_LENGTH            0xFFC00ED0         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB23_LENGTH            0xFFC00EF0         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB24_LENGTH            0xFFC00F10         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB25_LENGTH            0xFFC00F30         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB26_LENGTH            0xFFC00F50         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB27_LENGTH            0xFFC00F70         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB28_LENGTH            0xFFC00F90         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB29_LENGTH            0xFFC00FB0         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB30_LENGTH            0xFFC00FD0         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB31_LENGTH            0xFFC00FF0         /* CAN0 Mailbox Word 4 Register */
#define CAN0_MB00_TIMESTAMP         0xFFC00C14         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB01_TIMESTAMP         0xFFC00C34         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB02_TIMESTAMP         0xFFC00C54         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB03_TIMESTAMP         0xFFC00C74         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB04_TIMESTAMP         0xFFC00C94         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB05_TIMESTAMP         0xFFC00CB4         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB06_TIMESTAMP         0xFFC00CD4         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB07_TIMESTAMP         0xFFC00CF4         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB08_TIMESTAMP         0xFFC00D14         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB09_TIMESTAMP         0xFFC00D34         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB10_TIMESTAMP         0xFFC00D54         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB11_TIMESTAMP         0xFFC00D74         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB12_TIMESTAMP         0xFFC00D94         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB13_TIMESTAMP         0xFFC00DB4         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB14_TIMESTAMP         0xFFC00DD4         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB15_TIMESTAMP         0xFFC00DF4         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB16_TIMESTAMP         0xFFC00E14         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB17_TIMESTAMP         0xFFC00E34         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB18_TIMESTAMP         0xFFC00E54         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB19_TIMESTAMP         0xFFC00E74         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB20_TIMESTAMP         0xFFC00E94         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB21_TIMESTAMP         0xFFC00EB4         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB22_TIMESTAMP         0xFFC00ED4         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB23_TIMESTAMP         0xFFC00EF4         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB24_TIMESTAMP         0xFFC00F14         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB25_TIMESTAMP         0xFFC00F34         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB26_TIMESTAMP         0xFFC00F54         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB27_TIMESTAMP         0xFFC00F74         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB28_TIMESTAMP         0xFFC00F94         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB29_TIMESTAMP         0xFFC00FB4         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB30_TIMESTAMP         0xFFC00FD4         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB31_TIMESTAMP         0xFFC00FF4         /* CAN0 Mailbox Word 5 Register */
#define CAN0_MB00_ID0               0xFFC00C18         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB01_ID0               0xFFC00C38         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB02_ID0               0xFFC00C58         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB03_ID0               0xFFC00C78         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB04_ID0               0xFFC00C98         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB05_ID0               0xFFC00CB8         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB06_ID0               0xFFC00CD8         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB07_ID0               0xFFC00CF8         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB08_ID0               0xFFC00D18         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB09_ID0               0xFFC00D38         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB10_ID0               0xFFC00D58         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB11_ID0               0xFFC00D78         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB12_ID0               0xFFC00D98         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB13_ID0               0xFFC00DB8         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB14_ID0               0xFFC00DD8         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB15_ID0               0xFFC00DF8         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB16_ID0               0xFFC00E18         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB17_ID0               0xFFC00E38         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB18_ID0               0xFFC00E58         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB19_ID0               0xFFC00E78         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB20_ID0               0xFFC00E98         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB21_ID0               0xFFC00EB8         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB22_ID0               0xFFC00ED8         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB23_ID0               0xFFC00EF8         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB24_ID0               0xFFC00F18         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB25_ID0               0xFFC00F38         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB26_ID0               0xFFC00F58         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB27_ID0               0xFFC00F78         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB28_ID0               0xFFC00F98         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB29_ID0               0xFFC00FB8         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB30_ID0               0xFFC00FD8         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB31_ID0               0xFFC00FF8         /* CAN0 Mailbox Word 6 Register */
#define CAN0_MB00_ID1               0xFFC00C1C         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB01_ID1               0xFFC00C3C         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB02_ID1               0xFFC00C5C         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB03_ID1               0xFFC00C7C         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB04_ID1               0xFFC00C9C         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB05_ID1               0xFFC00CBC         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB06_ID1               0xFFC00CDC         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB07_ID1               0xFFC00CFC         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB08_ID1               0xFFC00D1C         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB09_ID1               0xFFC00D3C         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB10_ID1               0xFFC00D5C         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB11_ID1               0xFFC00D7C         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB12_ID1               0xFFC00D9C         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB13_ID1               0xFFC00DBC         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB14_ID1               0xFFC00DDC         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB15_ID1               0xFFC00DFC         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB16_ID1               0xFFC00E1C         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB17_ID1               0xFFC00E3C         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB18_ID1               0xFFC00E5C         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB19_ID1               0xFFC00E7C         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB20_ID1               0xFFC00E9C         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB21_ID1               0xFFC00EBC         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB22_ID1               0xFFC00EDC         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB23_ID1               0xFFC00EFC         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB24_ID1               0xFFC00F1C         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB25_ID1               0xFFC00F3C         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB26_ID1               0xFFC00F5C         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB27_ID1               0xFFC00F7C         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB28_ID1               0xFFC00F9C         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB29_ID1               0xFFC00FBC         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB30_ID1               0xFFC00FDC         /* CAN0 Mailbox Word 7 Register */
#define CAN0_MB31_ID1               0xFFC00FFC         /* CAN0 Mailbox Word 7 Register */

/* =========================
	LINK PORT Registers
   ========================= */
#define LP0_CTL                     0xFFC01000         /* LP0 Control Register */
#define LP0_STAT                    0xFFC01004         /* LP0 Status Register */
#define LP0_DIV                     0xFFC01008         /* LP0 Clock Divider Value */
#define LP0_CNT                     0xFFC0100C         /* LP0 Current Count Value of Clock Divider */
#define LP0_TX                      0xFFC01010         /* LP0 Transmit Buffer */
#define LP0_RX                      0xFFC01014         /* LP0 Receive Buffer */
#define LP0_TXIN_SHDW               0xFFC01018         /* LP0 Shadow Input Transmit Buffer */
#define LP0_TXOUT_SHDW              0xFFC0101C         /* LP0 Shadow Output Transmit Buffer */
#define LP1_CTL                     0xFFC01100         /* LP1 Control Register */
#define LP1_STAT                    0xFFC01104         /* LP1 Status Register */
#define LP1_DIV                     0xFFC01108         /* LP1 Clock Divider Value */
#define LP1_CNT                     0xFFC0110C         /* LP1 Current Count Value of Clock Divider */
#define LP1_TX                      0xFFC01110         /* LP1 Transmit Buffer */
#define LP1_RX                      0xFFC01114         /* LP1 Receive Buffer */
#define LP1_TXIN_SHDW               0xFFC01118         /* LP1 Shadow Input Transmit Buffer */
#define LP1_TXOUT_SHDW              0xFFC0111C         /* LP1 Shadow Output Transmit Buffer */
#define LP2_CTL                     0xFFC01200         /* LP2 Control Register */
#define LP2_STAT                    0xFFC01204         /* LP2 Status Register */
#define LP2_DIV                     0xFFC01208         /* LP2 Clock Divider Value */
#define LP2_CNT                     0xFFC0120C         /* LP2 Current Count Value of Clock Divider */
#define LP2_TX                      0xFFC01210         /* LP2 Transmit Buffer */
#define LP2_RX                      0xFFC01214         /* LP2 Receive Buffer */
#define LP2_TXIN_SHDW               0xFFC01218         /* LP2 Shadow Input Transmit Buffer */
#define LP2_TXOUT_SHDW              0xFFC0121C         /* LP2 Shadow Output Transmit Buffer */
#define LP3_CTL                     0xFFC01300         /* LP3 Control Register */
#define LP3_STAT                    0xFFC01304         /* LP3 Status Register */
#define LP3_DIV                     0xFFC01308         /* LP3 Clock Divider Value */
#define LP3_CNT                     0xFFC0130C         /* LP3 Current Count Value of Clock Divider */
#define LP3_TX                      0xFFC01310         /* LP3 Transmit Buffer */
#define LP3_RX                      0xFFC01314         /* LP3 Receive Buffer */
#define LP3_TXIN_SHDW               0xFFC01318         /* LP3 Shadow Input Transmit Buffer */
#define LP3_TXOUT_SHDW              0xFFC0131C         /* LP3 Shadow Output Transmit Buffer */

/* =========================
        TIMER Registers
   ========================= */
#define TIMER_REVID                0xFFC01400         /* GPTIMER Timer IP Version ID */
#define TIMER_RUN                  0xFFC01404         /* GPTIMER Timer Run Register */
#define TIMER_RUN_SET              0xFFC01408         /* GPTIMER Run Register Alias to Set */
#define TIMER_RUN_CLR              0xFFC0140C         /* GPTIMER Run Register Alias to Clear */
#define TIMER_STOP_CFG             0xFFC01410         /* GPTIMER Stop Config Register */
#define TIMER_STOP_CFG_SET         0xFFC01414         /* GPTIMER Stop Config Alias to Set */
#define TIMER_STOP_CFG_CLR         0xFFC01418         /* GPTIMER Stop Config Alias to Clear */
#define TIMER_DATA_IMSK            0xFFC0141C         /* GPTIMER Data Interrupt Mask register */
#define TIMER_STAT_IMSK            0xFFC01420         /* GPTIMER Status Interrupt Mask register */
#define TIMER_TRG_MSK              0xFFC01424         /* GPTIMER Output Trigger Mask register */
#define TIMER_TRG_IE               0xFFC01428         /* GPTIMER Slave Trigger Enable register */
#define TIMER_DATA_ILAT            0xFFC0142C         /* GPTIMER Data Interrupt Register */
#define TIMER_STAT_ILAT            0xFFC01430         /* GPTIMER Status (Error) Interrupt Register */
#define TIMER_ERR_TYPE             0xFFC01434         /* GPTIMER Register Indicating Type of Error */
#define TIMER_BCAST_PER            0xFFC01438         /* GPTIMER Broadcast Period */
#define TIMER_BCAST_WID            0xFFC0143C         /* GPTIMER Broadcast Width */
#define TIMER_BCAST_DLY            0xFFC01440         /* GPTIMER Broadcast Delay */

/* =========================
	TIMER0~7
   ========================= */
#define TIMER0_CONFIG             0xFFC01460         /* TIMER0 Per Timer Config Register */
#define TIMER0_COUNTER            0xFFC01464         /* TIMER0 Per Timer Counter Register */
#define TIMER0_PERIOD             0xFFC01468         /* TIMER0 Per Timer Period Register */
#define TIMER0_WIDTH              0xFFC0146C         /* TIMER0 Per Timer Width Register */
#define TIMER0_DELAY              0xFFC01470         /* TIMER0 Per Timer Delay Register */

#define TIMER1_CONFIG             0xFFC01480         /* TIMER1 Per Timer Config Register */
#define TIMER1_COUNTER            0xFFC01484         /* TIMER1 Per Timer Counter Register */
#define TIMER1_PERIOD             0xFFC01488         /* TIMER1 Per Timer Period Register */
#define TIMER1_WIDTH              0xFFC0148C         /* TIMER1 Per Timer Width Register */
#define TIMER1_DELAY              0xFFC01490         /* TIMER1 Per Timer Delay Register */

#define TIMER2_CONFIG             0xFFC014A0         /* TIMER2 Per Timer Config Register */
#define TIMER2_COUNTER            0xFFC014A4         /* TIMER2 Per Timer Counter Register */
#define TIMER2_PERIOD             0xFFC014A8         /* TIMER2 Per Timer Period Register */
#define TIMER2_WIDTH              0xFFC014AC         /* TIMER2 Per Timer Width Register */
#define TIMER2_DELAY              0xFFC014B0         /* TIMER2 Per Timer Delay Register */

#define TIMER3_CONFIG             0xFFC014C0         /* TIMER3 Per Timer Config Register */
#define TIMER3_COUNTER            0xFFC014C4         /* TIMER3 Per Timer Counter Register */
#define TIMER3_PERIOD             0xFFC014C8         /* TIMER3 Per Timer Period Register */
#define TIMER3_WIDTH              0xFFC014CC         /* TIMER3 Per Timer Width Register */
#define TIMER3_DELAY              0xFFC014D0         /* TIMER3 Per Timer Delay Register */

#define TIMER4_CONFIG             0xFFC014E0         /* TIMER4 Per Timer Config Register */
#define TIMER4_COUNTER            0xFFC014E4         /* TIMER4 Per Timer Counter Register */
#define TIMER4_PERIOD             0xFFC014E8         /* TIMER4 Per Timer Period Register */
#define TIMER4_WIDTH              0xFFC014EC         /* TIMER4 Per Timer Width Register */
#define TIMER4_DELAY              0xFFC014F0         /* TIMER4 Per Timer Delay Register */

#define TIMER5_CONFIG             0xFFC01500         /* TIMER5 Per Timer Config Register */
#define TIMER5_COUNTER            0xFFC01504         /* TIMER5 Per Timer Counter Register */
#define TIMER5_PERIOD             0xFFC01508         /* TIMER5 Per Timer Period Register */
#define TIMER5_WIDTH              0xFFC0150C         /* TIMER5 Per Timer Width Register */
#define TIMER5_DELAY              0xFFC01510         /* TIMER5 Per Timer Delay Register */

#define TIMER6_CONFIG             0xFFC01520         /* TIMER6 Per Timer Config Register */
#define TIMER6_COUNTER            0xFFC01524         /* TIMER6 Per Timer Counter Register */
#define TIMER6_PERIOD             0xFFC01528         /* TIMER6 Per Timer Period Register */
#define TIMER6_WIDTH              0xFFC0152C         /* TIMER6 Per Timer Width Register */
#define TIMER6_DELAY              0xFFC01530         /* TIMER6 Per Timer Delay Register */

#define TIMER7_CONFIG             0xFFC01540         /* TIMER7 Per Timer Config Register */
#define TIMER7_COUNTER            0xFFC01544         /* TIMER7 Per Timer Counter Register */
#define TIMER7_PERIOD             0xFFC01548         /* TIMER7 Per Timer Period Register */
#define TIMER7_WIDTH              0xFFC0154C         /* TIMER7 Per Timer Width Register */
#define TIMER7_DELAY              0xFFC01550         /* TIMER7 Per Timer Delay Register */

/* =========================
	CRC Registers
   ========================= */

/* =========================
	CRC0
   ========================= */
#define REG_CRC0_CTL                    0xFFC01C00         /* CRC0 Control Register */
#define REG_CRC0_DCNT                   0xFFC01C04         /* CRC0 Data Word Count Register */
#define REG_CRC0_DCNTRLD                0xFFC01C08         /* CRC0 Data Word Count Reload Register */
#define REG_CRC0_COMP                   0xFFC01C14         /* CRC0 DATA Compare Register */
#define REG_CRC0_FILLVAL                0xFFC01C18         /* CRC0 Fill Value Register */
#define REG_CRC0_DFIFO                  0xFFC01C1C         /* CRC0 DATA FIFO Register */
#define REG_CRC0_INEN                   0xFFC01C20         /* CRC0 Interrupt Enable Register */
#define REG_CRC0_INEN_SET               0xFFC01C24         /* CRC0 Interrupt Enable Set Register */
#define REG_CRC0_INEN_CLR               0xFFC01C28         /* CRC0 Interrupt Enable Clear Register */
#define REG_CRC0_POLY                   0xFFC01C2C         /* CRC0 Polynomial Register */
#define REG_CRC0_STAT                   0xFFC01C40         /* CRC0 Status Register */
#define REG_CRC0_DCNTCAP                0xFFC01C44         /* CRC0 DATA Count Capture Register */
#define REG_CRC0_RESULT_FIN             0xFFC01C4C         /* CRC0 Final CRC Result Register */
#define REG_CRC0_RESULT_CUR             0xFFC01C50         /* CRC0 Current CRC Result Register */
#define REG_CRC0_REVID                  0xFFC01C60         /* CRC0 Revision ID Register */

/* =========================
	CRC1
   ========================= */
#define REG_CRC1_CTL                    0xFFC01D00         /* CRC1 Control Register */
#define REG_CRC1_DCNT                   0xFFC01D04         /* CRC1 Data Word Count Register */
#define REG_CRC1_DCNTRLD                0xFFC01D08         /* CRC1 Data Word Count Reload Register */
#define REG_CRC1_COMP                   0xFFC01D14         /* CRC1 DATA Compare Register */
#define REG_CRC1_FILLVAL                0xFFC01D18         /* CRC1 Fill Value Register */
#define REG_CRC1_DFIFO                  0xFFC01D1C         /* CRC1 DATA FIFO Register */
#define REG_CRC1_INEN                   0xFFC01D20         /* CRC1 Interrupt Enable Register */
#define REG_CRC1_INEN_SET               0xFFC01D24         /* CRC1 Interrupt Enable Set Register */
#define REG_CRC1_INEN_CLR               0xFFC01D28         /* CRC1 Interrupt Enable Clear Register */
#define REG_CRC1_POLY                   0xFFC01D2C         /* CRC1 Polynomial Register */
#define REG_CRC1_STAT                   0xFFC01D40         /* CRC1 Status Register */
#define REG_CRC1_DCNTCAP                0xFFC01D44         /* CRC1 DATA Count Capture Register */
#define REG_CRC1_RESULT_FIN             0xFFC01D4C         /* CRC1 Final CRC Result Register */
#define REG_CRC1_RESULT_CUR             0xFFC01D50         /* CRC1 Current CRC Result Register */
#define REG_CRC1_REVID                  0xFFC01D60         /* CRC1 Revision ID Register */

/* =========================
        TWI Registers
   ========================= */

/* =========================
        TWI0
   ========================= */
#define TWI0_CLKDIV                    0xFFC01E00         /* TWI0 SCL Clock Divider */
#define TWI0_CONTROL                   0xFFC01E04         /* TWI0 Control Register */
#define TWI0_SLAVE_CTL                 0xFFC01E08         /* TWI0 Slave Mode Control Register */
#define TWI0_SLAVE_STAT                0xFFC01E0C         /* TWI0 Slave Mode Status Register */
#define TWI0_SLAVE_ADDR                0xFFC01E10         /* TWI0 Slave Mode Address Register */
#define TWI0_MASTER_CTL                0xFFC01E14         /* TWI0 Master Mode Control Registers */
#define TWI0_MASTER_STAT               0xFFC01E18         /* TWI0 Master Mode Status Register */
#define TWI0_MASTER_ADDR               0xFFC01E1C         /* TWI0 Master Mode Address Register */
#define TWI0_INT_STAT                  0xFFC01E20         /* TWI0 Interrupt Status Register */
#define TWI0_INT_MASK                  0xFFC01E24         /* TWI0 Interrupt Mask Register */
#define TWI0_FIFO_CTL                  0xFFC01E28         /* TWI0 FIFO Control Register */
#define TWI0_FIFO_STAT                 0xFFC01E2C         /* TWI0 FIFO Status Register */
#define TWI0_XMT_DATA8                 0xFFC01E80         /* TWI0 FIFO Transmit Data Single-Byte Register */
#define TWI0_XMT_DATA16                0xFFC01E84         /* TWI0 FIFO Transmit Data Double-Byte Register */
#define TWI0_RCV_DATA8                 0xFFC01E88         /* TWI0 FIFO Transmit Data Single-Byte Register */
#define TWI0_RCV_DATA16                0xFFC01E8C         /* TWI0 FIFO Transmit Data Double-Byte Register */

/* =========================
        TWI1
   ========================= */
#define TWI1_CLKDIV                 0xFFC01F00         /* TWI1 SCL Clock Divider */
#define TWI1_CONTROL                    0xFFC01F04         /* TWI1 Control Register */
#define TWI1_SLAVE_CTL                 0xFFC01F08         /* TWI1 Slave Mode Control Register */
#define TWI1_SLAVE_STAT                0xFFC01F0C         /* TWI1 Slave Mode Status Register */
#define TWI1_SLAVE_ADDR                0xFFC01F10         /* TWI1 Slave Mode Address Register */
#define TWI1_MASTER_CTL                0xFFC01F14         /* TWI1 Master Mode Control Registers */
#define TWI1_MASTER_STAT               0xFFC01F18         /* TWI1 Master Mode Status Register */
#define TWI1_MASTER_ADDR               0xFFC01F1C         /* TWI1 Master Mode Address Register */
#define TWI1_INT_STAT                  0xFFC01F20         /* TWI1 Interrupt Status Register */
#define TWI1_INT_MASK                   0xFFC01F24         /* TWI1 Interrupt Mask Register */
#define TWI1_FIFO_CTL                0xFFC01F28         /* TWI1 FIFO Control Register */
#define TWI1_FIFO_STAT               0xFFC01F2C         /* TWI1 FIFO Status Register */
#define TWI1_XMT_DATA8                0xFFC01F80         /* TWI1 FIFO Transmit Data Single-Byte Register */
#define TWI1_XMT_DATA16               0xFFC01F84         /* TWI1 FIFO Transmit Data Double-Byte Register */
#define TWI1_RCV_DATA8                0xFFC01F88         /* TWI1 FIFO Transmit Data Single-Byte Register */
#define TWI1_RCV_DATA16               0xFFC01F8C         /* TWI1 FIFO Transmit Data Double-Byte Register */


/* =========================
        UART Registers
   ========================= */

/* =========================
        UART0
   ========================= */
#define UART0_REVID                 0xFFC02000         /* UART0 Revision ID Register */
#define UART0_CTL                   0xFFC02004         /* UART0 Control Register */
#define UART0_STAT                  0xFFC02008         /* UART0 Status Register */
#define UART0_SCR                   0xFFC0200C         /* UART0 Scratch Register */
#define UART0_CLK                   0xFFC02010         /* UART0 Clock Rate Register */
#define UART0_IER                   0xFFC02014         /* UART0 Interrupt Mask Register */
#define UART0_IER_SET               0xFFC02018         /* UART0 Interrupt Mask Set Register */
#define UART0_IER_CLR               0xFFC0201C         /* UART0 Interrupt Mask Clear Register */
#define UART0_RBR                   0xFFC02020         /* UART0 Receive Buffer Register */
#define UART0_THR                   0xFFC02024         /* UART0 Transmit Hold Register */
#define UART0_TAIP                  0xFFC02028         /* UART0 Transmit Address/Insert Pulse Register */
#define UART0_TSR                   0xFFC0202C         /* UART0 Transmit Shift Register */
#define UART0_RSR                   0xFFC02030         /* UART0 Receive Shift Register */
#define UART0_TXDIV                 0xFFC02034         /* UART0 Transmit Clock Devider Register */
#define UART0_RXDIV                 0xFFC02038         /* UART0 Receive Clock Devider Register */

/* =========================
        UART1
   ========================= */
#define UART1_REVID                 0xFFC02400         /* UART1 Revision ID Register */
#define UART1_CTL                   0xFFC02404         /* UART1 Control Register */
#define UART1_STAT                  0xFFC02408         /* UART1 Status Register */
#define UART1_SCR                   0xFFC0240C         /* UART1 Scratch Register */
#define UART1_CLK                   0xFFC02410         /* UART1 Clock Rate Register */
#define UART1_IER                   0xFFC02414         /* UART1 Interrupt Mask Register */
#define UART1_IER_SET               0xFFC02418         /* UART1 Interrupt Mask Set Register */
#define UART1_IER_CLR               0xFFC0241C         /* UART1 Interrupt Mask Clear Register */
#define UART1_RBR                   0xFFC02420         /* UART1 Receive Buffer Register */
#define UART1_THR                   0xFFC02424         /* UART1 Transmit Hold Register */
#define UART1_TAIP                  0xFFC02428         /* UART1 Transmit Address/Insert Pulse Register */
#define UART1_TSR                   0xFFC0242C         /* UART1 Transmit Shift Register */
#define UART1_RSR                   0xFFC02430         /* UART1 Receive Shift Register */
#define UART1_TXDIV                 0xFFC02434         /* UART1 Transmit Clock Devider Register */
#define UART1_RXDIV                 0xFFC02438         /* UART1 Receive Clock Devider Register */


/* =========================
        PORT Registers
   ========================= */

/* =========================
        PORTA
   ========================= */
#define PORTA_FER                   0xFFC03000         /* PORTA Port x Function Enable Register */
#define PORTA_FER_SET               0xFFC03004         /* PORTA Port x Function Enable Set Register */
#define PORTA_FER_CLEAR               0xFFC03008         /* PORTA Port x Function Enable Clear Register */
#define PORTA_DATA                  0xFFC0300C         /* PORTA Port x GPIO Data Register */
#define PORTA_DATA_SET              0xFFC03010         /* PORTA Port x GPIO Data Set Register */
#define PORTA_DATA_CLEAR              0xFFC03014         /* PORTA Port x GPIO Data Clear Register */
#define PORTA_DIR                   0xFFC03018         /* PORTA Port x GPIO Direction Register */
#define PORTA_DIR_SET               0xFFC0301C         /* PORTA Port x GPIO Direction Set Register */
#define PORTA_DIR_CLEAR               0xFFC03020         /* PORTA Port x GPIO Direction Clear Register */
#define PORTA_INEN                  0xFFC03024         /* PORTA Port x GPIO Input Enable Register */
#define PORTA_INEN_SET              0xFFC03028         /* PORTA Port x GPIO Input Enable Set Register */
#define PORTA_INEN_CLEAR              0xFFC0302C         /* PORTA Port x GPIO Input Enable Clear Register */
#define PORTA_MUX                   0xFFC03030         /* PORTA Port x Multiplexer Control Register */
#define PORTA_DATA_TGL              0xFFC03034         /* PORTA Port x GPIO Input Enable Toggle Register */
#define PORTA_POL                   0xFFC03038         /* PORTA Port x GPIO Programming Inversion Register */
#define PORTA_POL_SET               0xFFC0303C         /* PORTA Port x GPIO Programming Inversion Set Register */
#define PORTA_POL_CLEAR               0xFFC03040         /* PORTA Port x GPIO Programming Inversion Clear Register */
#define PORTA_LOCK                  0xFFC03044         /* PORTA Port x GPIO Lock Register */
#define PORTA_REVID                 0xFFC0307C         /* PORTA Port x GPIO Revision ID */

/* =========================
        PORTB
   ========================= */
#define PORTB_FER                   0xFFC03080         /* PORTB Port x Function Enable Register */
#define PORTB_FER_SET               0xFFC03084         /* PORTB Port x Function Enable Set Register */
#define PORTB_FER_CLEAR               0xFFC03088         /* PORTB Port x Function Enable Clear Register */
#define PORTB_DATA                  0xFFC0308C         /* PORTB Port x GPIO Data Register */
#define PORTB_DATA_SET              0xFFC03090         /* PORTB Port x GPIO Data Set Register */
#define PORTB_DATA_CLEAR              0xFFC03094         /* PORTB Port x GPIO Data Clear Register */
#define PORTB_DIR                   0xFFC03098         /* PORTB Port x GPIO Direction Register */
#define PORTB_DIR_SET               0xFFC0309C         /* PORTB Port x GPIO Direction Set Register */
#define PORTB_DIR_CLEAR               0xFFC030A0         /* PORTB Port x GPIO Direction Clear Register */
#define PORTB_INEN                  0xFFC030A4         /* PORTB Port x GPIO Input Enable Register */
#define PORTB_INEN_SET              0xFFC030A8         /* PORTB Port x GPIO Input Enable Set Register */
#define PORTB_INEN_CLEAR              0xFFC030AC         /* PORTB Port x GPIO Input Enable Clear Register */
#define PORTB_MUX                   0xFFC030B0         /* PORTB Port x Multiplexer Control Register */
#define PORTB_DATA_TGL              0xFFC030B4         /* PORTB Port x GPIO Input Enable Toggle Register */
#define PORTB_POL                   0xFFC030B8         /* PORTB Port x GPIO Programming Inversion Register */
#define PORTB_POL_SET               0xFFC030BC         /* PORTB Port x GPIO Programming Inversion Set Register */
#define PORTB_POL_CLEAR               0xFFC030C0         /* PORTB Port x GPIO Programming Inversion Clear Register */
#define PORTB_LOCK                  0xFFC030C4         /* PORTB Port x GPIO Lock Register */
#define PORTB_REVID                 0xFFC030FC         /* PORTB Port x GPIO Revision ID */

/* =========================
        PORTC
   ========================= */
#define PORTC_FER                   0xFFC03100         /* PORTC Port x Function Enable Register */
#define PORTC_FER_SET               0xFFC03104         /* PORTC Port x Function Enable Set Register */
#define PORTC_FER_CLEAR               0xFFC03108         /* PORTC Port x Function Enable Clear Register */
#define PORTC_DATA                  0xFFC0310C         /* PORTC Port x GPIO Data Register */
#define PORTC_DATA_SET              0xFFC03110         /* PORTC Port x GPIO Data Set Register */
#define PORTC_DATA_CLEAR              0xFFC03114         /* PORTC Port x GPIO Data Clear Register */
#define PORTC_DIR                   0xFFC03118         /* PORTC Port x GPIO Direction Register */
#define PORTC_DIR_SET               0xFFC0311C         /* PORTC Port x GPIO Direction Set Register */
#define PORTC_DIR_CLEAR               0xFFC03120         /* PORTC Port x GPIO Direction Clear Register */
#define PORTC_INEN                  0xFFC03124         /* PORTC Port x GPIO Input Enable Register */
#define PORTC_INEN_SET              0xFFC03128         /* PORTC Port x GPIO Input Enable Set Register */
#define PORTC_INEN_CLEAR              0xFFC0312C         /* PORTC Port x GPIO Input Enable Clear Register */
#define PORTC_MUX                   0xFFC03130         /* PORTC Port x Multiplexer Control Register */
#define PORTC_DATA_TGL              0xFFC03134         /* PORTC Port x GPIO Input Enable Toggle Register */
#define PORTC_POL                   0xFFC03138         /* PORTC Port x GPIO Programming Inversion Register */
#define PORTC_POL_SET               0xFFC0313C         /* PORTC Port x GPIO Programming Inversion Set Register */
#define PORTC_POL_CLEAR               0xFFC03140         /* PORTC Port x GPIO Programming Inversion Clear Register */
#define PORTC_LOCK                  0xFFC03144         /* PORTC Port x GPIO Lock Register */
#define PORTC_REVID                 0xFFC0317C         /* PORTC Port x GPIO Revision ID */

/* =========================
        PORTD
   ========================= */
#define PORTD_FER                   0xFFC03180         /* PORTD Port x Function Enable Register */
#define PORTD_FER_SET               0xFFC03184         /* PORTD Port x Function Enable Set Register */
#define PORTD_FER_CLEAR               0xFFC03188         /* PORTD Port x Function Enable Clear Register */
#define PORTD_DATA                  0xFFC0318C         /* PORTD Port x GPIO Data Register */
#define PORTD_DATA_SET              0xFFC03190         /* PORTD Port x GPIO Data Set Register */
#define PORTD_DATA_CLEAR              0xFFC03194         /* PORTD Port x GPIO Data Clear Register */
#define PORTD_DIR                   0xFFC03198         /* PORTD Port x GPIO Direction Register */
#define PORTD_DIR_SET               0xFFC0319C         /* PORTD Port x GPIO Direction Set Register */
#define PORTD_DIR_CLEAR               0xFFC031A0         /* PORTD Port x GPIO Direction Clear Register */
#define PORTD_INEN                  0xFFC031A4         /* PORTD Port x GPIO Input Enable Register */
#define PORTD_INEN_SET              0xFFC031A8         /* PORTD Port x GPIO Input Enable Set Register */
#define PORTD_INEN_CLEAR              0xFFC031AC         /* PORTD Port x GPIO Input Enable Clear Register */
#define PORTD_MUX                   0xFFC031B0         /* PORTD Port x Multiplexer Control Register */
#define PORTD_DATA_TGL              0xFFC031B4         /* PORTD Port x GPIO Input Enable Toggle Register */
#define PORTD_POL                   0xFFC031B8         /* PORTD Port x GPIO Programming Inversion Register */
#define PORTD_POL_SET               0xFFC031BC         /* PORTD Port x GPIO Programming Inversion Set Register */
#define PORTD_POL_CLEAR               0xFFC031C0         /* PORTD Port x GPIO Programming Inversion Clear Register */
#define PORTD_LOCK                  0xFFC031C4         /* PORTD Port x GPIO Lock Register */
#define PORTD_REVID                 0xFFC031FC         /* PORTD Port x GPIO Revision ID */

/* =========================
        PORTE
   ========================= */
#define PORTE_FER                   0xFFC03200         /* PORTE Port x Function Enable Register */
#define PORTE_FER_SET               0xFFC03204         /* PORTE Port x Function Enable Set Register */
#define PORTE_FER_CLEAR               0xFFC03208         /* PORTE Port x Function Enable Clear Register */
#define PORTE_DATA                  0xFFC0320C         /* PORTE Port x GPIO Data Register */
#define PORTE_DATA_SET              0xFFC03210         /* PORTE Port x GPIO Data Set Register */
#define PORTE_DATA_CLEAR              0xFFC03214         /* PORTE Port x GPIO Data Clear Register */
#define PORTE_DIR                   0xFFC03218         /* PORTE Port x GPIO Direction Register */
#define PORTE_DIR_SET               0xFFC0321C         /* PORTE Port x GPIO Direction Set Register */
#define PORTE_DIR_CLEAR               0xFFC03220         /* PORTE Port x GPIO Direction Clear Register */
#define PORTE_INEN                  0xFFC03224         /* PORTE Port x GPIO Input Enable Register */
#define PORTE_INEN_SET              0xFFC03228         /* PORTE Port x GPIO Input Enable Set Register */
#define PORTE_INEN_CLEAR              0xFFC0322C         /* PORTE Port x GPIO Input Enable Clear Register */
#define PORTE_MUX                   0xFFC03230         /* PORTE Port x Multiplexer Control Register */
#define PORTE_DATA_TGL              0xFFC03234         /* PORTE Port x GPIO Input Enable Toggle Register */
#define PORTE_POL                   0xFFC03238         /* PORTE Port x GPIO Programming Inversion Register */
#define PORTE_POL_SET               0xFFC0323C         /* PORTE Port x GPIO Programming Inversion Set Register */
#define PORTE_POL_CLEAR               0xFFC03240         /* PORTE Port x GPIO Programming Inversion Clear Register */
#define PORTE_LOCK                  0xFFC03244         /* PORTE Port x GPIO Lock Register */
#define PORTE_REVID                 0xFFC0327C         /* PORTE Port x GPIO Revision ID */

/* =========================
        PORTF
   ========================= */
#define PORTF_FER                   0xFFC03280         /* PORTF Port x Function Enable Register */
#define PORTF_FER_SET               0xFFC03284         /* PORTF Port x Function Enable Set Register */
#define PORTF_FER_CLEAR               0xFFC03288         /* PORTF Port x Function Enable Clear Register */
#define PORTF_DATA                  0xFFC0328C         /* PORTF Port x GPIO Data Register */
#define PORTF_DATA_SET              0xFFC03290         /* PORTF Port x GPIO Data Set Register */
#define PORTF_DATA_CLEAR              0xFFC03294         /* PORTF Port x GPIO Data Clear Register */
#define PORTF_DIR                   0xFFC03298         /* PORTF Port x GPIO Direction Register */
#define PORTF_DIR_SET               0xFFC0329C         /* PORTF Port x GPIO Direction Set Register */
#define PORTF_DIR_CLEAR               0xFFC032A0         /* PORTF Port x GPIO Direction Clear Register */
#define PORTF_INEN                  0xFFC032A4         /* PORTF Port x GPIO Input Enable Register */
#define PORTF_INEN_SET              0xFFC032A8         /* PORTF Port x GPIO Input Enable Set Register */
#define PORTF_INEN_CLEAR              0xFFC032AC         /* PORTF Port x GPIO Input Enable Clear Register */
#define PORTF_MUX                   0xFFC032B0         /* PORTF Port x Multiplexer Control Register */
#define PORTF_DATA_TGL              0xFFC032B4         /* PORTF Port x GPIO Input Enable Toggle Register */
#define PORTF_POL                   0xFFC032B8         /* PORTF Port x GPIO Programming Inversion Register */
#define PORTF_POL_SET               0xFFC032BC         /* PORTF Port x GPIO Programming Inversion Set Register */
#define PORTF_POL_CLEAR               0xFFC032C0         /* PORTF Port x GPIO Programming Inversion Clear Register */
#define PORTF_LOCK                  0xFFC032C4         /* PORTF Port x GPIO Lock Register */
#define PORTF_REVID                 0xFFC032FC         /* PORTF Port x GPIO Revision ID */

/* =========================
        PORTG
   ========================= */
#define PORTG_FER                   0xFFC03300         /* PORTG Port x Function Enable Register */
#define PORTG_FER_SET               0xFFC03304         /* PORTG Port x Function Enable Set Register */
#define PORTG_FER_CLEAR               0xFFC03308         /* PORTG Port x Function Enable Clear Register */
#define PORTG_DATA                  0xFFC0330C         /* PORTG Port x GPIO Data Register */
#define PORTG_DATA_SET              0xFFC03310         /* PORTG Port x GPIO Data Set Register */
#define PORTG_DATA_CLEAR              0xFFC03314         /* PORTG Port x GPIO Data Clear Register */
#define PORTG_DIR                   0xFFC03318         /* PORTG Port x GPIO Direction Register */
#define PORTG_DIR_SET               0xFFC0331C         /* PORTG Port x GPIO Direction Set Register */
#define PORTG_DIR_CLEAR               0xFFC03320         /* PORTG Port x GPIO Direction Clear Register */
#define PORTG_INEN                  0xFFC03324         /* PORTG Port x GPIO Input Enable Register */
#define PORTG_INEN_SET              0xFFC03328         /* PORTG Port x GPIO Input Enable Set Register */
#define PORTG_INEN_CLEAR              0xFFC0332C         /* PORTG Port x GPIO Input Enable Clear Register */
#define PORTG_MUX                   0xFFC03330         /* PORTG Port x Multiplexer Control Register */
#define PORTG_DATA_TGL              0xFFC03334         /* PORTG Port x GPIO Input Enable Toggle Register */
#define PORTG_POL                   0xFFC03338         /* PORTG Port x GPIO Programming Inversion Register */
#define PORTG_POL_SET               0xFFC0333C         /* PORTG Port x GPIO Programming Inversion Set Register */
#define PORTG_POL_CLEAR               0xFFC03340         /* PORTG Port x GPIO Programming Inversion Clear Register */
#define PORTG_LOCK                  0xFFC03344         /* PORTG Port x GPIO Lock Register */
#define PORTG_REVID                 0xFFC0337C         /* PORTG Port x GPIO Revision ID */

/* ==================================================
        Pads Controller Registers
   ================================================== */

/* =========================
        PADS0
   ========================= */
#define PADS0_EMAC_PTP_CLKSEL	    0xFFC03404         /* PADS0 Clock Selection for EMAC and PTP */
#define PADS0_TWI_VSEL		    0xFFC03408         /* PADS0 TWI Voltage Selection */
#define PADS0_PORTS_HYST	    0xFFC03440         /* PADS0 Hysteresis Enable Register */

/* =========================
        PINT Registers
   ========================= */

/* =========================
        PINT0
   ========================= */
#define PINT0_MASK_SET              0xFFC04000         /* PINT0 Pint Mask Set Register */
#define PINT0_MASK_CLEAR            0xFFC04004         /* PINT0 Pint Mask Clear Register */
#define PINT0_REQUEST               0xFFC04008         /* PINT0 Pint Request Register */
#define PINT0_ASSIGN                0xFFC0400C         /* PINT0 Pint Assign Register */
#define PINT0_EDGE_SET              0xFFC04010         /* PINT0 Pint Edge Set Register */
#define PINT0_EDGE_CLEAR            0xFFC04014         /* PINT0 Pint Edge Clear Register */
#define PINT0_INVERT_SET            0xFFC04018         /* PINT0 Pint Invert Set Register */
#define PINT0_INVERT_CLEAR          0xFFC0401C         /* PINT0 Pint Invert Clear Register */
#define PINT0_PINSTATE              0xFFC04020         /* PINT0 Pint Pinstate Register */
#define PINT0_LATCH                 0xFFC04024         /* PINT0 Pint Latch Register */

/* =========================
        PINT1
   ========================= */
#define PINT1_MASK_SET              0xFFC04100         /* PINT1 Pint Mask Set Register */
#define PINT1_MASK_CLEAR            0xFFC04104         /* PINT1 Pint Mask Clear Register */
#define PINT1_REQUEST               0xFFC04108         /* PINT1 Pint Request Register */
#define PINT1_ASSIGN                0xFFC0410C         /* PINT1 Pint Assign Register */
#define PINT1_EDGE_SET              0xFFC04110         /* PINT1 Pint Edge Set Register */
#define PINT1_EDGE_CLEAR            0xFFC04114         /* PINT1 Pint Edge Clear Register */
#define PINT1_INVERT_SET            0xFFC04118         /* PINT1 Pint Invert Set Register */
#define PINT1_INVERT_CLEAR          0xFFC0411C         /* PINT1 Pint Invert Clear Register */
#define PINT1_PINSTATE              0xFFC04120         /* PINT1 Pint Pinstate Register */
#define PINT1_LATCH                 0xFFC04124         /* PINT1 Pint Latch Register */

/* =========================
        PINT2
   ========================= */
#define PINT2_MASK_SET              0xFFC04200         /* PINT2 Pint Mask Set Register */
#define PINT2_MASK_CLEAR            0xFFC04204         /* PINT2 Pint Mask Clear Register */
#define PINT2_REQUEST               0xFFC04208         /* PINT2 Pint Request Register */
#define PINT2_ASSIGN                0xFFC0420C         /* PINT2 Pint Assign Register */
#define PINT2_EDGE_SET              0xFFC04210         /* PINT2 Pint Edge Set Register */
#define PINT2_EDGE_CLEAR            0xFFC04214         /* PINT2 Pint Edge Clear Register */
#define PINT2_INVERT_SET            0xFFC04218         /* PINT2 Pint Invert Set Register */
#define PINT2_INVERT_CLEAR          0xFFC0421C         /* PINT2 Pint Invert Clear Register */
#define PINT2_PINSTATE              0xFFC04220         /* PINT2 Pint Pinstate Register */
#define PINT2_LATCH                 0xFFC04224         /* PINT2 Pint Latch Register */

/* =========================
        PINT3
   ========================= */
#define PINT3_MASK_SET              0xFFC04300         /* PINT3 Pint Mask Set Register */
#define PINT3_MASK_CLEAR            0xFFC04304         /* PINT3 Pint Mask Clear Register */
#define PINT3_REQUEST               0xFFC04308         /* PINT3 Pint Request Register */
#define PINT3_ASSIGN                0xFFC0430C         /* PINT3 Pint Assign Register */
#define PINT3_EDGE_SET              0xFFC04310         /* PINT3 Pint Edge Set Register */
#define PINT3_EDGE_CLEAR            0xFFC04314         /* PINT3 Pint Edge Clear Register */
#define PINT3_INVERT_SET            0xFFC04318         /* PINT3 Pint Invert Set Register */
#define PINT3_INVERT_CLEAR          0xFFC0431C         /* PINT3 Pint Invert Clear Register */
#define PINT3_PINSTATE              0xFFC04320         /* PINT3 Pint Pinstate Register */
#define PINT3_LATCH                 0xFFC04324         /* PINT3 Pint Latch Register */

/* =========================
        PINT4
   ========================= */
#define PINT4_MASK_SET              0xFFC04400         /* PINT4 Pint Mask Set Register */
#define PINT4_MASK_CLEAR            0xFFC04404         /* PINT4 Pint Mask Clear Register */
#define PINT4_REQUEST               0xFFC04408         /* PINT4 Pint Request Register */
#define PINT4_ASSIGN                0xFFC0440C         /* PINT4 Pint Assign Register */
#define PINT4_EDGE_SET              0xFFC04410         /* PINT4 Pint Edge Set Register */
#define PINT4_EDGE_CLEAR            0xFFC04414         /* PINT4 Pint Edge Clear Register */
#define PINT4_INVERT_SET            0xFFC04418         /* PINT4 Pint Invert Set Register */
#define PINT4_INVERT_CLEAR          0xFFC0441C         /* PINT4 Pint Invert Clear Register */
#define PINT4_PINSTATE              0xFFC04420         /* PINT4 Pint Pinstate Register */
#define PINT4_LATCH                 0xFFC04424         /* PINT4 Pint Latch Register */

/* =========================
        PINT5
   ========================= */
#define PINT5_MASK_SET              0xFFC04500         /* PINT5 Pint Mask Set Register */
#define PINT5_MASK_CLEAR            0xFFC04504         /* PINT5 Pint Mask Clear Register */
#define PINT5_REQUEST               0xFFC04508         /* PINT5 Pint Request Register */
#define PINT5_ASSIGN                0xFFC0450C         /* PINT5 Pint Assign Register */
#define PINT5_EDGE_SET              0xFFC04510         /* PINT5 Pint Edge Set Register */
#define PINT5_EDGE_CLEAR            0xFFC04514         /* PINT5 Pint Edge Clear Register */
#define PINT5_INVERT_SET            0xFFC04518         /* PINT5 Pint Invert Set Register */
#define PINT5_INVERT_CLEAR          0xFFC0451C         /* PINT5 Pint Invert Clear Register */
#define PINT5_PINSTATE              0xFFC04520         /* PINT5 Pint Pinstate Register */
#define PINT5_LATCH                 0xFFC04524         /* PINT5 Pint Latch Register */


/* =========================
        SMC Registers
   ========================= */

/* =========================
        SMC0
   ========================= */
#define SMC_GCTL                   0xFFC16004         /* SMC0 SMC Control Register */
#define SMC_GSTAT                  0xFFC16008         /* SMC0 SMC Status Register */
#define SMC_B0CTL                  0xFFC1600C         /* SMC0 SMC Bank0 Control Register */
#define SMC_B0TIM                  0xFFC16010         /* SMC0 SMC Bank0 Timing Register */
#define SMC_B0ETIM                 0xFFC16014         /* SMC0 SMC Bank0 Extended Timing Register */
#define SMC_B1CTL                  0xFFC1601C         /* SMC0 SMC BANK1 Control Register */
#define SMC_B1TIM                  0xFFC16020         /* SMC0 SMC BANK1 Timing Register */
#define SMC_B1ETIM                 0xFFC16024         /* SMC0 SMC BANK1 Extended Timing Register */
#define SMC_B2CTL                  0xFFC1602C         /* SMC0 SMC BANK2 Control Register */
#define SMC_B2TIM                  0xFFC16030         /* SMC0 SMC BANK2 Timing Register */
#define SMC_B2ETIM                 0xFFC16034         /* SMC0 SMC BANK2 Extended Timing Register */
#define SMC_B3CTL                  0xFFC1603C         /* SMC0 SMC BANK3 Control Register */
#define SMC_B3TIM                  0xFFC16040         /* SMC0 SMC BANK3 Timing Register */
#define SMC_B3ETIM                 0xFFC16044         /* SMC0 SMC BANK3 Extended Timing Register */


/* =========================
        WDOG Registers
   ========================= */

/* =========================
        WDOG0
   ========================= */
#define WDOG0_CTL                   0xFFC17000         /* WDOG0 Control Register */
#define WDOG0_CNT                   0xFFC17004         /* WDOG0 Count Register */
#define WDOG0_STAT                  0xFFC17008         /* WDOG0 Watchdog Timer Status Register */
#define WDOG_CTL		WDOG0_CTL
#define WDOG_CNT		WDOG0_CNT
#define WDOG_STAT		WDOG0_STAT

/* =========================
        WDOG1
   ========================= */
#define WDOG1_CTL                   0xFFC17800         /* WDOG1 Control Register */
#define WDOG1_CNT                   0xFFC17804         /* WDOG1 Count Register */
#define WDOG1_STAT                  0xFFC17808         /* WDOG1 Watchdog Timer Status Register */


/* =========================
        SDU Registers
   ========================= */

/* =========================
        SDU0
   ========================= */
#define SDU0_IDCODE                 0xFFC1F020         /* SDU0 ID Code Register */
#define SDU0_CTL                    0xFFC1F050         /* SDU0 Control Register */
#define SDU0_STAT                   0xFFC1F054         /* SDU0 Status Register */
#define SDU0_MACCTL                 0xFFC1F058         /* SDU0 Memory Access Control Register */
#define SDU0_MACADDR                0xFFC1F05C         /* SDU0 Memory Access Address Register */
#define SDU0_MACDATA                0xFFC1F060         /* SDU0 Memory Access Data Register */
#define SDU0_DMARD                  0xFFC1F064         /* SDU0 DMA Read Data Register */
#define SDU0_DMAWD                  0xFFC1F068         /* SDU0 DMA Write Data Register */
#define SDU0_MSG                    0xFFC1F080         /* SDU0 Message Register */
#define SDU0_MSG_SET                0xFFC1F084         /* SDU0 Message Set Register */
#define SDU0_MSG_CLR                0xFFC1F088         /* SDU0 Message Clear Register */
#define SDU0_GHLT                   0xFFC1F08C         /* SDU0 Group Halt Register */


/* =========================
        EMAC Registers
   ========================= */
/* =========================
        EMAC0
   ========================= */
#define EMAC0_MACCFG                0xFFC20000         /* EMAC0 MAC Configuration Register */
#define EMAC0_MACFRMFILT            0xFFC20004         /* EMAC0 Filter Register for filtering Received Frames */
#define EMAC0_HASHTBL_HI            0xFFC20008         /* EMAC0 Contains the Upper 32 bits of the hash table */
#define EMAC0_HASHTBL_LO            0xFFC2000C         /* EMAC0 Contains the lower 32 bits of the hash table */
#define EMAC0_GMII_ADDR             0xFFC20010         /* EMAC0 Management Address Register */
#define EMAC0_GMII_DATA             0xFFC20014         /* EMAC0 Management Data Register */
#define EMAC0_FLOWCTL               0xFFC20018         /* EMAC0 MAC FLow Control Register */
#define EMAC0_VLANTAG               0xFFC2001C         /* EMAC0 VLAN Tag Register */
#define EMAC0_VER                   0xFFC20020         /* EMAC0 EMAC Version Register */
#define EMAC0_DBG                   0xFFC20024         /* EMAC0 EMAC Debug Register */
#define EMAC0_RMTWKUP               0xFFC20028         /* EMAC0 Remote wake up frame register */
#define EMAC0_PMT_CTLSTAT           0xFFC2002C         /* EMAC0 PMT Control and Status Register */
#define EMAC0_ISTAT                 0xFFC20038         /* EMAC0 EMAC Interrupt Status Register */
#define EMAC0_IMSK                  0xFFC2003C         /* EMAC0 EMAC Interrupt Mask Register */
#define EMAC0_ADDR0_HI              0xFFC20040         /* EMAC0 EMAC Address0 High Register */
#define EMAC0_ADDR0_LO              0xFFC20044         /* EMAC0 EMAC Address0 Low Register */
#define EMAC0_MMC_CTL               0xFFC20100         /* EMAC0 MMC Control Register */
#define EMAC0_MMC_RXINT             0xFFC20104         /* EMAC0 MMC RX Interrupt Register */
#define EMAC0_MMC_TXINT             0xFFC20108         /* EMAC0 MMC TX Interrupt Register */
#define EMAC0_MMC_RXIMSK            0xFFC2010C         /* EMAC0 MMC RX Interrupt Mask Register */
#define EMAC0_MMC_TXIMSK            0xFFC20110         /* EMAC0 MMC TX Interrupt Mask Register */
#define EMAC0_TXOCTCNT_GB           0xFFC20114         /* EMAC0 Num bytes transmitted exclusive of preamble */
#define EMAC0_TXFRMCNT_GB           0xFFC20118         /* EMAC0 Num frames transmitted exclusive of retired */
#define EMAC0_TXBCASTFRM_G          0xFFC2011C         /* EMAC0 Number of good broadcast frames transmitted. */
#define EMAC0_TXMCASTFRM_G          0xFFC20120         /* EMAC0 Number of good multicast frames transmitted. */
#define EMAC0_TX64_GB               0xFFC20124         /* EMAC0 Number of 64 byte length frames */
#define EMAC0_TX65TO127_GB          0xFFC20128         /* EMAC0 Number of frames of length b/w 65-127 (inclusive) bytes */
#define EMAC0_TX128TO255_GB         0xFFC2012C         /* EMAC0 Number of frames of length b/w 128-255 (inclusive) bytes */
#define EMAC0_TX256TO511_GB         0xFFC20130         /* EMAC0 Number of frames of length b/w 256-511 (inclusive) bytes */
#define EMAC0_TX512TO1023_GB        0xFFC20134         /* EMAC0 Number of frames of length b/w 512-1023 (inclusive) bytes */
#define EMAC0_TX1024TOMAX_GB        0xFFC20138         /* EMAC0 Number of frames of length b/w 1024-max (inclusive) bytes */
#define EMAC0_TXUCASTFRM_GB         0xFFC2013C         /* EMAC0 Number of good and bad unicast frames transmitted */
#define EMAC0_TXMCASTFRM_GB         0xFFC20140         /* EMAC0 Number of good and bad multicast frames transmitted */
#define EMAC0_TXBCASTFRM_GB         0xFFC20144         /* EMAC0 Number of good and bad broadcast frames transmitted */
#define EMAC0_TXUNDR_ERR            0xFFC20148         /* EMAC0 Number of frames aborted due to frame underflow error */
#define EMAC0_TXSNGCOL_G            0xFFC2014C         /* EMAC0 Number of transmitted frames after single collision */
#define EMAC0_TXMULTCOL_G           0xFFC20150         /* EMAC0 Number of transmitted frames with more than one collision */
#define EMAC0_TXDEFERRED            0xFFC20154         /* EMAC0 Number of transmitted frames after deferral */
#define EMAC0_TXLATECOL             0xFFC20158         /* EMAC0 Number of frames aborted due to late collision error */
#define EMAC0_TXEXCESSCOL           0xFFC2015C         /* EMAC0 Number of aborted frames due to excessive collisions */
#define EMAC0_TXCARR_ERR            0xFFC20160         /* EMAC0 Number of aborted frames due to carrier sense error */
#define EMAC0_TXOCTCNT_G            0xFFC20164         /* EMAC0 Number of bytes transmitted in good frames only */
#define EMAC0_TXFRMCNT_G            0xFFC20168         /* EMAC0 Number of good frames transmitted. */
#define EMAC0_TXEXCESSDEF           0xFFC2016C         /* EMAC0 Number of frames aborted due to excessive deferral */
#define EMAC0_TXPAUSEFRM            0xFFC20170         /* EMAC0 Number of good PAUSE frames transmitted. */
#define EMAC0_TXVLANFRM_G           0xFFC20174         /* EMAC0 Number of VLAN frames transmitted */
#define EMAC0_RXFRMCNT_GB           0xFFC20180         /* EMAC0 Number of good and bad frames received. */
#define EMAC0_RXOCTCNT_GB           0xFFC20184         /* EMAC0 Number of bytes received in good and bad frames */
#define EMAC0_RXOCTCNT_G            0xFFC20188         /* EMAC0 Number of bytes received only in good frames */
#define EMAC0_RXBCASTFRM_G          0xFFC2018C         /* EMAC0 Number of good broadcast frames received. */
#define EMAC0_RXMCASTFRM_G          0xFFC20190         /* EMAC0 Number of good multicast frames received */
#define EMAC0_RXCRC_ERR             0xFFC20194         /* EMAC0 Number of frames received with CRC error */
#define EMAC0_RXALIGN_ERR           0xFFC20198         /* EMAC0 Number of frames with alignment error */
#define EMAC0_RXRUNT_ERR            0xFFC2019C         /* EMAC0 Number of frames received with runt error. */
#define EMAC0_RXJAB_ERR             0xFFC201A0         /* EMAC0 Number of frames received with length greater than 1518 */
#define EMAC0_RXUSIZE_G             0xFFC201A4         /* EMAC0 Number of frames received with length 64 */
#define EMAC0_RXOSIZE_G             0xFFC201A8         /* EMAC0 Number of frames received with length greater than maxium */
#define EMAC0_RX64_GB               0xFFC201AC         /* EMAC0 Number of good and bad frames of lengh 64 bytes */
#define EMAC0_RX65TO127_GB          0xFFC201B0         /* EMAC0 Number of good and bad frame between 64-127(inclusive) */
#define EMAC0_RX128TO255_GB         0xFFC201B4         /* EMAC0 Number of good and bad frames received with length between 128 and 255 (inclusive) bytes, exclusive of preamble. */
#define EMAC0_RX256TO511_GB         0xFFC201B8         /* EMAC0 Number of good and bad frames between 256-511(inclusive) */
#define EMAC0_RX512TO1023_GB        0xFFC201BC         /* EMAC0 Number of good and bad frames received between 512-1023 */
#define EMAC0_RX1024TOMAX_GB        0xFFC201C0         /* EMAC0 Number of frames received between 1024 and maxsize */
#define EMAC0_RXUCASTFRM_G          0xFFC201C4         /* EMAC0 Number of good unicast frames received. */
#define EMAC0_RXLEN_ERR             0xFFC201C8         /* EMAC0 Number of frames received with length error */
#define EMAC0_RXOORTYPE             0xFFC201CC         /* EMAC0 Number of frames with length not equal to valid frame size */
#define EMAC0_RXPAUSEFRM            0xFFC201D0         /* EMAC0 Number of good and valid PAUSE frames received. */
#define EMAC0_RXFIFO_OVF            0xFFC201D4         /* EMAC0 Number of missed received frames due to FIFO overflow. This counter is not present in the GMAC-CORE configuration. */
#define EMAC0_RXVLANFRM_GB          0xFFC201D8         /* EMAC0 Number of good and bad VLAN frames received. */
#define EMAC0_RXWDOG_ERR            0xFFC201DC         /* EMAC0 Frames received with error due to watchdog timeout */
#define EMAC0_IPC_RXIMSK            0xFFC20200         /* EMAC0 MMC IPC RX Interrupt Mask Register */
#define EMAC0_IPC_RXINT             0xFFC20208         /* EMAC0 MMC IPC RX Interrupt Register */
#define EMAC0_RXIPV4_GD_FRM         0xFFC20210         /* EMAC0 Number of good IPv4 datagrams */
#define EMAC0_RXIPV4_HDR_ERR_FRM    0xFFC20214         /* EMAC0 Number of IPv4 datagrams with header errors */
#define EMAC0_RXIPV4_NOPAY_FRM      0xFFC20218         /* EMAC0 Number of IPv4 datagrams without checksum */
#define EMAC0_RXIPV4_FRAG_FRM       0xFFC2021C         /* EMAC0 Number of good IPv4 datagrams with fragmentation */
#define EMAC0_RXIPV4_UDSBL_FRM      0xFFC20220         /* EMAC0 Number of IPv4 UDP datagrams with disabled checksum */
#define EMAC0_RXIPV6_GD_FRM         0xFFC20224         /* EMAC0 Number of IPv4 datagrams with TCP/UDP/ICMP payloads */
#define EMAC0_RXIPV6_HDR_ERR_FRM    0xFFC20228         /* EMAC0 Number of IPv6 datagrams with header errors */
#define EMAC0_RXIPV6_NOPAY_FRM      0xFFC2022C         /* EMAC0 Number of IPv6 datagrams with no TCP/UDP/ICMP payload */
#define EMAC0_RXUDP_GD_FRM          0xFFC20230         /* EMAC0 Number of good IP datagrames with good UDP payload */
#define EMAC0_RXUDP_ERR_FRM         0xFFC20234         /* EMAC0 Number of good IP datagrams with UDP checksum errors */
#define EMAC0_RXTCP_GD_FRM          0xFFC20238         /* EMAC0 Number of good IP datagrams with a good TCP payload */
#define EMAC0_RXTCP_ERR_FRM         0xFFC2023C         /* EMAC0 Number of good IP datagrams with TCP checksum errors */
#define EMAC0_RXICMP_GD_FRM         0xFFC20240         /* EMAC0 Number of good IP datagrams with a good ICMP payload */
#define EMAC0_RXICMP_ERR_FRM        0xFFC20244         /* EMAC0 Number of good IP datagrams with ICMP checksum errors */
#define EMAC0_RXIPV4_GD_OCT         0xFFC20250         /* EMAC0 Bytes received in IPv4 datagrams including tcp,udp or icmp */
#define EMAC0_RXIPV4_HDR_ERR_OCT    0xFFC20254         /* EMAC0 Bytes received in IPv4 datagrams with header errors */
#define EMAC0_RXIPV4_NOPAY_OCT      0xFFC20258         /* EMAC0 Bytes received in IPv4 datagrams without tcp,udp,icmp load */
#define EMAC0_RXIPV4_FRAG_OCT       0xFFC2025C         /* EMAC0 Bytes received in fragmented IPv4 datagrams */
#define EMAC0_RXIPV4_UDSBL_OCT      0xFFC20260         /* EMAC0 Bytes received in UDP segment with checksum disabled */
#define EMAC0_RXIPV6_GD_OCT         0xFFC20264         /* EMAC0 Bytes received in good IPv6  including tcp,udp or icmp load */
#define EMAC0_RXIPV6_HDR_ERR_OCT    0xFFC20268         /* EMAC0 Number of bytes received in IPv6 with header errors */
#define EMAC0_RXIPV6_NOPAY_OCT      0xFFC2026C         /* EMAC0 Bytes received in IPv6 without tcp,udp or icmp load */
#define EMAC0_RXUDP_GD_OCT          0xFFC20270         /* EMAC0 Number of bytes received in good UDP segments */
#define EMAC0_RXUDP_ERR_OCT         0xFFC20274         /* EMAC0 Number of bytes received in UDP segment with checksum err */
#define EMAC0_RXTCP_GD_OCT          0xFFC20278         /* EMAC0 Number of bytes received in a good TCP segment */
#define EMAC0_RXTCP_ERR_OCT         0xFFC2027C         /* EMAC0 Number of bytes received in TCP segment with checksum err */
#define EMAC0_RXICMP_GD_OCT         0xFFC20280         /* EMAC0 Number of bytes received in a good ICMP segment */
#define EMAC0_RXICMP_ERR_OCT        0xFFC20284         /* EMAC0 Bytes received in an ICMP segment with checksum errors */
#define EMAC0_TM_CTL                0xFFC20700         /* EMAC0 EMAC Time Stamp Control Register */
#define EMAC0_TM_SUBSEC             0xFFC20704         /* EMAC0 EMAC Time Stamp Sub Second Increment */
#define EMAC0_TM_SEC                0xFFC20708         /* EMAC0 EMAC Time Stamp Second Register */
#define EMAC0_TM_NSEC               0xFFC2070C         /* EMAC0 EMAC Time Stamp Nano Second Register */
#define EMAC0_TM_SECUPDT            0xFFC20710         /* EMAC0 EMAC Time Stamp Seconds Update */
#define EMAC0_TM_NSECUPDT           0xFFC20714         /* EMAC0 EMAC Time Stamp Nano Seconds Update */
#define EMAC0_TM_ADDEND             0xFFC20718         /* EMAC0 EMAC Time Stamp Addend Register */
#define EMAC0_TM_TGTM               0xFFC2071C         /* EMAC0 EMAC Time Stamp Target Time Sec. */
#define EMAC0_TM_NTGTM              0xFFC20720         /* EMAC0 EMAC Time Stamp Target Time Nanosec. */
#define EMAC0_TM_HISEC              0xFFC20724         /* EMAC0 EMAC Time Stamp High Second Register */
#define EMAC0_TM_STMPSTAT           0xFFC20728         /* EMAC0 EMAC Time Stamp Status Register */
#define EMAC0_TM_PPSCTL             0xFFC2072C         /* EMAC0 EMAC PPS Control Register */
#define EMAC0_TM_AUXSTMP_NSEC       0xFFC20730         /* EMAC0 EMAC Auxillary Time Stamp Nano Register */
#define EMAC0_TM_AUXSTMP_SEC        0xFFC20734         /* EMAC0 EMAC Auxillary Time Stamp Sec Register */
#define EMAC0_DMA_BUSMODE           0xFFC21000         /* EMAC0 Bus Operating Modes for EMAC DMA */
#define EMAC0_DMA_TXPOLL            0xFFC21004         /* EMAC0 TX DMA Poll demand register */
#define EMAC0_DMA_RXPOLL            0xFFC21008         /* EMAC0 RX DMA Poll demand register */
#define EMAC0_DMA_RXDSC_ADDR        0xFFC2100C         /* EMAC0 RX Descriptor List Address */
#define EMAC0_DMA_TXDSC_ADDR        0xFFC21010         /* EMAC0 TX Descriptor List Address */
#define EMAC0_DMA_STAT              0xFFC21014         /* EMAC0 DMA Status Register */
#define EMAC0_DMA_OPMODE            0xFFC21018         /* EMAC0 DMA Operation Mode Register */
#define EMAC0_DMA_IEN               0xFFC2101C         /* EMAC0 DMA Interrupt Enable Register */
#define EMAC0_DMA_MISS_FRM          0xFFC21020         /* EMAC0 DMA missed frame and buffer overflow counter */
#define EMAC0_DMA_RXIWDOG           0xFFC21024         /* EMAC0 DMA RX Interrupt Watch Dog timer */
#define EMAC0_DMA_BMMODE            0xFFC21028         /* EMAC0 AXI Bus Mode Register */
#define EMAC0_DMA_BMSTAT            0xFFC2102C         /* EMAC0 AXI Status Register */
#define EMAC0_DMA_TXDSC_CUR         0xFFC21048         /* EMAC0 TX current descriptor register */
#define EMAC0_DMA_RXDSC_CUR         0xFFC2104C         /* EMAC0 RX current descriptor register */
#define EMAC0_DMA_TXBUF_CUR         0xFFC21050         /* EMAC0 TX current buffer pointer register */
#define EMAC0_DMA_RXBUF_CUR         0xFFC21054         /* EMAC0 RX current buffer pointer register */
#define EMAC0_HWFEAT                0xFFC21058         /* EMAC0 Hardware Feature Register */

/* =========================
        EMAC1
   ========================= */
#define EMAC1_MACCFG                0xFFC22000         /* EMAC1 MAC Configuration Register */
#define EMAC1_MACFRMFILT            0xFFC22004         /* EMAC1 Filter Register for filtering Received Frames */
#define EMAC1_HASHTBL_HI            0xFFC22008         /* EMAC1 Contains the Upper 32 bits of the hash table */
#define EMAC1_HASHTBL_LO            0xFFC2200C         /* EMAC1 Contains the lower 32 bits of the hash table */
#define EMAC1_GMII_ADDR             0xFFC22010         /* EMAC1 Management Address Register */
#define EMAC1_GMII_DATA             0xFFC22014         /* EMAC1 Management Data Register */
#define EMAC1_FLOWCTL               0xFFC22018         /* EMAC1 MAC FLow Control Register */
#define EMAC1_VLANTAG               0xFFC2201C         /* EMAC1 VLAN Tag Register */
#define EMAC1_VER                   0xFFC22020         /* EMAC1 EMAC Version Register */
#define EMAC1_DBG                   0xFFC22024         /* EMAC1 EMAC Debug Register */
#define EMAC1_RMTWKUP               0xFFC22028         /* EMAC1 Remote wake up frame register */
#define EMAC1_PMT_CTLSTAT           0xFFC2202C         /* EMAC1 PMT Control and Status Register */
#define EMAC1_ISTAT                 0xFFC22038         /* EMAC1 EMAC Interrupt Status Register */
#define EMAC1_IMSK                  0xFFC2203C         /* EMAC1 EMAC Interrupt Mask Register */
#define EMAC1_ADDR0_HI              0xFFC22040         /* EMAC1 EMAC Address0 High Register */
#define EMAC1_ADDR0_LO              0xFFC22044         /* EMAC1 EMAC Address0 Low Register */
#define EMAC1_MMC_CTL               0xFFC22100         /* EMAC1 MMC Control Register */
#define EMAC1_MMC_RXINT             0xFFC22104         /* EMAC1 MMC RX Interrupt Register */
#define EMAC1_MMC_TXINT             0xFFC22108         /* EMAC1 MMC TX Interrupt Register */
#define EMAC1_MMC_RXIMSK            0xFFC2210C         /* EMAC1 MMC RX Interrupt Mask Register */
#define EMAC1_MMC_TXIMSK            0xFFC22110         /* EMAC1 MMC TX Interrupt Mask Register */
#define EMAC1_TXOCTCNT_GB           0xFFC22114         /* EMAC1 Num bytes transmitted exclusive of preamble */
#define EMAC1_TXFRMCNT_GB           0xFFC22118         /* EMAC1 Num frames transmitted exclusive of retired */
#define EMAC1_TXBCASTFRM_G          0xFFC2211C         /* EMAC1 Number of good broadcast frames transmitted. */
#define EMAC1_TXMCASTFRM_G          0xFFC22120         /* EMAC1 Number of good multicast frames transmitted. */
#define EMAC1_TX64_GB               0xFFC22124         /* EMAC1 Number of 64 byte length frames */
#define EMAC1_TX65TO127_GB          0xFFC22128         /* EMAC1 Number of frames of length b/w 65-127 (inclusive) bytes */
#define EMAC1_TX128TO255_GB         0xFFC2212C         /* EMAC1 Number of frames of length b/w 128-255 (inclusive) bytes */
#define EMAC1_TX256TO511_GB         0xFFC22130         /* EMAC1 Number of frames of length b/w 256-511 (inclusive) bytes */
#define EMAC1_TX512TO1023_GB        0xFFC22134         /* EMAC1 Number of frames of length b/w 512-1023 (inclusive) bytes */
#define EMAC1_TX1024TOMAX_GB        0xFFC22138         /* EMAC1 Number of frames of length b/w 1024-max (inclusive) bytes */
#define EMAC1_TXUCASTFRM_GB         0xFFC2213C         /* EMAC1 Number of good and bad unicast frames transmitted */
#define EMAC1_TXMCASTFRM_GB         0xFFC22140         /* EMAC1 Number of good and bad multicast frames transmitted */
#define EMAC1_TXBCASTFRM_GB         0xFFC22144         /* EMAC1 Number of good and bad broadcast frames transmitted */
#define EMAC1_TXUNDR_ERR            0xFFC22148         /* EMAC1 Number of frames aborted due to frame underflow error */
#define EMAC1_TXSNGCOL_G            0xFFC2214C         /* EMAC1 Number of transmitted frames after single collision */
#define EMAC1_TXMULTCOL_G           0xFFC22150         /* EMAC1 Number of transmitted frames with more than one collision */
#define EMAC1_TXDEFERRED            0xFFC22154         /* EMAC1 Number of transmitted frames after deferral */
#define EMAC1_TXLATECOL             0xFFC22158         /* EMAC1 Number of frames aborted due to late collision error */
#define EMAC1_TXEXCESSCOL           0xFFC2215C         /* EMAC1 Number of aborted frames due to excessive collisions */
#define EMAC1_TXCARR_ERR            0xFFC22160         /* EMAC1 Number of aborted frames due to carrier sense error */
#define EMAC1_TXOCTCNT_G            0xFFC22164         /* EMAC1 Number of bytes transmitted in good frames only */
#define EMAC1_TXFRMCNT_G            0xFFC22168         /* EMAC1 Number of good frames transmitted. */
#define EMAC1_TXEXCESSDEF           0xFFC2216C         /* EMAC1 Number of frames aborted due to excessive deferral */
#define EMAC1_TXPAUSEFRM            0xFFC22170         /* EMAC1 Number of good PAUSE frames transmitted. */
#define EMAC1_TXVLANFRM_G           0xFFC22174         /* EMAC1 Number of VLAN frames transmitted */
#define EMAC1_RXFRMCNT_GB           0xFFC22180         /* EMAC1 Number of good and bad frames received. */
#define EMAC1_RXOCTCNT_GB           0xFFC22184         /* EMAC1 Number of bytes received in good and bad frames */
#define EMAC1_RXOCTCNT_G            0xFFC22188         /* EMAC1 Number of bytes received only in good frames */
#define EMAC1_RXBCASTFRM_G          0xFFC2218C         /* EMAC1 Number of good broadcast frames received. */
#define EMAC1_RXMCASTFRM_G          0xFFC22190         /* EMAC1 Number of good multicast frames received */
#define EMAC1_RXCRC_ERR             0xFFC22194         /* EMAC1 Number of frames received with CRC error */
#define EMAC1_RXALIGN_ERR           0xFFC22198         /* EMAC1 Number of frames with alignment error */
#define EMAC1_RXRUNT_ERR            0xFFC2219C         /* EMAC1 Number of frames received with runt error. */
#define EMAC1_RXJAB_ERR             0xFFC221A0         /* EMAC1 Number of frames received with length greater than 1518 */
#define EMAC1_RXUSIZE_G             0xFFC221A4         /* EMAC1 Number of frames received with length 64 */
#define EMAC1_RXOSIZE_G             0xFFC221A8         /* EMAC1 Number of frames received with length greater than maxium */
#define EMAC1_RX64_GB               0xFFC221AC         /* EMAC1 Number of good and bad frames of lengh 64 bytes */
#define EMAC1_RX65TO127_GB          0xFFC221B0         /* EMAC1 Number of good and bad frame between 64-127(inclusive) */
#define EMAC1_RX128TO255_GB         0xFFC221B4         /* EMAC1 Number of good and bad frames received with length between 128 and 255 (inclusive) bytes, exclusive of preamble. */
#define EMAC1_RX256TO511_GB         0xFFC221B8         /* EMAC1 Number of good and bad frames between 256-511(inclusive) */
#define EMAC1_RX512TO1023_GB        0xFFC221BC         /* EMAC1 Number of good and bad frames received between 512-1023 */
#define EMAC1_RX1024TOMAX_GB        0xFFC221C0         /* EMAC1 Number of frames received between 1024 and maxsize */
#define EMAC1_RXUCASTFRM_G          0xFFC221C4         /* EMAC1 Number of good unicast frames received. */
#define EMAC1_RXLEN_ERR             0xFFC221C8         /* EMAC1 Number of frames received with length error */
#define EMAC1_RXOORTYPE             0xFFC221CC         /* EMAC1 Number of frames with length not equal to valid frame size */
#define EMAC1_RXPAUSEFRM            0xFFC221D0         /* EMAC1 Number of good and valid PAUSE frames received. */
#define EMAC1_RXFIFO_OVF            0xFFC221D4         /* EMAC1 Number of missed received frames due to FIFO overflow. This counter is not present in the GMAC-CORE configuration. */
#define EMAC1_RXVLANFRM_GB          0xFFC221D8         /* EMAC1 Number of good and bad VLAN frames received. */
#define EMAC1_RXWDOG_ERR            0xFFC221DC         /* EMAC1 Frames received with error due to watchdog timeout */
#define EMAC1_IPC_RXIMSK            0xFFC22200         /* EMAC1 MMC IPC RX Interrupt Mask Register */
#define EMAC1_IPC_RXINT             0xFFC22208         /* EMAC1 MMC IPC RX Interrupt Register */
#define EMAC1_RXIPV4_GD_FRM         0xFFC22210         /* EMAC1 Number of good IPv4 datagrams */
#define EMAC1_RXIPV4_HDR_ERR_FRM    0xFFC22214         /* EMAC1 Number of IPv4 datagrams with header errors */
#define EMAC1_RXIPV4_NOPAY_FRM      0xFFC22218         /* EMAC1 Number of IPv4 datagrams without checksum */
#define EMAC1_RXIPV4_FRAG_FRM       0xFFC2221C         /* EMAC1 Number of good IPv4 datagrams with fragmentation */
#define EMAC1_RXIPV4_UDSBL_FRM      0xFFC22220         /* EMAC1 Number of IPv4 UDP datagrams with disabled checksum */
#define EMAC1_RXIPV6_GD_FRM         0xFFC22224         /* EMAC1 Number of IPv4 datagrams with TCP/UDP/ICMP payloads */
#define EMAC1_RXIPV6_HDR_ERR_FRM    0xFFC22228         /* EMAC1 Number of IPv6 datagrams with header errors */
#define EMAC1_RXIPV6_NOPAY_FRM      0xFFC2222C         /* EMAC1 Number of IPv6 datagrams with no TCP/UDP/ICMP payload */
#define EMAC1_RXUDP_GD_FRM          0xFFC22230         /* EMAC1 Number of good IP datagrames with good UDP payload */
#define EMAC1_RXUDP_ERR_FRM         0xFFC22234         /* EMAC1 Number of good IP datagrams with UDP checksum errors */
#define EMAC1_RXTCP_GD_FRM          0xFFC22238         /* EMAC1 Number of good IP datagrams with a good TCP payload */
#define EMAC1_RXTCP_ERR_FRM         0xFFC2223C         /* EMAC1 Number of good IP datagrams with TCP checksum errors */
#define EMAC1_RXICMP_GD_FRM         0xFFC22240         /* EMAC1 Number of good IP datagrams with a good ICMP payload */
#define EMAC1_RXICMP_ERR_FRM        0xFFC22244         /* EMAC1 Number of good IP datagrams with ICMP checksum errors */
#define EMAC1_RXIPV4_GD_OCT         0xFFC22250         /* EMAC1 Bytes received in IPv4 datagrams including tcp,udp or icmp */
#define EMAC1_RXIPV4_HDR_ERR_OCT    0xFFC22254         /* EMAC1 Bytes received in IPv4 datagrams with header errors */
#define EMAC1_RXIPV4_NOPAY_OCT      0xFFC22258         /* EMAC1 Bytes received in IPv4 datagrams without tcp,udp,icmp load */
#define EMAC1_RXIPV4_FRAG_OCT       0xFFC2225C         /* EMAC1 Bytes received in fragmented IPv4 datagrams */
#define EMAC1_RXIPV4_UDSBL_OCT      0xFFC22260         /* EMAC1 Bytes received in UDP segment with checksum disabled */
#define EMAC1_RXIPV6_GD_OCT         0xFFC22264         /* EMAC1 Bytes received in good IPv6  including tcp,udp or icmp load */
#define EMAC1_RXIPV6_HDR_ERR_OCT    0xFFC22268         /* EMAC1 Number of bytes received in IPv6 with header errors */
#define EMAC1_RXIPV6_NOPAY_OCT      0xFFC2226C         /* EMAC1 Bytes received in IPv6 without tcp,udp or icmp load */
#define EMAC1_RXUDP_GD_OCT          0xFFC22270         /* EMAC1 Number of bytes received in good UDP segments */
#define EMAC1_RXUDP_ERR_OCT         0xFFC22274         /* EMAC1 Number of bytes received in UDP segment with checksum err */
#define EMAC1_RXTCP_GD_OCT          0xFFC22278         /* EMAC1 Number of bytes received in a good TCP segment */
#define EMAC1_RXTCP_ERR_OCT         0xFFC2227C         /* EMAC1 Number of bytes received in TCP segment with checksum err */
#define EMAC1_RXICMP_GD_OCT         0xFFC22280         /* EMAC1 Number of bytes received in a good ICMP segment */
#define EMAC1_RXICMP_ERR_OCT        0xFFC22284         /* EMAC1 Bytes received in an ICMP segment with checksum errors */
#define EMAC1_TM_CTL                0xFFC22700         /* EMAC1 EMAC Time Stamp Control Register */
#define EMAC1_TM_SUBSEC             0xFFC22704         /* EMAC1 EMAC Time Stamp Sub Second Increment */
#define EMAC1_TM_SEC                0xFFC22708         /* EMAC1 EMAC Time Stamp Second Register */
#define EMAC1_TM_NSEC               0xFFC2270C         /* EMAC1 EMAC Time Stamp Nano Second Register */
#define EMAC1_TM_SECUPDT            0xFFC22710         /* EMAC1 EMAC Time Stamp Seconds Update */
#define EMAC1_TM_NSECUPDT           0xFFC22714         /* EMAC1 EMAC Time Stamp Nano Seconds Update */
#define EMAC1_TM_ADDEND             0xFFC22718         /* EMAC1 EMAC Time Stamp Addend Register */
#define EMAC1_TM_TGTM               0xFFC2271C         /* EMAC1 EMAC Time Stamp Target Time Sec. */
#define EMAC1_TM_NTGTM              0xFFC22720         /* EMAC1 EMAC Time Stamp Target Time Nanosec. */
#define EMAC1_TM_HISEC              0xFFC22724         /* EMAC1 EMAC Time Stamp High Second Register */
#define EMAC1_TM_STMPSTAT           0xFFC22728         /* EMAC1 EMAC Time Stamp Status Register */
#define EMAC1_TM_PPSCTL             0xFFC2272C         /* EMAC1 EMAC PPS Control Register */
#define EMAC1_TM_AUXSTMP_NSEC       0xFFC22730         /* EMAC1 EMAC Auxillary Time Stamp Nano Register */
#define EMAC1_TM_AUXSTMP_SEC        0xFFC22734         /* EMAC1 EMAC Auxillary Time Stamp Sec Register */
#define EMAC1_DMA_BUSMODE           0xFFC23000         /* EMAC1 Bus Operating Modes for EMAC DMA */
#define EMAC1_DMA_TXPOLL            0xFFC23004         /* EMAC1 TX DMA Poll demand register */
#define EMAC1_DMA_RXPOLL            0xFFC23008         /* EMAC1 RX DMA Poll demand register */
#define EMAC1_DMA_RXDSC_ADDR        0xFFC2300C         /* EMAC1 RX Descriptor List Address */
#define EMAC1_DMA_TXDSC_ADDR        0xFFC23010         /* EMAC1 TX Descriptor List Address */
#define EMAC1_DMA_STAT              0xFFC23014         /* EMAC1 DMA Status Register */
#define EMAC1_DMA_OPMODE            0xFFC23018         /* EMAC1 DMA Operation Mode Register */
#define EMAC1_DMA_IEN               0xFFC2301C         /* EMAC1 DMA Interrupt Enable Register */
#define EMAC1_DMA_MISS_FRM          0xFFC23020         /* EMAC1 DMA missed frame and buffer overflow counter */
#define EMAC1_DMA_RXIWDOG           0xFFC23024         /* EMAC1 DMA RX Interrupt Watch Dog timer */
#define EMAC1_DMA_BMMODE            0xFFC23028         /* EMAC1 AXI Bus Mode Register */
#define EMAC1_DMA_BMSTAT            0xFFC2302C         /* EMAC1 AXI Status Register */
#define EMAC1_DMA_TXDSC_CUR         0xFFC23048         /* EMAC1 TX current descriptor register */
#define EMAC1_DMA_RXDSC_CUR         0xFFC2304C         /* EMAC1 RX current descriptor register */
#define EMAC1_DMA_TXBUF_CUR         0xFFC23050         /* EMAC1 TX current buffer pointer register */
#define EMAC1_DMA_RXBUF_CUR         0xFFC23054         /* EMAC1 RX current buffer pointer register */
#define EMAC1_HWFEAT                0xFFC23058         /* EMAC1 Hardware Feature Register */


/* =========================
        SPI Registers
   ========================= */

/* =========================
        SPI0
   ========================= */
#define SPI0_REGBASE                0xFFC40400
#define SPI0_CTL                    0xFFC40404         /* SPI0 Control Register */
#define SPI0_RXCTL                  0xFFC40408         /* SPI0 RX Control Register */
#define SPI0_TXCTL                  0xFFC4040C         /* SPI0 TX Control Register */
#define SPI0_CLK                    0xFFC40410         /* SPI0 Clock Rate Register */
#define SPI0_DLY                    0xFFC40414         /* SPI0 Delay Register */
#define SPI0_SLVSEL                 0xFFC40418         /* SPI0 Slave Select Register */
#define SPI0_RWC                    0xFFC4041C         /* SPI0 Received Word-Count Register */
#define SPI0_RWCR                   0xFFC40420         /* SPI0 Received Word-Count Reload Register */
#define SPI0_TWC                    0xFFC40424         /* SPI0 Transmitted Word-Count Register */
#define SPI0_TWCR                   0xFFC40428         /* SPI0 Transmitted Word-Count Reload Register */
#define SPI0_IMSK                   0xFFC40430         /* SPI0 Interrupt Mask Register */
#define SPI0_IMSK_CLR               0xFFC40434         /* SPI0 Interrupt Mask Clear Register */
#define SPI0_IMSK_SET               0xFFC40438         /* SPI0 Interrupt Mask Set Register */
#define SPI0_STAT                   0xFFC40440         /* SPI0 Status Register */
#define SPI0_ILAT                   0xFFC40444         /* SPI0 Masked Interrupt Condition Register */
#define SPI0_ILAT_CLR               0xFFC40448         /* SPI0 Masked Interrupt Clear Register */
#define SPI0_RFIFO                  0xFFC40450         /* SPI0 Receive FIFO Data Register */
#define SPI0_TFIFO                  0xFFC40458         /* SPI0 Transmit FIFO Data Register */

/* =========================
        SPI1
   ========================= */
#define SPI1_REGBASE                0xFFC40500
#define SPI1_CTL                    0xFFC40504         /* SPI1 Control Register */
#define SPI1_RXCTL                  0xFFC40508         /* SPI1 RX Control Register */
#define SPI1_TXCTL                  0xFFC4050C         /* SPI1 TX Control Register */
#define SPI1_CLK                    0xFFC40510         /* SPI1 Clock Rate Register */
#define SPI1_DLY                    0xFFC40514         /* SPI1 Delay Register */
#define SPI1_SLVSEL                 0xFFC40518         /* SPI1 Slave Select Register */
#define SPI1_RWC                    0xFFC4051C         /* SPI1 Received Word-Count Register */
#define SPI1_RWCR                   0xFFC40520         /* SPI1 Received Word-Count Reload Register */
#define SPI1_TWC                    0xFFC40524         /* SPI1 Transmitted Word-Count Register */
#define SPI1_TWCR                   0xFFC40528         /* SPI1 Transmitted Word-Count Reload Register */
#define SPI1_IMSK                   0xFFC40530         /* SPI1 Interrupt Mask Register */
#define SPI1_IMSK_CLR               0xFFC40534         /* SPI1 Interrupt Mask Clear Register */
#define SPI1_IMSK_SET               0xFFC40538         /* SPI1 Interrupt Mask Set Register */
#define SPI1_STAT                   0xFFC40540         /* SPI1 Status Register */
#define SPI1_ILAT                   0xFFC40544         /* SPI1 Masked Interrupt Condition Register */
#define SPI1_ILAT_CLR               0xFFC40548         /* SPI1 Masked Interrupt Clear Register */
#define SPI1_RFIFO                  0xFFC40550         /* SPI1 Receive FIFO Data Register */
#define SPI1_TFIFO                  0xFFC40558         /* SPI1 Transmit FIFO Data Register */

/* =========================
	SPORT Registers
   ========================= */

/* =========================
	SPORT0
   ========================= */
#define SPORT0_CTL_A                0xFFC40000         /* SPORT0 'A' Control Register */
#define SPORT0_DIV_A                0xFFC40004         /* SPORT0 'A' Clock and FS Divide Register */
#define SPORT0_MCTL_A               0xFFC40008         /* SPORT0 'A' Multichannel Control Register */
#define SPORT0_CS0_A                0xFFC4000C         /* SPORT0 'A' Multichannel Select Register (Channels 0-31) */
#define SPORT0_CS1_A                0xFFC40010         /* SPORT0 'A' Multichannel Select Register (Channels 32-63) */
#define SPORT0_CS2_A                0xFFC40014         /* SPORT0 'A' Multichannel Select Register (Channels 64-95) */
#define SPORT0_CS3_A                0xFFC40018         /* SPORT0 'A' Multichannel Select Register (Channels 96-127) */
#define SPORT0_CNT_A                0xFFC4001C         /* SPORT0 'A' Frame Sync And Clock Divisor Current Count */
#define SPORT0_ERR_A                0xFFC40020         /* SPORT0 'A' Error Register */
#define SPORT0_MSTAT_A              0xFFC40024         /* SPORT0 'A' Multichannel Mode Status Register */
#define SPORT0_CTL2_A               0xFFC40028         /* SPORT0 'A' Control Register 2 */
#define SPORT0_TXPRI_A              0xFFC40040         /* SPORT0 'A' Primary Channel Transmit Buffer Register */
#define SPORT0_RXPRI_A              0xFFC40044         /* SPORT0 'A' Primary Channel Receive Buffer Register */
#define SPORT0_TXSEC_A              0xFFC40048         /* SPORT0 'A' Secondary Channel Transmit Buffer Register */
#define SPORT0_RXSEC_A              0xFFC4004C         /* SPORT0 'A' Secondary Channel Receive Buffer Register */
#define SPORT0_CTL_B                0xFFC40080         /* SPORT0 'B' Control Register */
#define SPORT0_DIV_B                0xFFC40084         /* SPORT0 'B' Clock and FS Divide Register */
#define SPORT0_MCTL_B               0xFFC40088         /* SPORT0 'B' Multichannel Control Register */
#define SPORT0_CS0_B                0xFFC4008C         /* SPORT0 'B' Multichannel Select Register (Channels 0-31) */
#define SPORT0_CS1_B                0xFFC40090         /* SPORT0 'B' Multichannel Select Register (Channels 32-63) */
#define SPORT0_CS2_B                0xFFC40094         /* SPORT0 'B' Multichannel Select Register (Channels 64-95) */
#define SPORT0_CS3_B                0xFFC40098         /* SPORT0 'B' Multichannel Select Register (Channels 96-127) */
#define SPORT0_CNT_B                0xFFC4009C         /* SPORT0 'B' Frame Sync And Clock Divisor Current Count */
#define SPORT0_ERR_B                0xFFC400A0         /* SPORT0 'B' Error Register */
#define SPORT0_MSTAT_B              0xFFC400A4         /* SPORT0 'B' Multichannel Mode Status Register */
#define SPORT0_CTL2_B               0xFFC400A8         /* SPORT0 'B' Control Register 2 */
#define SPORT0_TXPRI_B              0xFFC400C0         /* SPORT0 'B' Primary Channel Transmit Buffer Register */
#define SPORT0_RXPRI_B              0xFFC400C4         /* SPORT0 'B' Primary Channel Receive Buffer Register */
#define SPORT0_TXSEC_B              0xFFC400C8         /* SPORT0 'B' Secondary Channel Transmit Buffer Register */
#define SPORT0_RXSEC_B              0xFFC400CC         /* SPORT0 'B' Secondary Channel Receive Buffer Register */

/* =========================
	SPORT1
   ========================= */
#define SPORT1_CTL_A                0xFFC40100         /* SPORT1 'A' Control Register */
#define SPORT1_DIV_A                0xFFC40104         /* SPORT1 'A' Clock and FS Divide Register */
#define SPORT1_MCTL_A               0xFFC40108         /* SPORT1 'A' Multichannel Control Register */
#define SPORT1_CS0_A                0xFFC4010C         /* SPORT1 'A' Multichannel Select Register (Channels 0-31) */
#define SPORT1_CS1_A                0xFFC40110         /* SPORT1 'A' Multichannel Select Register (Channels 32-63) */
#define SPORT1_CS2_A                0xFFC40114         /* SPORT1 'A' Multichannel Select Register (Channels 64-95) */
#define SPORT1_CS3_A                0xFFC40118         /* SPORT1 'A' Multichannel Select Register (Channels 96-127) */
#define SPORT1_CNT_A                0xFFC4011C         /* SPORT1 'A' Frame Sync And Clock Divisor Current Count */
#define SPORT1_ERR_A                0xFFC40120         /* SPORT1 'A' Error Register */
#define SPORT1_MSTAT_A              0xFFC40124         /* SPORT1 'A' Multichannel Mode Status Register */
#define SPORT1_CTL2_A               0xFFC40128         /* SPORT1 'A' Control Register 2 */
#define SPORT1_TXPRI_A              0xFFC40140         /* SPORT1 'A' Primary Channel Transmit Buffer Register */
#define SPORT1_RXPRI_A              0xFFC40144         /* SPORT1 'A' Primary Channel Receive Buffer Register */
#define SPORT1_TXSEC_A              0xFFC40148         /* SPORT1 'A' Secondary Channel Transmit Buffer Register */
#define SPORT1_RXSEC_A              0xFFC4014C         /* SPORT1 'A' Secondary Channel Receive Buffer Register */
#define SPORT1_CTL_B                0xFFC40180         /* SPORT1 'B' Control Register */
#define SPORT1_DIV_B                0xFFC40184         /* SPORT1 'B' Clock and FS Divide Register */
#define SPORT1_MCTL_B               0xFFC40188         /* SPORT1 'B' Multichannel Control Register */
#define SPORT1_CS0_B                0xFFC4018C         /* SPORT1 'B' Multichannel Select Register (Channels 0-31) */
#define SPORT1_CS1_B                0xFFC40190         /* SPORT1 'B' Multichannel Select Register (Channels 32-63) */
#define SPORT1_CS2_B                0xFFC40194         /* SPORT1 'B' Multichannel Select Register (Channels 64-95) */
#define SPORT1_CS3_B                0xFFC40198         /* SPORT1 'B' Multichannel Select Register (Channels 96-127) */
#define SPORT1_CNT_B                0xFFC4019C         /* SPORT1 'B' Frame Sync And Clock Divisor Current Count */
#define SPORT1_ERR_B                0xFFC401A0         /* SPORT1 'B' Error Register */
#define SPORT1_MSTAT_B              0xFFC401A4         /* SPORT1 'B' Multichannel Mode Status Register */
#define SPORT1_CTL2_B               0xFFC401A8         /* SPORT1 'B' Control Register 2 */
#define SPORT1_TXPRI_B              0xFFC401C0         /* SPORT1 'B' Primary Channel Transmit Buffer Register */
#define SPORT1_RXPRI_B              0xFFC401C4         /* SPORT1 'B' Primary Channel Receive Buffer Register */
#define SPORT1_TXSEC_B              0xFFC401C8         /* SPORT1 'B' Secondary Channel Transmit Buffer Register */
#define SPORT1_RXSEC_B              0xFFC401CC         /* SPORT1 'B' Secondary Channel Receive Buffer Register */

/* =========================
	SPORT2
   ========================= */
#define SPORT2_CTL_A                0xFFC40200         /* SPORT2 'A' Control Register */
#define SPORT2_DIV_A                0xFFC40204         /* SPORT2 'A' Clock and FS Divide Register */
#define SPORT2_MCTL_A               0xFFC40208         /* SPORT2 'A' Multichannel Control Register */
#define SPORT2_CS0_A                0xFFC4020C         /* SPORT2 'A' Multichannel Select Register (Channels 0-31) */
#define SPORT2_CS1_A                0xFFC40210         /* SPORT2 'A' Multichannel Select Register (Channels 32-63) */
#define SPORT2_CS2_A                0xFFC40214         /* SPORT2 'A' Multichannel Select Register (Channels 64-95) */
#define SPORT2_CS3_A                0xFFC40218         /* SPORT2 'A' Multichannel Select Register (Channels 96-127) */
#define SPORT2_CNT_A                0xFFC4021C         /* SPORT2 'A' Frame Sync And Clock Divisor Current Count */
#define SPORT2_ERR_A                0xFFC40220         /* SPORT2 'A' Error Register */
#define SPORT2_MSTAT_A              0xFFC40224         /* SPORT2 'A' Multichannel Mode Status Register */
#define SPORT2_CTL2_A               0xFFC40228         /* SPORT2 'A' Control Register 2 */
#define SPORT2_TXPRI_A              0xFFC40240         /* SPORT2 'A' Primary Channel Transmit Buffer Register */
#define SPORT2_RXPRI_A              0xFFC40244         /* SPORT2 'A' Primary Channel Receive Buffer Register */
#define SPORT2_TXSEC_A              0xFFC40248         /* SPORT2 'A' Secondary Channel Transmit Buffer Register */
#define SPORT2_RXSEC_A              0xFFC4024C         /* SPORT2 'A' Secondary Channel Receive Buffer Register */
#define SPORT2_CTL_B                0xFFC40280         /* SPORT2 'B' Control Register */
#define SPORT2_DIV_B                0xFFC40284         /* SPORT2 'B' Clock and FS Divide Register */
#define SPORT2_MCTL_B               0xFFC40288         /* SPORT2 'B' Multichannel Control Register */
#define SPORT2_CS0_B                0xFFC4028C         /* SPORT2 'B' Multichannel Select Register (Channels 0-31) */
#define SPORT2_CS1_B                0xFFC40290         /* SPORT2 'B' Multichannel Select Register (Channels 32-63) */
#define SPORT2_CS2_B                0xFFC40294         /* SPORT2 'B' Multichannel Select Register (Channels 64-95) */
#define SPORT2_CS3_B                0xFFC40298         /* SPORT2 'B' Multichannel Select Register (Channels 96-127) */
#define SPORT2_CNT_B                0xFFC4029C         /* SPORT2 'B' Frame Sync And Clock Divisor Current Count */
#define SPORT2_ERR_B                0xFFC402A0         /* SPORT2 'B' Error Register */
#define SPORT2_MSTAT_B              0xFFC402A4         /* SPORT2 'B' Multichannel Mode Status Register */
#define SPORT2_CTL2_B               0xFFC402A8         /* SPORT2 'B' Control Register 2 */
#define SPORT2_TXPRI_B              0xFFC402C0         /* SPORT2 'B' Primary Channel Transmit Buffer Register */
#define SPORT2_RXPRI_B              0xFFC402C4         /* SPORT2 'B' Primary Channel Receive Buffer Register */
#define SPORT2_TXSEC_B              0xFFC402C8         /* SPORT2 'B' Secondary Channel Transmit Buffer Register */
#define SPORT2_RXSEC_B              0xFFC402CC         /* SPORT2 'B' Secondary Channel Receive Buffer Register */

/* =========================
	EPPI Registers
   ========================= */

/* =========================
	EPPI0
   ========================= */
#define EPPI0_STAT                  0xFFC18000         /* EPPI0 Status Register */
#define EPPI0_HCNT                  0xFFC18004         /* EPPI0 Horizontal Transfer Count Register */
#define EPPI0_HDLY                  0xFFC18008         /* EPPI0 Horizontal Delay Count Register */
#define EPPI0_VCNT                  0xFFC1800C         /* EPPI0 Vertical Transfer Count Register */
#define EPPI0_VDLY                  0xFFC18010         /* EPPI0 Vertical Delay Count Register */
#define EPPI0_FRAME                 0xFFC18014         /* EPPI0 Lines Per Frame Register */
#define EPPI0_LINE                  0xFFC18018         /* EPPI0 Samples Per Line Register */
#define EPPI0_CLKDIV                0xFFC1801C         /* EPPI0 Clock Divide Register */
#define EPPI0_CTL                   0xFFC18020         /* EPPI0 Control Register */
#define EPPI0_FS1_WLHB              0xFFC18024         /* EPPI0 FS1 Width Register / EPPI Horizontal Blanking Samples Per Line Register */
#define EPPI0_FS1_PASPL             0xFFC18028         /* EPPI0 FS1 Period Register / EPPI Active Samples Per Line Register */
#define EPPI0_FS2_WLVB              0xFFC1802C         /* EPPI0 FS2 Width Register / EPPI Lines Of Vertical Blanking Register */
#define EPPI0_FS2_PALPF             0xFFC18030         /* EPPI0 FS2 Period Register / EPPI Active Lines Per Field Register */
#define EPPI0_IMSK                  0xFFC18034         /* EPPI0 Interrupt Mask Register */
#define EPPI0_ODDCLIP               0xFFC1803C         /* EPPI0 Clipping Register for ODD (Chroma) Data */
#define EPPI0_EVENCLIP              0xFFC18040         /* EPPI0 Clipping Register for EVEN (Luma) Data */
#define EPPI0_FS1_DLY               0xFFC18044         /* EPPI0 Frame Sync 1 Delay Value */
#define EPPI0_FS2_DLY               0xFFC18048         /* EPPI0 Frame Sync 2 Delay Value */
#define EPPI0_CTL2                  0xFFC1804C         /* EPPI0 Control Register 2 */

/* =========================
	EPPI1
   ========================= */
#define EPPI1_STAT                  0xFFC18400         /* EPPI1 Status Register */
#define EPPI1_HCNT                  0xFFC18404         /* EPPI1 Horizontal Transfer Count Register */
#define EPPI1_HDLY                  0xFFC18408         /* EPPI1 Horizontal Delay Count Register */
#define EPPI1_VCNT                  0xFFC1840C         /* EPPI1 Vertical Transfer Count Register */
#define EPPI1_VDLY                  0xFFC18410         /* EPPI1 Vertical Delay Count Register */
#define EPPI1_FRAME                 0xFFC18414         /* EPPI1 Lines Per Frame Register */
#define EPPI1_LINE                  0xFFC18418         /* EPPI1 Samples Per Line Register */
#define EPPI1_CLKDIV                0xFFC1841C         /* EPPI1 Clock Divide Register */
#define EPPI1_CTL                   0xFFC18420         /* EPPI1 Control Register */
#define EPPI1_FS1_WLHB              0xFFC18424         /* EPPI1 FS1 Width Register / EPPI Horizontal Blanking Samples Per Line Register */
#define EPPI1_FS1_PASPL             0xFFC18428         /* EPPI1 FS1 Period Register / EPPI Active Samples Per Line Register */
#define EPPI1_FS2_WLVB              0xFFC1842C         /* EPPI1 FS2 Width Register / EPPI Lines Of Vertical Blanking Register */
#define EPPI1_FS2_PALPF             0xFFC18430         /* EPPI1 FS2 Period Register / EPPI Active Lines Per Field Register */
#define EPPI1_IMSK                  0xFFC18434         /* EPPI1 Interrupt Mask Register */
#define EPPI1_ODDCLIP               0xFFC1843C         /* EPPI1 Clipping Register for ODD (Chroma) Data */
#define EPPI1_EVENCLIP              0xFFC18440         /* EPPI1 Clipping Register for EVEN (Luma) Data */
#define EPPI1_FS1_DLY               0xFFC18444         /* EPPI1 Frame Sync 1 Delay Value */
#define EPPI1_FS2_DLY               0xFFC18448         /* EPPI1 Frame Sync 2 Delay Value */
#define EPPI1_CTL2                  0xFFC1844C         /* EPPI1 Control Register 2 */

/* =========================
	EPPI2
   ========================= */
#define EPPI2_STAT                  0xFFC18800         /* EPPI2 Status Register */
#define EPPI2_HCNT                  0xFFC18804         /* EPPI2 Horizontal Transfer Count Register */
#define EPPI2_HDLY                  0xFFC18808         /* EPPI2 Horizontal Delay Count Register */
#define EPPI2_VCNT                  0xFFC1880C         /* EPPI2 Vertical Transfer Count Register */
#define EPPI2_VDLY                  0xFFC18810         /* EPPI2 Vertical Delay Count Register */
#define EPPI2_FRAME                 0xFFC18814         /* EPPI2 Lines Per Frame Register */
#define EPPI2_LINE                  0xFFC18818         /* EPPI2 Samples Per Line Register */
#define EPPI2_CLKDIV                0xFFC1881C         /* EPPI2 Clock Divide Register */
#define EPPI2_CTL                   0xFFC18820         /* EPPI2 Control Register */
#define EPPI2_FS1_WLHB              0xFFC18824         /* EPPI2 FS1 Width Register / EPPI Horizontal Blanking Samples Per Line Register */
#define EPPI2_FS1_PASPL             0xFFC18828         /* EPPI2 FS1 Period Register / EPPI Active Samples Per Line Register */
#define EPPI2_FS2_WLVB              0xFFC1882C         /* EPPI2 FS2 Width Register / EPPI Lines Of Vertical Blanking Register */
#define EPPI2_FS2_PALPF             0xFFC18830         /* EPPI2 FS2 Period Register / EPPI Active Lines Per Field Register */
#define EPPI2_IMSK                  0xFFC18834         /* EPPI2 Interrupt Mask Register */
#define EPPI2_ODDCLIP               0xFFC1883C         /* EPPI2 Clipping Register for ODD (Chroma) Data */
#define EPPI2_EVENCLIP              0xFFC18840         /* EPPI2 Clipping Register for EVEN (Luma) Data */
#define EPPI2_FS1_DLY               0xFFC18844         /* EPPI2 Frame Sync 1 Delay Value */
#define EPPI2_FS2_DLY               0xFFC18848         /* EPPI2 Frame Sync 2 Delay Value */
#define EPPI2_CTL2                  0xFFC1884C         /* EPPI2 Control Register 2 */



/* =========================
        DDE Registers
   ========================= */

/* =========================
        DMA0
   ========================= */
#define DMA0_NEXT_DESC_PTR          0xFFC41000         /* DMA0 Pointer to Next Initial Descriptor */
#define DMA0_START_ADDR             0xFFC41004         /* DMA0 Start Address of Current Buffer */
#define DMA0_CONFIG                 0xFFC41008         /* DMA0 Configuration Register */
#define DMA0_X_COUNT                0xFFC4100C         /* DMA0 Inner Loop Count Start Value */
#define DMA0_X_MODIFY               0xFFC41010         /* DMA0 Inner Loop Address Increment */
#define DMA0_Y_COUNT                0xFFC41014         /* DMA0 Outer Loop Count Start Value (2D only) */
#define DMA0_Y_MODIFY               0xFFC41018         /* DMA0 Outer Loop Address Increment (2D only) */
#define DMA0_CURR_DESC_PTR          0xFFC41024         /* DMA0 Current Descriptor Pointer */
#define DMA0_PREV_DESC_PTR          0xFFC41028         /* DMA0 Previous Initial Descriptor Pointer */
#define DMA0_CURR_ADDR              0xFFC4102C         /* DMA0 Current Address */
#define DMA0_IRQ_STATUS             0xFFC41030         /* DMA0 Status Register */
#define DMA0_CURR_X_COUNT           0xFFC41034         /* DMA0 Current Count(1D) or intra-row XCNT (2D) */
#define DMA0_CURR_Y_COUNT           0xFFC41038         /* DMA0 Current Row Count (2D only) */
#define DMA0_BWL_COUNT              0xFFC41040         /* DMA0 Bandwidth Limit Count */
#define DMA0_CURR_BWL_COUNT         0xFFC41044         /* DMA0 Bandwidth Limit Count Current */
#define DMA0_BWM_COUNT              0xFFC41048         /* DMA0 Bandwidth Monitor Count */
#define DMA0_CURR_BWM_COUNT         0xFFC4104C         /* DMA0 Bandwidth Monitor Count Current */

/* =========================
        DMA1
   ========================= */
#define DMA1_NEXT_DESC_PTR             0xFFC41080         /* DMA1 Pointer to Next Initial Descriptor */
#define DMA1_START_ADDR              0xFFC41084         /* DMA1 Start Address of Current Buffer */
#define DMA1_CONFIG                    0xFFC41088         /* DMA1 Configuration Register */
#define DMA1_X_COUNT                   0xFFC4108C         /* DMA1 Inner Loop Count Start Value */
#define DMA1_X_MODIFY                   0xFFC41090         /* DMA1 Inner Loop Address Increment */
#define DMA1_Y_COUNT                   0xFFC41094         /* DMA1 Outer Loop Count Start Value (2D only) */
#define DMA1_Y_MODIFY                   0xFFC41098         /* DMA1 Outer Loop Address Increment (2D only) */
#define DMA1_CURR_DESC_PTR             0xFFC410A4         /* DMA1 Current Descriptor Pointer */
#define DMA1_PREV_DESC_PTR             0xFFC410A8         /* DMA1 Previous Initial Descriptor Pointer */
#define DMA1_CURR_ADDR               0xFFC410AC         /* DMA1 Current Address */
#define DMA1_IRQ_STATUS                   0xFFC410B0         /* DMA1 Status Register */
#define DMA1_CURR_X_COUNT               0xFFC410B4         /* DMA1 Current Count(1D) or intra-row XCNT (2D) */
#define DMA1_CURR_Y_COUNT               0xFFC410B8         /* DMA1 Current Row Count (2D only) */
#define DMA1_BWL_COUNT                 0xFFC410C0         /* DMA1 Bandwidth Limit Count */
#define DMA1_CURR_BWL_COUNT             0xFFC410C4         /* DMA1 Bandwidth Limit Count Current */
#define DMA1_BWM_COUNT                 0xFFC410C8         /* DMA1 Bandwidth Monitor Count */
#define DMA1_CURR_BWM_COUNT             0xFFC410CC         /* DMA1 Bandwidth Monitor Count Current */

/* =========================
        DMA2
   ========================= */
#define DMA2_NEXT_DESC_PTR             0xFFC41100         /* DMA2 Pointer to Next Initial Descriptor */
#define DMA2_START_ADDR              0xFFC41104         /* DMA2 Start Address of Current Buffer */
#define DMA2_CONFIG                    0xFFC41108         /* DMA2 Configuration Register */
#define DMA2_X_COUNT                   0xFFC4110C         /* DMA2 Inner Loop Count Start Value */
#define DMA2_X_MODIFY                   0xFFC41110         /* DMA2 Inner Loop Address Increment */
#define DMA2_Y_COUNT                   0xFFC41114         /* DMA2 Outer Loop Count Start Value (2D only) */
#define DMA2_Y_MODIFY                   0xFFC41118         /* DMA2 Outer Loop Address Increment (2D only) */
#define DMA2_CURR_DESC_PTR             0xFFC41124         /* DMA2 Current Descriptor Pointer */
#define DMA2_PREV_DESC_PTR             0xFFC41128         /* DMA2 Previous Initial Descriptor Pointer */
#define DMA2_CURR_ADDR               0xFFC4112C         /* DMA2 Current Address */
#define DMA2_IRQ_STATUS                   0xFFC41130         /* DMA2 Status Register */
#define DMA2_CURR_X_COUNT               0xFFC41134         /* DMA2 Current Count(1D) or intra-row XCNT (2D) */
#define DMA2_CURR_Y_COUNT               0xFFC41138         /* DMA2 Current Row Count (2D only) */
#define DMA2_BWL_COUNT                 0xFFC41140         /* DMA2 Bandwidth Limit Count */
#define DMA2_CURR_BWL_COUNT             0xFFC41144         /* DMA2 Bandwidth Limit Count Current */
#define DMA2_BWM_COUNT                 0xFFC41148         /* DMA2 Bandwidth Monitor Count */
#define DMA2_CURR_BWM_COUNT             0xFFC4114C         /* DMA2 Bandwidth Monitor Count Current */

/* =========================
        DMA3
   ========================= */
#define DMA3_NEXT_DESC_PTR             0xFFC41180         /* DMA3 Pointer to Next Initial Descriptor */
#define DMA3_START_ADDR              0xFFC41184         /* DMA3 Start Address of Current Buffer */
#define DMA3_CONFIG                    0xFFC41188         /* DMA3 Configuration Register */
#define DMA3_X_COUNT                   0xFFC4118C         /* DMA3 Inner Loop Count Start Value */
#define DMA3_X_MODIFY                   0xFFC41190         /* DMA3 Inner Loop Address Increment */
#define DMA3_Y_COUNT                   0xFFC41194         /* DMA3 Outer Loop Count Start Value (2D only) */
#define DMA3_Y_MODIFY                   0xFFC41198         /* DMA3 Outer Loop Address Increment (2D only) */
#define DMA3_CURR_DESC_PTR             0xFFC411A4         /* DMA3 Current Descriptor Pointer */
#define DMA3_PREV_DESC_PTR             0xFFC411A8         /* DMA3 Previous Initial Descriptor Pointer */
#define DMA3_CURR_ADDR               0xFFC411AC         /* DMA3 Current Address */
#define DMA3_IRQ_STATUS                   0xFFC411B0         /* DMA3 Status Register */
#define DMA3_CURR_X_COUNT               0xFFC411B4         /* DMA3 Current Count(1D) or intra-row XCNT (2D) */
#define DMA3_CURR_Y_COUNT               0xFFC411B8         /* DMA3 Current Row Count (2D only) */
#define DMA3_BWL_COUNT                 0xFFC411C0         /* DMA3 Bandwidth Limit Count */
#define DMA3_CURR_BWL_COUNT             0xFFC411C4         /* DMA3 Bandwidth Limit Count Current */
#define DMA3_BWM_COUNT                 0xFFC411C8         /* DMA3 Bandwidth Monitor Count */
#define DMA3_CURR_BWM_COUNT             0xFFC411CC         /* DMA3 Bandwidth Monitor Count Current */

/* =========================
        DMA4
   ========================= */
#define DMA4_NEXT_DESC_PTR             0xFFC41200         /* DMA4 Pointer to Next Initial Descriptor */
#define DMA4_START_ADDR              0xFFC41204         /* DMA4 Start Address of Current Buffer */
#define DMA4_CONFIG                    0xFFC41208         /* DMA4 Configuration Register */
#define DMA4_X_COUNT                   0xFFC4120C         /* DMA4 Inner Loop Count Start Value */
#define DMA4_X_MODIFY                   0xFFC41210         /* DMA4 Inner Loop Address Increment */
#define DMA4_Y_COUNT                   0xFFC41214         /* DMA4 Outer Loop Count Start Value (2D only) */
#define DMA4_Y_MODIFY                   0xFFC41218         /* DMA4 Outer Loop Address Increment (2D only) */
#define DMA4_CURR_DESC_PTR             0xFFC41224         /* DMA4 Current Descriptor Pointer */
#define DMA4_PREV_DESC_PTR             0xFFC41228         /* DMA4 Previous Initial Descriptor Pointer */
#define DMA4_CURR_ADDR               0xFFC4122C         /* DMA4 Current Address */
#define DMA4_IRQ_STATUS                   0xFFC41230         /* DMA4 Status Register */
#define DMA4_CURR_X_COUNT               0xFFC41234         /* DMA4 Current Count(1D) or intra-row XCNT (2D) */
#define DMA4_CURR_Y_COUNT               0xFFC41238         /* DMA4 Current Row Count (2D only) */
#define DMA4_BWL_COUNT                 0xFFC41240         /* DMA4 Bandwidth Limit Count */
#define DMA4_CURR_BWL_COUNT             0xFFC41244         /* DMA4 Bandwidth Limit Count Current */
#define DMA4_BWM_COUNT                 0xFFC41248         /* DMA4 Bandwidth Monitor Count */
#define DMA4_CURR_BWM_COUNT             0xFFC4124C         /* DMA4 Bandwidth Monitor Count Current */

/* =========================
        DMA5
   ========================= */
#define DMA5_NEXT_DESC_PTR             0xFFC41280         /* DMA5 Pointer to Next Initial Descriptor */
#define DMA5_START_ADDR              0xFFC41284         /* DMA5 Start Address of Current Buffer */
#define DMA5_CONFIG                    0xFFC41288         /* DMA5 Configuration Register */
#define DMA5_X_COUNT                   0xFFC4128C         /* DMA5 Inner Loop Count Start Value */
#define DMA5_X_MODIFY                   0xFFC41290         /* DMA5 Inner Loop Address Increment */
#define DMA5_Y_COUNT                   0xFFC41294         /* DMA5 Outer Loop Count Start Value (2D only) */
#define DMA5_Y_MODIFY                   0xFFC41298         /* DMA5 Outer Loop Address Increment (2D only) */
#define DMA5_CURR_DESC_PTR             0xFFC412A4         /* DMA5 Current Descriptor Pointer */
#define DMA5_PREV_DESC_PTR             0xFFC412A8         /* DMA5 Previous Initial Descriptor Pointer */
#define DMA5_CURR_ADDR               0xFFC412AC         /* DMA5 Current Address */
#define DMA5_IRQ_STATUS                   0xFFC412B0         /* DMA5 Status Register */
#define DMA5_CURR_X_COUNT               0xFFC412B4         /* DMA5 Current Count(1D) or intra-row XCNT (2D) */
#define DMA5_CURR_Y_COUNT               0xFFC412B8         /* DMA5 Current Row Count (2D only) */
#define DMA5_BWL_COUNT                 0xFFC412C0         /* DMA5 Bandwidth Limit Count */
#define DMA5_CURR_BWL_COUNT             0xFFC412C4         /* DMA5 Bandwidth Limit Count Current */
#define DMA5_BWM_COUNT                 0xFFC412C8         /* DMA5 Bandwidth Monitor Count */
#define DMA5_CURR_BWM_COUNT             0xFFC412CC         /* DMA5 Bandwidth Monitor Count Current */

/* =========================
        DMA6
   ========================= */
#define DMA6_NEXT_DESC_PTR             0xFFC41300         /* DMA6 Pointer to Next Initial Descriptor */
#define DMA6_START_ADDR              0xFFC41304         /* DMA6 Start Address of Current Buffer */
#define DMA6_CONFIG                    0xFFC41308         /* DMA6 Configuration Register */
#define DMA6_X_COUNT                   0xFFC4130C         /* DMA6 Inner Loop Count Start Value */
#define DMA6_X_MODIFY                   0xFFC41310         /* DMA6 Inner Loop Address Increment */
#define DMA6_Y_COUNT                   0xFFC41314         /* DMA6 Outer Loop Count Start Value (2D only) */
#define DMA6_Y_MODIFY                   0xFFC41318         /* DMA6 Outer Loop Address Increment (2D only) */
#define DMA6_CURR_DESC_PTR             0xFFC41324         /* DMA6 Current Descriptor Pointer */
#define DMA6_PREV_DESC_PTR             0xFFC41328         /* DMA6 Previous Initial Descriptor Pointer */
#define DMA6_CURR_ADDR               0xFFC4132C         /* DMA6 Current Address */
#define DMA6_IRQ_STATUS                   0xFFC41330         /* DMA6 Status Register */
#define DMA6_CURR_X_COUNT               0xFFC41334         /* DMA6 Current Count(1D) or intra-row XCNT (2D) */
#define DMA6_CURR_Y_COUNT               0xFFC41338         /* DMA6 Current Row Count (2D only) */
#define DMA6_BWL_COUNT                 0xFFC41340         /* DMA6 Bandwidth Limit Count */
#define DMA6_CURR_BWL_COUNT             0xFFC41344         /* DMA6 Bandwidth Limit Count Current */
#define DMA6_BWM_COUNT                 0xFFC41348         /* DMA6 Bandwidth Monitor Count */
#define DMA6_CURR_BWM_COUNT             0xFFC4134C         /* DMA6 Bandwidth Monitor Count Current */

/* =========================
        DMA7
   ========================= */
#define DMA7_NEXT_DESC_PTR             0xFFC41380         /* DMA7 Pointer to Next Initial Descriptor */
#define DMA7_START_ADDR              0xFFC41384         /* DMA7 Start Address of Current Buffer */
#define DMA7_CONFIG                    0xFFC41388         /* DMA7 Configuration Register */
#define DMA7_X_COUNT                   0xFFC4138C         /* DMA7 Inner Loop Count Start Value */
#define DMA7_X_MODIFY                   0xFFC41390         /* DMA7 Inner Loop Address Increment */
#define DMA7_Y_COUNT                   0xFFC41394         /* DMA7 Outer Loop Count Start Value (2D only) */
#define DMA7_Y_MODIFY                   0xFFC41398         /* DMA7 Outer Loop Address Increment (2D only) */
#define DMA7_CURR_DESC_PTR             0xFFC413A4         /* DMA7 Current Descriptor Pointer */
#define DMA7_PREV_DESC_PTR             0xFFC413A8         /* DMA7 Previous Initial Descriptor Pointer */
#define DMA7_CURR_ADDR               0xFFC413AC         /* DMA7 Current Address */
#define DMA7_IRQ_STATUS                   0xFFC413B0         /* DMA7 Status Register */
#define DMA7_CURR_X_COUNT               0xFFC413B4         /* DMA7 Current Count(1D) or intra-row XCNT (2D) */
#define DMA7_CURR_Y_COUNT               0xFFC413B8         /* DMA7 Current Row Count (2D only) */
#define DMA7_BWL_COUNT                 0xFFC413C0         /* DMA7 Bandwidth Limit Count */
#define DMA7_CURR_BWL_COUNT             0xFFC413C4         /* DMA7 Bandwidth Limit Count Current */
#define DMA7_BWM_COUNT                 0xFFC413C8         /* DMA7 Bandwidth Monitor Count */
#define DMA7_CURR_BWM_COUNT             0xFFC413CC         /* DMA7 Bandwidth Monitor Count Current */

/* =========================
        DMA8
   ========================= */
#define DMA8_NEXT_DESC_PTR             0xFFC41400         /* DMA8 Pointer to Next Initial Descriptor */
#define DMA8_START_ADDR              0xFFC41404         /* DMA8 Start Address of Current Buffer */
#define DMA8_CONFIG                    0xFFC41408         /* DMA8 Configuration Register */
#define DMA8_X_COUNT                   0xFFC4140C         /* DMA8 Inner Loop Count Start Value */
#define DMA8_X_MODIFY                   0xFFC41410         /* DMA8 Inner Loop Address Increment */
#define DMA8_Y_COUNT                   0xFFC41414         /* DMA8 Outer Loop Count Start Value (2D only) */
#define DMA8_Y_MODIFY                   0xFFC41418         /* DMA8 Outer Loop Address Increment (2D only) */
#define DMA8_CURR_DESC_PTR             0xFFC41424         /* DMA8 Current Descriptor Pointer */
#define DMA8_PREV_DESC_PTR             0xFFC41428         /* DMA8 Previous Initial Descriptor Pointer */
#define DMA8_CURR_ADDR               0xFFC4142C         /* DMA8 Current Address */
#define DMA8_IRQ_STATUS                   0xFFC41430         /* DMA8 Status Register */
#define DMA8_CURR_X_COUNT               0xFFC41434         /* DMA8 Current Count(1D) or intra-row XCNT (2D) */
#define DMA8_CURR_Y_COUNT               0xFFC41438         /* DMA8 Current Row Count (2D only) */
#define DMA8_BWL_COUNT                 0xFFC41440         /* DMA8 Bandwidth Limit Count */
#define DMA8_CURR_BWL_COUNT             0xFFC41444         /* DMA8 Bandwidth Limit Count Current */
#define DMA8_BWM_COUNT                 0xFFC41448         /* DMA8 Bandwidth Monitor Count */
#define DMA8_CURR_BWM_COUNT             0xFFC4144C         /* DMA8 Bandwidth Monitor Count Current */

/* =========================
        DMA9
   ========================= */
#define DMA9_NEXT_DESC_PTR             0xFFC41480         /* DMA9 Pointer to Next Initial Descriptor */
#define DMA9_START_ADDR              0xFFC41484         /* DMA9 Start Address of Current Buffer */
#define DMA9_CONFIG                    0xFFC41488         /* DMA9 Configuration Register */
#define DMA9_X_COUNT                   0xFFC4148C         /* DMA9 Inner Loop Count Start Value */
#define DMA9_X_MODIFY                   0xFFC41490         /* DMA9 Inner Loop Address Increment */
#define DMA9_Y_COUNT                   0xFFC41494         /* DMA9 Outer Loop Count Start Value (2D only) */
#define DMA9_Y_MODIFY                   0xFFC41498         /* DMA9 Outer Loop Address Increment (2D only) */
#define DMA9_CURR_DESC_PTR             0xFFC414A4         /* DMA9 Current Descriptor Pointer */
#define DMA9_PREV_DESC_PTR             0xFFC414A8         /* DMA9 Previous Initial Descriptor Pointer */
#define DMA9_CURR_ADDR               0xFFC414AC         /* DMA9 Current Address */
#define DMA9_IRQ_STATUS                   0xFFC414B0         /* DMA9 Status Register */
#define DMA9_CURR_X_COUNT               0xFFC414B4         /* DMA9 Current Count(1D) or intra-row XCNT (2D) */
#define DMA9_CURR_Y_COUNT               0xFFC414B8         /* DMA9 Current Row Count (2D only) */
#define DMA9_BWL_COUNT                 0xFFC414C0         /* DMA9 Bandwidth Limit Count */
#define DMA9_CURR_BWL_COUNT             0xFFC414C4         /* DMA9 Bandwidth Limit Count Current */
#define DMA9_BWM_COUNT                 0xFFC414C8         /* DMA9 Bandwidth Monitor Count */
#define DMA9_CURR_BWM_COUNT             0xFFC414CC         /* DMA9 Bandwidth Monitor Count Current */

/* =========================
        DMA10
   ========================= */
#define DMA10_NEXT_DESC_PTR            0xFFC05000         /* DMA10 Pointer to Next Initial Descriptor */
#define DMA10_START_ADDR             0xFFC05004         /* DMA10 Start Address of Current Buffer */
#define DMA10_CONFIG                   0xFFC05008         /* DMA10 Configuration Register */
#define DMA10_X_COUNT                  0xFFC0500C         /* DMA10 Inner Loop Count Start Value */
#define DMA10_X_MODIFY                  0xFFC05010         /* DMA10 Inner Loop Address Increment */
#define DMA10_Y_COUNT                  0xFFC05014         /* DMA10 Outer Loop Count Start Value (2D only) */
#define DMA10_Y_MODIFY                  0xFFC05018         /* DMA10 Outer Loop Address Increment (2D only) */
#define DMA10_CURR_DESC_PTR            0xFFC05024         /* DMA10 Current Descriptor Pointer */
#define DMA10_PREV_DESC_PTR            0xFFC05028         /* DMA10 Previous Initial Descriptor Pointer */
#define DMA10_CURR_ADDR              0xFFC0502C         /* DMA10 Current Address */
#define DMA10_IRQ_STATUS                  0xFFC05030         /* DMA10 Status Register */
#define DMA10_CURR_X_COUNT              0xFFC05034         /* DMA10 Current Count(1D) or intra-row XCNT (2D) */
#define DMA10_CURR_Y_COUNT              0xFFC05038         /* DMA10 Current Row Count (2D only) */
#define DMA10_BWL_COUNT                0xFFC05040         /* DMA10 Bandwidth Limit Count */
#define DMA10_CURR_BWL_COUNT            0xFFC05044         /* DMA10 Bandwidth Limit Count Current */
#define DMA10_BWM_COUNT                0xFFC05048         /* DMA10 Bandwidth Monitor Count */
#define DMA10_CURR_BWM_COUNT            0xFFC0504C         /* DMA10 Bandwidth Monitor Count Current */

/* =========================
        DMA11
   ========================= */
#define DMA11_NEXT_DESC_PTR            0xFFC05080         /* DMA11 Pointer to Next Initial Descriptor */
#define DMA11_START_ADDR             0xFFC05084         /* DMA11 Start Address of Current Buffer */
#define DMA11_CONFIG                   0xFFC05088         /* DMA11 Configuration Register */
#define DMA11_X_COUNT                  0xFFC0508C         /* DMA11 Inner Loop Count Start Value */
#define DMA11_X_MODIFY                  0xFFC05090         /* DMA11 Inner Loop Address Increment */
#define DMA11_Y_COUNT                  0xFFC05094         /* DMA11 Outer Loop Count Start Value (2D only) */
#define DMA11_Y_MODIFY                  0xFFC05098         /* DMA11 Outer Loop Address Increment (2D only) */
#define DMA11_CURR_DESC_PTR            0xFFC050A4         /* DMA11 Current Descriptor Pointer */
#define DMA11_PREV_DESC_PTR            0xFFC050A8         /* DMA11 Previous Initial Descriptor Pointer */
#define DMA11_CURR_ADDR              0xFFC050AC         /* DMA11 Current Address */
#define DMA11_IRQ_STATUS                  0xFFC050B0         /* DMA11 Status Register */
#define DMA11_CURR_X_COUNT              0xFFC050B4         /* DMA11 Current Count(1D) or intra-row XCNT (2D) */
#define DMA11_CURR_Y_COUNT              0xFFC050B8         /* DMA11 Current Row Count (2D only) */
#define DMA11_BWL_COUNT                0xFFC050C0         /* DMA11 Bandwidth Limit Count */
#define DMA11_CURR_BWL_COUNT            0xFFC050C4         /* DMA11 Bandwidth Limit Count Current */
#define DMA11_BWM_COUNT                0xFFC050C8         /* DMA11 Bandwidth Monitor Count */
#define DMA11_CURR_BWM_COUNT            0xFFC050CC         /* DMA11 Bandwidth Monitor Count Current */

/* =========================
        DMA12
   ========================= */
#define DMA12_NEXT_DESC_PTR            0xFFC05100         /* DMA12 Pointer to Next Initial Descriptor */
#define DMA12_START_ADDR             0xFFC05104         /* DMA12 Start Address of Current Buffer */
#define DMA12_CONFIG                   0xFFC05108         /* DMA12 Configuration Register */
#define DMA12_X_COUNT                  0xFFC0510C         /* DMA12 Inner Loop Count Start Value */
#define DMA12_X_MODIFY                  0xFFC05110         /* DMA12 Inner Loop Address Increment */
#define DMA12_Y_COUNT                  0xFFC05114         /* DMA12 Outer Loop Count Start Value (2D only) */
#define DMA12_Y_MODIFY                  0xFFC05118         /* DMA12 Outer Loop Address Increment (2D only) */
#define DMA12_CURR_DESC_PTR            0xFFC05124         /* DMA12 Current Descriptor Pointer */
#define DMA12_PREV_DESC_PTR            0xFFC05128         /* DMA12 Previous Initial Descriptor Pointer */
#define DMA12_CURR_ADDR              0xFFC0512C         /* DMA12 Current Address */
#define DMA12_IRQ_STATUS                  0xFFC05130         /* DMA12 Status Register */
#define DMA12_CURR_X_COUNT              0xFFC05134         /* DMA12 Current Count(1D) or intra-row XCNT (2D) */
#define DMA12_CURR_Y_COUNT              0xFFC05138         /* DMA12 Current Row Count (2D only) */
#define DMA12_BWL_COUNT                0xFFC05140         /* DMA12 Bandwidth Limit Count */
#define DMA12_CURR_BWL_COUNT            0xFFC05144         /* DMA12 Bandwidth Limit Count Current */
#define DMA12_BWM_COUNT                0xFFC05148         /* DMA12 Bandwidth Monitor Count */
#define DMA12_CURR_BWM_COUNT            0xFFC0514C         /* DMA12 Bandwidth Monitor Count Current */

/* =========================
        DMA13
   ========================= */
#define DMA13_NEXT_DESC_PTR            0xFFC07000         /* DMA13 Pointer to Next Initial Descriptor */
#define DMA13_START_ADDR             0xFFC07004         /* DMA13 Start Address of Current Buffer */
#define DMA13_CONFIG                   0xFFC07008         /* DMA13 Configuration Register */
#define DMA13_X_COUNT                  0xFFC0700C         /* DMA13 Inner Loop Count Start Value */
#define DMA13_X_MODIFY                  0xFFC07010         /* DMA13 Inner Loop Address Increment */
#define DMA13_Y_COUNT                  0xFFC07014         /* DMA13 Outer Loop Count Start Value (2D only) */
#define DMA13_Y_MODIFY                  0xFFC07018         /* DMA13 Outer Loop Address Increment (2D only) */
#define DMA13_CURR_DESC_PTR            0xFFC07024         /* DMA13 Current Descriptor Pointer */
#define DMA13_PREV_DESC_PTR            0xFFC07028         /* DMA13 Previous Initial Descriptor Pointer */
#define DMA13_CURR_ADDR              0xFFC0702C         /* DMA13 Current Address */
#define DMA13_IRQ_STATUS                  0xFFC07030         /* DMA13 Status Register */
#define DMA13_CURR_X_COUNT              0xFFC07034         /* DMA13 Current Count(1D) or intra-row XCNT (2D) */
#define DMA13_CURR_Y_COUNT              0xFFC07038         /* DMA13 Current Row Count (2D only) */
#define DMA13_BWL_COUNT                0xFFC07040         /* DMA13 Bandwidth Limit Count */
#define DMA13_CURR_BWL_COUNT            0xFFC07044         /* DMA13 Bandwidth Limit Count Current */
#define DMA13_BWM_COUNT                0xFFC07048         /* DMA13 Bandwidth Monitor Count */
#define DMA13_CURR_BWM_COUNT            0xFFC0704C         /* DMA13 Bandwidth Monitor Count Current */

/* =========================
        DMA14
   ========================= */
#define DMA14_NEXT_DESC_PTR            0xFFC07080         /* DMA14 Pointer to Next Initial Descriptor */
#define DMA14_START_ADDR             0xFFC07084         /* DMA14 Start Address of Current Buffer */
#define DMA14_CONFIG                   0xFFC07088         /* DMA14 Configuration Register */
#define DMA14_X_COUNT                  0xFFC0708C         /* DMA14 Inner Loop Count Start Value */
#define DMA14_X_MODIFY                  0xFFC07090         /* DMA14 Inner Loop Address Increment */
#define DMA14_Y_COUNT                  0xFFC07094         /* DMA14 Outer Loop Count Start Value (2D only) */
#define DMA14_Y_MODIFY                  0xFFC07098         /* DMA14 Outer Loop Address Increment (2D only) */
#define DMA14_CURR_DESC_PTR            0xFFC070A4         /* DMA14 Current Descriptor Pointer */
#define DMA14_PREV_DESC_PTR            0xFFC070A8         /* DMA14 Previous Initial Descriptor Pointer */
#define DMA14_CURR_ADDR              0xFFC070AC         /* DMA14 Current Address */
#define DMA14_IRQ_STATUS                  0xFFC070B0         /* DMA14 Status Register */
#define DMA14_CURR_X_COUNT              0xFFC070B4         /* DMA14 Current Count(1D) or intra-row XCNT (2D) */
#define DMA14_CURR_Y_COUNT              0xFFC070B8         /* DMA14 Current Row Count (2D only) */
#define DMA14_BWL_COUNT                0xFFC070C0         /* DMA14 Bandwidth Limit Count */
#define DMA14_CURR_BWL_COUNT            0xFFC070C4         /* DMA14 Bandwidth Limit Count Current */
#define DMA14_BWM_COUNT                0xFFC070C8         /* DMA14 Bandwidth Monitor Count */
#define DMA14_CURR_BWM_COUNT            0xFFC070CC         /* DMA14 Bandwidth Monitor Count Current */

/* =========================
        DMA15
   ========================= */
#define DMA15_NEXT_DESC_PTR            0xFFC07100         /* DMA15 Pointer to Next Initial Descriptor */
#define DMA15_START_ADDR             0xFFC07104         /* DMA15 Start Address of Current Buffer */
#define DMA15_CONFIG                   0xFFC07108         /* DMA15 Configuration Register */
#define DMA15_X_COUNT                  0xFFC0710C         /* DMA15 Inner Loop Count Start Value */
#define DMA15_X_MODIFY                  0xFFC07110         /* DMA15 Inner Loop Address Increment */
#define DMA15_Y_COUNT                  0xFFC07114         /* DMA15 Outer Loop Count Start Value (2D only) */
#define DMA15_Y_MODIFY                  0xFFC07118         /* DMA15 Outer Loop Address Increment (2D only) */
#define DMA15_CURR_DESC_PTR            0xFFC07124         /* DMA15 Current Descriptor Pointer */
#define DMA15_PREV_DESC_PTR            0xFFC07128         /* DMA15 Previous Initial Descriptor Pointer */
#define DMA15_CURR_ADDR              0xFFC0712C         /* DMA15 Current Address */
#define DMA15_IRQ_STATUS                  0xFFC07130         /* DMA15 Status Register */
#define DMA15_CURR_X_COUNT              0xFFC07134         /* DMA15 Current Count(1D) or intra-row XCNT (2D) */
#define DMA15_CURR_Y_COUNT              0xFFC07138         /* DMA15 Current Row Count (2D only) */
#define DMA15_BWL_COUNT                0xFFC07140         /* DMA15 Bandwidth Limit Count */
#define DMA15_CURR_BWL_COUNT            0xFFC07144         /* DMA15 Bandwidth Limit Count Current */
#define DMA15_BWM_COUNT                0xFFC07148         /* DMA15 Bandwidth Monitor Count */
#define DMA15_CURR_BWM_COUNT            0xFFC0714C         /* DMA15 Bandwidth Monitor Count Current */

/* =========================
        DMA16
   ========================= */
#define DMA16_NEXT_DESC_PTR            0xFFC07180         /* DMA16 Pointer to Next Initial Descriptor */
#define DMA16_START_ADDR             0xFFC07184         /* DMA16 Start Address of Current Buffer */
#define DMA16_CONFIG                   0xFFC07188         /* DMA16 Configuration Register */
#define DMA16_X_COUNT                  0xFFC0718C         /* DMA16 Inner Loop Count Start Value */
#define DMA16_X_MODIFY                  0xFFC07190         /* DMA16 Inner Loop Address Increment */
#define DMA16_Y_COUNT                  0xFFC07194         /* DMA16 Outer Loop Count Start Value (2D only) */
#define DMA16_Y_MODIFY                  0xFFC07198         /* DMA16 Outer Loop Address Increment (2D only) */
#define DMA16_CURR_DESC_PTR            0xFFC071A4         /* DMA16 Current Descriptor Pointer */
#define DMA16_PREV_DESC_PTR            0xFFC071A8         /* DMA16 Previous Initial Descriptor Pointer */
#define DMA16_CURR_ADDR              0xFFC071AC         /* DMA16 Current Address */
#define DMA16_IRQ_STATUS                  0xFFC071B0         /* DMA16 Status Register */
#define DMA16_CURR_X_COUNT              0xFFC071B4         /* DMA16 Current Count(1D) or intra-row XCNT (2D) */
#define DMA16_CURR_Y_COUNT              0xFFC071B8         /* DMA16 Current Row Count (2D only) */
#define DMA16_BWL_COUNT                0xFFC071C0         /* DMA16 Bandwidth Limit Count */
#define DMA16_CURR_BWL_COUNT            0xFFC071C4         /* DMA16 Bandwidth Limit Count Current */
#define DMA16_BWM_COUNT                0xFFC071C8         /* DMA16 Bandwidth Monitor Count */
#define DMA16_CURR_BWM_COUNT            0xFFC071CC         /* DMA16 Bandwidth Monitor Count Current */

/* =========================
        DMA17
   ========================= */
#define DMA17_NEXT_DESC_PTR            0xFFC07200         /* DMA17 Pointer to Next Initial Descriptor */
#define DMA17_START_ADDR             0xFFC07204         /* DMA17 Start Address of Current Buffer */
#define DMA17_CONFIG                   0xFFC07208         /* DMA17 Configuration Register */
#define DMA17_X_COUNT                  0xFFC0720C         /* DMA17 Inner Loop Count Start Value */
#define DMA17_X_MODIFY                  0xFFC07210         /* DMA17 Inner Loop Address Increment */
#define DMA17_Y_COUNT                  0xFFC07214         /* DMA17 Outer Loop Count Start Value (2D only) */
#define DMA17_Y_MODIFY                  0xFFC07218         /* DMA17 Outer Loop Address Increment (2D only) */
#define DMA17_CURR_DESC_PTR            0xFFC07224         /* DMA17 Current Descriptor Pointer */
#define DMA17_PREV_DESC_PTR            0xFFC07228         /* DMA17 Previous Initial Descriptor Pointer */
#define DMA17_CURR_ADDR              0xFFC0722C         /* DMA17 Current Address */
#define DMA17_IRQ_STATUS                  0xFFC07230         /* DMA17 Status Register */
#define DMA17_CURR_X_COUNT              0xFFC07234         /* DMA17 Current Count(1D) or intra-row XCNT (2D) */
#define DMA17_CURR_Y_COUNT              0xFFC07238         /* DMA17 Current Row Count (2D only) */
#define DMA17_BWL_COUNT                0xFFC07240         /* DMA17 Bandwidth Limit Count */
#define DMA17_CURR_BWL_COUNT            0xFFC07244         /* DMA17 Bandwidth Limit Count Current */
#define DMA17_BWM_COUNT                0xFFC07248         /* DMA17 Bandwidth Monitor Count */
#define DMA17_CURR_BWM_COUNT            0xFFC0724C         /* DMA17 Bandwidth Monitor Count Current */

/* =========================
        DMA18
   ========================= */
#define DMA18_NEXT_DESC_PTR            0xFFC07280         /* DMA18 Pointer to Next Initial Descriptor */
#define DMA18_START_ADDR             0xFFC07284         /* DMA18 Start Address of Current Buffer */
#define DMA18_CONFIG                   0xFFC07288         /* DMA18 Configuration Register */
#define DMA18_X_COUNT                  0xFFC0728C         /* DMA18 Inner Loop Count Start Value */
#define DMA18_X_MODIFY                  0xFFC07290         /* DMA18 Inner Loop Address Increment */
#define DMA18_Y_COUNT                  0xFFC07294         /* DMA18 Outer Loop Count Start Value (2D only) */
#define DMA18_Y_MODIFY                  0xFFC07298         /* DMA18 Outer Loop Address Increment (2D only) */
#define DMA18_CURR_DESC_PTR            0xFFC072A4         /* DMA18 Current Descriptor Pointer */
#define DMA18_PREV_DESC_PTR            0xFFC072A8         /* DMA18 Previous Initial Descriptor Pointer */
#define DMA18_CURR_ADDR              0xFFC072AC         /* DMA18 Current Address */
#define DMA18_IRQ_STATUS                  0xFFC072B0         /* DMA18 Status Register */
#define DMA18_CURR_X_COUNT              0xFFC072B4         /* DMA18 Current Count(1D) or intra-row XCNT (2D) */
#define DMA18_CURR_Y_COUNT              0xFFC072B8         /* DMA18 Current Row Count (2D only) */
#define DMA18_BWL_COUNT                0xFFC072C0         /* DMA18 Bandwidth Limit Count */
#define DMA18_CURR_BWL_COUNT            0xFFC072C4         /* DMA18 Bandwidth Limit Count Current */
#define DMA18_BWM_COUNT                0xFFC072C8         /* DMA18 Bandwidth Monitor Count */
#define DMA18_CURR_BWM_COUNT            0xFFC072CC         /* DMA18 Bandwidth Monitor Count Current */

/* =========================
        DMA19
   ========================= */
#define DMA19_NEXT_DESC_PTR            0xFFC07300         /* DMA19 Pointer to Next Initial Descriptor */
#define DMA19_START_ADDR             0xFFC07304         /* DMA19 Start Address of Current Buffer */
#define DMA19_CONFIG                   0xFFC07308         /* DMA19 Configuration Register */
#define DMA19_X_COUNT                  0xFFC0730C         /* DMA19 Inner Loop Count Start Value */
#define DMA19_X_MODIFY                  0xFFC07310         /* DMA19 Inner Loop Address Increment */
#define DMA19_Y_COUNT                  0xFFC07314         /* DMA19 Outer Loop Count Start Value (2D only) */
#define DMA19_Y_MODIFY                  0xFFC07318         /* DMA19 Outer Loop Address Increment (2D only) */
#define DMA19_CURR_DESC_PTR            0xFFC07324         /* DMA19 Current Descriptor Pointer */
#define DMA19_PREV_DESC_PTR            0xFFC07328         /* DMA19 Previous Initial Descriptor Pointer */
#define DMA19_CURR_ADDR              0xFFC0732C         /* DMA19 Current Address */
#define DMA19_IRQ_STATUS                  0xFFC07330         /* DMA19 Status Register */
#define DMA19_CURR_X_COUNT              0xFFC07334         /* DMA19 Current Count(1D) or intra-row XCNT (2D) */
#define DMA19_CURR_Y_COUNT              0xFFC07338         /* DMA19 Current Row Count (2D only) */
#define DMA19_BWL_COUNT                0xFFC07340         /* DMA19 Bandwidth Limit Count */
#define DMA19_CURR_BWL_COUNT            0xFFC07344         /* DMA19 Bandwidth Limit Count Current */
#define DMA19_BWM_COUNT                0xFFC07348         /* DMA19 Bandwidth Monitor Count */
#define DMA19_CURR_BWM_COUNT            0xFFC0734C         /* DMA19 Bandwidth Monitor Count Current */

/* =========================
        DMA20
   ========================= */
#define DMA20_NEXT_DESC_PTR            0xFFC07380         /* DMA20 Pointer to Next Initial Descriptor */
#define DMA20_START_ADDR             0xFFC07384         /* DMA20 Start Address of Current Buffer */
#define DMA20_CONFIG                   0xFFC07388         /* DMA20 Configuration Register */
#define DMA20_X_COUNT                  0xFFC0738C         /* DMA20 Inner Loop Count Start Value */
#define DMA20_X_MODIFY                  0xFFC07390         /* DMA20 Inner Loop Address Increment */
#define DMA20_Y_COUNT                  0xFFC07394         /* DMA20 Outer Loop Count Start Value (2D only) */
#define DMA20_Y_MODIFY                  0xFFC07398         /* DMA20 Outer Loop Address Increment (2D only) */
#define DMA20_CURR_DESC_PTR            0xFFC073A4         /* DMA20 Current Descriptor Pointer */
#define DMA20_PREV_DESC_PTR            0xFFC073A8         /* DMA20 Previous Initial Descriptor Pointer */
#define DMA20_CURR_ADDR              0xFFC073AC         /* DMA20 Current Address */
#define DMA20_IRQ_STATUS                  0xFFC073B0         /* DMA20 Status Register */
#define DMA20_CURR_X_COUNT              0xFFC073B4         /* DMA20 Current Count(1D) or intra-row XCNT (2D) */
#define DMA20_CURR_Y_COUNT              0xFFC073B8         /* DMA20 Current Row Count (2D only) */
#define DMA20_BWL_COUNT                0xFFC073C0         /* DMA20 Bandwidth Limit Count */
#define DMA20_CURR_BWL_COUNT            0xFFC073C4         /* DMA20 Bandwidth Limit Count Current */
#define DMA20_BWM_COUNT                0xFFC073C8         /* DMA20 Bandwidth Monitor Count */
#define DMA20_CURR_BWM_COUNT            0xFFC073CC         /* DMA20 Bandwidth Monitor Count Current */

/* =========================
        DMA21
   ========================= */
#define DMA21_NEXT_DESC_PTR            0xFFC09000         /* DMA21 Pointer to Next Initial Descriptor */
#define DMA21_START_ADDR             0xFFC09004         /* DMA21 Start Address of Current Buffer */
#define DMA21_CONFIG                   0xFFC09008         /* DMA21 Configuration Register */
#define DMA21_X_COUNT                  0xFFC0900C         /* DMA21 Inner Loop Count Start Value */
#define DMA21_X_MODIFY                  0xFFC09010         /* DMA21 Inner Loop Address Increment */
#define DMA21_Y_COUNT                  0xFFC09014         /* DMA21 Outer Loop Count Start Value (2D only) */
#define DMA21_Y_MODIFY                  0xFFC09018         /* DMA21 Outer Loop Address Increment (2D only) */
#define DMA21_CURR_DESC_PTR            0xFFC09024         /* DMA21 Current Descriptor Pointer */
#define DMA21_PREV_DESC_PTR            0xFFC09028         /* DMA21 Previous Initial Descriptor Pointer */
#define DMA21_CURR_ADDR              0xFFC0902C         /* DMA21 Current Address */
#define DMA21_IRQ_STATUS                  0xFFC09030         /* DMA21 Status Register */
#define DMA21_CURR_X_COUNT              0xFFC09034         /* DMA21 Current Count(1D) or intra-row XCNT (2D) */
#define DMA21_CURR_Y_COUNT              0xFFC09038         /* DMA21 Current Row Count (2D only) */
#define DMA21_BWL_COUNT                0xFFC09040         /* DMA21 Bandwidth Limit Count */
#define DMA21_CURR_BWL_COUNT            0xFFC09044         /* DMA21 Bandwidth Limit Count Current */
#define DMA21_BWM_COUNT                0xFFC09048         /* DMA21 Bandwidth Monitor Count */
#define DMA21_CURR_BWM_COUNT            0xFFC0904C         /* DMA21 Bandwidth Monitor Count Current */

/* =========================
        DMA22
   ========================= */
#define DMA22_NEXT_DESC_PTR            0xFFC09080         /* DMA22 Pointer to Next Initial Descriptor */
#define DMA22_START_ADDR             0xFFC09084         /* DMA22 Start Address of Current Buffer */
#define DMA22_CONFIG                   0xFFC09088         /* DMA22 Configuration Register */
#define DMA22_X_COUNT                  0xFFC0908C         /* DMA22 Inner Loop Count Start Value */
#define DMA22_X_MODIFY                  0xFFC09090         /* DMA22 Inner Loop Address Increment */
#define DMA22_Y_COUNT                  0xFFC09094         /* DMA22 Outer Loop Count Start Value (2D only) */
#define DMA22_Y_MODIFY                  0xFFC09098         /* DMA22 Outer Loop Address Increment (2D only) */
#define DMA22_CURR_DESC_PTR            0xFFC090A4         /* DMA22 Current Descriptor Pointer */
#define DMA22_PREV_DESC_PTR            0xFFC090A8         /* DMA22 Previous Initial Descriptor Pointer */
#define DMA22_CURR_ADDR              0xFFC090AC         /* DMA22 Current Address */
#define DMA22_IRQ_STATUS                  0xFFC090B0         /* DMA22 Status Register */
#define DMA22_CURR_X_COUNT              0xFFC090B4         /* DMA22 Current Count(1D) or intra-row XCNT (2D) */
#define DMA22_CURR_Y_COUNT              0xFFC090B8         /* DMA22 Current Row Count (2D only) */
#define DMA22_BWL_COUNT                0xFFC090C0         /* DMA22 Bandwidth Limit Count */
#define DMA22_CURR_BWL_COUNT            0xFFC090C4         /* DMA22 Bandwidth Limit Count Current */
#define DMA22_BWM_COUNT                0xFFC090C8         /* DMA22 Bandwidth Monitor Count */
#define DMA22_CURR_BWM_COUNT            0xFFC090CC         /* DMA22 Bandwidth Monitor Count Current */

/* =========================
        DMA23
   ========================= */
#define DMA23_NEXT_DESC_PTR            0xFFC09100         /* DMA23 Pointer to Next Initial Descriptor */
#define DMA23_START_ADDR             0xFFC09104         /* DMA23 Start Address of Current Buffer */
#define DMA23_CONFIG                   0xFFC09108         /* DMA23 Configuration Register */
#define DMA23_X_COUNT                  0xFFC0910C         /* DMA23 Inner Loop Count Start Value */
#define DMA23_X_MODIFY                  0xFFC09110         /* DMA23 Inner Loop Address Increment */
#define DMA23_Y_COUNT                  0xFFC09114         /* DMA23 Outer Loop Count Start Value (2D only) */
#define DMA23_Y_MODIFY                  0xFFC09118         /* DMA23 Outer Loop Address Increment (2D only) */
#define DMA23_CURR_DESC_PTR            0xFFC09124         /* DMA23 Current Descriptor Pointer */
#define DMA23_PREV_DESC_PTR            0xFFC09128         /* DMA23 Previous Initial Descriptor Pointer */
#define DMA23_CURR_ADDR              0xFFC0912C         /* DMA23 Current Address */
#define DMA23_IRQ_STATUS                  0xFFC09130         /* DMA23 Status Register */
#define DMA23_CURR_X_COUNT              0xFFC09134         /* DMA23 Current Count(1D) or intra-row XCNT (2D) */
#define DMA23_CURR_Y_COUNT              0xFFC09138         /* DMA23 Current Row Count (2D only) */
#define DMA23_BWL_COUNT                0xFFC09140         /* DMA23 Bandwidth Limit Count */
#define DMA23_CURR_BWL_COUNT            0xFFC09144         /* DMA23 Bandwidth Limit Count Current */
#define DMA23_BWM_COUNT                0xFFC09148         /* DMA23 Bandwidth Monitor Count */
#define DMA23_CURR_BWM_COUNT            0xFFC0914C         /* DMA23 Bandwidth Monitor Count Current */

/* =========================
        DMA24
   ========================= */
#define DMA24_NEXT_DESC_PTR            0xFFC09180         /* DMA24 Pointer to Next Initial Descriptor */
#define DMA24_START_ADDR             0xFFC09184         /* DMA24 Start Address of Current Buffer */
#define DMA24_CONFIG                   0xFFC09188         /* DMA24 Configuration Register */
#define DMA24_X_COUNT                  0xFFC0918C         /* DMA24 Inner Loop Count Start Value */
#define DMA24_X_MODIFY                  0xFFC09190         /* DMA24 Inner Loop Address Increment */
#define DMA24_Y_COUNT                  0xFFC09194         /* DMA24 Outer Loop Count Start Value (2D only) */
#define DMA24_Y_MODIFY                  0xFFC09198         /* DMA24 Outer Loop Address Increment (2D only) */
#define DMA24_CURR_DESC_PTR            0xFFC091A4         /* DMA24 Current Descriptor Pointer */
#define DMA24_PREV_DESC_PTR            0xFFC091A8         /* DMA24 Previous Initial Descriptor Pointer */
#define DMA24_CURR_ADDR              0xFFC091AC         /* DMA24 Current Address */
#define DMA24_IRQ_STATUS                  0xFFC091B0         /* DMA24 Status Register */
#define DMA24_CURR_X_COUNT              0xFFC091B4         /* DMA24 Current Count(1D) or intra-row XCNT (2D) */
#define DMA24_CURR_Y_COUNT              0xFFC091B8         /* DMA24 Current Row Count (2D only) */
#define DMA24_BWL_COUNT                0xFFC091C0         /* DMA24 Bandwidth Limit Count */
#define DMA24_CURR_BWL_COUNT            0xFFC091C4         /* DMA24 Bandwidth Limit Count Current */
#define DMA24_BWM_COUNT                0xFFC091C8         /* DMA24 Bandwidth Monitor Count */
#define DMA24_CURR_BWM_COUNT            0xFFC091CC         /* DMA24 Bandwidth Monitor Count Current */

/* =========================
        DMA25
   ========================= */
#define DMA25_NEXT_DESC_PTR            0xFFC09200         /* DMA25 Pointer to Next Initial Descriptor */
#define DMA25_START_ADDR             0xFFC09204         /* DMA25 Start Address of Current Buffer */
#define DMA25_CONFIG                   0xFFC09208         /* DMA25 Configuration Register */
#define DMA25_X_COUNT                  0xFFC0920C         /* DMA25 Inner Loop Count Start Value */
#define DMA25_X_MODIFY                  0xFFC09210         /* DMA25 Inner Loop Address Increment */
#define DMA25_Y_COUNT                  0xFFC09214         /* DMA25 Outer Loop Count Start Value (2D only) */
#define DMA25_Y_MODIFY                  0xFFC09218         /* DMA25 Outer Loop Address Increment (2D only) */
#define DMA25_CURR_DESC_PTR            0xFFC09224         /* DMA25 Current Descriptor Pointer */
#define DMA25_PREV_DESC_PTR            0xFFC09228         /* DMA25 Previous Initial Descriptor Pointer */
#define DMA25_CURR_ADDR              0xFFC0922C         /* DMA25 Current Address */
#define DMA25_IRQ_STATUS                  0xFFC09230         /* DMA25 Status Register */
#define DMA25_CURR_X_COUNT              0xFFC09234         /* DMA25 Current Count(1D) or intra-row XCNT (2D) */
#define DMA25_CURR_Y_COUNT              0xFFC09238         /* DMA25 Current Row Count (2D only) */
#define DMA25_BWL_COUNT                0xFFC09240         /* DMA25 Bandwidth Limit Count */
#define DMA25_CURR_BWL_COUNT            0xFFC09244         /* DMA25 Bandwidth Limit Count Current */
#define DMA25_BWM_COUNT                0xFFC09248         /* DMA25 Bandwidth Monitor Count */
#define DMA25_CURR_BWM_COUNT            0xFFC0924C         /* DMA25 Bandwidth Monitor Count Current */

/* =========================
        DMA26
   ========================= */
#define DMA26_NEXT_DESC_PTR            0xFFC09280         /* DMA26 Pointer to Next Initial Descriptor */
#define DMA26_START_ADDR             0xFFC09284         /* DMA26 Start Address of Current Buffer */
#define DMA26_CONFIG                   0xFFC09288         /* DMA26 Configuration Register */
#define DMA26_X_COUNT                  0xFFC0928C         /* DMA26 Inner Loop Count Start Value */
#define DMA26_X_MODIFY                  0xFFC09290         /* DMA26 Inner Loop Address Increment */
#define DMA26_Y_COUNT                  0xFFC09294         /* DMA26 Outer Loop Count Start Value (2D only) */
#define DMA26_Y_MODIFY                  0xFFC09298         /* DMA26 Outer Loop Address Increment (2D only) */
#define DMA26_CURR_DESC_PTR            0xFFC092A4         /* DMA26 Current Descriptor Pointer */
#define DMA26_PREV_DESC_PTR            0xFFC092A8         /* DMA26 Previous Initial Descriptor Pointer */
#define DMA26_CURR_ADDR              0xFFC092AC         /* DMA26 Current Address */
#define DMA26_IRQ_STATUS                  0xFFC092B0         /* DMA26 Status Register */
#define DMA26_CURR_X_COUNT              0xFFC092B4         /* DMA26 Current Count(1D) or intra-row XCNT (2D) */
#define DMA26_CURR_Y_COUNT              0xFFC092B8         /* DMA26 Current Row Count (2D only) */
#define DMA26_BWL_COUNT                0xFFC092C0         /* DMA26 Bandwidth Limit Count */
#define DMA26_CURR_BWL_COUNT            0xFFC092C4         /* DMA26 Bandwidth Limit Count Current */
#define DMA26_BWM_COUNT                0xFFC092C8         /* DMA26 Bandwidth Monitor Count */
#define DMA26_CURR_BWM_COUNT            0xFFC092CC         /* DMA26 Bandwidth Monitor Count Current */

/* =========================
        DMA27
   ========================= */
#define DMA27_NEXT_DESC_PTR            0xFFC09300         /* DMA27 Pointer to Next Initial Descriptor */
#define DMA27_START_ADDR             0xFFC09304         /* DMA27 Start Address of Current Buffer */
#define DMA27_CONFIG                   0xFFC09308         /* DMA27 Configuration Register */
#define DMA27_X_COUNT                  0xFFC0930C         /* DMA27 Inner Loop Count Start Value */
#define DMA27_X_MODIFY                  0xFFC09310         /* DMA27 Inner Loop Address Increment */
#define DMA27_Y_COUNT                  0xFFC09314         /* DMA27 Outer Loop Count Start Value (2D only) */
#define DMA27_Y_MODIFY                  0xFFC09318         /* DMA27 Outer Loop Address Increment (2D only) */
#define DMA27_CURR_DESC_PTR            0xFFC09324         /* DMA27 Current Descriptor Pointer */
#define DMA27_PREV_DESC_PTR            0xFFC09328         /* DMA27 Previous Initial Descriptor Pointer */
#define DMA27_CURR_ADDR              0xFFC0932C         /* DMA27 Current Address */
#define DMA27_IRQ_STATUS                  0xFFC09330         /* DMA27 Status Register */
#define DMA27_CURR_X_COUNT              0xFFC09334         /* DMA27 Current Count(1D) or intra-row XCNT (2D) */
#define DMA27_CURR_Y_COUNT              0xFFC09338         /* DMA27 Current Row Count (2D only) */
#define DMA27_BWL_COUNT                0xFFC09340         /* DMA27 Bandwidth Limit Count */
#define DMA27_CURR_BWL_COUNT            0xFFC09344         /* DMA27 Bandwidth Limit Count Current */
#define DMA27_BWM_COUNT                0xFFC09348         /* DMA27 Bandwidth Monitor Count */
#define DMA27_CURR_BWM_COUNT            0xFFC0934C         /* DMA27 Bandwidth Monitor Count Current */

/* =========================
        DMA28
   ========================= */
#define DMA28_NEXT_DESC_PTR            0xFFC09380         /* DMA28 Pointer to Next Initial Descriptor */
#define DMA28_START_ADDR             0xFFC09384         /* DMA28 Start Address of Current Buffer */
#define DMA28_CONFIG                   0xFFC09388         /* DMA28 Configuration Register */
#define DMA28_X_COUNT                  0xFFC0938C         /* DMA28 Inner Loop Count Start Value */
#define DMA28_X_MODIFY                  0xFFC09390         /* DMA28 Inner Loop Address Increment */
#define DMA28_Y_COUNT                  0xFFC09394         /* DMA28 Outer Loop Count Start Value (2D only) */
#define DMA28_Y_MODIFY                  0xFFC09398         /* DMA28 Outer Loop Address Increment (2D only) */
#define DMA28_CURR_DESC_PTR            0xFFC093A4         /* DMA28 Current Descriptor Pointer */
#define DMA28_PREV_DESC_PTR            0xFFC093A8         /* DMA28 Previous Initial Descriptor Pointer */
#define DMA28_CURR_ADDR              0xFFC093AC         /* DMA28 Current Address */
#define DMA28_IRQ_STATUS                  0xFFC093B0         /* DMA28 Status Register */
#define DMA28_CURR_X_COUNT              0xFFC093B4         /* DMA28 Current Count(1D) or intra-row XCNT (2D) */
#define DMA28_CURR_Y_COUNT              0xFFC093B8         /* DMA28 Current Row Count (2D only) */
#define DMA28_BWL_COUNT                0xFFC093C0         /* DMA28 Bandwidth Limit Count */
#define DMA28_CURR_BWL_COUNT            0xFFC093C4         /* DMA28 Bandwidth Limit Count Current */
#define DMA28_BWM_COUNT                0xFFC093C8         /* DMA28 Bandwidth Monitor Count */
#define DMA28_CURR_BWM_COUNT            0xFFC093CC         /* DMA28 Bandwidth Monitor Count Current */

/* =========================
        DMA29
   ========================= */
#define DMA29_NEXT_DESC_PTR            0xFFC0B000         /* DMA29 Pointer to Next Initial Descriptor */
#define DMA29_START_ADDR             0xFFC0B004         /* DMA29 Start Address of Current Buffer */
#define DMA29_CONFIG                   0xFFC0B008         /* DMA29 Configuration Register */
#define DMA29_X_COUNT                  0xFFC0B00C         /* DMA29 Inner Loop Count Start Value */
#define DMA29_X_MODIFY                  0xFFC0B010         /* DMA29 Inner Loop Address Increment */
#define DMA29_Y_COUNT                  0xFFC0B014         /* DMA29 Outer Loop Count Start Value (2D only) */
#define DMA29_Y_MODIFY                  0xFFC0B018         /* DMA29 Outer Loop Address Increment (2D only) */
#define DMA29_CURR_DESC_PTR            0xFFC0B024         /* DMA29 Current Descriptor Pointer */
#define DMA29_PREV_DESC_PTR            0xFFC0B028         /* DMA29 Previous Initial Descriptor Pointer */
#define DMA29_CURR_ADDR              0xFFC0B02C         /* DMA29 Current Address */
#define DMA29_IRQ_STATUS                  0xFFC0B030         /* DMA29 Status Register */
#define DMA29_CURR_X_COUNT              0xFFC0B034         /* DMA29 Current Count(1D) or intra-row XCNT (2D) */
#define DMA29_CURR_Y_COUNT              0xFFC0B038         /* DMA29 Current Row Count (2D only) */
#define DMA29_BWL_COUNT                0xFFC0B040         /* DMA29 Bandwidth Limit Count */
#define DMA29_CURR_BWL_COUNT            0xFFC0B044         /* DMA29 Bandwidth Limit Count Current */
#define DMA29_BWM_COUNT                0xFFC0B048         /* DMA29 Bandwidth Monitor Count */
#define DMA29_CURR_BWM_COUNT            0xFFC0B04C         /* DMA29 Bandwidth Monitor Count Current */

/* =========================
        DMA30
   ========================= */
#define DMA30_NEXT_DESC_PTR            0xFFC0B080         /* DMA30 Pointer to Next Initial Descriptor */
#define DMA30_START_ADDR             0xFFC0B084         /* DMA30 Start Address of Current Buffer */
#define DMA30_CONFIG                   0xFFC0B088         /* DMA30 Configuration Register */
#define DMA30_X_COUNT                  0xFFC0B08C         /* DMA30 Inner Loop Count Start Value */
#define DMA30_X_MODIFY                  0xFFC0B090         /* DMA30 Inner Loop Address Increment */
#define DMA30_Y_COUNT                  0xFFC0B094         /* DMA30 Outer Loop Count Start Value (2D only) */
#define DMA30_Y_MODIFY                  0xFFC0B098         /* DMA30 Outer Loop Address Increment (2D only) */
#define DMA30_CURR_DESC_PTR            0xFFC0B0A4         /* DMA30 Current Descriptor Pointer */
#define DMA30_PREV_DESC_PTR            0xFFC0B0A8         /* DMA30 Previous Initial Descriptor Pointer */
#define DMA30_CURR_ADDR              0xFFC0B0AC         /* DMA30 Current Address */
#define DMA30_IRQ_STATUS                  0xFFC0B0B0         /* DMA30 Status Register */
#define DMA30_CURR_X_COUNT              0xFFC0B0B4         /* DMA30 Current Count(1D) or intra-row XCNT (2D) */
#define DMA30_CURR_Y_COUNT              0xFFC0B0B8         /* DMA30 Current Row Count (2D only) */
#define DMA30_BWL_COUNT                0xFFC0B0C0         /* DMA30 Bandwidth Limit Count */
#define DMA30_CURR_BWL_COUNT            0xFFC0B0C4         /* DMA30 Bandwidth Limit Count Current */
#define DMA30_BWM_COUNT                0xFFC0B0C8         /* DMA30 Bandwidth Monitor Count */
#define DMA30_CURR_BWM_COUNT            0xFFC0B0CC         /* DMA30 Bandwidth Monitor Count Current */

/* =========================
        DMA31
   ========================= */
#define DMA31_NEXT_DESC_PTR            0xFFC0B100         /* DMA31 Pointer to Next Initial Descriptor */
#define DMA31_START_ADDR             0xFFC0B104         /* DMA31 Start Address of Current Buffer */
#define DMA31_CONFIG                   0xFFC0B108         /* DMA31 Configuration Register */
#define DMA31_X_COUNT                  0xFFC0B10C         /* DMA31 Inner Loop Count Start Value */
#define DMA31_X_MODIFY                  0xFFC0B110         /* DMA31 Inner Loop Address Increment */
#define DMA31_Y_COUNT                  0xFFC0B114         /* DMA31 Outer Loop Count Start Value (2D only) */
#define DMA31_Y_MODIFY                  0xFFC0B118         /* DMA31 Outer Loop Address Increment (2D only) */
#define DMA31_CURR_DESC_PTR            0xFFC0B124         /* DMA31 Current Descriptor Pointer */
#define DMA31_PREV_DESC_PTR            0xFFC0B128         /* DMA31 Previous Initial Descriptor Pointer */
#define DMA31_CURR_ADDR              0xFFC0B12C         /* DMA31 Current Address */
#define DMA31_IRQ_STATUS                  0xFFC0B130         /* DMA31 Status Register */
#define DMA31_CURR_X_COUNT              0xFFC0B134         /* DMA31 Current Count(1D) or intra-row XCNT (2D) */
#define DMA31_CURR_Y_COUNT              0xFFC0B138         /* DMA31 Current Row Count (2D only) */
#define DMA31_BWL_COUNT                0xFFC0B140         /* DMA31 Bandwidth Limit Count */
#define DMA31_CURR_BWL_COUNT            0xFFC0B144         /* DMA31 Bandwidth Limit Count Current */
#define DMA31_BWM_COUNT                0xFFC0B148         /* DMA31 Bandwidth Monitor Count */
#define DMA31_CURR_BWM_COUNT            0xFFC0B14C         /* DMA31 Bandwidth Monitor Count Current */

/* =========================
        DMA32
   ========================= */
#define DMA32_NEXT_DESC_PTR            0xFFC0B180         /* DMA32 Pointer to Next Initial Descriptor */
#define DMA32_START_ADDR             0xFFC0B184         /* DMA32 Start Address of Current Buffer */
#define DMA32_CONFIG                   0xFFC0B188         /* DMA32 Configuration Register */
#define DMA32_X_COUNT                  0xFFC0B18C         /* DMA32 Inner Loop Count Start Value */
#define DMA32_X_MODIFY                  0xFFC0B190         /* DMA32 Inner Loop Address Increment */
#define DMA32_Y_COUNT                  0xFFC0B194         /* DMA32 Outer Loop Count Start Value (2D only) */
#define DMA32_Y_MODIFY                  0xFFC0B198         /* DMA32 Outer Loop Address Increment (2D only) */
#define DMA32_CURR_DESC_PTR            0xFFC0B1A4         /* DMA32 Current Descriptor Pointer */
#define DMA32_PREV_DESC_PTR            0xFFC0B1A8         /* DMA32 Previous Initial Descriptor Pointer */
#define DMA32_CURR_ADDR              0xFFC0B1AC         /* DMA32 Current Address */
#define DMA32_IRQ_STATUS                  0xFFC0B1B0         /* DMA32 Status Register */
#define DMA32_CURR_X_COUNT              0xFFC0B1B4         /* DMA32 Current Count(1D) or intra-row XCNT (2D) */
#define DMA32_CURR_Y_COUNT              0xFFC0B1B8         /* DMA32 Current Row Count (2D only) */
#define DMA32_BWL_COUNT                0xFFC0B1C0         /* DMA32 Bandwidth Limit Count */
#define DMA32_CURR_BWL_COUNT            0xFFC0B1C4         /* DMA32 Bandwidth Limit Count Current */
#define DMA32_BWM_COUNT                0xFFC0B1C8         /* DMA32 Bandwidth Monitor Count */
#define DMA32_CURR_BWM_COUNT            0xFFC0B1CC         /* DMA32 Bandwidth Monitor Count Current */

/* =========================
        DMA33
   ========================= */
#define DMA33_NEXT_DESC_PTR            0xFFC0D000         /* DMA33 Pointer to Next Initial Descriptor */
#define DMA33_START_ADDR             0xFFC0D004         /* DMA33 Start Address of Current Buffer */
#define DMA33_CONFIG                   0xFFC0D008         /* DMA33 Configuration Register */
#define DMA33_X_COUNT                  0xFFC0D00C         /* DMA33 Inner Loop Count Start Value */
#define DMA33_X_MODIFY                  0xFFC0D010         /* DMA33 Inner Loop Address Increment */
#define DMA33_Y_COUNT                  0xFFC0D014         /* DMA33 Outer Loop Count Start Value (2D only) */
#define DMA33_Y_MODIFY                  0xFFC0D018         /* DMA33 Outer Loop Address Increment (2D only) */
#define DMA33_CURR_DESC_PTR            0xFFC0D024         /* DMA33 Current Descriptor Pointer */
#define DMA33_PREV_DESC_PTR            0xFFC0D028         /* DMA33 Previous Initial Descriptor Pointer */
#define DMA33_CURR_ADDR              0xFFC0D02C         /* DMA33 Current Address */
#define DMA33_IRQ_STATUS                  0xFFC0D030         /* DMA33 Status Register */
#define DMA33_CURR_X_COUNT              0xFFC0D034         /* DMA33 Current Count(1D) or intra-row XCNT (2D) */
#define DMA33_CURR_Y_COUNT              0xFFC0D038         /* DMA33 Current Row Count (2D only) */
#define DMA33_BWL_COUNT                0xFFC0D040         /* DMA33 Bandwidth Limit Count */
#define DMA33_CURR_BWL_COUNT            0xFFC0D044         /* DMA33 Bandwidth Limit Count Current */
#define DMA33_BWM_COUNT                0xFFC0D048         /* DMA33 Bandwidth Monitor Count */
#define DMA33_CURR_BWM_COUNT            0xFFC0D04C         /* DMA33 Bandwidth Monitor Count Current */

/* =========================
        DMA34
   ========================= */
#define DMA34_NEXT_DESC_PTR            0xFFC0D080         /* DMA34 Pointer to Next Initial Descriptor */
#define DMA34_START_ADDR             0xFFC0D084         /* DMA34 Start Address of Current Buffer */
#define DMA34_CONFIG                   0xFFC0D088         /* DMA34 Configuration Register */
#define DMA34_X_COUNT                  0xFFC0D08C         /* DMA34 Inner Loop Count Start Value */
#define DMA34_X_MODIFY                  0xFFC0D090         /* DMA34 Inner Loop Address Increment */
#define DMA34_Y_COUNT                  0xFFC0D094         /* DMA34 Outer Loop Count Start Value (2D only) */
#define DMA34_Y_MODIFY                  0xFFC0D098         /* DMA34 Outer Loop Address Increment (2D only) */
#define DMA34_CURR_DESC_PTR            0xFFC0D0A4         /* DMA34 Current Descriptor Pointer */
#define DMA34_PREV_DESC_PTR            0xFFC0D0A8         /* DMA34 Previous Initial Descriptor Pointer */
#define DMA34_CURR_ADDR              0xFFC0D0AC         /* DMA34 Current Address */
#define DMA34_IRQ_STATUS                  0xFFC0D0B0         /* DMA34 Status Register */
#define DMA34_CURR_X_COUNT              0xFFC0D0B4         /* DMA34 Current Count(1D) or intra-row XCNT (2D) */
#define DMA34_CURR_Y_COUNT              0xFFC0D0B8         /* DMA34 Current Row Count (2D only) */
#define DMA34_BWL_COUNT                0xFFC0D0C0         /* DMA34 Bandwidth Limit Count */
#define DMA34_CURR_BWL_COUNT            0xFFC0D0C4         /* DMA34 Bandwidth Limit Count Current */
#define DMA34_BWM_COUNT                0xFFC0D0C8         /* DMA34 Bandwidth Monitor Count */
#define DMA34_CURR_BWM_COUNT            0xFFC0D0CC         /* DMA34 Bandwidth Monitor Count Current */

/* =========================
        DMA35
   ========================= */
#define DMA35_NEXT_DESC_PTR            0xFFC10000         /* DMA35 Pointer to Next Initial Descriptor */
#define DMA35_START_ADDR             0xFFC10004         /* DMA35 Start Address of Current Buffer */
#define DMA35_CONFIG                   0xFFC10008         /* DMA35 Configuration Register */
#define DMA35_X_COUNT                  0xFFC1000C         /* DMA35 Inner Loop Count Start Value */
#define DMA35_X_MODIFY                  0xFFC10010         /* DMA35 Inner Loop Address Increment */
#define DMA35_Y_COUNT                  0xFFC10014         /* DMA35 Outer Loop Count Start Value (2D only) */
#define DMA35_Y_MODIFY                  0xFFC10018         /* DMA35 Outer Loop Address Increment (2D only) */
#define DMA35_CURR_DESC_PTR            0xFFC10024         /* DMA35 Current Descriptor Pointer */
#define DMA35_PREV_DESC_PTR            0xFFC10028         /* DMA35 Previous Initial Descriptor Pointer */
#define DMA35_CURR_ADDR              0xFFC1002C         /* DMA35 Current Address */
#define DMA35_IRQ_STATUS                  0xFFC10030         /* DMA35 Status Register */
#define DMA35_CURR_X_COUNT              0xFFC10034         /* DMA35 Current Count(1D) or intra-row XCNT (2D) */
#define DMA35_CURR_Y_COUNT              0xFFC10038         /* DMA35 Current Row Count (2D only) */
#define DMA35_BWL_COUNT                0xFFC10040         /* DMA35 Bandwidth Limit Count */
#define DMA35_CURR_BWL_COUNT            0xFFC10044         /* DMA35 Bandwidth Limit Count Current */
#define DMA35_BWM_COUNT                0xFFC10048         /* DMA35 Bandwidth Monitor Count */
#define DMA35_CURR_BWM_COUNT            0xFFC1004C         /* DMA35 Bandwidth Monitor Count Current */

/* =========================
        DMA36
   ========================= */
#define DMA36_NEXT_DESC_PTR            0xFFC10080         /* DMA36 Pointer to Next Initial Descriptor */
#define DMA36_START_ADDR             0xFFC10084         /* DMA36 Start Address of Current Buffer */
#define DMA36_CONFIG                   0xFFC10088         /* DMA36 Configuration Register */
#define DMA36_X_COUNT                  0xFFC1008C         /* DMA36 Inner Loop Count Start Value */
#define DMA36_X_MODIFY                  0xFFC10090         /* DMA36 Inner Loop Address Increment */
#define DMA36_Y_COUNT                  0xFFC10094         /* DMA36 Outer Loop Count Start Value (2D only) */
#define DMA36_Y_MODIFY                  0xFFC10098         /* DMA36 Outer Loop Address Increment (2D only) */
#define DMA36_CURR_DESC_PTR            0xFFC100A4         /* DMA36 Current Descriptor Pointer */
#define DMA36_PREV_DESC_PTR            0xFFC100A8         /* DMA36 Previous Initial Descriptor Pointer */
#define DMA36_CURR_ADDR              0xFFC100AC         /* DMA36 Current Address */
#define DMA36_IRQ_STATUS                  0xFFC100B0         /* DMA36 Status Register */
#define DMA36_CURR_X_COUNT              0xFFC100B4         /* DMA36 Current Count(1D) or intra-row XCNT (2D) */
#define DMA36_CURR_Y_COUNT              0xFFC100B8         /* DMA36 Current Row Count (2D only) */
#define DMA36_BWL_COUNT                0xFFC100C0         /* DMA36 Bandwidth Limit Count */
#define DMA36_CURR_BWL_COUNT            0xFFC100C4         /* DMA36 Bandwidth Limit Count Current */
#define DMA36_BWM_COUNT                0xFFC100C8         /* DMA36 Bandwidth Monitor Count */
#define DMA36_CURR_BWM_COUNT            0xFFC100CC         /* DMA36 Bandwidth Monitor Count Current */

/* =========================
        DMA37
   ========================= */
#define DMA37_NEXT_DESC_PTR            0xFFC10100         /* DMA37 Pointer to Next Initial Descriptor */
#define DMA37_START_ADDR             0xFFC10104         /* DMA37 Start Address of Current Buffer */
#define DMA37_CONFIG                   0xFFC10108         /* DMA37 Configuration Register */
#define DMA37_X_COUNT                  0xFFC1010C         /* DMA37 Inner Loop Count Start Value */
#define DMA37_X_MODIFY                  0xFFC10110         /* DMA37 Inner Loop Address Increment */
#define DMA37_Y_COUNT                  0xFFC10114         /* DMA37 Outer Loop Count Start Value (2D only) */
#define DMA37_Y_MODIFY                  0xFFC10118         /* DMA37 Outer Loop Address Increment (2D only) */
#define DMA37_CURR_DESC_PTR            0xFFC10124         /* DMA37 Current Descriptor Pointer */
#define DMA37_PREV_DESC_PTR            0xFFC10128         /* DMA37 Previous Initial Descriptor Pointer */
#define DMA37_CURR_ADDR              0xFFC1012C         /* DMA37 Current Address */
#define DMA37_IRQ_STATUS                  0xFFC10130         /* DMA37 Status Register */
#define DMA37_CURR_X_COUNT              0xFFC10134         /* DMA37 Current Count(1D) or intra-row XCNT (2D) */
#define DMA37_CURR_Y_COUNT              0xFFC10138         /* DMA37 Current Row Count (2D only) */
#define DMA37_BWL_COUNT                0xFFC10140         /* DMA37 Bandwidth Limit Count */
#define DMA37_CURR_BWL_COUNT            0xFFC10144         /* DMA37 Bandwidth Limit Count Current */
#define DMA37_BWM_COUNT                0xFFC10148         /* DMA37 Bandwidth Monitor Count */
#define DMA37_CURR_BWM_COUNT            0xFFC1014C         /* DMA37 Bandwidth Monitor Count Current */

/* =========================
        DMA38
   ========================= */
#define DMA38_NEXT_DESC_PTR            0xFFC12000         /* DMA38 Pointer to Next Initial Descriptor */
#define DMA38_START_ADDR             0xFFC12004         /* DMA38 Start Address of Current Buffer */
#define DMA38_CONFIG                   0xFFC12008         /* DMA38 Configuration Register */
#define DMA38_X_COUNT                  0xFFC1200C         /* DMA38 Inner Loop Count Start Value */
#define DMA38_X_MODIFY                  0xFFC12010         /* DMA38 Inner Loop Address Increment */
#define DMA38_Y_COUNT                  0xFFC12014         /* DMA38 Outer Loop Count Start Value (2D only) */
#define DMA38_Y_MODIFY                  0xFFC12018         /* DMA38 Outer Loop Address Increment (2D only) */
#define DMA38_CURR_DESC_PTR            0xFFC12024         /* DMA38 Current Descriptor Pointer */
#define DMA38_PREV_DESC_PTR            0xFFC12028         /* DMA38 Previous Initial Descriptor Pointer */
#define DMA38_CURR_ADDR              0xFFC1202C         /* DMA38 Current Address */
#define DMA38_IRQ_STATUS                  0xFFC12030         /* DMA38 Status Register */
#define DMA38_CURR_X_COUNT              0xFFC12034         /* DMA38 Current Count(1D) or intra-row XCNT (2D) */
#define DMA38_CURR_Y_COUNT              0xFFC12038         /* DMA38 Current Row Count (2D only) */
#define DMA38_BWL_COUNT                0xFFC12040         /* DMA38 Bandwidth Limit Count */
#define DMA38_CURR_BWL_COUNT            0xFFC12044         /* DMA38 Bandwidth Limit Count Current */
#define DMA38_BWM_COUNT                0xFFC12048         /* DMA38 Bandwidth Monitor Count */
#define DMA38_CURR_BWM_COUNT            0xFFC1204C         /* DMA38 Bandwidth Monitor Count Current */

/* =========================
        DMA39
   ========================= */
#define DMA39_NEXT_DESC_PTR            0xFFC12080         /* DMA39 Pointer to Next Initial Descriptor */
#define DMA39_START_ADDR             0xFFC12084         /* DMA39 Start Address of Current Buffer */
#define DMA39_CONFIG                   0xFFC12088         /* DMA39 Configuration Register */
#define DMA39_X_COUNT                  0xFFC1208C         /* DMA39 Inner Loop Count Start Value */
#define DMA39_X_MODIFY                  0xFFC12090         /* DMA39 Inner Loop Address Increment */
#define DMA39_Y_COUNT                  0xFFC12094         /* DMA39 Outer Loop Count Start Value (2D only) */
#define DMA39_Y_MODIFY                  0xFFC12098         /* DMA39 Outer Loop Address Increment (2D only) */
#define DMA39_CURR_DESC_PTR            0xFFC120A4         /* DMA39 Current Descriptor Pointer */
#define DMA39_PREV_DESC_PTR            0xFFC120A8         /* DMA39 Previous Initial Descriptor Pointer */
#define DMA39_CURR_ADDR              0xFFC120AC         /* DMA39 Current Address */
#define DMA39_IRQ_STATUS                  0xFFC120B0         /* DMA39 Status Register */
#define DMA39_CURR_X_COUNT              0xFFC120B4         /* DMA39 Current Count(1D) or intra-row XCNT (2D) */
#define DMA39_CURR_Y_COUNT              0xFFC120B8         /* DMA39 Current Row Count (2D only) */
#define DMA39_BWL_COUNT                0xFFC120C0         /* DMA39 Bandwidth Limit Count */
#define DMA39_CURR_BWL_COUNT            0xFFC120C4         /* DMA39 Bandwidth Limit Count Current */
#define DMA39_BWM_COUNT                0xFFC120C8         /* DMA39 Bandwidth Monitor Count */
#define DMA39_CURR_BWM_COUNT            0xFFC120CC         /* DMA39 Bandwidth Monitor Count Current */

/* =========================
        DMA40
   ========================= */
#define DMA40_NEXT_DESC_PTR            0xFFC12100         /* DMA40 Pointer to Next Initial Descriptor */
#define DMA40_START_ADDR             0xFFC12104         /* DMA40 Start Address of Current Buffer */
#define DMA40_CONFIG                   0xFFC12108         /* DMA40 Configuration Register */
#define DMA40_X_COUNT                  0xFFC1210C         /* DMA40 Inner Loop Count Start Value */
#define DMA40_X_MODIFY                  0xFFC12110         /* DMA40 Inner Loop Address Increment */
#define DMA40_Y_COUNT                  0xFFC12114         /* DMA40 Outer Loop Count Start Value (2D only) */
#define DMA40_Y_MODIFY                  0xFFC12118         /* DMA40 Outer Loop Address Increment (2D only) */
#define DMA40_CURR_DESC_PTR            0xFFC12124         /* DMA40 Current Descriptor Pointer */
#define DMA40_PREV_DESC_PTR            0xFFC12128         /* DMA40 Previous Initial Descriptor Pointer */
#define DMA40_CURR_ADDR              0xFFC1212C         /* DMA40 Current Address */
#define DMA40_IRQ_STATUS                  0xFFC12130         /* DMA40 Status Register */
#define DMA40_CURR_X_COUNT              0xFFC12134         /* DMA40 Current Count(1D) or intra-row XCNT (2D) */
#define DMA40_CURR_Y_COUNT              0xFFC12138         /* DMA40 Current Row Count (2D only) */
#define DMA40_BWL_COUNT                0xFFC12140         /* DMA40 Bandwidth Limit Count */
#define DMA40_CURR_BWL_COUNT            0xFFC12144         /* DMA40 Bandwidth Limit Count Current */
#define DMA40_BWM_COUNT                0xFFC12148         /* DMA40 Bandwidth Monitor Count */
#define DMA40_CURR_BWM_COUNT            0xFFC1214C         /* DMA40 Bandwidth Monitor Count Current */

/* =========================
        DMA41
   ========================= */
#define DMA41_NEXT_DESC_PTR            0xFFC12180         /* DMA41 Pointer to Next Initial Descriptor */
#define DMA41_START_ADDR             0xFFC12184         /* DMA41 Start Address of Current Buffer */
#define DMA41_CONFIG                   0xFFC12188         /* DMA41 Configuration Register */
#define DMA41_X_COUNT                  0xFFC1218C         /* DMA41 Inner Loop Count Start Value */
#define DMA41_X_MODIFY                  0xFFC12190         /* DMA41 Inner Loop Address Increment */
#define DMA41_Y_COUNT                  0xFFC12194         /* DMA41 Outer Loop Count Start Value (2D only) */
#define DMA41_Y_MODIFY                  0xFFC12198         /* DMA41 Outer Loop Address Increment (2D only) */
#define DMA41_CURR_DESC_PTR            0xFFC121A4         /* DMA41 Current Descriptor Pointer */
#define DMA41_PREV_DESC_PTR            0xFFC121A8         /* DMA41 Previous Initial Descriptor Pointer */
#define DMA41_CURR_ADDR              0xFFC121AC         /* DMA41 Current Address */
#define DMA41_IRQ_STATUS                  0xFFC121B0         /* DMA41 Status Register */
#define DMA41_CURR_X_COUNT              0xFFC121B4         /* DMA41 Current Count(1D) or intra-row XCNT (2D) */
#define DMA41_CURR_Y_COUNT              0xFFC121B8         /* DMA41 Current Row Count (2D only) */
#define DMA41_BWL_COUNT                0xFFC121C0         /* DMA41 Bandwidth Limit Count */
#define DMA41_CURR_BWL_COUNT            0xFFC121C4         /* DMA41 Bandwidth Limit Count Current */
#define DMA41_BWM_COUNT                0xFFC121C8         /* DMA41 Bandwidth Monitor Count */
#define DMA41_CURR_BWM_COUNT            0xFFC121CC         /* DMA41 Bandwidth Monitor Count Current */

/* =========================
        DMA42
   ========================= */
#define DMA42_NEXT_DESC_PTR            0xFFC14000         /* DMA42 Pointer to Next Initial Descriptor */
#define DMA42_START_ADDR             0xFFC14004         /* DMA42 Start Address of Current Buffer */
#define DMA42_CONFIG                   0xFFC14008         /* DMA42 Configuration Register */
#define DMA42_X_COUNT                  0xFFC1400C         /* DMA42 Inner Loop Count Start Value */
#define DMA42_X_MODIFY                  0xFFC14010         /* DMA42 Inner Loop Address Increment */
#define DMA42_Y_COUNT                  0xFFC14014         /* DMA42 Outer Loop Count Start Value (2D only) */
#define DMA42_Y_MODIFY                  0xFFC14018         /* DMA42 Outer Loop Address Increment (2D only) */
#define DMA42_CURR_DESC_PTR            0xFFC14024         /* DMA42 Current Descriptor Pointer */
#define DMA42_PREV_DESC_PTR            0xFFC14028         /* DMA42 Previous Initial Descriptor Pointer */
#define DMA42_CURR_ADDR              0xFFC1402C         /* DMA42 Current Address */
#define DMA42_IRQ_STATUS                  0xFFC14030         /* DMA42 Status Register */
#define DMA42_CURR_X_COUNT              0xFFC14034         /* DMA42 Current Count(1D) or intra-row XCNT (2D) */
#define DMA42_CURR_Y_COUNT              0xFFC14038         /* DMA42 Current Row Count (2D only) */
#define DMA42_BWL_COUNT                0xFFC14040         /* DMA42 Bandwidth Limit Count */
#define DMA42_CURR_BWL_COUNT            0xFFC14044         /* DMA42 Bandwidth Limit Count Current */
#define DMA42_BWM_COUNT                0xFFC14048         /* DMA42 Bandwidth Monitor Count */
#define DMA42_CURR_BWM_COUNT            0xFFC1404C         /* DMA42 Bandwidth Monitor Count Current */

/* =========================
        DMA43
   ========================= */
#define DMA43_NEXT_DESC_PTR            0xFFC14080         /* DMA43 Pointer to Next Initial Descriptor */
#define DMA43_START_ADDR             0xFFC14084         /* DMA43 Start Address of Current Buffer */
#define DMA43_CONFIG                   0xFFC14088         /* DMA43 Configuration Register */
#define DMA43_X_COUNT                  0xFFC1408C         /* DMA43 Inner Loop Count Start Value */
#define DMA43_X_MODIFY                  0xFFC14090         /* DMA43 Inner Loop Address Increment */
#define DMA43_Y_COUNT                  0xFFC14094         /* DMA43 Outer Loop Count Start Value (2D only) */
#define DMA43_Y_MODIFY                  0xFFC14098         /* DMA43 Outer Loop Address Increment (2D only) */
#define DMA43_CURR_DESC_PTR            0xFFC140A4         /* DMA43 Current Descriptor Pointer */
#define DMA43_PREV_DESC_PTR            0xFFC140A8         /* DMA43 Previous Initial Descriptor Pointer */
#define DMA43_CURR_ADDR              0xFFC140AC         /* DMA43 Current Address */
#define DMA43_IRQ_STATUS                  0xFFC140B0         /* DMA43 Status Register */
#define DMA43_CURR_X_COUNT              0xFFC140B4         /* DMA43 Current Count(1D) or intra-row XCNT (2D) */
#define DMA43_CURR_Y_COUNT              0xFFC140B8         /* DMA43 Current Row Count (2D only) */
#define DMA43_BWL_COUNT                0xFFC140C0         /* DMA43 Bandwidth Limit Count */
#define DMA43_CURR_BWL_COUNT            0xFFC140C4         /* DMA43 Bandwidth Limit Count Current */
#define DMA43_BWM_COUNT                0xFFC140C8         /* DMA43 Bandwidth Monitor Count */
#define DMA43_CURR_BWM_COUNT            0xFFC140CC         /* DMA43 Bandwidth Monitor Count Current */

/* =========================
        DMA44
   ========================= */
#define DMA44_NEXT_DESC_PTR            0xFFC14100         /* DMA44 Pointer to Next Initial Descriptor */
#define DMA44_START_ADDR             0xFFC14104         /* DMA44 Start Address of Current Buffer */
#define DMA44_CONFIG                   0xFFC14108         /* DMA44 Configuration Register */
#define DMA44_X_COUNT                  0xFFC1410C         /* DMA44 Inner Loop Count Start Value */
#define DMA44_X_MODIFY                  0xFFC14110         /* DMA44 Inner Loop Address Increment */
#define DMA44_Y_COUNT                  0xFFC14114         /* DMA44 Outer Loop Count Start Value (2D only) */
#define DMA44_Y_MODIFY                  0xFFC14118         /* DMA44 Outer Loop Address Increment (2D only) */
#define DMA44_CURR_DESC_PTR            0xFFC14124         /* DMA44 Current Descriptor Pointer */
#define DMA44_PREV_DESC_PTR            0xFFC14128         /* DMA44 Previous Initial Descriptor Pointer */
#define DMA44_CURR_ADDR              0xFFC1412C         /* DMA44 Current Address */
#define DMA44_IRQ_STATUS                  0xFFC14130         /* DMA44 Status Register */
#define DMA44_CURR_X_COUNT              0xFFC14134         /* DMA44 Current Count(1D) or intra-row XCNT (2D) */
#define DMA44_CURR_Y_COUNT              0xFFC14138         /* DMA44 Current Row Count (2D only) */
#define DMA44_BWL_COUNT                0xFFC14140         /* DMA44 Bandwidth Limit Count */
#define DMA44_CURR_BWL_COUNT            0xFFC14144         /* DMA44 Bandwidth Limit Count Current */
#define DMA44_BWM_COUNT                0xFFC14148         /* DMA44 Bandwidth Monitor Count */
#define DMA44_CURR_BWM_COUNT            0xFFC1414C         /* DMA44 Bandwidth Monitor Count Current */

/* =========================
        DMA45
   ========================= */
#define DMA45_NEXT_DESC_PTR            0xFFC14180         /* DMA45 Pointer to Next Initial Descriptor */
#define DMA45_START_ADDR             0xFFC14184         /* DMA45 Start Address of Current Buffer */
#define DMA45_CONFIG                   0xFFC14188         /* DMA45 Configuration Register */
#define DMA45_X_COUNT                  0xFFC1418C         /* DMA45 Inner Loop Count Start Value */
#define DMA45_X_MODIFY                  0xFFC14190         /* DMA45 Inner Loop Address Increment */
#define DMA45_Y_COUNT                  0xFFC14194         /* DMA45 Outer Loop Count Start Value (2D only) */
#define DMA45_Y_MODIFY                  0xFFC14198         /* DMA45 Outer Loop Address Increment (2D only) */
#define DMA45_CURR_DESC_PTR            0xFFC141A4         /* DMA45 Current Descriptor Pointer */
#define DMA45_PREV_DESC_PTR            0xFFC141A8         /* DMA45 Previous Initial Descriptor Pointer */
#define DMA45_CURR_ADDR              0xFFC141AC         /* DMA45 Current Address */
#define DMA45_IRQ_STATUS                  0xFFC141B0         /* DMA45 Status Register */
#define DMA45_CURR_X_COUNT              0xFFC141B4         /* DMA45 Current Count(1D) or intra-row XCNT (2D) */
#define DMA45_CURR_Y_COUNT              0xFFC141B8         /* DMA45 Current Row Count (2D only) */
#define DMA45_BWL_COUNT                0xFFC141C0         /* DMA45 Bandwidth Limit Count */
#define DMA45_CURR_BWL_COUNT            0xFFC141C4         /* DMA45 Bandwidth Limit Count Current */
#define DMA45_BWM_COUNT                0xFFC141C8         /* DMA45 Bandwidth Monitor Count */
#define DMA45_CURR_BWM_COUNT            0xFFC141CC         /* DMA45 Bandwidth Monitor Count Current */

/* =========================
        DMA46
   ========================= */
#define DMA46_NEXT_DESC_PTR            0xFFC14200         /* DMA46 Pointer to Next Initial Descriptor */
#define DMA46_START_ADDR             0xFFC14204         /* DMA46 Start Address of Current Buffer */
#define DMA46_CONFIG                   0xFFC14208         /* DMA46 Configuration Register */
#define DMA46_X_COUNT                  0xFFC1420C         /* DMA46 Inner Loop Count Start Value */
#define DMA46_X_MODIFY                  0xFFC14210         /* DMA46 Inner Loop Address Increment */
#define DMA46_Y_COUNT                  0xFFC14214         /* DMA46 Outer Loop Count Start Value (2D only) */
#define DMA46_Y_MODIFY                  0xFFC14218         /* DMA46 Outer Loop Address Increment (2D only) */
#define DMA46_CURR_DESC_PTR            0xFFC14224         /* DMA46 Current Descriptor Pointer */
#define DMA46_PREV_DESC_PTR            0xFFC14228         /* DMA46 Previous Initial Descriptor Pointer */
#define DMA46_CURR_ADDR              0xFFC1422C         /* DMA46 Current Address */
#define DMA46_IRQ_STATUS                  0xFFC14230         /* DMA46 Status Register */
#define DMA46_CURR_X_COUNT              0xFFC14234         /* DMA46 Current Count(1D) or intra-row XCNT (2D) */
#define DMA46_CURR_Y_COUNT              0xFFC14238         /* DMA46 Current Row Count (2D only) */
#define DMA46_BWL_COUNT                0xFFC14240         /* DMA46 Bandwidth Limit Count */
#define DMA46_CURR_BWL_COUNT            0xFFC14244         /* DMA46 Bandwidth Limit Count Current */
#define DMA46_BWM_COUNT                0xFFC14248         /* DMA46 Bandwidth Monitor Count */
#define DMA46_CURR_BWM_COUNT            0xFFC1424C         /* DMA46 Bandwidth Monitor Count Current */


/********************************************************************************
    DMA Alias Definitions
 ********************************************************************************/
#define MDMA0_DEST_CRC0_NEXT_DESC_PTR   (DMA22_NEXT_DESC_PTR)
#define MDMA0_DEST_CRC0_START_ADDR    (DMA22_START_ADDR)
#define MDMA0_DEST_CRC0_CONFIG          (DMA22_CONFIG)
#define MDMA0_DEST_CRC0_X_COUNT         (DMA22_X_COUNT)
#define MDMA0_DEST_CRC0_X_MODIFY         (DMA22_X_MODIFY)
#define MDMA0_DEST_CRC0_Y_COUNT         (DMA22_Y_COUNT)
#define MDMA0_DEST_CRC0_Y_MODIFY         (DMA22_Y_MODIFY)
#define MDMA0_DEST_CRC0_CURR_DESC_PTR   (DMA22_CURR_DESC_PTR)
#define MDMA0_DEST_CRC0_PREV_DESC_PTR   (DMA22_PREV_DESC_PTR)
#define MDMA0_DEST_CRC0_CURR_ADDR     (DMA22_CURR_ADDR)
#define MDMA0_DEST_CRC0_IRQ_STATUS         (DMA22_IRQ_STATUS)
#define MDMA0_DEST_CRC0_CURR_X_COUNT     (DMA22_CURR_X_COUNT)
#define MDMA0_DEST_CRC0_CURR_Y_COUNT     (DMA22_CURR_Y_COUNT)
#define MDMA0_DEST_CRC0_BWL_COUNT       (DMA22_BWL_COUNT)
#define MDMA0_DEST_CRC0_CURR_BWL_COUNT   (DMA22_CURR_BWL_COUNT)
#define MDMA0_DEST_CRC0_BWM_COUNT       (DMA22_BWM_COUNT)
#define MDMA0_DEST_CRC0_CURR_BWM_COUNT   (DMA22_CURR_BWM_COUNT)
#define MDMA0_SRC_CRC0_NEXT_DESC_PTR    (DMA21_NEXT_DESC_PTR)
#define MDMA0_SRC_CRC0_START_ADDR     (DMA21_START_ADDR)
#define MDMA0_SRC_CRC0_CONFIG           (DMA21_CONFIG)
#define MDMA0_SRC_CRC0_X_COUNT          (DMA21_X_COUNT)
#define MDMA0_SRC_CRC0_X_MODIFY          (DMA21_X_MODIFY)
#define MDMA0_SRC_CRC0_Y_COUNT          (DMA21_Y_COUNT)
#define MDMA0_SRC_CRC0_Y_MODIFY          (DMA21_Y_MODIFY)
#define MDMA0_SRC_CRC0_CURR_DESC_PTR    (DMA21_CURR_DESC_PTR)
#define MDMA0_SRC_CRC0_PREV_DESC_PTR    (DMA21_PREV_DESC_PTR)
#define MDMA0_SRC_CRC0_CURR_ADDR      (DMA21_CURR_ADDR)
#define MDMA0_SRC_CRC0_IRQ_STATUS          (DMA21_IRQ_STATUS)
#define MDMA0_SRC_CRC0_CURR_X_COUNT      (DMA21_CURR_X_COUNT)
#define MDMA0_SRC_CRC0_CURR_Y_COUNT      (DMA21_CURR_Y_COUNT)
#define MDMA0_SRC_CRC0_BWL_COUNT        (DMA21_BWL_COUNT)
#define MDMA0_SRC_CRC0_CURR_BWL_COUNT    (DMA21_CURR_BWL_COUNT)
#define MDMA0_SRC_CRC0_BWM_COUNT        (DMA21_BWM_COUNT)
#define MDMA0_SRC_CRC0_CURR_BWM_COUNT    (DMA21_CURR_BWM_COUNT)
#define MDMA1_DEST_CRC1_NEXT_DESC_PTR   (DMA24_NEXT_DESC_PTR)
#define MDMA1_DEST_CRC1_START_ADDR    (DMA24_START_ADDR)
#define MDMA1_DEST_CRC1_CONFIG          (DMA24_CONFIG)
#define MDMA1_DEST_CRC1_X_COUNT         (DMA24_X_COUNT)
#define MDMA1_DEST_CRC1_X_MODIFY         (DMA24_X_MODIFY)
#define MDMA1_DEST_CRC1_Y_COUNT         (DMA24_Y_COUNT)
#define MDMA1_DEST_CRC1_Y_MODIFY         (DMA24_Y_MODIFY)
#define MDMA1_DEST_CRC1_CURR_DESC_PTR   (DMA24_CURR_DESC_PTR)
#define MDMA1_DEST_CRC1_PREV_DESC_PTR   (DMA24_PREV_DESC_PTR)
#define MDMA1_DEST_CRC1_CURR_ADDR     (DMA24_CURR_ADDR)
#define MDMA1_DEST_CRC1_IRQ_STATUS         (DMA24_IRQ_STATUS)
#define MDMA1_DEST_CRC1_CURR_X_COUNT     (DMA24_CURR_X_COUNT)
#define MDMA1_DEST_CRC1_CURR_Y_COUNT     (DMA24_CURR_Y_COUNT)
#define MDMA1_DEST_CRC1_BWL_COUNT       (DMA24_BWL_COUNT)
#define MDMA1_DEST_CRC1_CURR_BWL_COUNT   (DMA24_CURR_BWL_COUNT)
#define MDMA1_DEST_CRC1_BWM_COUNT       (DMA24_BWM_COUNT)
#define MDMA1_DEST_CRC1_CURR_BWM_COUNT   (DMA24_CURR_BWM_COUNT)
#define MDMA1_SRC_CRC1_NEXT_DESC_PTR    (DMA23_NEXT_DESC_PTR)
#define MDMA1_SRC_CRC1_START_ADDR     (DMA23_START_ADDR)
#define MDMA1_SRC_CRC1_CONFIG           (DMA23_CONFIG)
#define MDMA1_SRC_CRC1_X_COUNT          (DMA23_X_COUNT)
#define MDMA1_SRC_CRC1_X_MODIFY          (DMA23_X_MODIFY)
#define MDMA1_SRC_CRC1_Y_COUNT          (DMA23_Y_COUNT)
#define MDMA1_SRC_CRC1_Y_MODIFY          (DMA23_Y_MODIFY)
#define MDMA1_SRC_CRC1_CURR_DESC_PTR    (DMA23_CURR_DESC_PTR)
#define MDMA1_SRC_CRC1_PREV_DESC_PTR    (DMA23_PREV_DESC_PTR)
#define MDMA1_SRC_CRC1_CURR_ADDR      (DMA23_CURR_ADDR)
#define MDMA1_SRC_CRC1_IRQ_STATUS          (DMA23_IRQ_STATUS)
#define MDMA1_SRC_CRC1_CURR_X_COUNT      (DMA23_CURR_X_COUNT)
#define MDMA1_SRC_CRC1_CURR_Y_COUNT      (DMA23_CURR_Y_COUNT)
#define MDMA1_SRC_CRC1_BWL_COUNT        (DMA23_BWL_COUNT)
#define MDMA1_SRC_CRC1_CURR_BWL_COUNT    (DMA23_CURR_BWL_COUNT)
#define MDMA1_SRC_CRC1_BWM_COUNT        (DMA23_BWM_COUNT)
#define MDMA1_SRC_CRC1_CURR_BWM_COUNT    (DMA23_CURR_BWM_COUNT)
#define MDMA2_DEST_NEXT_DESC_PTR            (DMA26_NEXT_DESC_PTR)
#define MDMA2_DEST_START_ADDR             (DMA26_START_ADDR)
#define MDMA2_DEST_CONFIG                   (DMA26_CONFIG)
#define MDMA2_DEST_X_COUNT                  (DMA26_X_COUNT)
#define MDMA2_DEST_X_MODIFY                  (DMA26_X_MODIFY)
#define MDMA2_DEST_Y_COUNT                  (DMA26_Y_COUNT)
#define MDMA2_DEST_Y_MODIFY                  (DMA26_Y_MODIFY)
#define MDMA2_DEST_CURR_DESC_PTR            (DMA26_CURR_DESC_PTR)
#define MDMA2_DEST_PREV_DESC_PTR            (DMA26_PREV_DESC_PTR)
#define MDMA2_DEST_CURR_ADDR              (DMA26_CURR_ADDR)
#define MDMA2_DEST_IRQ_STATUS                  (DMA26_IRQ_STATUS)
#define MDMA2_DEST_CURR_X_COUNT              (DMA26_CURR_X_COUNT)
#define MDMA2_DEST_CURR_Y_COUNT              (DMA26_CURR_Y_COUNT)
#define MDMA2_DEST_BWL_COUNT                (DMA26_BWL_COUNT)
#define MDMA2_DEST_CURR_BWL_COUNT            (DMA26_CURR_BWL_COUNT)
#define MDMA2_DEST_BWM_COUNT                (DMA26_BWM_COUNT)
#define MDMA2_DEST_CURR_BWM_COUNT            (DMA26_CURR_BWM_COUNT)
#define MDMA2_SRC_NEXT_DESC_PTR            (DMA25_NEXT_DESC_PTR)
#define MDMA2_SRC_START_ADDR             (DMA25_START_ADDR)
#define MDMA2_SRC_CONFIG                   (DMA25_CONFIG)
#define MDMA2_SRC_X_COUNT                  (DMA25_X_COUNT)
#define MDMA2_SRC_X_MODIFY                  (DMA25_X_MODIFY)
#define MDMA2_SRC_Y_COUNT                  (DMA25_Y_COUNT)
#define MDMA2_SRC_Y_MODIFY                  (DMA25_Y_MODIFY)
#define MDMA2_SRC_CURR_DESC_PTR            (DMA25_CURR_DESC_PTR)
#define MDMA2_SRC_PREV_DESC_PTR            (DMA25_PREV_DESC_PTR)
#define MDMA2_SRC_CURR_ADDR              (DMA25_CURR_ADDR)
#define MDMA2_SRC_IRQ_STATUS                  (DMA25_IRQ_STATUS)
#define MDMA2_SRC_CURR_X_COUNT              (DMA25_CURR_X_COUNT)
#define MDMA2_SRC_CURR_Y_COUNT              (DMA25_CURR_Y_COUNT)
#define MDMA2_SRC_BWL_COUNT                (DMA25_BWL_COUNT)
#define MDMA2_SRC_CURR_BWL_COUNT            (DMA25_CURR_BWL_COUNT)
#define MDMA2_SRC_BWM_COUNT                (DMA25_BWM_COUNT)
#define MDMA2_SRC_CURR_BWM_COUNT            (DMA25_CURR_BWM_COUNT)
#define MDMA3_DEST_NEXT_DESC_PTR            (DMA28_NEXT_DESC_PTR)
#define MDMA3_DEST_START_ADDR             (DMA28_START_ADDR)
#define MDMA3_DEST_CONFIG                   (DMA28_CONFIG)
#define MDMA3_DEST_X_COUNT                  (DMA28_X_COUNT)
#define MDMA3_DEST_X_MODIFY                  (DMA28_X_MODIFY)
#define MDMA3_DEST_Y_COUNT                  (DMA28_Y_COUNT)
#define MDMA3_DEST_Y_MODIFY                  (DMA28_Y_MODIFY)
#define MDMA3_DEST_CURR_DESC_PTR            (DMA28_CURR_DESC_PTR)
#define MDMA3_DEST_PREV_DESC_PTR            (DMA28_PREV_DESC_PTR)
#define MDMA3_DEST_CURR_ADDR              (DMA28_CURR_ADDR)
#define MDMA3_DEST_IRQ_STATUS                  (DMA28_IRQ_STATUS)
#define MDMA3_DEST_CURR_X_COUNT              (DMA28_CURR_X_COUNT)
#define MDMA3_DEST_CURR_Y_COUNT              (DMA28_CURR_Y_COUNT)
#define MDMA3_DEST_BWL_COUNT                (DMA28_BWL_COUNT)
#define MDMA3_DEST_CURR_BWL_COUNT            (DMA28_CURR_BWL_COUNT)
#define MDMA3_DEST_BWM_COUNT                (DMA28_BWM_COUNT)
#define MDMA3_DEST_CURR_BWM_COUNT            (DMA28_CURR_BWM_COUNT)
#define MDMA3_SRC_NEXT_DESC_PTR            (DMA27_NEXT_DESC_PTR)
#define MDMA3_SRC_START_ADDR             (DMA27_START_ADDR)
#define MDMA3_SRC_CONFIG                   (DMA27_CONFIG)
#define MDMA3_SRC_X_COUNT                  (DMA27_X_COUNT)
#define MDMA3_SRC_X_MODIFY                  (DMA27_X_MODIFY)
#define MDMA3_SRC_Y_COUNT                  (DMA27_Y_COUNT)
#define MDMA3_SRC_Y_MODIFY                  (DMA27_Y_MODIFY)
#define MDMA3_SRC_CURR_DESC_PTR            (DMA27_CURR_DESC_PTR)
#define MDMA3_SRC_PREV_DESC_PTR            (DMA27_PREV_DESC_PTR)
#define MDMA3_SRC_CURR_ADDR              (DMA27_CURR_ADDR)
#define MDMA3_SRC_IRQ_STATUS                  (DMA27_IRQ_STATUS)
#define MDMA3_SRC_CURR_X_COUNT              (DMA27_CURR_X_COUNT)
#define MDMA3_SRC_CURR_Y_COUNT              (DMA27_CURR_Y_COUNT)
#define MDMA3_SRC_BWL_COUNT                (DMA27_BWL_COUNT)
#define MDMA3_SRC_CURR_BWL_COUNT            (DMA27_CURR_BWL_COUNT)
#define MDMA3_SRC_BWM_COUNT                (DMA27_BWM_COUNT)
#define MDMA3_SRC_CURR_BWM_COUNT            (DMA27_CURR_BWM_COUNT)


/* =========================
        DMC Registers
   ========================= */

/* =========================
        DMC0
   ========================= */
#define DMC0_ID                     0xFFC80000         /* DMC0 Identification Register */
#define DMC0_CTL                    0xFFC80004         /* DMC0 Control Register */
#define DMC0_STAT                   0xFFC80008         /* DMC0 Status Register */
#define DMC0_EFFCTL                 0xFFC8000C         /* DMC0 Efficiency Controller */
#define DMC0_PRIO                   0xFFC80010         /* DMC0 Priority ID Register */
#define DMC0_PRIOMSK                0xFFC80014         /* DMC0 Priority ID Mask */
#define DMC0_CFG                    0xFFC80040         /* DMC0 SDRAM Configuration */
#define DMC0_TR0                    0xFFC80044         /* DMC0 Timing Register 0 */
#define DMC0_TR1                    0xFFC80048         /* DMC0 Timing Register 1 */
#define DMC0_TR2                    0xFFC8004C         /* DMC0 Timing Register 2 */
#define DMC0_MSK                    0xFFC8005C         /* DMC0 Mode Register Mask */
#define DMC0_MR                     0xFFC80060         /* DMC0 Mode Shadow register */
#define DMC0_EMR1                   0xFFC80064         /* DMC0 EMR1 Shadow Register */
#define DMC0_EMR2                   0xFFC80068         /* DMC0 EMR2 Shadow Register */
#define DMC0_EMR3                   0xFFC8006C         /* DMC0 EMR3 Shadow Register */
#define DMC0_DLLCTL                 0xFFC80080         /* DMC0 DLL Control Register */
#define DMC0_PADCTL                 0xFFC800C0         /* DMC0 PAD Control Register 0 */

#define DEVSZ_64                0x000         /* DMC External Bank Size = 64Mbit */
#define DEVSZ_128               0x100         /* DMC External Bank Size = 128Mbit */
#define DEVSZ_256               0x200         /* DMC External Bank Size = 256Mbit */
#define DEVSZ_512               0x300         /* DMC External Bank Size = 512Mbit */
#define DEVSZ_1G                0x400         /* DMC External Bank Size = 1Gbit */
#define DEVSZ_2G                0x500         /* DMC External Bank Size = 2Gbit */

/* =========================
        L2CTL Registers
   ========================= */

/* =========================
        L2CTL0
   ========================= */
#define L2CTL0_CTL                  0xFFCA3000         /* L2CTL0 L2 Control Register */
#define L2CTL0_ACTL_C0              0xFFCA3004         /* L2CTL0 L2 Core 0 Access Control Register */
#define L2CTL0_ACTL_C1              0xFFCA3008         /* L2CTL0 L2 Core 1 Access Control Register */
#define L2CTL0_ACTL_SYS             0xFFCA300C         /* L2CTL0 L2 System Access Control Register */
#define L2CTL0_STAT                 0xFFCA3010         /* L2CTL0 L2 Status Register */
#define L2CTL0_RPCR                 0xFFCA3014         /* L2CTL0 L2 Read Priority Count Register */
#define L2CTL0_WPCR                 0xFFCA3018         /* L2CTL0 L2 Write Priority Count Register */
#define L2CTL0_RFA                  0xFFCA3024         /* L2CTL0 L2 Refresh Address Register */
#define L2CTL0_ERRADDR0             0xFFCA3040         /* L2CTL0 L2 Bank 0 ECC Error Address Register */
#define L2CTL0_ERRADDR1             0xFFCA3044         /* L2CTL0 L2 Bank 1 ECC Error Address Register */
#define L2CTL0_ERRADDR2             0xFFCA3048         /* L2CTL0 L2 Bank 2 ECC Error Address Register */
#define L2CTL0_ERRADDR3             0xFFCA304C         /* L2CTL0 L2 Bank 3 ECC Error Address Register */
#define L2CTL0_ERRADDR4             0xFFCA3050         /* L2CTL0 L2 Bank 4 ECC Error Address Register */
#define L2CTL0_ERRADDR5             0xFFCA3054         /* L2CTL0 L2 Bank 5 ECC Error Address Register */
#define L2CTL0_ERRADDR6             0xFFCA3058         /* L2CTL0 L2 Bank 6 ECC Error Address Register */
#define L2CTL0_ERRADDR7             0xFFCA305C         /* L2CTL0 L2 Bank 7 ECC Error Address Register */
#define L2CTL0_ET0                  0xFFCA3080         /* L2CTL0 L2 AXI Error 0 Type Register */
#define L2CTL0_EADDR0               0xFFCA3084         /* L2CTL0 L2 AXI Error 0 Address Register */
#define L2CTL0_ET1                  0xFFCA3088         /* L2CTL0 L2 AXI Error 1 Type Register */
#define L2CTL0_EADDR1               0xFFCA308C         /* L2CTL0 L2 AXI Error 1 Address Register */


/* =========================
        SEC Registers
   ========================= */
/* ------------------------------------------------------------------------------------------------------------------------
       SEC Core Interface (SCI) Register Definitions
   ------------------------------------------------------------------------------------------------------------------------ */

#define SEC_SCI_BASE 0xFFCA4400
#define SEC_SCI_OFF 0x40
#define SEC_CCTL 0x0         /* SEC Core Control Register n */
#define SEC_CSTAT 0x4         /* SEC Core Status Register n */
#define SEC_CPND 0x8         /* SEC Core Pending IRQ Register n */
#define SEC_CACT 0xC         /* SEC Core Active IRQ Register n */
#define SEC_CPMSK 0x10         /* SEC Core IRQ Priority Mask Register n */
#define SEC_CGMSK 0x14         /* SEC Core IRQ Group Mask Register n */
#define SEC_CPLVL 0x18         /* SEC Core IRQ Priority Level Register n */
#define SEC_CSID 0x1C         /* SEC Core IRQ Source ID Register n */

#define bfin_read_SEC_SCI(n, reg) bfin_read32(SEC_SCI_BASE + (n) * SEC_SCI_OFF + reg)
#define bfin_write_SEC_SCI(n, reg, val) \
	bfin_write32(SEC_SCI_BASE + (n) * SEC_SCI_OFF + reg, val)

/* ------------------------------------------------------------------------------------------------------------------------
       SEC Fault Management Interface (SFI) Register Definitions
   ------------------------------------------------------------------------------------------------------------------------ */
#define SEC_FCTL                   0xFFCA4010         /* SEC Fault Control Register */
#define SEC_FSTAT                  0xFFCA4014         /* SEC Fault Status Register */
#define SEC_FSID                   0xFFCA4018         /* SEC Fault Source ID Register */
#define SEC_FEND                   0xFFCA401C         /* SEC Fault End Register */
#define SEC_FDLY                   0xFFCA4020         /* SEC Fault Delay Register */
#define SEC_FDLY_CUR               0xFFCA4024         /* SEC Fault Delay Current Register */
#define SEC_FSRDLY                 0xFFCA4028         /* SEC Fault System Reset Delay Register */
#define SEC_FSRDLY_CUR             0xFFCA402C         /* SEC Fault System Reset Delay Current Register */
#define SEC_FCOPP                  0xFFCA4030         /* SEC Fault COP Period Register */
#define SEC_FCOPP_CUR              0xFFCA4034         /* SEC Fault COP Period Current Register */

/* ------------------------------------------------------------------------------------------------------------------------
       SEC Global Register Definitions
   ------------------------------------------------------------------------------------------------------------------------ */
#define SEC_GCTL                   0xFFCA4000         /* SEC Global Control Register */
#define SEC_GSTAT                  0xFFCA4004         /* SEC Global Status Register */
#define SEC_RAISE                  0xFFCA4008         /* SEC Global Raise Register */
#define SEC_END                    0xFFCA400C         /* SEC Global End Register */

/* ------------------------------------------------------------------------------------------------------------------------
       SEC Source Interface (SSI) Register Definitions
   ------------------------------------------------------------------------------------------------------------------------ */
#define SEC_SCTL0                  0xFFCA4800         /* SEC IRQ Source Control Register n */
#define SEC_SCTL1                  0xFFCA4808         /* SEC IRQ Source Control Register n */
#define SEC_SCTL2                  0xFFCA4810         /* SEC IRQ Source Control Register n */
#define SEC_SCTL3                  0xFFCA4818         /* SEC IRQ Source Control Register n */
#define SEC_SCTL4                  0xFFCA4820         /* SEC IRQ Source Control Register n */
#define SEC_SCTL5                  0xFFCA4828         /* SEC IRQ Source Control Register n */
#define SEC_SCTL6                  0xFFCA4830         /* SEC IRQ Source Control Register n */
#define SEC_SCTL7                  0xFFCA4838         /* SEC IRQ Source Control Register n */
#define SEC_SCTL8                  0xFFCA4840         /* SEC IRQ Source Control Register n */
#define SEC_SCTL9                  0xFFCA4848         /* SEC IRQ Source Control Register n */
#define SEC_SCTL10                 0xFFCA4850         /* SEC IRQ Source Control Register n */
#define SEC_SCTL11                 0xFFCA4858         /* SEC IRQ Source Control Register n */
#define SEC_SCTL12                 0xFFCA4860         /* SEC IRQ Source Control Register n */
#define SEC_SCTL13                 0xFFCA4868         /* SEC IRQ Source Control Register n */
#define SEC_SCTL14                 0xFFCA4870         /* SEC IRQ Source Control Register n */
#define SEC_SCTL15                 0xFFCA4878         /* SEC IRQ Source Control Register n */
#define SEC_SCTL16                 0xFFCA4880         /* SEC IRQ Source Control Register n */
#define SEC_SCTL17                 0xFFCA4888         /* SEC IRQ Source Control Register n */
#define SEC_SCTL18                 0xFFCA4890         /* SEC IRQ Source Control Register n */
#define SEC_SCTL19                 0xFFCA4898         /* SEC IRQ Source Control Register n */
#define SEC_SCTL20                 0xFFCA48A0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL21                 0xFFCA48A8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL22                 0xFFCA48B0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL23                 0xFFCA48B8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL24                 0xFFCA48C0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL25                 0xFFCA48C8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL26                 0xFFCA48D0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL27                 0xFFCA48D8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL28                 0xFFCA48E0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL29                 0xFFCA48E8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL30                 0xFFCA48F0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL31                 0xFFCA48F8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL32                 0xFFCA4900         /* SEC IRQ Source Control Register n */
#define SEC_SCTL33                 0xFFCA4908         /* SEC IRQ Source Control Register n */
#define SEC_SCTL34                 0xFFCA4910         /* SEC IRQ Source Control Register n */
#define SEC_SCTL35                 0xFFCA4918         /* SEC IRQ Source Control Register n */
#define SEC_SCTL36                 0xFFCA4920         /* SEC IRQ Source Control Register n */
#define SEC_SCTL37                 0xFFCA4928         /* SEC IRQ Source Control Register n */
#define SEC_SCTL38                 0xFFCA4930         /* SEC IRQ Source Control Register n */
#define SEC_SCTL39                 0xFFCA4938         /* SEC IRQ Source Control Register n */
#define SEC_SCTL40                 0xFFCA4940         /* SEC IRQ Source Control Register n */
#define SEC_SCTL41                 0xFFCA4948         /* SEC IRQ Source Control Register n */
#define SEC_SCTL42                 0xFFCA4950         /* SEC IRQ Source Control Register n */
#define SEC_SCTL43                 0xFFCA4958         /* SEC IRQ Source Control Register n */
#define SEC_SCTL44                 0xFFCA4960         /* SEC IRQ Source Control Register n */
#define SEC_SCTL45                 0xFFCA4968         /* SEC IRQ Source Control Register n */
#define SEC_SCTL46                 0xFFCA4970         /* SEC IRQ Source Control Register n */
#define SEC_SCTL47                 0xFFCA4978         /* SEC IRQ Source Control Register n */
#define SEC_SCTL48                 0xFFCA4980         /* SEC IRQ Source Control Register n */
#define SEC_SCTL49                 0xFFCA4988         /* SEC IRQ Source Control Register n */
#define SEC_SCTL50                 0xFFCA4990         /* SEC IRQ Source Control Register n */
#define SEC_SCTL51                 0xFFCA4998         /* SEC IRQ Source Control Register n */
#define SEC_SCTL52                 0xFFCA49A0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL53                 0xFFCA49A8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL54                 0xFFCA49B0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL55                 0xFFCA49B8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL56                 0xFFCA49C0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL57                 0xFFCA49C8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL58                 0xFFCA49D0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL59                 0xFFCA49D8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL60                 0xFFCA49E0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL61                 0xFFCA49E8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL62                 0xFFCA49F0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL63                 0xFFCA49F8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL64                 0xFFCA4A00         /* SEC IRQ Source Control Register n */
#define SEC_SCTL65                 0xFFCA4A08         /* SEC IRQ Source Control Register n */
#define SEC_SCTL66                 0xFFCA4A10         /* SEC IRQ Source Control Register n */
#define SEC_SCTL67                 0xFFCA4A18         /* SEC IRQ Source Control Register n */
#define SEC_SCTL68                 0xFFCA4A20         /* SEC IRQ Source Control Register n */
#define SEC_SCTL69                 0xFFCA4A28         /* SEC IRQ Source Control Register n */
#define SEC_SCTL70                 0xFFCA4A30         /* SEC IRQ Source Control Register n */
#define SEC_SCTL71                 0xFFCA4A38         /* SEC IRQ Source Control Register n */
#define SEC_SCTL72                 0xFFCA4A40         /* SEC IRQ Source Control Register n */
#define SEC_SCTL73                 0xFFCA4A48         /* SEC IRQ Source Control Register n */
#define SEC_SCTL74                 0xFFCA4A50         /* SEC IRQ Source Control Register n */
#define SEC_SCTL75                 0xFFCA4A58         /* SEC IRQ Source Control Register n */
#define SEC_SCTL76                 0xFFCA4A60         /* SEC IRQ Source Control Register n */
#define SEC_SCTL77                 0xFFCA4A68         /* SEC IRQ Source Control Register n */
#define SEC_SCTL78                 0xFFCA4A70         /* SEC IRQ Source Control Register n */
#define SEC_SCTL79                 0xFFCA4A78         /* SEC IRQ Source Control Register n */
#define SEC_SCTL80                 0xFFCA4A80         /* SEC IRQ Source Control Register n */
#define SEC_SCTL81                 0xFFCA4A88         /* SEC IRQ Source Control Register n */
#define SEC_SCTL82                 0xFFCA4A90         /* SEC IRQ Source Control Register n */
#define SEC_SCTL83                 0xFFCA4A98         /* SEC IRQ Source Control Register n */
#define SEC_SCTL84                 0xFFCA4AA0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL85                 0xFFCA4AA8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL86                 0xFFCA4AB0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL87                 0xFFCA4AB8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL88                 0xFFCA4AC0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL89                 0xFFCA4AC8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL90                 0xFFCA4AD0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL91                 0xFFCA4AD8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL92                 0xFFCA4AE0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL93                 0xFFCA4AE8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL94                 0xFFCA4AF0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL95                 0xFFCA4AF8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL96                 0xFFCA4B00         /* SEC IRQ Source Control Register n */
#define SEC_SCTL97                 0xFFCA4B08         /* SEC IRQ Source Control Register n */
#define SEC_SCTL98                 0xFFCA4B10         /* SEC IRQ Source Control Register n */
#define SEC_SCTL99                 0xFFCA4B18         /* SEC IRQ Source Control Register n */
#define SEC_SCTL100                0xFFCA4B20         /* SEC IRQ Source Control Register n */
#define SEC_SCTL101                0xFFCA4B28         /* SEC IRQ Source Control Register n */
#define SEC_SCTL102                0xFFCA4B30         /* SEC IRQ Source Control Register n */
#define SEC_SCTL103                0xFFCA4B38         /* SEC IRQ Source Control Register n */
#define SEC_SCTL104                0xFFCA4B40         /* SEC IRQ Source Control Register n */
#define SEC_SCTL105                0xFFCA4B48         /* SEC IRQ Source Control Register n */
#define SEC_SCTL106                0xFFCA4B50         /* SEC IRQ Source Control Register n */
#define SEC_SCTL107                0xFFCA4B58         /* SEC IRQ Source Control Register n */
#define SEC_SCTL108                0xFFCA4B60         /* SEC IRQ Source Control Register n */
#define SEC_SCTL109                0xFFCA4B68         /* SEC IRQ Source Control Register n */
#define SEC_SCTL110                0xFFCA4B70         /* SEC IRQ Source Control Register n */
#define SEC_SCTL111                0xFFCA4B78         /* SEC IRQ Source Control Register n */
#define SEC_SCTL112                0xFFCA4B80         /* SEC IRQ Source Control Register n */
#define SEC_SCTL113                0xFFCA4B88         /* SEC IRQ Source Control Register n */
#define SEC_SCTL114                0xFFCA4B90         /* SEC IRQ Source Control Register n */
#define SEC_SCTL115                0xFFCA4B98         /* SEC IRQ Source Control Register n */
#define SEC_SCTL116                0xFFCA4BA0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL117                0xFFCA4BA8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL118                0xFFCA4BB0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL119                0xFFCA4BB8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL120                0xFFCA4BC0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL121                0xFFCA4BC8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL122                0xFFCA4BD0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL123                0xFFCA4BD8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL124                0xFFCA4BE0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL125                0xFFCA4BE8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL126                0xFFCA4BF0         /* SEC IRQ Source Control Register n */
#define SEC_SCTL127                0xFFCA4BF8         /* SEC IRQ Source Control Register n */
#define SEC_SCTL128                0xFFCA4C00         /* SEC IRQ Source Control Register n */
#define SEC_SCTL129                0xFFCA4C08         /* SEC IRQ Source Control Register n */
#define SEC_SCTL130                0xFFCA4C10         /* SEC IRQ Source Control Register n */
#define SEC_SCTL131                0xFFCA4C18         /* SEC IRQ Source Control Register n */
#define SEC_SCTL132                0xFFCA4C20         /* SEC IRQ Source Control Register n */
#define SEC_SCTL133                0xFFCA4C28         /* SEC IRQ Source Control Register n */
#define SEC_SCTL134                0xFFCA4C30         /* SEC IRQ Source Control Register n */
#define SEC_SCTL135                0xFFCA4C38         /* SEC IRQ Source Control Register n */
#define SEC_SCTL136                0xFFCA4C40         /* SEC IRQ Source Control Register n */
#define SEC_SCTL137                0xFFCA4C48         /* SEC IRQ Source Control Register n */
#define SEC_SCTL138                0xFFCA4C50         /* SEC IRQ Source Control Register n */
#define SEC_SCTL139                0xFFCA4C58         /* SEC IRQ Source Control Register n */
#define SEC_SSTAT0                 0xFFCA4804         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT1                 0xFFCA480C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT2                 0xFFCA4814         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT3                 0xFFCA481C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT4                 0xFFCA4824         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT5                 0xFFCA482C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT6                 0xFFCA4834         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT7                 0xFFCA483C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT8                 0xFFCA4844         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT9                 0xFFCA484C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT10                0xFFCA4854         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT11                0xFFCA485C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT12                0xFFCA4864         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT13                0xFFCA486C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT14                0xFFCA4874         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT15                0xFFCA487C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT16                0xFFCA4884         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT17                0xFFCA488C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT18                0xFFCA4894         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT19                0xFFCA489C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT20                0xFFCA48A4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT21                0xFFCA48AC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT22                0xFFCA48B4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT23                0xFFCA48BC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT24                0xFFCA48C4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT25                0xFFCA48CC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT26                0xFFCA48D4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT27                0xFFCA48DC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT28                0xFFCA48E4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT29                0xFFCA48EC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT30                0xFFCA48F4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT31                0xFFCA48FC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT32                0xFFCA4904         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT33                0xFFCA490C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT34                0xFFCA4914         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT35                0xFFCA491C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT36                0xFFCA4924         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT37                0xFFCA492C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT38                0xFFCA4934         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT39                0xFFCA493C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT40                0xFFCA4944         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT41                0xFFCA494C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT42                0xFFCA4954         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT43                0xFFCA495C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT44                0xFFCA4964         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT45                0xFFCA496C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT46                0xFFCA4974         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT47                0xFFCA497C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT48                0xFFCA4984         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT49                0xFFCA498C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT50                0xFFCA4994         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT51                0xFFCA499C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT52                0xFFCA49A4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT53                0xFFCA49AC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT54                0xFFCA49B4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT55                0xFFCA49BC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT56                0xFFCA49C4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT57                0xFFCA49CC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT58                0xFFCA49D4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT59                0xFFCA49DC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT60                0xFFCA49E4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT61                0xFFCA49EC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT62                0xFFCA49F4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT63                0xFFCA49FC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT64                0xFFCA4A04         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT65                0xFFCA4A0C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT66                0xFFCA4A14         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT67                0xFFCA4A1C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT68                0xFFCA4A24         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT69                0xFFCA4A2C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT70                0xFFCA4A34         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT71                0xFFCA4A3C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT72                0xFFCA4A44         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT73                0xFFCA4A4C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT74                0xFFCA4A54         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT75                0xFFCA4A5C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT76                0xFFCA4A64         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT77                0xFFCA4A6C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT78                0xFFCA4A74         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT79                0xFFCA4A7C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT80                0xFFCA4A84         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT81                0xFFCA4A8C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT82                0xFFCA4A94         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT83                0xFFCA4A9C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT84                0xFFCA4AA4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT85                0xFFCA4AAC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT86                0xFFCA4AB4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT87                0xFFCA4ABC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT88                0xFFCA4AC4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT89                0xFFCA4ACC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT90                0xFFCA4AD4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT91                0xFFCA4ADC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT92                0xFFCA4AE4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT93                0xFFCA4AEC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT94                0xFFCA4AF4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT95                0xFFCA4AFC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT96                0xFFCA4B04         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT97                0xFFCA4B0C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT98                0xFFCA4B14         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT99                0xFFCA4B1C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT100               0xFFCA4B24         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT101               0xFFCA4B2C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT102               0xFFCA4B34         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT103               0xFFCA4B3C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT104               0xFFCA4B44         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT105               0xFFCA4B4C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT106               0xFFCA4B54         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT107               0xFFCA4B5C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT108               0xFFCA4B64         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT109               0xFFCA4B6C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT110               0xFFCA4B74         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT111               0xFFCA4B7C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT112               0xFFCA4B84         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT113               0xFFCA4B8C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT114               0xFFCA4B94         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT115               0xFFCA4B9C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT116               0xFFCA4BA4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT117               0xFFCA4BAC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT118               0xFFCA4BB4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT119               0xFFCA4BBC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT120               0xFFCA4BC4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT121               0xFFCA4BCC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT122               0xFFCA4BD4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT123               0xFFCA4BDC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT124               0xFFCA4BE4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT125               0xFFCA4BEC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT126               0xFFCA4BF4         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT127               0xFFCA4BFC         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT128               0xFFCA4C04         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT129               0xFFCA4C0C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT130               0xFFCA4C14         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT131               0xFFCA4C1C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT132               0xFFCA4C24         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT133               0xFFCA4C2C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT134               0xFFCA4C34         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT135               0xFFCA4C3C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT136               0xFFCA4C44         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT137               0xFFCA4C4C         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT138               0xFFCA4C54         /* SEC IRQ Source Status Register n */
#define SEC_SSTAT139               0xFFCA4C5C         /* SEC IRQ Source Status Register n */

/* ------------------------------------------------------------------------------------------------------------------------
        SEC_CCTL                             Pos/Masks     Description
   ------------------------------------------------------------------------------------------------------------------------ */
#define SEC_CCTL_LOCK                   0x80000000    /* LOCK: Lock */
#define SEC_CCTL_NMI_EN                 0x00010000    /* NMIEN: Enable */
#define SEC_CCTL_WAITIDLE               0x00001000    /* WFI: Wait for Idle */
#define SEC_CCTL_RESET                  0x00000002    /* RESET: Reset */
#define SEC_CCTL_EN                     0x00000001    /* EN: Enable */

/* ------------------------------------------------------------------------------------------------------------------------
        SEC_CSTAT                            Pos/Masks     Description
   ------------------------------------------------------------------------------------------------------------------------ */
#define SEC_CSTAT_NMI                   0x00010000    /* NMI Status */
#define SEC_CSTAT_WAITING               0x00001000    /* WFI: Waiting */
#define SEC_CSTAT_VALID_SID             0x00000400    /* SIDV: Valid */
#define SEC_CSTAT_VALID_ACT             0x00000200    /* ACTV: Valid */
#define SEC_CSTAT_VALID_PND             0x00000100    /* PNDV: Valid */
#define SEC_CSTAT_ERRC                  0x00000030    /* Error Cause */
#define SEC_CSTAT_ACKERR                0x00000010    /* ERRC: Acknowledge Error */
#define SEC_CSTAT_ERR                   0x00000002    /* ERR: Error Occurred */

/* ------------------------------------------------------------------------------------------------------------------------
        SEC_CPND                             Pos/Masks     Description
   ------------------------------------------------------------------------------------------------------------------------ */
#define SEC_CPND_PRIO                   0x0000FF00    /* Highest Pending IRQ Priority */
#define SEC_CPND_SID                    0x000000FF    /* Highest Pending IRQ Source ID */

/* ------------------------------------------------------------------------------------------------------------------------
        SEC_CACT                             Pos/Masks     Description
   ------------------------------------------------------------------------------------------------------------------------ */
#define SEC_CACT_PRIO                   0x0000FF00    /* Highest Active IRQ Priority */
#define SEC_CACT_SID                    0x000000FF    /* Highest Active IRQ Source ID */

/* ------------------------------------------------------------------------------------------------------------------------
        SEC_CPMSK                            Pos/Masks     Description
   ------------------------------------------------------------------------------------------------------------------------ */
#define SEC_CPMSK_LOCK                  0x80000000    /* LOCK: Lock */
#define SEC_CPMSK_PRIO                  0x000000FF    /* IRQ Priority Mask */

/* ------------------------------------------------------------------------------------------------------------------------
        SEC_CGMSK                            Pos/Masks     Description
   ------------------------------------------------------------------------------------------------------------------------ */
#define SEC_CGMSK_LOCK                  0x80000000    /* LOCK: Lock */
#define SEC_CGMSK_MASK                  0x00000100    /* UGRP: Mask Ungrouped Sources */
#define SEC_CGMSK_GRP                   0x0000000F    /* Grouped Mask */

/* ------------------------------------------------------------------------------------------------------------------------
        SEC_CPLVL                            Pos/Masks     Description
   ------------------------------------------------------------------------------------------------------------------------ */
#define SEC_CPLVL_LOCK                  0x80000000    /* LOCK: Lock */
#define SEC_CPLVL_PLVL                  0x00000007    /* Priority Levels */

/* ------------------------------------------------------------------------------------------------------------------------
        SEC_CSID                             Pos/Masks     Description
   ------------------------------------------------------------------------------------------------------------------------ */
#define SEC_CSID_SID                    0x000000FF    /* Source ID */


/* ------------------------------------------------------------------------------------------------------------------------
        SEC_FCTL                             Pos/Masks     Description
   ------------------------------------------------------------------------------------------------------------------------ */
#define SEC_FCTL_LOCK                   0x80000000    /* LOCK: Lock */
#define SEC_FCTL_FLTPND_MODE            0x00002000    /* TES: Fault Pending Mode */
#define SEC_FCTL_COP_MODE               0x00001000    /* CMS: COP Mode */
#define SEC_FCTL_FLTIN_EN               0x00000080    /* FIEN: Enable */
#define SEC_FCTL_SYSRST_EN              0x00000040    /* SREN: Enable */
#define SEC_FCTL_TRGOUT_EN              0x00000020    /* TOEN: Enable */
#define SEC_FCTL_FLTOUT_EN              0x00000010    /* FOEN: Enable */
#define SEC_FCTL_RESET                  0x00000002    /* RESET: Reset */
#define SEC_FCTL_EN                     0x00000001    /* EN: Enable */

/* ------------------------------------------------------------------------------------------------------------------------
        SEC_FSTAT                            Pos/Masks     Description
   ------------------------------------------------------------------------------------------------------------------------ */
#define SEC_FSTAT_NXTFLT                0x00000400    /* NPND: Pending */
#define SEC_FSTAT_FLTACT                0x00000200    /* ACT: Active Fault */
#define SEC_FSTAT_FLTPND                0x00000100    /* PND: Pending */
#define SEC_FSTAT_ERRC                  0x00000030    /* Error Cause */
#define SEC_FSTAT_ENDERR                0x00000020    /* ERRC: End Error */
#define SEC_FSTAT_ERR                   0x00000002    /* ERR: Error Occurred */

/* ------------------------------------------------------------------------------------------------------------------------
        SEC_FSID                             Pos/Masks     Description
   ------------------------------------------------------------------------------------------------------------------------ */
#define SEC_FSID_SRC_EXTFLT             0x00010000    /* FEXT: Fault External */
#define SEC_FSID_SID                    0x000000FF    /* Source ID */

/* ------------------------------------------------------------------------------------------------------------------------
        SEC_FEND                             Pos/Masks     Description
   ------------------------------------------------------------------------------------------------------------------------ */
#define SEC_FEND_END_EXTFLT             0x00010000    /* FEXT: Fault External */
#define SEC_FEND_SID                    0x000000FF    /* Source ID */


/* ------------------------------------------------------------------------------------------------------------------------
        SEC_GCTL                             Pos/Masks     Description
   ------------------------------------------------------------------------------------------------------------------------ */
#define SEC_GCTL_LOCK                   0x80000000    /* Lock */
#define SEC_GCTL_RESET                  0x00000002    /* Reset */
#define SEC_GCTL_EN                     0x00000001    /* Enable */

/* ------------------------------------------------------------------------------------------------------------------------
        SEC_GSTAT                            Pos/Masks     Description
   ------------------------------------------------------------------------------------------------------------------------ */
#define SEC_GSTAT_LWERR                 0x80000000    /* LWERR: Error Occurred */
#define SEC_GSTAT_ADRERR                0x40000000    /* ADRERR: Error Occurred */
#define SEC_GSTAT_SID                   0x00FF0000    /* Source ID for SSI Error */
#define SEC_GSTAT_SCI                   0x00000F00    /* SCI ID for SCI Error */
#define SEC_GSTAT_ERRC                  0x00000030    /* Error Cause */
#define SEC_GSTAT_SCIERR                0x00000010    /* ERRC: SCI Error */
#define SEC_GSTAT_SSIERR                0x00000020    /* ERRC: SSI Error */
#define SEC_GSTAT_ERR                   0x00000002    /* ERR: Error Occurred */

/* ------------------------------------------------------------------------------------------------------------------------
        SEC_RAISE                            Pos/Masks     Description
   ------------------------------------------------------------------------------------------------------------------------ */
#define SEC_RAISE_SID                   0x000000FF    /* Source ID IRQ Set to Pending */

/* ------------------------------------------------------------------------------------------------------------------------
        SEC_END                              Pos/Masks     Description
   ------------------------------------------------------------------------------------------------------------------------ */
#define SEC_END_SID                     0x000000FF    /* Source ID IRQ to End */


/* ------------------------------------------------------------------------------------------------------------------------
        SEC_SCTL                             Pos/Masks     Description
   ------------------------------------------------------------------------------------------------------------------------ */
#define SEC_SCTL_LOCK                   0x80000000    /* Lock */
#define SEC_SCTL_CTG                    0x0F000000    /* Core Target Select */
#define SEC_SCTL_GRP                    0x000F0000    /* Group Select */
#define SEC_SCTL_PRIO                   0x0000FF00    /* Priority Level Select */
#define SEC_SCTL_ERR_EN                 0x00000010    /* ERREN: Enable */
#define SEC_SCTL_EDGE                   0x00000008    /* ES: Edge Sensitive */
#define SEC_SCTL_SRC_EN                 0x00000004    /* SEN: Enable */
#define SEC_SCTL_FAULT_EN               0x00000002    /* FEN: Enable */
#define SEC_SCTL_INT_EN                 0x00000001    /* IEN: Enable */

/* ------------------------------------------------------------------------------------------------------------------------
        SEC_SSTAT                            Pos/Masks     Description
   ------------------------------------------------------------------------------------------------------------------------ */
#define SEC_SSTAT_CHID                  0x00FF0000    /* Channel ID */
#define SEC_SSTAT_ACTIVE_SRC            0x00000200    /* ACT: Active Source */
#define SEC_SSTAT_PENDING               0x00000100    /* PND: Pending */
#define SEC_SSTAT_ERRC                  0x00000030    /* Error Cause */
#define SEC_SSTAT_ENDERR                0x00000020    /* ERRC: End Error */
#define SEC_SSTAT_ERR                   0x00000002    /* Error */


/* =========================
        RCU Registers
   ========================= */

/* =========================
        RCU0
   ========================= */
#define RCU0_CTL                    0xFFCA6000         /* RCU0 Control Register */
#define RCU0_STAT                   0xFFCA6004         /* RCU0 Status Register */
#define RCU0_CRCTL                  0xFFCA6008         /* RCU0 Core Reset Control Register */
#define RCU0_CRSTAT                 0xFFCA600C         /* RCU0 Core Reset Status Register */
#define RCU0_SIDIS                  0xFFCA6010         /* RCU0 System Interface Disable Register */
#define RCU0_SISTAT                 0xFFCA6014         /* RCU0 System Interface Status Register */
#define RCU0_SVECT_LCK              0xFFCA6018         /* RCU0 SVECT Lock Register */
#define RCU0_BCODE                  0xFFCA601C         /* RCU0 Boot Code Register */
#define RCU0_SVECT0                 0xFFCA6020         /* RCU0 Software Vector Register n */
#define RCU0_SVECT1                 0xFFCA6024         /* RCU0 Software Vector Register n */


/* =========================
        CGU0
   ========================= */
#define CGU0_CTL                    0xFFCA8000         /* CGU0 Control Register */
#define CGU0_STAT                   0xFFCA8004         /* CGU0 Status Register */
#define CGU0_DIV                    0xFFCA8008         /* CGU0 Divisor Register */
#define CGU0_CLKOUTSEL              0xFFCA800C         /* CGU0 CLKOUT Select Register */


/* =========================
        DPM Registers
   ========================= */

/* =========================
        DPM0
   ========================= */
#define DPM0_CTL                    0xFFCA9000         /* DPM0 Control Register */
#define DPM0_STAT                   0xFFCA9004         /* DPM0 Status Register */
#define DPM0_CCBF_DIS               0xFFCA9008         /* DPM0 Core Clock Buffer Disable Register */
#define DPM0_CCBF_EN                0xFFCA900C         /* DPM0 Core Clock Buffer Enable Register */
#define DPM0_CCBF_STAT              0xFFCA9010         /* DPM0 Core Clock Buffer Status Register */
#define DPM0_CCBF_STAT_STKY         0xFFCA9014         /* DPM0 Core Clock Buffer Status Sticky Register */
#define DPM0_SCBF_DIS               0xFFCA9018         /* DPM0 System Clock Buffer Disable Register */
#define DPM0_WAKE_EN                0xFFCA901C         /* DPM0 Wakeup Enable Register */
#define DPM0_WAKE_POL               0xFFCA9020         /* DPM0 Wakeup Polarity Register */
#define DPM0_WAKE_STAT              0xFFCA9024         /* DPM0 Wakeup Status Register */
#define DPM0_HIB_DIS                0xFFCA9028         /* DPM0 Hibernate Disable Register */
#define DPM0_PGCNTR                 0xFFCA902C         /* DPM0 Power Good Counter Register */
#define DPM0_RESTORE0               0xFFCA9030         /* DPM0 Restore Register */
#define DPM0_RESTORE1               0xFFCA9034         /* DPM0 Restore Register */
#define DPM0_RESTORE2               0xFFCA9038         /* DPM0 Restore Register */
#define DPM0_RESTORE3               0xFFCA903C         /* DPM0 Restore Register */
#define DPM0_RESTORE4               0xFFCA9040         /* DPM0 Restore Register */
#define DPM0_RESTORE5               0xFFCA9044         /* DPM0 Restore Register */
#define DPM0_RESTORE6               0xFFCA9048         /* DPM0 Restore Register */
#define DPM0_RESTORE7               0xFFCA904C         /* DPM0 Restore Register */
#define DPM0_RESTORE8               0xFFCA9050         /* DPM0 Restore Register */
#define DPM0_RESTORE9               0xFFCA9054         /* DPM0 Restore Register */
#define DPM0_RESTORE10              0xFFCA9058         /* DPM0 Restore Register */
#define DPM0_RESTORE11              0xFFCA905C         /* DPM0 Restore Register */
#define DPM0_RESTORE12              0xFFCA9060         /* DPM0 Restore Register */
#define DPM0_RESTORE13              0xFFCA9064         /* DPM0 Restore Register */
#define DPM0_RESTORE14              0xFFCA9068         /* DPM0 Restore Register */
#define DPM0_RESTORE15              0xFFCA906C         /* DPM0 Restore Register */


/* =========================
        DBG Registers
   ========================= */

/* USB register */
#define USB_FADDR                  0xFFCC1000         /* USB Device Address in Peripheral Mode */
#define USB_POWER                  0xFFCC1001         /* USB Power and Device Control */
#define USB_INTRTX                 0xFFCC1002         /* USB Transmit Interrupt */
#define USB_INTRRX                 0xFFCC1004         /* USB Receive Interrupts */
#define USB_INTRTXE                0xFFCC1006         /* USB Transmit Interrupt Enable */
#define USB_INTRRXE                0xFFCC1008         /* USB Receive Interrupt Enable */
#define USB_INTRUSB                    0xFFCC100A         /* USB USB Interrupts */
#define USB_INTRUSBE                    0xFFCC100B         /* USB USB Interrupt Enable */
#define USB_FRAME                  0xFFCC100C         /* USB Frame Number */
#define USB_INDEX                  0xFFCC100E         /* USB Index */
#define USB_TESTMODE               0xFFCC100F         /* USB Testmodes */
#define USB_EPI_TXMAXP0            0xFFCC1010         /* USB Transmit Maximum Packet Length */
#define USB_EP_NI0_TXMAXP          0xFFCC1010
#define USB_EP0I_CSR0_H            0xFFCC1012         /* USB Config and Status EP0 */
#define USB_EPI_TXCSR0_H           0xFFCC1012         /* USB Transmit Configuration and Status */
#define USB_EP0I_CSR0_P            0xFFCC1012         /* USB Config and Status EP0 */
#define USB_EPI_TXCSR0_P           0xFFCC1012         /* USB Transmit Configuration and Status */
#define USB_EPI_RXMAXP0            0xFFCC1014         /* USB Receive Maximum Packet Length */
#define USB_EPI_RXCSR0_H           0xFFCC1016         /* USB Receive Configuration and Status Register */
#define USB_EPI_RXCSR0_P           0xFFCC1016         /* USB Receive Configuration and Status Register */
#define USB_EP0I_CNT0              0xFFCC1018         /* USB Number of Received Bytes for Endpoint 0 */
#define USB_EPI_RXCNT0             0xFFCC1018         /* USB Number of Byte Received */
#define USB_EP0I_TYPE0             0xFFCC101A         /* USB Speed for Endpoint 0 */
#define USB_EPI_TXTYPE0            0xFFCC101A         /* USB Transmit Type */
#define USB_EP0I_NAKLIMIT0         0xFFCC101B         /* USB NAK Response Timeout for Endpoint 0 */
#define USB_EPI_TXINTERVAL0        0xFFCC101B         /* USB Transmit Polling Interval */
#define USB_EPI_RXTYPE0            0xFFCC101C         /* USB Receive Type */
#define USB_EPI_RXINTERVAL0        0xFFCC101D         /* USB Receive Polling Interval */
#define USB_EP0I_CFGDATA0          0xFFCC101F         /* USB Configuration Information */
#define USB_FIFOB0                 0xFFCC1020         /* USB FIFO Data */
#define USB_FIFOB1                 0xFFCC1024         /* USB FIFO Data */
#define USB_FIFOB2                 0xFFCC1028         /* USB FIFO Data */
#define USB_FIFOB3                 0xFFCC102C         /* USB FIFO Data */
#define USB_FIFOB4                 0xFFCC1030         /* USB FIFO Data */
#define USB_FIFOB5                 0xFFCC1034         /* USB FIFO Data */
#define USB_FIFOB6                 0xFFCC1038         /* USB FIFO Data */
#define USB_FIFOB7                 0xFFCC103C         /* USB FIFO Data */
#define USB_FIFOB8                 0xFFCC1040         /* USB FIFO Data */
#define USB_FIFOB9                 0xFFCC1044         /* USB FIFO Data */
#define USB_FIFOB10                0xFFCC1048         /* USB FIFO Data */
#define USB_FIFOB11                0xFFCC104C         /* USB FIFO Data */
#define USB_FIFOH0                 0xFFCC1020         /* USB FIFO Data */
#define USB_FIFOH1                 0xFFCC1024         /* USB FIFO Data */
#define USB_FIFOH2                 0xFFCC1028         /* USB FIFO Data */
#define USB_FIFOH3                 0xFFCC102C         /* USB FIFO Data */
#define USB_FIFOH4                 0xFFCC1030         /* USB FIFO Data */
#define USB_FIFOH5                 0xFFCC1034         /* USB FIFO Data */
#define USB_FIFOH6                 0xFFCC1038         /* USB FIFO Data */
#define USB_FIFOH7                 0xFFCC103C         /* USB FIFO Data */
#define USB_FIFOH8                 0xFFCC1040         /* USB FIFO Data */
#define USB_FIFOH9                 0xFFCC1044         /* USB FIFO Data */
#define USB_FIFOH10                0xFFCC1048         /* USB FIFO Data */
#define USB_FIFOH11                0xFFCC104C         /* USB FIFO Data */
#define USB_FIFO0                  0xFFCC1020         /* USB FIFO Data */
#define USB_EP0_FIFO               0xFFCC1020
#define USB_FIFO1                  0xFFCC1024         /* USB FIFO Data */
#define USB_FIFO2                  0xFFCC1028         /* USB FIFO Data */
#define USB_FIFO3                  0xFFCC102C         /* USB FIFO Data */
#define USB_FIFO4                  0xFFCC1030         /* USB FIFO Data */
#define USB_FIFO5                  0xFFCC1034         /* USB FIFO Data */
#define USB_FIFO6                  0xFFCC1038         /* USB FIFO Data */
#define USB_FIFO7                  0xFFCC103C         /* USB FIFO Data */
#define USB_FIFO8                  0xFFCC1040         /* USB FIFO Data */
#define USB_FIFO9                  0xFFCC1044         /* USB FIFO Data */
#define USB_FIFO10                 0xFFCC1048         /* USB FIFO Data */
#define USB_FIFO11                 0xFFCC104C         /* USB FIFO Data */
#define USB_OTG_DEV_CTL                0xFFCC1060         /* USB Device Control */
#define USB_TXFIFOSZ               0xFFCC1062         /* USB Transmit FIFO Size */
#define USB_RXFIFOSZ               0xFFCC1063         /* USB Receive FIFO Size */
#define USB_TXFIFOADDR             0xFFCC1064         /* USB Transmit FIFO Address */
#define USB_RXFIFOADDR             0xFFCC1066         /* USB Receive FIFO Address */
#define USB_VENDSTAT               0xFFCC1068         /* USB Vendor Status */
#define USB_HWVERS                 0xFFCC106C         /* USB Hardware Version */
#define USB_EPINFO                 0xFFCC1078         /* USB Endpoint Info */
#define USB_RAMINFO                0xFFCC1079         /* USB Ram Information */
#define USB_LINKINFO               0xFFCC107A         /* USB Programmable Delay Values */
#define USB_VPLEN                  0xFFCC107B         /* USB VBus Pulse Duration */
#define USB_HS_EOF1                0xFFCC107C         /* USB High Speed End of Frame Remaining */
#define USB_FS_EOF1                0xFFCC107D         /* USB Full Speed End of Frame Remaining */
#define USB_LS_EOF1                0xFFCC107E         /* USB Low Speed End of Frame Remaining */
#define USB_SOFT_RST               0xFFCC107F         /* USB Software Reset */
#define USB_TXFUNCADDR0            0xFFCC1080         /* USB Transmit Function Address */
#define USB_TXFUNCADDR1            0xFFCC1088         /* USB Transmit Function Address */
#define USB_TXFUNCADDR2            0xFFCC1090         /* USB Transmit Function Address */
#define USB_TXFUNCADDR3            0xFFCC1098         /* USB Transmit Function Address */
#define USB_TXFUNCADDR4            0xFFCC10A0         /* USB Transmit Function Address */
#define USB_TXFUNCADDR5            0xFFCC10A8         /* USB Transmit Function Address */
#define USB_TXFUNCADDR6            0xFFCC10B0         /* USB Transmit Function Address */
#define USB_TXFUNCADDR7            0xFFCC10B8         /* USB Transmit Function Address */
#define USB_TXFUNCADDR8            0xFFCC10C0         /* USB Transmit Function Address */
#define USB_TXFUNCADDR9            0xFFCC10C8         /* USB Transmit Function Address */
#define USB_TXFUNCADDR10           0xFFCC10D0         /* USB Transmit Function Address */
#define USB_TXFUNCADDR11           0xFFCC10D8         /* USB Transmit Function Address */
#define USB_TXHUBADDR0             0xFFCC1082         /* USB Transmit Hub Address */
#define USB_TXHUBADDR1             0xFFCC108A         /* USB Transmit Hub Address */
#define USB_TXHUBADDR2             0xFFCC1092         /* USB Transmit Hub Address */
#define USB_TXHUBADDR3             0xFFCC109A         /* USB Transmit Hub Address */
#define USB_TXHUBADDR4             0xFFCC10A2         /* USB Transmit Hub Address */
#define USB_TXHUBADDR5             0xFFCC10AA         /* USB Transmit Hub Address */
#define USB_TXHUBADDR6             0xFFCC10B2         /* USB Transmit Hub Address */
#define USB_TXHUBADDR7             0xFFCC10BA         /* USB Transmit Hub Address */
#define USB_TXHUBADDR8             0xFFCC10C2         /* USB Transmit Hub Address */
#define USB_TXHUBADDR9             0xFFCC10CA         /* USB Transmit Hub Address */
#define USB_TXHUBADDR10            0xFFCC10D2         /* USB Transmit Hub Address */
#define USB_TXHUBADDR11            0xFFCC10DA         /* USB Transmit Hub Address */
#define USB_TXHUBPORT0             0xFFCC1083         /* USB Transmit Hub Port */
#define USB_TXHUBPORT1             0xFFCC108B         /* USB Transmit Hub Port */
#define USB_TXHUBPORT2             0xFFCC1093         /* USB Transmit Hub Port */
#define USB_TXHUBPORT3             0xFFCC109B         /* USB Transmit Hub Port */
#define USB_TXHUBPORT4             0xFFCC10A3         /* USB Transmit Hub Port */
#define USB_TXHUBPORT5             0xFFCC10AB         /* USB Transmit Hub Port */
#define USB_TXHUBPORT6             0xFFCC10B3         /* USB Transmit Hub Port */
#define USB_TXHUBPORT7             0xFFCC10BB         /* USB Transmit Hub Port */
#define USB_TXHUBPORT8             0xFFCC10C3         /* USB Transmit Hub Port */
#define USB_TXHUBPORT9             0xFFCC10CB         /* USB Transmit Hub Port */
#define USB_TXHUBPORT10            0xFFCC10D3         /* USB Transmit Hub Port */
#define USB_TXHUBPORT11            0xFFCC10DB         /* USB Transmit Hub Port */
#define USB_RXFUNCADDR0            0xFFCC1084         /* USB Receive Function Address */
#define USB_RXFUNCADDR1            0xFFCC108C         /* USB Receive Function Address */
#define USB_RXFUNCADDR2            0xFFCC1094         /* USB Receive Function Address */
#define USB_RXFUNCADDR3            0xFFCC109C         /* USB Receive Function Address */
#define USB_RXFUNCADDR4            0xFFCC10A4         /* USB Receive Function Address */
#define USB_RXFUNCADDR5            0xFFCC10AC         /* USB Receive Function Address */
#define USB_RXFUNCADDR6            0xFFCC10B4         /* USB Receive Function Address */
#define USB_RXFUNCADDR7            0xFFCC10BC         /* USB Receive Function Address */
#define USB_RXFUNCADDR8            0xFFCC10C4         /* USB Receive Function Address */
#define USB_RXFUNCADDR9            0xFFCC10CC         /* USB Receive Function Address */
#define USB_RXFUNCADDR10           0xFFCC10D4         /* USB Receive Function Address */
#define USB_RXFUNCADDR11           0xFFCC10DC         /* USB Receive Function Address */
#define USB_RXHUBADDR0             0xFFCC1086         /* USB Receive Hub Address */
#define USB_RXHUBADDR1             0xFFCC108E         /* USB Receive Hub Address */
#define USB_RXHUBADDR2             0xFFCC1096         /* USB Receive Hub Address */
#define USB_RXHUBADDR3             0xFFCC109E         /* USB Receive Hub Address */
#define USB_RXHUBADDR4             0xFFCC10A6         /* USB Receive Hub Address */
#define USB_RXHUBADDR5             0xFFCC10AE         /* USB Receive Hub Address */
#define USB_RXHUBADDR6             0xFFCC10B6         /* USB Receive Hub Address */
#define USB_RXHUBADDR7             0xFFCC10BE         /* USB Receive Hub Address */
#define USB_RXHUBADDR8             0xFFCC10C6         /* USB Receive Hub Address */
#define USB_RXHUBADDR9             0xFFCC10CE         /* USB Receive Hub Address */
#define USB_RXHUBADDR10            0xFFCC10D6         /* USB Receive Hub Address */
#define USB_RXHUBADDR11            0xFFCC10DE         /* USB Receive Hub Address */
#define USB_RXHUBPORT0             0xFFCC1087         /* USB Receive Hub Port */
#define USB_RXHUBPORT1             0xFFCC108F         /* USB Receive Hub Port */
#define USB_RXHUBPORT2             0xFFCC1097         /* USB Receive Hub Port */
#define USB_RXHUBPORT3             0xFFCC109F         /* USB Receive Hub Port */
#define USB_RXHUBPORT4             0xFFCC10A7         /* USB Receive Hub Port */
#define USB_RXHUBPORT5             0xFFCC10AF         /* USB Receive Hub Port */
#define USB_RXHUBPORT6             0xFFCC10B7         /* USB Receive Hub Port */
#define USB_RXHUBPORT7             0xFFCC10BF         /* USB Receive Hub Port */
#define USB_RXHUBPORT8             0xFFCC10C7         /* USB Receive Hub Port */
#define USB_RXHUBPORT9             0xFFCC10CF         /* USB Receive Hub Port */
#define USB_RXHUBPORT10            0xFFCC10D7         /* USB Receive Hub Port */
#define USB_RXHUBPORT11            0xFFCC10DF         /* USB Receive Hub Port */
#define USB_EP0_CSR0_H             0xFFCC1102         /* USB Config and Status EP0 */
#define USB_EP0_CSR0_P             0xFFCC1102         /* USB Config and Status EP0 */
#define USB_EP0_CNT0               0xFFCC1108         /* USB Number of Received Bytes for Endpoint 0 */
#define USB_EP0_TYPE0              0xFFCC110A         /* USB Speed for Endpoint 0 */
#define USB_EP0_NAKLIMIT0          0xFFCC110B         /* USB NAK Response Timeout for Endpoint 0 */
#define USB_EP0_CFGDATA0           0xFFCC110F         /* USB Configuration Information */
#define USB_EP_TXMAXP0             0xFFCC1110         /* USB Transmit Maximum Packet Length */
#define USB_EP_TXMAXP1             0xFFCC1120         /* USB Transmit Maximum Packet Length */
#define USB_EP_TXMAXP2             0xFFCC1130         /* USB Transmit Maximum Packet Length */
#define USB_EP_TXMAXP3             0xFFCC1140         /* USB Transmit Maximum Packet Length */
#define USB_EP_TXMAXP4             0xFFCC1150         /* USB Transmit Maximum Packet Length */
#define USB_EP_TXMAXP5             0xFFCC1160         /* USB Transmit Maximum Packet Length */
#define USB_EP_TXMAXP6             0xFFCC1170         /* USB Transmit Maximum Packet Length */
#define USB_EP_TXMAXP7             0xFFCC1180         /* USB Transmit Maximum Packet Length */
#define USB_EP_TXMAXP8             0xFFCC1190         /* USB Transmit Maximum Packet Length */
#define USB_EP_TXMAXP9             0xFFCC11A0         /* USB Transmit Maximum Packet Length */
#define USB_EP_TXMAXP10            0xFFCC11B0         /* USB Transmit Maximum Packet Length */
#define USB_EP_TXCSR0_H            0xFFCC1112         /* USB Transmit Configuration and Status */
#define USB_EP_TXCSR1_H            0xFFCC1122         /* USB Transmit Configuration and Status */
#define USB_EP_TXCSR2_H            0xFFCC1132         /* USB Transmit Configuration and Status */
#define USB_EP_TXCSR3_H            0xFFCC1142         /* USB Transmit Configuration and Status */
#define USB_EP_TXCSR4_H            0xFFCC1152         /* USB Transmit Configuration and Status */
#define USB_EP_TXCSR5_H            0xFFCC1162         /* USB Transmit Configuration and Status */
#define USB_EP_TXCSR6_H            0xFFCC1172         /* USB Transmit Configuration and Status */
#define USB_EP_TXCSR7_H            0xFFCC1182         /* USB Transmit Configuration and Status */
#define USB_EP_TXCSR8_H            0xFFCC1192         /* USB Transmit Configuration and Status */
#define USB_EP_TXCSR9_H            0xFFCC11A2         /* USB Transmit Configuration and Status */
#define USB_EP_TXCSR10_H           0xFFCC11B2         /* USB Transmit Configuration and Status */
#define USB_EP_TXCSR0_P            0xFFCC1112         /* USB Transmit Configuration and Status */
#define USB_EP_TXCSR1_P            0xFFCC1122         /* USB Transmit Configuration and Status */
#define USB_EP_TXCSR2_P            0xFFCC1132         /* USB Transmit Configuration and Status */
#define USB_EP_TXCSR3_P            0xFFCC1142         /* USB Transmit Configuration and Status */
#define USB_EP_TXCSR4_P            0xFFCC1152         /* USB Transmit Configuration and Status */
#define USB_EP_TXCSR5_P            0xFFCC1162         /* USB Transmit Configuration and Status */
#define USB_EP_TXCSR6_P            0xFFCC1172         /* USB Transmit Configuration and Status */
#define USB_EP_TXCSR7_P            0xFFCC1182         /* USB Transmit Configuration and Status */
#define USB_EP_TXCSR8_P            0xFFCC1192         /* USB Transmit Configuration and Status */
#define USB_EP_TXCSR9_P            0xFFCC11A2         /* USB Transmit Configuration and Status */
#define USB_EP_TXCSR10_P           0xFFCC11B2         /* USB Transmit Configuration and Status */
#define USB_EP_RXMAXP0             0xFFCC1114         /* USB Receive Maximum Packet Length */
#define USB_EP_RXMAXP1             0xFFCC1124         /* USB Receive Maximum Packet Length */
#define USB_EP_RXMAXP2             0xFFCC1134         /* USB Receive Maximum Packet Length */
#define USB_EP_RXMAXP3             0xFFCC1144         /* USB Receive Maximum Packet Length */
#define USB_EP_RXMAXP4             0xFFCC1154         /* USB Receive Maximum Packet Length */
#define USB_EP_RXMAXP5             0xFFCC1164         /* USB Receive Maximum Packet Length */
#define USB_EP_RXMAXP6             0xFFCC1174         /* USB Receive Maximum Packet Length */
#define USB_EP_RXMAXP7             0xFFCC1184         /* USB Receive Maximum Packet Length */
#define USB_EP_RXMAXP8             0xFFCC1194         /* USB Receive Maximum Packet Length */
#define USB_EP_RXMAXP9             0xFFCC11A4         /* USB Receive Maximum Packet Length */
#define USB_EP_RXMAXP10            0xFFCC11B4         /* USB Receive Maximum Packet Length */
#define USB_EP_RXCSR0_H            0xFFCC1116         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCSR1_H            0xFFCC1126         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCSR2_H            0xFFCC1136         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCSR3_H            0xFFCC1146         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCSR4_H            0xFFCC1156         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCSR5_H            0xFFCC1166         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCSR6_H            0xFFCC1176         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCSR7_H            0xFFCC1186         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCSR8_H            0xFFCC1196         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCSR9_H            0xFFCC11A6         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCSR10_H           0xFFCC11B6         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCSR0_P            0xFFCC1116         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCSR1_P            0xFFCC1126         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCSR2_P            0xFFCC1136         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCSR3_P            0xFFCC1146         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCSR4_P            0xFFCC1156         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCSR5_P            0xFFCC1166         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCSR6_P            0xFFCC1176         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCSR7_P            0xFFCC1186         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCSR8_P            0xFFCC1196         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCSR9_P            0xFFCC11A6         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCSR10_P           0xFFCC11B6         /* USB Receive Configuration and Status Register */
#define USB_EP_RXCNT0              0xFFCC1118         /* USB Number of Byte Received */
#define USB_EP_RXCNT1              0xFFCC1128         /* USB Number of Byte Received */
#define USB_EP_RXCNT2              0xFFCC1138         /* USB Number of Byte Received */
#define USB_EP_RXCNT3              0xFFCC1148         /* USB Number of Byte Received */
#define USB_EP_RXCNT4              0xFFCC1158         /* USB Number of Byte Received */
#define USB_EP_RXCNT5              0xFFCC1168         /* USB Number of Byte Received */
#define USB_EP_RXCNT6              0xFFCC1178         /* USB Number of Byte Received */
#define USB_EP_RXCNT7              0xFFCC1188         /* USB Number of Byte Received */
#define USB_EP_RXCNT8              0xFFCC1198         /* USB Number of Byte Received */
#define USB_EP_RXCNT9              0xFFCC11A8         /* USB Number of Byte Received */
#define USB_EP_RXCNT10             0xFFCC11B8         /* USB Number of Byte Received */
#define USB_EP_TXTYPE0             0xFFCC111A         /* USB Transmit Type */
#define USB_EP_TXTYPE1             0xFFCC112A         /* USB Transmit Type */
#define USB_EP_TXTYPE2             0xFFCC113A         /* USB Transmit Type */
#define USB_EP_TXTYPE3             0xFFCC114A         /* USB Transmit Type */
#define USB_EP_TXTYPE4             0xFFCC115A         /* USB Transmit Type */
#define USB_EP_TXTYPE5             0xFFCC116A         /* USB Transmit Type */
#define USB_EP_TXTYPE6             0xFFCC117A         /* USB Transmit Type */
#define USB_EP_TXTYPE7             0xFFCC118A         /* USB Transmit Type */
#define USB_EP_TXTYPE8             0xFFCC119A         /* USB Transmit Type */
#define USB_EP_TXTYPE9             0xFFCC11AA         /* USB Transmit Type */
#define USB_EP_TXTYPE10            0xFFCC11BA         /* USB Transmit Type */
#define USB_EP_TXINTERVAL0         0xFFCC111B         /* USB Transmit Polling Interval */
#define USB_EP_TXINTERVAL1         0xFFCC112B         /* USB Transmit Polling Interval */
#define USB_EP_TXINTERVAL2         0xFFCC113B         /* USB Transmit Polling Interval */
#define USB_EP_TXINTERVAL3         0xFFCC114B         /* USB Transmit Polling Interval */
#define USB_EP_TXINTERVAL4         0xFFCC115B         /* USB Transmit Polling Interval */
#define USB_EP_TXINTERVAL5         0xFFCC116B         /* USB Transmit Polling Interval */
#define USB_EP_TXINTERVAL6         0xFFCC117B         /* USB Transmit Polling Interval */
#define USB_EP_TXINTERVAL7         0xFFCC118B         /* USB Transmit Polling Interval */
#define USB_EP_TXINTERVAL8         0xFFCC119B         /* USB Transmit Polling Interval */
#define USB_EP_TXINTERVAL9         0xFFCC11AB         /* USB Transmit Polling Interval */
#define USB_EP_TXINTERVAL10        0xFFCC11BB         /* USB Transmit Polling Interval */
#define USB_EP_RXTYPE0             0xFFCC111C         /* USB Receive Type */
#define USB_EP_RXTYPE1             0xFFCC112C         /* USB Receive Type */
#define USB_EP_RXTYPE2             0xFFCC113C         /* USB Receive Type */
#define USB_EP_RXTYPE3             0xFFCC114C         /* USB Receive Type */
#define USB_EP_RXTYPE4             0xFFCC115C         /* USB Receive Type */
#define USB_EP_RXTYPE5             0xFFCC116C         /* USB Receive Type */
#define USB_EP_RXTYPE6             0xFFCC117C         /* USB Receive Type */
#define USB_EP_RXTYPE7             0xFFCC118C         /* USB Receive Type */
#define USB_EP_RXTYPE8             0xFFCC119C         /* USB Receive Type */
#define USB_EP_RXTYPE9             0xFFCC11AC         /* USB Receive Type */
#define USB_EP_RXTYPE10            0xFFCC11BC         /* USB Receive Type */
#define USB_EP_RXINTERVAL0         0xFFCC111D         /* USB Receive Polling Interval */
#define USB_EP_RXINTERVAL1         0xFFCC112D         /* USB Receive Polling Interval */
#define USB_EP_RXINTERVAL2         0xFFCC113D         /* USB Receive Polling Interval */
#define USB_EP_RXINTERVAL3         0xFFCC114D         /* USB Receive Polling Interval */
#define USB_EP_RXINTERVAL4         0xFFCC115D         /* USB Receive Polling Interval */
#define USB_EP_RXINTERVAL5         0xFFCC116D         /* USB Receive Polling Interval */
#define USB_EP_RXINTERVAL6         0xFFCC117D         /* USB Receive Polling Interval */
#define USB_EP_RXINTERVAL7         0xFFCC118D         /* USB Receive Polling Interval */
#define USB_EP_RXINTERVAL8         0xFFCC119D         /* USB Receive Polling Interval */
#define USB_EP_RXINTERVAL9         0xFFCC11AD         /* USB Receive Polling Interval */
#define USB_EP_RXINTERVAL10        0xFFCC11BD         /* USB Receive Polling Interval */
#define USB_DMA_IRQ                0xFFCC1200         /* USB Interrupt Register */
#define USB_DMA_CTL0               0xFFCC1204         /* USB DMA Control */
#define USB_DMA_CTL1               0xFFCC1214         /* USB DMA Control */
#define USB_DMA_CTL2               0xFFCC1224         /* USB DMA Control */
#define USB_DMA_CTL3               0xFFCC1234         /* USB DMA Control */
#define USB_DMA_CTL4               0xFFCC1244         /* USB DMA Control */
#define USB_DMA_CTL5               0xFFCC1254         /* USB DMA Control */
#define USB_DMA_CTL6               0xFFCC1264         /* USB DMA Control */
#define USB_DMA_CTL7               0xFFCC1274         /* USB DMA Control */
#define USB_DMA_ADDR0              0xFFCC1208         /* USB DMA Address */
#define USB_DMA_ADDR1              0xFFCC1218         /* USB DMA Address */
#define USB_DMA_ADDR2              0xFFCC1228         /* USB DMA Address */
#define USB_DMA_ADDR3              0xFFCC1238         /* USB DMA Address */
#define USB_DMA_ADDR4              0xFFCC1248         /* USB DMA Address */
#define USB_DMA_ADDR5              0xFFCC1258         /* USB DMA Address */
#define USB_DMA_ADDR6              0xFFCC1268         /* USB DMA Address */
#define USB_DMA_ADDR7              0xFFCC1278         /* USB DMA Address */
#define USB_DMA_CNT0               0xFFCC120C         /* USB DMA Count */
#define USB_DMA_CNT1               0xFFCC121C         /* USB DMA Count */
#define USB_DMA_CNT2               0xFFCC122C         /* USB DMA Count */
#define USB_DMA_CNT3               0xFFCC123C         /* USB DMA Count */
#define USB_DMA_CNT4               0xFFCC124C         /* USB DMA Count */
#define USB_DMA_CNT5               0xFFCC125C         /* USB DMA Count */
#define USB_DMA_CNT6               0xFFCC126C         /* USB DMA Count */
#define USB_DMA_CNT7               0xFFCC127C         /* USB DMA Count */
#define USB_RQPKTCNT0              0xFFCC1300         /* USB Request Packet Count */
#define USB_RQPKTCNT1              0xFFCC1304         /* USB Request Packet Count */
#define USB_RQPKTCNT2              0xFFCC1308         /* USB Request Packet Count */
#define USB_RQPKTCNT3              0xFFCC130C         /* USB Request Packet Count */
#define USB_RQPKTCNT4              0xFFCC1310         /* USB Request Packet Count */
#define USB_RQPKTCNT5              0xFFCC1314         /* USB Request Packet Count */
#define USB_RQPKTCNT6              0xFFCC1318         /* USB Request Packet Count */
#define USB_RQPKTCNT7              0xFFCC131C         /* USB Request Packet Count */
#define USB_RQPKTCNT8              0xFFCC1320         /* USB Request Packet Count */
#define USB_RQPKTCNT9              0xFFCC1324         /* USB Request Packet Count */
#define USB_RQPKTCNT10             0xFFCC1328         /* USB Request Packet Count */
#define USB_CT_UCH                 0xFFCC1344         /* USB Chirp Timeout */
#define USB_CT_HHSRTN              0xFFCC1346         /* USB High Speed Resume Return to Normal */
#define USB_CT_HSBT                0xFFCC1348         /* USB High Speed Timeout */
#define USB_LPM_ATTR               0xFFCC1360         /* USB LPM Attribute */
#define USB_LPM_CTL                0xFFCC1362         /* USB LPM Control */
#define USB_LPM_IEN                0xFFCC1363         /* USB LPM Interrupt Enable */
#define USB_LPM_IRQ                0xFFCC1364         /* USB LPM Interrupt */
#define USB_LPM_FADDR              0xFFCC1365         /* USB LPM Function Address */
#define USB_VBUS_CTL               0xFFCC1380         /* USB VBus Control */
#define USB_BAT_CHG                0xFFCC1381         /* USB Battery Charging */
#define USB_PHY_CTL                0xFFCC1394         /* USB PHY Control */
#define USB_TESTCTL                0xFFCC1397         /* USB Test Control */
#define USB_PLL_OSC                0xFFCC1398         /* USB PLL and Oscillator Control */



/* =========================
        CHIPID
   ========================= */

#define                           CHIPID  0xffc00014
/* CHIPID Masks */
#define                   CHIPID_VERSION  0xF0000000
#define                    CHIPID_FAMILY  0x0FFFF000
#define               CHIPID_MANUFACTURE  0x00000FFE


#endif /* _DEF_BF60X_H */
