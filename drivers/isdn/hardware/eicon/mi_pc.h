
/*
 *
  Copyright (c) Eicon Networks, 2002.
 *
  This source file is supplied for the use with
  Eicon Networks range of DIVA Server Adapters.
 *
  Eicon File Revision :    2.1
 *
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.
 *
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License for more details.
 *
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
/*----------------------------------------------------------------------------
// MAESTRA ISA PnP */
#define BRI_MEMORY_BASE                 0x1f700000
#define BRI_MEMORY_SIZE                 0x00100000  /* 1MB on the BRI                         */
#define BRI_SHARED_RAM_SIZE             0x00010000  /* 64k shared RAM                         */
#define BRI_RAY_TAYLOR_DSP_CODE_SIZE    0x00020000  /* max 128k DSP-Code (Ray Taylor's code)  */
#define BRI_ORG_MAX_DSP_CODE_SIZE       0x00050000  /* max 320k DSP-Code (Telindus)           */
#define BRI_V90D_MAX_DSP_CODE_SIZE      0x00060000  /* max 384k DSP-Code if V.90D included    */
#define BRI_CACHED_ADDR(x)              (((x) & 0x1fffffffL) | 0x80000000L)
#define BRI_UNCACHED_ADDR(x)            (((x) & 0x1fffffffL) | 0xa0000000L)
#define ADDR  4
#define ADDRH 6
#define DATA  0
#define RESET 7
#define DEFAULT_ADDRESS 0x240
#define DEFAULT_IRQ     3
#define M_PCI_ADDR   0x04  /* MAESTRA BRI PCI */
#define M_PCI_ADDRH  0x0c  /* MAESTRA BRI PCI */
#define M_PCI_DATA   0x00  /* MAESTRA BRI PCI */
#define M_PCI_RESET  0x10  /* MAESTRA BRI PCI */
/*----------------------------------------------------------------------------
// MAESTRA PRI PCI */
#define MP_IRQ_RESET                    0xc18       /* offset of isr in the CONFIG memory bar */
#define MP_IRQ_RESET_VAL                0xfe        /* value to clear an interrupt            */
#define MP_MEMORY_SIZE                  0x00400000  /* 4MB on standard PRI                    */
#define MP2_MEMORY_SIZE                 0x00800000  /* 8MB on PRI Rev. 2                      */
#define MP_SHARED_RAM_OFFSET            0x00001000  /* offset of shared RAM base in the DRAM memory bar */
#define MP_SHARED_RAM_SIZE              0x00010000  /* 64k shared RAM                         */
#define MP_PROTOCOL_OFFSET              (MP_SHARED_RAM_OFFSET + MP_SHARED_RAM_SIZE)
#define MP_RAY_TAYLOR_DSP_CODE_SIZE     0x00040000  /* max 256k DSP-Code (Ray Taylor's code)  */
#define MP_ORG_MAX_DSP_CODE_SIZE        0x00060000  /* max 384k DSP-Code (Telindus)           */
#define MP_V90D_MAX_DSP_CODE_SIZE       0x00070000  /* max 448k DSP-Code if V.90D included)   */
#define MP_VOIP_MAX_DSP_CODE_SIZE       0x00090000  /* max 576k DSP-Code if voice over IP included */
#define MP_CACHED_ADDR(x)               (((x) & 0x1fffffffL) | 0x80000000L)
#define MP_UNCACHED_ADDR(x)             (((x) & 0x1fffffffL) | 0xa0000000L)
#define MP_RESET         0x20        /* offset of RESET register in the DEVICES memory bar */
/* RESET register bits */
#define _MP_S2M_RESET    0x10        /* active lo   */
#define _MP_LED2         0x08        /* 1 = on      */
#define _MP_LED1         0x04        /* 1 = on      */
#define _MP_DSP_RESET    0x02        /* active lo   */
#define _MP_RISC_RESET   0x81        /* active hi, bit 7 for compatibility with old boards */
/* CPU exception context structure in MP shared ram after trap */
typedef struct mp_xcptcontext_s MP_XCPTC;
struct mp_xcptcontext_s {
    dword       sr;
    dword       cr;
    dword       epc;
    dword       vaddr;
    dword       regs[32];
    dword       mdlo;
    dword       mdhi;
    dword       reseverd;
    dword       xclass;
};
/* boot interface structure for PRI */
struct mp_load {
  dword     volatile cmd;
  dword     volatile addr;
  dword     volatile len;
  dword     volatile err;
  dword     volatile live;
  dword     volatile res1[0x1b];
  dword     volatile TrapId;    /* has value 0x999999XX on a CPU trap */
  dword     volatile res2[0x03];
  MP_XCPTC  volatile xcpt;      /* contains register dump */
  dword     volatile rest[((0x1020>>2)-6) - 0x1b - 1 - 0x03 - (sizeof(MP_XCPTC)>>2)];
  dword     volatile signature;
  dword data[60000]; /* real interface description */
};
/*----------------------------------------------------------------------------*/
/* SERVER 4BRI (Quattro PCI)                                                  */
#define MQ_BOARD_REG_OFFSET             0x800000    /* PC relative On board registers offset  */
#define MQ_BREG_RISC                    0x1200      /* RISC Reset ect                         */
#define MQ_RISC_COLD_RESET_MASK         0x0001      /* RISC Cold reset                        */
#define MQ_RISC_WARM_RESET_MASK         0x0002      /* RISC Warm reset                        */
#define MQ_BREG_IRQ_TEST                0x0608      /* Interrupt request, no CPU interaction  */
#define MQ_IRQ_REQ_ON                   0x1
#define MQ_IRQ_REQ_OFF                  0x0
#define MQ_BOARD_DSP_OFFSET             0xa00000    /* PC relative On board DSP regs offset   */
#define MQ_DSP1_ADDR_OFFSET             0x0008      /* Addr register offset DSP 1 subboard 1  */
#define MQ_DSP2_ADDR_OFFSET             0x0208      /* Addr register offset DSP 2 subboard 1  */
#define MQ_DSP1_DATA_OFFSET             0x0000      /* Data register offset DSP 1 subboard 1  */
#define MQ_DSP2_DATA_OFFSET             0x0200      /* Data register offset DSP 2 subboard 1  */
#define MQ_DSP_JUNK_OFFSET              0x0400      /* DSP Data/Addr regs subboard offset     */
#define MQ_ISAC_DSP_RESET               0x0028      /* ISAC and DSP reset address offset      */
#define MQ_BOARD_ISAC_DSP_RESET         0x800028    /* ISAC and DSP reset address offset      */
#define MQ_INSTANCE_COUNT               4           /* 4BRI consists of four instances        */
#define MQ_MEMORY_SIZE                  0x00400000  /* 4MB on standard 4BRI                   */
#define MQ_CTRL_SIZE                    0x00002000  /* 8K memory mapped registers             */
#define MQ_SHARED_RAM_SIZE              0x00010000  /* 64k shared RAM                         */
#define MQ_ORG_MAX_DSP_CODE_SIZE        0x00050000  /* max 320k DSP-Code (Telindus) */
#define MQ_V90D_MAX_DSP_CODE_SIZE       0x00060000  /* max 384K DSP-Code if V.90D included */
#define MQ_VOIP_MAX_DSP_CODE_SIZE       0x00028000  /* max 4*160k = 640K DSP-Code if voice over IP included */
#define MQ_CACHED_ADDR(x)               (((x) & 0x1fffffffL) | 0x80000000L)
#define MQ_UNCACHED_ADDR(x)             (((x) & 0x1fffffffL) | 0xa0000000L)
/*--------------------------------------------------------------------------------------------*/
/* Additional definitions reflecting the different address map of the  SERVER 4BRI V2          */
#define MQ2_BREG_RISC                   0x0200      /* RISC Reset ect                         */
#define MQ2_BREG_IRQ_TEST               0x0400      /* Interrupt request, no CPU interaction  */
#define MQ2_BOARD_DSP_OFFSET            0x800000    /* PC relative On board DSP regs offset   */
#define MQ2_DSP1_DATA_OFFSET            0x1800      /* Data register offset DSP 1 subboard 1  */
#define MQ2_DSP1_ADDR_OFFSET            0x1808      /* Addr register offset DSP 1 subboard 1  */
#define MQ2_DSP2_DATA_OFFSET            0x1810      /* Data register offset DSP 2 subboard 1  */
#define MQ2_DSP2_ADDR_OFFSET            0x1818      /* Addr register offset DSP 2 subboard 1  */
#define MQ2_DSP_JUNK_OFFSET             0x1000      /* DSP Data/Addr regs subboard offset     */
#define MQ2_ISAC_DSP_RESET              0x0000      /* ISAC and DSP reset address offset      */
#define MQ2_BOARD_ISAC_DSP_RESET        0x800000    /* ISAC and DSP reset address offset      */
#define MQ2_IPACX_CONFIG                0x0300      /* IPACX Configuration TE(0)/NT(1)        */
#define MQ2_BOARD_IPACX_CONFIG          0x800300    /*     ""                                 */
#define MQ2_MEMORY_SIZE                 0x01000000  /* 16MB code/data memory                  */
#define MQ2_CTRL_SIZE                   0x00008000  /* 32K memory mapped registers            */
/*----------------------------------------------------------------------------*/
/* SERVER BRI 2M/2F as derived from 4BRI V2                                   */
#define BRI2_MEMORY_SIZE                0x00800000  /* 8MB code/data memory                   */
#define BRI2_PROTOCOL_MEMORY_SIZE       (MQ2_MEMORY_SIZE >> 2) /*  same as one 4BRI Rev.2 task */
#define BRI2_CTRL_SIZE                  0x00008000  /* 32K memory mapped registers            */
#define M_INSTANCE_COUNT                1           /*  BRI consists of one instance          */
/*
 * Some useful constants for proper initialization of the GT6401x
 */
#define ID_REG        0x0000      /*Pci reg-contain the Dev&Ven ID of the card*/
#define RAS0_BASEREG  0x0010      /*Ras0 register - contain the base addr Ras0*/
#define RAS2_BASEREG  0x0014
#define CS_BASEREG    0x0018
#define BOOT_BASEREG  0x001c
#define GTREGS_BASEREG 0x0024   /*GTRegsBase reg-contain the base addr where*/
                                /*the GT64010 internal regs where mapped    */
/*
 *  GT64010 internal registers
 */
        /* DRAM device coding  */
#define LOW_RAS0_DREG 0x0400    /*Ras0 low decode address*/
#define HI_RAS0_DREG  0x0404    /*Ras0 high decode address*/
#define LOW_RAS1_DREG 0x0408    /*Ras1 low decode address*/
#define HI_RAS1_DREG  0x040c    /*Ras1 high decode address*/
#define LOW_RAS2_DREG 0x0410    /*Ras2 low decode address*/
#define HI_RAS2_DREG  0x0414    /*Ras2 high decode address*/
#define LOW_RAS3_DREG 0x0418    /*Ras3 low decode address*/
#define HI_RAS3_DREG  0x041c    /*Ras3 high decode address*/
        /* I/O CS device coding  */
#define LOW_CS0_DREG  0x0420 /* CS0* low decode register */
#define HI_CS0_DREG   0x0424 /* CS0* high decode register */
#define LOW_CS1_DREG  0x0428 /* CS1* low decode register */
#define HI_CS1_DREG   0x042c /* CS1* high decode register */
#define LOW_CS2_DREG  0x0430 /* CS2* low decode register */
#define HI_CS2_DREG   0x0434 /* CS2* high decode register */
#define LOW_CS3_DREG  0x0438 /* CS3* low decode register */
#define HI_CS3_DREG   0x043c /* CS3* high decode register */
        /* Boot PROM device coding */
#define LOW_BOOTCS_DREG 0x0440 /* Boot CS low decode register */
#define HI_BOOTCS_DREG 0x0444 /* Boot CS High decode register */
        /* DRAM group coding (for CPU)  */
#define LO_RAS10_GREG 0x0008    /*Ras1..0 group low decode address*/
#define HI_RAS10_GREG 0x0010    /*Ras1..0 group high decode address*/
#define LO_RAS32_GREG 0x0018    /*Ras3..2 group low decode address  */
#define HI_RAS32_GREG 0x0020    /*Ras3..2 group high decode address  */
        /* I/O CS group coding for (CPU)  */
#define LO_CS20_GREG  0x0028 /* CS2..0 group low decode register */
#define HI_CS20_GREG  0x0030 /* CS2..0 group high decode register */
#define LO_CS3B_GREG  0x0038 /* CS3 & PROM group low decode register */
#define HI_CS3B_GREG  0x0040 /* CS3 & PROM group high decode register */
        /* Galileo specific PCI config. */
#define PCI_TIMEOUT_RET 0x0c04 /* Time Out and retry register */
#define RAS10_BANKSIZE 0x0c08 /* RAS 1..0 group PCI bank size */
#define RAS32_BANKSIZE 0x0c0c /* RAS 3..2 group PCI bank size */
#define CS20_BANKSIZE 0x0c10 /* CS 2..0 group PCI bank size */
#define CS3B_BANKSIZE 0x0c14 /* CS 3 & Boot group PCI bank size */
#define DRAM_SIZE     0x0001      /*Dram size in mega bytes*/
#define PROM_SIZE     0x08000     /*Prom size in bytes*/
/*--------------------------------------------------------------------------*/
#define OFFS_DIVA_INIT_TASK_COUNT 0x68
#define OFFS_DSP_CODE_BASE_ADDR   0x6c
#define OFFS_XLOG_BUF_ADDR        0x70
#define OFFS_XLOG_COUNT_ADDR      0x74
#define OFFS_XLOG_OUT_ADDR        0x78
#define OFFS_PROTOCOL_END_ADDR    0x7c
#define OFFS_PROTOCOL_ID_STRING   0x80
/*--------------------------------------------------------------------------*/
