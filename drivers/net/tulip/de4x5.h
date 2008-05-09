/*
    Copyright 1994 Digital Equipment Corporation.

    This software may be used and distributed according to  the terms of the
    GNU General Public License, incorporated herein by reference.

    The author may    be  reached as davies@wanton.lkg.dec.com  or   Digital
    Equipment Corporation, 550 King Street, Littleton MA 01460.

    =========================================================================
*/

/*
** DC21040 CSR<1..15> Register Address Map
*/
#define DE4X5_BMR    iobase+(0x000 << lp->bus)  /* Bus Mode Register */
#define DE4X5_TPD    iobase+(0x008 << lp->bus)  /* Transmit Poll Demand Reg */
#define DE4X5_RPD    iobase+(0x010 << lp->bus)  /* Receive Poll Demand Reg */
#define DE4X5_RRBA   iobase+(0x018 << lp->bus)  /* RX Ring Base Address Reg */
#define DE4X5_TRBA   iobase+(0x020 << lp->bus)  /* TX Ring Base Address Reg */
#define DE4X5_STS    iobase+(0x028 << lp->bus)  /* Status Register */
#define DE4X5_OMR    iobase+(0x030 << lp->bus)  /* Operation Mode Register */
#define DE4X5_IMR    iobase+(0x038 << lp->bus)  /* Interrupt Mask Register */
#define DE4X5_MFC    iobase+(0x040 << lp->bus)  /* Missed Frame Counter */
#define DE4X5_APROM  iobase+(0x048 << lp->bus)  /* Ethernet Address PROM */
#define DE4X5_BROM   iobase+(0x048 << lp->bus)  /* Boot ROM Register */
#define DE4X5_SROM   iobase+(0x048 << lp->bus)  /* Serial ROM Register */
#define DE4X5_MII    iobase+(0x048 << lp->bus)  /* MII Interface Register */
#define DE4X5_DDR    iobase+(0x050 << lp->bus)  /* Data Diagnostic Register */
#define DE4X5_FDR    iobase+(0x058 << lp->bus)  /* Full Duplex Register */
#define DE4X5_GPT    iobase+(0x058 << lp->bus)  /* General Purpose Timer Reg.*/
#define DE4X5_GEP    iobase+(0x060 << lp->bus)  /* General Purpose Register */
#define DE4X5_SISR   iobase+(0x060 << lp->bus)  /* SIA Status Register */
#define DE4X5_SICR   iobase+(0x068 << lp->bus)  /* SIA Connectivity Register */
#define DE4X5_STRR   iobase+(0x070 << lp->bus)  /* SIA TX/RX Register */
#define DE4X5_SIGR   iobase+(0x078 << lp->bus)  /* SIA General Register */

/*
** EISA Register Address Map
*/
#define EISA_ID      iobase+0x0c80   /* EISA ID Registers */
#define EISA_ID0     iobase+0x0c80   /* EISA ID Register 0 */
#define EISA_ID1     iobase+0x0c81   /* EISA ID Register 1 */
#define EISA_ID2     iobase+0x0c82   /* EISA ID Register 2 */
#define EISA_ID3     iobase+0x0c83   /* EISA ID Register 3 */
#define EISA_CR      iobase+0x0c84   /* EISA Control Register */
#define EISA_REG0    iobase+0x0c88   /* EISA Configuration Register 0 */
#define EISA_REG1    iobase+0x0c89   /* EISA Configuration Register 1 */
#define EISA_REG2    iobase+0x0c8a   /* EISA Configuration Register 2 */
#define EISA_REG3    iobase+0x0c8f   /* EISA Configuration Register 3 */
#define EISA_APROM   iobase+0x0c90   /* Ethernet Address PROM */

/*
** PCI/EISA Configuration Registers Address Map
*/
#define PCI_CFID     iobase+0x0008   /* PCI Configuration ID Register */
#define PCI_CFCS     iobase+0x000c   /* PCI Command/Status Register */
#define PCI_CFRV     iobase+0x0018   /* PCI Revision Register */
#define PCI_CFLT     iobase+0x001c   /* PCI Latency Timer Register */
#define PCI_CBIO     iobase+0x0028   /* PCI Base I/O Register */
#define PCI_CBMA     iobase+0x002c   /* PCI Base Memory Address Register */
#define PCI_CBER     iobase+0x0030   /* PCI Expansion ROM Base Address Reg. */
#define PCI_CFIT     iobase+0x003c   /* PCI Configuration Interrupt Register */
#define PCI_CFDA     iobase+0x0040   /* PCI Driver Area Register */
#define PCI_CFDD     iobase+0x0041   /* PCI Driver Dependent Area Register */
#define PCI_CFPM     iobase+0x0043   /* PCI Power Management Area Register */

/*
** EISA Configuration Register 0 bit definitions
*/
#define ER0_BSW       0x80           /* EISA Bus Slave Width, 1: 32 bits */
#define ER0_BMW       0x40           /* EISA Bus Master Width, 1: 32 bits */
#define ER0_EPT       0x20           /* EISA PREEMPT Time, 0: 23 BCLKs */
#define ER0_ISTS      0x10           /* Interrupt Status (X) */
#define ER0_LI        0x08           /* Latch Interrupts */
#define ER0_INTL      0x06           /* INTerrupt Level */
#define ER0_INTT      0x01           /* INTerrupt Type, 0: Level, 1: Edge */

/*
** EISA Configuration Register 1 bit definitions
*/
#define ER1_IAM       0xe0           /* ISA Address Mode */
#define ER1_IAE       0x10           /* ISA Addressing Enable */
#define ER1_UPIN      0x0f           /* User Pins */

/*
** EISA Configuration Register 2 bit definitions
*/
#define ER2_BRS       0xc0           /* Boot ROM Size */
#define ER2_BRA       0x3c           /* Boot ROM Address <16:13> */

/*
** EISA Configuration Register 3 bit definitions
*/
#define ER3_BWE       0x40           /* Burst Write Enable */
#define ER3_BRE       0x04           /* Burst Read Enable */
#define ER3_LSR       0x02           /* Local Software Reset */

/*
** PCI Configuration ID Register (PCI_CFID). The Device IDs are left
** shifted 8 bits to allow detection of DC21142 and DC21143 variants with
** the configuration revision register step number.
*/
#define CFID_DID    0xff00           /* Device ID */
#define CFID_VID    0x00ff           /* Vendor ID */
#define DC21040_DID 0x0200           /* Unique Device ID # */
#define DC21040_VID 0x1011           /* DC21040 Manufacturer */
#define DC21041_DID 0x1400           /* Unique Device ID # */
#define DC21041_VID 0x1011           /* DC21041 Manufacturer */
#define DC21140_DID 0x0900           /* Unique Device ID # */
#define DC21140_VID 0x1011           /* DC21140 Manufacturer */
#define DC2114x_DID 0x1900           /* Unique Device ID # */
#define DC2114x_VID 0x1011           /* DC2114[23] Manufacturer */

/*
** Chipset defines
*/
#define DC21040     DC21040_DID
#define DC21041     DC21041_DID
#define DC21140     DC21140_DID
#define DC2114x     DC2114x_DID
#define DC21142     (DC2114x_DID | 0x0010)
#define DC21143     (DC2114x_DID | 0x0030)
#define DC2114x_BRK 0x0020           /* CFRV break between DC21142 & DC21143 */

#define is_DC21040 ((vendor == DC21040_VID) && (device == DC21040_DID))
#define is_DC21041 ((vendor == DC21041_VID) && (device == DC21041_DID))
#define is_DC21140 ((vendor == DC21140_VID) && (device == DC21140_DID))
#define is_DC2114x ((vendor == DC2114x_VID) && (device == DC2114x_DID))
#define is_DC21142 ((vendor == DC2114x_VID) && (device == DC21142))
#define is_DC21143 ((vendor == DC2114x_VID) && (device == DC21143))

/*
** PCI Configuration Command/Status Register (PCI_CFCS)
*/
#define CFCS_DPE    0x80000000       /* Detected Parity Error (S) */
#define CFCS_SSE    0x40000000       /* Signal System Error   (S) */
#define CFCS_RMA    0x20000000       /* Receive Master Abort  (S) */
#define CFCS_RTA    0x10000000       /* Receive Target Abort  (S) */
#define CFCS_DST    0x06000000       /* DEVSEL Timing         (S) */
#define CFCS_DPR    0x01000000       /* Data Parity Report    (S) */
#define CFCS_FBB    0x00800000       /* Fast Back-To-Back     (S) */
#define CFCS_SEE    0x00000100       /* System Error Enable   (C) */
#define CFCS_PER    0x00000040       /* Parity Error Response (C) */
#define CFCS_MO     0x00000004       /* Master Operation      (C) */
#define CFCS_MSA    0x00000002       /* Memory Space Access   (C) */
#define CFCS_IOSA   0x00000001       /* I/O Space Access      (C) */

/*
** PCI Configuration Revision Register (PCI_CFRV)
*/
#define CFRV_BC     0xff000000       /* Base Class */
#define CFRV_SC     0x00ff0000       /* Subclass */
#define CFRV_RN     0x000000f0       /* Revision Number */
#define CFRV_SN     0x0000000f       /* Step Number */
#define BASE_CLASS  0x02000000       /* Indicates Network Controller */
#define SUB_CLASS   0x00000000       /* Indicates Ethernet Controller */
#define STEP_NUMBER 0x00000020       /* Increments for future chips */
#define REV_NUMBER  0x00000003       /* 0x00, 0x01, 0x02, 0x03: Rev in Step */
#define CFRV_MASK   0xffff0000       /* Register mask */

/*
** PCI Configuration Latency Timer Register (PCI_CFLT)
*/
#define CFLT_BC     0x0000ff00       /* Latency Timer bits */

/*
** PCI Configuration Base I/O Address Register (PCI_CBIO)
*/
#define CBIO_MASK   -128             /* Base I/O Address Mask */
#define CBIO_IOSI   0x00000001       /* I/O Space Indicator (RO, value is 1) */

/*
** PCI Configuration Card Information Structure Register (PCI_CCIS)
*/
#define CCIS_ROMI   0xf0000000       /* ROM Image */
#define CCIS_ASO    0x0ffffff8       /* Address Space Offset */
#define CCIS_ASI    0x00000007       /* Address Space Indicator */

/*
** PCI Configuration Subsystem ID Register (PCI_SSID)
*/
#define SSID_SSID   0xffff0000       /* Subsystem ID */
#define SSID_SVID   0x0000ffff       /* Subsystem Vendor ID */

/*
** PCI Configuration Expansion ROM Base Address Register (PCI_CBER)
*/
#define CBER_MASK   0xfffffc00       /* Expansion ROM Base Address Mask */
#define CBER_ROME   0x00000001       /* ROM Enable */

/*
** PCI Configuration Interrupt Register (PCI_CFIT)
*/
#define CFIT_MXLT   0xff000000       /* MAX_LAT Value (0.25us periods) */
#define CFIT_MNGT   0x00ff0000       /* MIN_GNT Value (0.25us periods) */
#define CFIT_IRQP   0x0000ff00       /* Interrupt Pin */
#define CFIT_IRQL   0x000000ff       /* Interrupt Line */

/*
** PCI Configuration Power Management Area Register (PCI_CFPM)
*/
#define SLEEP       0x80             /* Power Saving Sleep Mode */
#define SNOOZE      0x40             /* Power Saving Snooze Mode */
#define WAKEUP      0x00             /* Power Saving Wakeup */

#define PCI_CFDA_DSU 0x41            /* 8 bit Configuration Space Address */
#define PCI_CFDA_PSM 0x43            /* 8 bit Configuration Space Address */

/*
** DC21040 Bus Mode Register (DE4X5_BMR)
*/
#define BMR_RML    0x00200000       /* [Memory] Read Multiple */
#define BMR_DBO    0x00100000       /* Descriptor Byte Ordering (Endian) */
#define BMR_TAP    0x000e0000       /* Transmit Automatic Polling */
#define BMR_DAS    0x00010000       /* Diagnostic Address Space */
#define BMR_CAL    0x0000c000       /* Cache Alignment */
#define BMR_PBL    0x00003f00       /* Programmable Burst Length */
#define BMR_BLE    0x00000080       /* Big/Little Endian */
#define BMR_DSL    0x0000007c       /* Descriptor Skip Length */
#define BMR_BAR    0x00000002       /* Bus ARbitration */
#define BMR_SWR    0x00000001       /* Software Reset */

                                    /* Timings here are for 10BASE-T/AUI only*/
#define TAP_NOPOLL 0x00000000       /* No automatic polling */
#define TAP_200US  0x00020000       /* TX automatic polling every 200us */
#define TAP_800US  0x00040000       /* TX automatic polling every 800us */
#define TAP_1_6MS  0x00060000       /* TX automatic polling every 1.6ms */
#define TAP_12_8US 0x00080000       /* TX automatic polling every 12.8us */
#define TAP_25_6US 0x000a0000       /* TX automatic polling every 25.6us */
#define TAP_51_2US 0x000c0000       /* TX automatic polling every 51.2us */
#define TAP_102_4US 0x000e0000      /* TX automatic polling every 102.4us */

#define CAL_NOUSE  0x00000000       /* Not used */
#define CAL_8LONG  0x00004000       /* 8-longword alignment */
#define CAL_16LONG 0x00008000       /* 16-longword alignment */
#define CAL_32LONG 0x0000c000       /* 32-longword alignment */

#define PBL_0      0x00000000       /*  DMA burst length = amount in RX FIFO */
#define PBL_1      0x00000100       /*  1 longword  DMA burst length */
#define PBL_2      0x00000200       /*  2 longwords DMA burst length */
#define PBL_4      0x00000400       /*  4 longwords DMA burst length */
#define PBL_8      0x00000800       /*  8 longwords DMA burst length */
#define PBL_16     0x00001000       /* 16 longwords DMA burst length */
#define PBL_32     0x00002000       /* 32 longwords DMA burst length */

#define DSL_0      0x00000000       /*  0 longword  / descriptor */
#define DSL_1      0x00000004       /*  1 longword  / descriptor */
#define DSL_2      0x00000008       /*  2 longwords / descriptor */
#define DSL_4      0x00000010       /*  4 longwords / descriptor */
#define DSL_8      0x00000020       /*  8 longwords / descriptor */
#define DSL_16     0x00000040       /* 16 longwords / descriptor */
#define DSL_32     0x00000080       /* 32 longwords / descriptor */

/*
** DC21040 Transmit Poll Demand Register (DE4X5_TPD)
*/
#define TPD        0x00000001       /* Transmit Poll Demand */

/*
** DC21040 Receive Poll Demand Register (DE4X5_RPD)
*/
#define RPD        0x00000001       /* Receive Poll Demand */

/*
** DC21040 Receive Ring Base Address Register (DE4X5_RRBA)
*/
#define RRBA       0xfffffffc       /* RX Descriptor List Start Address */

/*
** DC21040 Transmit Ring Base Address Register (DE4X5_TRBA)
*/
#define TRBA       0xfffffffc       /* TX Descriptor List Start Address */

/*
** Status Register (DE4X5_STS)
*/
#define STS_GPI    0x04000000       /* General Purpose Port Interrupt */
#define STS_BE     0x03800000       /* Bus Error Bits */
#define STS_TS     0x00700000       /* Transmit Process State */
#define STS_RS     0x000e0000       /* Receive Process State */
#define STS_NIS    0x00010000       /* Normal Interrupt Summary */
#define STS_AIS    0x00008000       /* Abnormal Interrupt Summary */
#define STS_ER     0x00004000       /* Early Receive */
#define STS_FBE    0x00002000       /* Fatal Bus Error */
#define STS_SE     0x00002000       /* System Error */
#define STS_LNF    0x00001000       /* Link Fail */
#define STS_FD     0x00000800       /* Full-Duplex Short Frame Received */
#define STS_TM     0x00000800       /* Timer Expired (DC21041) */
#define STS_ETI    0x00000400       /* Early Transmit Interrupt */
#define STS_AT     0x00000400       /* AUI/TP Pin */
#define STS_RWT    0x00000200       /* Receive Watchdog Time-Out */
#define STS_RPS    0x00000100       /* Receive Process Stopped */
#define STS_RU     0x00000080       /* Receive Buffer Unavailable */
#define STS_RI     0x00000040       /* Receive Interrupt */
#define STS_UNF    0x00000020       /* Transmit Underflow */
#define STS_LNP    0x00000010       /* Link Pass */
#define STS_ANC    0x00000010       /* Autonegotiation Complete */
#define STS_TJT    0x00000008       /* Transmit Jabber Time-Out */
#define STS_TU     0x00000004       /* Transmit Buffer Unavailable */
#define STS_TPS    0x00000002       /* Transmit Process Stopped */
#define STS_TI     0x00000001       /* Transmit Interrupt */

#define EB_PAR     0x00000000       /* Parity Error */
#define EB_MA      0x00800000       /* Master Abort */
#define EB_TA      0x01000000       /* Target Abort */
#define EB_RES0    0x01800000       /* Reserved */
#define EB_RES1    0x02000000       /* Reserved */

#define TS_STOP    0x00000000       /* Stopped */
#define TS_FTD     0x00100000       /* Fetch Transmit Descriptor */
#define TS_WEOT    0x00200000       /* Wait for End Of Transmission */
#define TS_QDAT    0x00300000       /* Queue skb data into TX FIFO */
#define TS_RES     0x00400000       /* Reserved */
#define TS_SPKT    0x00500000       /* Setup Packet */
#define TS_SUSP    0x00600000       /* Suspended */
#define TS_CLTD    0x00700000       /* Close Transmit Descriptor */

#define RS_STOP    0x00000000       /* Stopped */
#define RS_FRD     0x00020000       /* Fetch Receive Descriptor */
#define RS_CEOR    0x00040000       /* Check for End of Receive Packet */
#define RS_WFRP    0x00060000       /* Wait for Receive Packet */
#define RS_SUSP    0x00080000       /* Suspended */
#define RS_CLRD    0x000a0000       /* Close Receive Descriptor */
#define RS_FLUSH   0x000c0000       /* Flush RX FIFO */
#define RS_QRFS    0x000e0000       /* Queue RX FIFO into RX Skb */

#define INT_CANCEL 0x0001ffff       /* For zeroing all interrupt sources */

/*
** Operation Mode Register (DE4X5_OMR)
*/
#define OMR_SC     0x80000000       /* Special Capture Effect Enable */
#define OMR_RA     0x40000000       /* Receive All */
#define OMR_SDP    0x02000000       /* SD Polarity - MUST BE ASSERTED */
#define OMR_SCR    0x01000000       /* Scrambler Mode */
#define OMR_PCS    0x00800000       /* PCS Function */
#define OMR_TTM    0x00400000       /* Transmit Threshold Mode */
#define OMR_SF     0x00200000       /* Store and Forward */
#define OMR_HBD    0x00080000       /* HeartBeat Disable */
#define OMR_PS     0x00040000       /* Port Select */
#define OMR_CA     0x00020000       /* Capture Effect Enable */
#define OMR_BP     0x00010000       /* Back Pressure */
#define OMR_TR     0x0000c000       /* Threshold Control Bits */
#define OMR_ST     0x00002000       /* Start/Stop Transmission Command */
#define OMR_FC     0x00001000       /* Force Collision Mode */
#define OMR_OM     0x00000c00       /* Operating Mode */
#define OMR_FDX    0x00000200       /* Full Duplex Mode */
#define OMR_FKD    0x00000100       /* Flaky Oscillator Disable */
#define OMR_PM     0x00000080       /* Pass All Multicast */
#define OMR_PR     0x00000040       /* Promiscuous Mode */
#define OMR_SB     0x00000020       /* Start/Stop Backoff Counter */
#define OMR_IF     0x00000010       /* Inverse Filtering */
#define OMR_PB     0x00000008       /* Pass Bad Frames */
#define OMR_HO     0x00000004       /* Hash Only Filtering Mode */
#define OMR_SR     0x00000002       /* Start/Stop Receive */
#define OMR_HP     0x00000001       /* Hash/Perfect Receive Filtering Mode */

#define TR_72      0x00000000       /* Threshold set to 72 (128) bytes */
#define TR_96      0x00004000       /* Threshold set to 96 (256) bytes */
#define TR_128     0x00008000       /* Threshold set to 128 (512) bytes */
#define TR_160     0x0000c000       /* Threshold set to 160 (1024) bytes */

#define OMR_DEF     (OMR_SDP)
#define OMR_SIA     (OMR_SDP | OMR_TTM)
#define OMR_SYM     (OMR_SDP | OMR_SCR | OMR_PCS | OMR_HBD | OMR_PS)
#define OMR_MII_10  (OMR_SDP | OMR_TTM | OMR_PS)
#define OMR_MII_100 (OMR_SDP | OMR_HBD | OMR_PS)

/*
** DC21040 Interrupt Mask Register (DE4X5_IMR)
*/
#define IMR_GPM    0x04000000       /* General Purpose Port Mask */
#define IMR_NIM    0x00010000       /* Normal Interrupt Summary Mask */
#define IMR_AIM    0x00008000       /* Abnormal Interrupt Summary Mask */
#define IMR_ERM    0x00004000       /* Early Receive Mask */
#define IMR_FBM    0x00002000       /* Fatal Bus Error Mask */
#define IMR_SEM    0x00002000       /* System Error Mask */
#define IMR_LFM    0x00001000       /* Link Fail Mask */
#define IMR_FDM    0x00000800       /* Full-Duplex (Short Frame) Mask */
#define IMR_TMM    0x00000800       /* Timer Expired Mask (DC21041) */
#define IMR_ETM    0x00000400       /* Early Transmit Interrupt Mask */
#define IMR_ATM    0x00000400       /* AUI/TP Switch Mask */
#define IMR_RWM    0x00000200       /* Receive Watchdog Time-Out Mask */
#define IMR_RSM    0x00000100       /* Receive Stopped Mask */
#define IMR_RUM    0x00000080       /* Receive Buffer Unavailable Mask */
#define IMR_RIM    0x00000040       /* Receive Interrupt Mask */
#define IMR_UNM    0x00000020       /* Underflow Interrupt Mask */
#define IMR_ANM    0x00000010       /* Autonegotiation Complete Mask */
#define IMR_LPM    0x00000010       /* Link Pass */
#define IMR_TJM    0x00000008       /* Transmit Time-Out Jabber Mask */
#define IMR_TUM    0x00000004       /* Transmit Buffer Unavailable Mask */
#define IMR_TSM    0x00000002       /* Transmission Stopped Mask */
#define IMR_TIM    0x00000001       /* Transmit Interrupt Mask */

/*
** Missed Frames and FIFO Overflow Counters (DE4X5_MFC)
*/
#define MFC_FOCO   0x10000000       /* FIFO Overflow Counter Overflow Bit */
#define MFC_FOC    0x0ffe0000       /* FIFO Overflow Counter Bits */
#define MFC_OVFL   0x00010000       /* Missed Frames Counter Overflow Bit */
#define MFC_CNTR   0x0000ffff       /* Missed Frames Counter Bits */
#define MFC_FOCM   0x1ffe0000       /* FIFO Overflow Counter Mask */

/*
** DC21040 Ethernet Address PROM (DE4X5_APROM)
*/
#define APROM_DN   0x80000000       /* Data Not Valid */
#define APROM_DT   0x000000ff       /* Address Byte */

/*
** DC21041 Boot/Ethernet Address ROM (DE4X5_BROM)
*/
#define BROM_MODE 0x00008000       /* MODE_1: 0,  MODE_0: 1  (read only) */
#define BROM_RD   0x00004000       /* Read from Boot ROM */
#define BROM_WR   0x00002000       /* Write to Boot ROM */
#define BROM_BR   0x00001000       /* Select Boot ROM when set */
#define BROM_SR   0x00000800       /* Select Serial ROM when set */
#define BROM_REG  0x00000400       /* External Register Select */
#define BROM_DT   0x000000ff       /* Data Byte */

/*
** DC21041 Serial/Ethernet Address ROM (DE4X5_SROM, DE4X5_MII)
*/
#define MII_MDI   0x00080000       /* MII Management Data In */
#define MII_MDO   0x00060000       /* MII Management Mode/Data Out */
#define MII_MRD   0x00040000       /* MII Management Define Read Mode */
#define MII_MWR   0x00000000       /* MII Management Define Write Mode */
#define MII_MDT   0x00020000       /* MII Management Data Out */
#define MII_MDC   0x00010000       /* MII Management Clock */
#define MII_RD    0x00004000       /* Read from MII */
#define MII_WR    0x00002000       /* Write to MII */
#define MII_SEL   0x00000800       /* Select MII when RESET */

#define SROM_MODE 0x00008000       /* MODE_1: 0,  MODE_0: 1  (read only) */
#define SROM_RD   0x00004000       /* Read from Boot ROM */
#define SROM_WR   0x00002000       /* Write to Boot ROM */
#define SROM_BR   0x00001000       /* Select Boot ROM when set */
#define SROM_SR   0x00000800       /* Select Serial ROM when set */
#define SROM_REG  0x00000400       /* External Register Select */
#define SROM_DT   0x000000ff       /* Data Byte */

#define DT_OUT    0x00000008       /* Serial Data Out */
#define DT_IN     0x00000004       /* Serial Data In */
#define DT_CLK    0x00000002       /* Serial ROM Clock */
#define DT_CS     0x00000001       /* Serial ROM Chip Select */

#define MII_PREAMBLE 0xffffffff    /* MII Management Preamble */
#define MII_TEST     0xaaaaaaaa    /* MII Test Signal */
#define MII_STRD     0x06          /* Start of Frame+Op Code: use low nibble */
#define MII_STWR     0x0a          /* Start of Frame+Op Code: use low nibble */

#define MII_CR       0x00          /* MII Management Control Register */
#define MII_SR       0x01          /* MII Management Status Register */
#define MII_ID0      0x02          /* PHY Identifier Register 0 */
#define MII_ID1      0x03          /* PHY Identifier Register 1 */
#define MII_ANA      0x04          /* Auto Negotiation Advertisement */
#define MII_ANLPA    0x05          /* Auto Negotiation Link Partner Ability */
#define MII_ANE      0x06          /* Auto Negotiation Expansion */
#define MII_ANP      0x07          /* Auto Negotiation Next Page TX */

#define DE4X5_MAX_MII 32           /* Maximum address of MII PHY devices */

/*
** MII Management Control Register
*/
#define MII_CR_RST  0x8000         /* RESET the PHY chip */
#define MII_CR_LPBK 0x4000         /* Loopback enable */
#define MII_CR_SPD  0x2000         /* 0: 10Mb/s; 1: 100Mb/s */
#define MII_CR_10   0x0000         /* Set 10Mb/s */
#define MII_CR_100  0x2000         /* Set 100Mb/s */
#define MII_CR_ASSE 0x1000         /* Auto Speed Select Enable */
#define MII_CR_PD   0x0800         /* Power Down */
#define MII_CR_ISOL 0x0400         /* Isolate Mode */
#define MII_CR_RAN  0x0200         /* Restart Auto Negotiation */
#define MII_CR_FDM  0x0100         /* Full Duplex Mode */
#define MII_CR_CTE  0x0080         /* Collision Test Enable */

/*
** MII Management Status Register
*/
#define MII_SR_T4C  0x8000         /* 100BASE-T4 capable */
#define MII_SR_TXFD 0x4000         /* 100BASE-TX Full Duplex capable */
#define MII_SR_TXHD 0x2000         /* 100BASE-TX Half Duplex capable */
#define MII_SR_TFD  0x1000         /* 10BASE-T Full Duplex capable */
#define MII_SR_THD  0x0800         /* 10BASE-T Half Duplex capable */
#define MII_SR_ASSC 0x0020         /* Auto Speed Selection Complete*/
#define MII_SR_RFD  0x0010         /* Remote Fault Detected */
#define MII_SR_ANC  0x0008         /* Auto Negotiation capable */
#define MII_SR_LKS  0x0004         /* Link Status */
#define MII_SR_JABD 0x0002         /* Jabber Detect */
#define MII_SR_XC   0x0001         /* Extended Capabilities */

/*
** MII Management Auto Negotiation Advertisement Register
*/
#define MII_ANA_TAF  0x03e0        /* Technology Ability Field */
#define MII_ANA_T4AM 0x0200        /* T4 Technology Ability Mask */
#define MII_ANA_TXAM 0x0180        /* TX Technology Ability Mask */
#define MII_ANA_FDAM 0x0140        /* Full Duplex Technology Ability Mask */
#define MII_ANA_HDAM 0x02a0        /* Half Duplex Technology Ability Mask */
#define MII_ANA_100M 0x0380        /* 100Mb Technology Ability Mask */
#define MII_ANA_10M  0x0060        /* 10Mb Technology Ability Mask */
#define MII_ANA_CSMA 0x0001        /* CSMA-CD Capable */

/*
** MII Management Auto Negotiation Remote End Register
*/
#define MII_ANLPA_NP   0x8000      /* Next Page (Enable) */
#define MII_ANLPA_ACK  0x4000      /* Remote Acknowledge */
#define MII_ANLPA_RF   0x2000      /* Remote Fault */
#define MII_ANLPA_TAF  0x03e0      /* Technology Ability Field */
#define MII_ANLPA_T4AM 0x0200      /* T4 Technology Ability Mask */
#define MII_ANLPA_TXAM 0x0180      /* TX Technology Ability Mask */
#define MII_ANLPA_FDAM 0x0140      /* Full Duplex Technology Ability Mask */
#define MII_ANLPA_HDAM 0x02a0      /* Half Duplex Technology Ability Mask */
#define MII_ANLPA_100M 0x0380      /* 100Mb Technology Ability Mask */
#define MII_ANLPA_10M  0x0060      /* 10Mb Technology Ability Mask */
#define MII_ANLPA_CSMA 0x0001      /* CSMA-CD Capable */

/*
** SROM Media Definitions (ABG SROM Section)
*/
#define MEDIA_NWAY     0x0080      /* Nway (Auto Negotiation) on PHY */
#define MEDIA_MII      0x0040      /* MII Present on the adapter */
#define MEDIA_FIBRE    0x0008      /* Fibre Media present */
#define MEDIA_AUI      0x0004      /* AUI Media present */
#define MEDIA_TP       0x0002      /* TP Media present */
#define MEDIA_BNC      0x0001      /* BNC Media present */

/*
** SROM Definitions (Digital Semiconductor Format)
*/
#define SROM_SSVID     0x0000      /* Sub-system Vendor ID offset */
#define SROM_SSID      0x0002      /* Sub-system ID offset */
#define SROM_CISPL     0x0004      /* CardBus CIS Pointer low offset */
#define SROM_CISPH     0x0006      /* CardBus CIS Pointer high offset */
#define SROM_IDCRC     0x0010      /* ID Block CRC offset*/
#define SROM_RSVD2     0x0011      /* ID Reserved 2 offset */
#define SROM_SFV       0x0012      /* SROM Format Version offset */
#define SROM_CCNT      0x0013      /* Controller Count offset */
#define SROM_HWADD     0x0014      /* Hardware Address offset */
#define SROM_MRSVD     0x007c      /* Manufacturer Reserved offset*/
#define SROM_CRC       0x007e      /* SROM CRC offset */

/*
** SROM Media Connection Definitions
*/
#define SROM_10BT      0x0000      /*  10BASE-T half duplex */
#define SROM_10BTN     0x0100      /*  10BASE-T with Nway */
#define SROM_10BTF     0x0204      /*  10BASE-T full duplex */
#define SROM_10BTNLP   0x0400      /*  10BASE-T without Link Pass test */
#define SROM_10B2      0x0001      /*  10BASE-2 (BNC) */
#define SROM_10B5      0x0002      /*  10BASE-5 (AUI) */
#define SROM_100BTH    0x0003      /*  100BASE-T half duplex */
#define SROM_100BTF    0x0205      /*  100BASE-T full duplex */
#define SROM_100BT4    0x0006      /*  100BASE-T4 */
#define SROM_100BFX    0x0007      /*  100BASE-FX half duplex (Fiber) */
#define SROM_M10BT     0x0009      /*  MII 10BASE-T half duplex */
#define SROM_M10BTF    0x020a      /*  MII 10BASE-T full duplex */
#define SROM_M100BT    0x000d      /*  MII 100BASE-T half duplex */
#define SROM_M100BTF   0x020e      /*  MII 100BASE-T full duplex */
#define SROM_M100BT4   0x000f      /*  MII 100BASE-T4 */
#define SROM_M100BF    0x0010      /*  MII 100BASE-FX half duplex */
#define SROM_M100BFF   0x0211      /*  MII 100BASE-FX full duplex */
#define SROM_PDA       0x0800      /*  Powerup & Dynamic Autosense */
#define SROM_PAO       0x8800      /*  Powerup Autosense Only */
#define SROM_NSMI      0xffff      /*  No Selected Media Information */

/*
** SROM Media Definitions
*/
#define SROM_10BASET   0x0000      /*  10BASE-T half duplex */
#define SROM_10BASE2   0x0001      /*  10BASE-2 (BNC) */
#define SROM_10BASE5   0x0002      /*  10BASE-5 (AUI) */
#define SROM_100BASET  0x0003      /*  100BASE-T half duplex */
#define SROM_10BASETF  0x0004      /*  10BASE-T full duplex */
#define SROM_100BASETF 0x0005      /*  100BASE-T full duplex */
#define SROM_100BASET4 0x0006      /*  100BASE-T4 */
#define SROM_100BASEF  0x0007      /*  100BASE-FX half duplex */
#define SROM_100BASEFF 0x0008      /*  100BASE-FX full duplex */

#define BLOCK_LEN      0x7f        /* Extended blocks length mask */
#define EXT_FIELD      0x40        /* Extended blocks extension field bit */
#define MEDIA_CODE     0x3f        /* Extended blocks media code mask */

/*
** SROM Compact Format Block Masks
*/
#define COMPACT_FI      0x80       /* Format Indicator */
#define COMPACT_LEN     0x04       /* Length */
#define COMPACT_MC      0x3f       /* Media Code */

/*
** SROM Extended Format Block Type 0 Masks
*/
#define BLOCK0_FI      0x80        /* Format Indicator */
#define BLOCK0_MCS     0x80        /* Media Code byte Sign */
#define BLOCK0_MC      0x3f        /* Media Code */

/*
** DC21040 Full Duplex Register (DE4X5_FDR)
*/
#define FDR_FDACV  0x0000ffff      /* Full Duplex Auto Configuration Value */

/*
** DC21041 General Purpose Timer Register (DE4X5_GPT)
*/
#define GPT_CON  0x00010000        /* One shot: 0,  Continuous: 1 */
#define GPT_VAL  0x0000ffff        /* Timer Value */

/*
** DC21140 General Purpose Register (DE4X5_GEP) (hardware dependent bits)
*/
/* Valid ONLY for DE500 hardware */
#define GEP_LNP  0x00000080        /* Link Pass               (input)        */
#define GEP_SLNK 0x00000040        /* SYM LINK                (input)        */
#define GEP_SDET 0x00000020        /* Signal Detect           (input)        */
#define GEP_HRST 0x00000010        /* Hard RESET (to PHY)     (output)       */
#define GEP_FDXD 0x00000008        /* Full Duplex Disable     (output)       */
#define GEP_PHYL 0x00000004        /* PHY Loopback            (output)       */
#define GEP_FLED 0x00000002        /* Force Activity LED on   (output)       */
#define GEP_MODE 0x00000001        /* 0: 10Mb/s,  1: 100Mb/s                 */
#define GEP_INIT 0x0000011f        /* Setup inputs (0) and outputs (1)       */
#define GEP_CTRL 0x00000100        /* GEP control bit                        */

/*
** SIA Register Defaults
*/
#define CSR13 0x00000001
#define CSR14 0x0003ff7f           /* Autonegotiation disabled               */
#define CSR15 0x00000008

/*
** SIA Status Register (DE4X5_SISR)
*/
#define SISR_LPC   0xffff0000      /* Link Partner's Code Word               */
#define SISR_LPN   0x00008000      /* Link Partner Negotiable                */
#define SISR_ANS   0x00007000      /* Auto Negotiation Arbitration State     */
#define SISR_NSN   0x00000800      /* Non Stable NLPs Detected (DC21041)     */
#define SISR_TRF   0x00000800      /* Transmit Remote Fault                  */
#define SISR_NSND  0x00000400      /* Non Stable NLPs Detected (DC21142)     */
#define SISR_ANR_FDS 0x00000400    /* Auto Negotiate Restart/Full Duplex Sel.*/
#define SISR_TRA   0x00000200      /* 10BASE-T Receive Port Activity         */
#define SISR_NRA   0x00000200      /* Non Selected Port Receive Activity     */
#define SISR_ARA   0x00000100      /* AUI Receive Port Activity              */
#define SISR_SRA   0x00000100      /* Selected Port Receive Activity         */
#define SISR_DAO   0x00000080      /* PLL All One                            */
#define SISR_DAZ   0x00000040      /* PLL All Zero                           */
#define SISR_DSP   0x00000020      /* PLL Self-Test Pass                     */
#define SISR_DSD   0x00000010      /* PLL Self-Test Done                     */
#define SISR_APS   0x00000008      /* Auto Polarity State                    */
#define SISR_LKF   0x00000004      /* Link Fail Status                       */
#define SISR_LS10  0x00000004      /* 10Mb/s Link Fail Status                */
#define SISR_NCR   0x00000002      /* Network Connection Error               */
#define SISR_LS100 0x00000002      /* 100Mb/s Link Fail Status               */
#define SISR_PAUI  0x00000001      /* AUI_TP Indication                      */
#define SISR_MRA   0x00000001      /* MII Receive Port Activity              */

#define ANS_NDIS   0x00000000      /* Nway disable                           */
#define ANS_TDIS   0x00001000      /* Transmit Disable                       */
#define ANS_ADET   0x00002000      /* Ability Detect                         */
#define ANS_ACK    0x00003000      /* Acknowledge                            */
#define ANS_CACK   0x00004000      /* Complete Acknowledge                   */
#define ANS_NWOK   0x00005000      /* Nway OK - FLP Link Good                */
#define ANS_LCHK   0x00006000      /* Link Check                             */

#define SISR_RST   0x00000301      /* CSR12 reset                            */
#define SISR_ANR   0x00001301      /* Autonegotiation restart                */

/*
** SIA Connectivity Register (DE4X5_SICR)
*/
#define SICR_SDM   0xffff0000       /* SIA Diagnostics Mode */
#define SICR_OE57  0x00008000       /* Output Enable 5 6 7 */
#define SICR_OE24  0x00004000       /* Output Enable 2 4 */
#define SICR_OE13  0x00002000       /* Output Enable 1 3 */
#define SICR_IE    0x00001000       /* Input Enable */
#define SICR_EXT   0x00000000       /* SIA MUX Select External SIA Mode */
#define SICR_D_SIA 0x00000400       /* SIA MUX Select Diagnostics - SIA Sigs */
#define SICR_DPLL  0x00000800       /* SIA MUX Select Diagnostics - DPLL Sigs*/
#define SICR_APLL  0x00000a00       /* SIA MUX Select Diagnostics - DPLL Sigs*/
#define SICR_D_RxM 0x00000c00       /* SIA MUX Select Diagnostics - RxM Sigs */
#define SICR_M_RxM 0x00000d00       /* SIA MUX Select Diagnostics - RxM Sigs */
#define SICR_LNKT  0x00000e00       /* SIA MUX Select Diagnostics - Link Test*/
#define SICR_SEL   0x00000f00       /* SIA MUX Select AUI or TP with LEDs */
#define SICR_ASE   0x00000080       /* APLL Start Enable*/
#define SICR_SIM   0x00000040       /* Serial Interface Input Multiplexer */
#define SICR_ENI   0x00000020       /* Encoder Input Multiplexer */
#define SICR_EDP   0x00000010       /* SIA PLL External Input Enable */
#define SICR_AUI   0x00000008       /* 10Base-T (0) or AUI (1) */
#define SICR_CAC   0x00000004       /* CSR Auto Configuration */
#define SICR_PS    0x00000002       /* Pin AUI/TP Selection */
#define SICR_SRL   0x00000001       /* SIA Reset */
#define SIA_RESET  0x00000000       /* SIA Reset Value */

/*
** SIA Transmit and Receive Register (DE4X5_STRR)
*/
#define STRR_TAS   0x00008000       /* 10Base-T/AUI Autosensing Enable */
#define STRR_SPP   0x00004000       /* Set Polarity Plus */
#define STRR_APE   0x00002000       /* Auto Polarity Enable */
#define STRR_LTE   0x00001000       /* Link Test Enable */
#define STRR_SQE   0x00000800       /* Signal Quality Enable */
#define STRR_CLD   0x00000400       /* Collision Detect Enable */
#define STRR_CSQ   0x00000200       /* Collision Squelch Enable */
#define STRR_RSQ   0x00000100       /* Receive Squelch Enable */
#define STRR_ANE   0x00000080       /* Auto Negotiate Enable */
#define STRR_HDE   0x00000040       /* Half Duplex Enable */
#define STRR_CPEN  0x00000030       /* Compensation Enable */
#define STRR_LSE   0x00000008       /* Link Pulse Send Enable */
#define STRR_DREN  0x00000004       /* Driver Enable */
#define STRR_LBK   0x00000002       /* Loopback Enable */
#define STRR_ECEN  0x00000001       /* Encoder Enable */
#define STRR_RESET 0xffffffff       /* Reset value for STRR */

/*
** SIA General Register (DE4X5_SIGR)
*/
#define SIGR_RMI   0x40000000       /* Receive Match Interrupt */
#define SIGR_GI1   0x20000000       /* General Port Interrupt 1 */
#define SIGR_GI0   0x10000000       /* General Port Interrupt 0 */
#define SIGR_CWE   0x08000000       /* Control Write Enable */
#define SIGR_RME   0x04000000       /* Receive Match Enable */
#define SIGR_GEI1  0x02000000       /* GEP Interrupt Enable on Port 1 */
#define SIGR_GEI0  0x01000000       /* GEP Interrupt Enable on Port 0 */
#define SIGR_LGS3  0x00800000       /* LED/GEP3 Select */
#define SIGR_LGS2  0x00400000       /* LED/GEP2 Select */
#define SIGR_LGS1  0x00200000       /* LED/GEP1 Select */
#define SIGR_LGS0  0x00100000       /* LED/GEP0 Select */
#define SIGR_MD    0x000f0000       /* General Purpose Mode and Data */
#define SIGR_LV2   0x00008000       /* General Purpose LED2 value */
#define SIGR_LE2   0x00004000       /* General Purpose LED2 enable */
#define SIGR_FRL   0x00002000       /* Force Receiver Low */
#define SIGR_DPST  0x00001000       /* PLL Self Test Start */
#define SIGR_LSD   0x00000800       /* LED Stretch Disable */
#define SIGR_FLF   0x00000400       /* Force Link Fail */
#define SIGR_FUSQ  0x00000200       /* Force Unsquelch */
#define SIGR_TSCK  0x00000100       /* Test Clock */
#define SIGR_LV1   0x00000080       /* General Purpose LED1 value */
#define SIGR_LE1   0x00000040       /* General Purpose LED1 enable */
#define SIGR_RWR   0x00000020       /* Receive Watchdog Release */
#define SIGR_RWD   0x00000010       /* Receive Watchdog Disable */
#define SIGR_ABM   0x00000008       /* BNC: 0,  AUI:1 */
#define SIGR_JCK   0x00000004       /* Jabber Clock */
#define SIGR_HUJ   0x00000002       /* Host Unjab */
#define SIGR_JBD   0x00000001       /* Jabber Disable */
#define SIGR_RESET 0xffff0000       /* Reset value for SIGR */

/*
** Receive Descriptor Bit Summary
*/
#define R_OWN      0x80000000       /* Own Bit */
#define RD_FF      0x40000000       /* Filtering Fail */
#define RD_FL      0x3fff0000       /* Frame Length */
#define RD_ES      0x00008000       /* Error Summary */
#define RD_LE      0x00004000       /* Length Error */
#define RD_DT      0x00003000       /* Data Type */
#define RD_RF      0x00000800       /* Runt Frame */
#define RD_MF      0x00000400       /* Multicast Frame */
#define RD_FS      0x00000200       /* First Descriptor */
#define RD_LS      0x00000100       /* Last Descriptor */
#define RD_TL      0x00000080       /* Frame Too Long */
#define RD_CS      0x00000040       /* Collision Seen */
#define RD_FT      0x00000020       /* Frame Type */
#define RD_RJ      0x00000010       /* Receive Watchdog */
#define RD_RE      0x00000008       /* Report on MII Error */
#define RD_DB      0x00000004       /* Dribbling Bit */
#define RD_CE      0x00000002       /* CRC Error */
#define RD_OF      0x00000001       /* Overflow */

#define RD_RER     0x02000000       /* Receive End Of Ring */
#define RD_RCH     0x01000000       /* Second Address Chained */
#define RD_RBS2    0x003ff800       /* Buffer 2 Size */
#define RD_RBS1    0x000007ff       /* Buffer 1 Size */

/*
** Transmit Descriptor Bit Summary
*/
#define T_OWN      0x80000000       /* Own Bit */
#define TD_ES      0x00008000       /* Error Summary */
#define TD_TO      0x00004000       /* Transmit Jabber Time-Out */
#define TD_LO      0x00000800       /* Loss Of Carrier */
#define TD_NC      0x00000400       /* No Carrier */
#define TD_LC      0x00000200       /* Late Collision */
#define TD_EC      0x00000100       /* Excessive Collisions */
#define TD_HF      0x00000080       /* Heartbeat Fail */
#define TD_CC      0x00000078       /* Collision Counter */
#define TD_LF      0x00000004       /* Link Fail */
#define TD_UF      0x00000002       /* Underflow Error */
#define TD_DE      0x00000001       /* Deferred */

#define TD_IC      0x80000000       /* Interrupt On Completion */
#define TD_LS      0x40000000       /* Last Segment */
#define TD_FS      0x20000000       /* First Segment */
#define TD_FT1     0x10000000       /* Filtering Type */
#define TD_SET     0x08000000       /* Setup Packet */
#define TD_AC      0x04000000       /* Add CRC Disable */
#define TD_TER     0x02000000       /* Transmit End Of Ring */
#define TD_TCH     0x01000000       /* Second Address Chained */
#define TD_DPD     0x00800000       /* Disabled Padding */
#define TD_FT0     0x00400000       /* Filtering Type */
#define TD_TBS2    0x003ff800       /* Buffer 2 Size */
#define TD_TBS1    0x000007ff       /* Buffer 1 Size */

#define PERFECT_F  0x00000000
#define HASH_F     TD_FT0
#define INVERSE_F  TD_FT1
#define HASH_O_F   (TD_FT1 | TD_F0)

/*
** Media / mode state machine definitions
** User selectable:
*/
#define TP              0x0040     /* 10Base-T (now equiv to _10Mb)        */
#define TP_NW           0x0002     /* 10Base-T with Nway                   */
#define BNC             0x0004     /* Thinwire                             */
#define AUI             0x0008     /* Thickwire                            */
#define BNC_AUI         0x0010     /* BNC/AUI on DC21040 indistinguishable */
#define _10Mb           0x0040     /* 10Mb/s Ethernet                      */
#define _100Mb          0x0080     /* 100Mb/s Ethernet                     */
#define AUTO            0x4000     /* Auto sense the media or speed        */

/*
** Internal states
*/
#define NC              0x0000     /* No Connection                        */
#define ANS             0x0020     /* Intermediate AutoNegotiation State   */
#define SPD_DET         0x0100     /* Parallel speed detection             */
#define INIT            0x0200     /* Initial state                        */
#define EXT_SIA         0x0400     /* External SIA for motherboard chip    */
#define ANS_SUSPECT     0x0802     /* Suspect the ANS (TP) port is down    */
#define TP_SUSPECT      0x0803     /* Suspect the TP port is down          */
#define BNC_AUI_SUSPECT 0x0804     /* Suspect the BNC or AUI port is down  */
#define EXT_SIA_SUSPECT 0x0805     /* Suspect the EXT SIA port is down     */
#define BNC_SUSPECT     0x0806     /* Suspect the BNC port is down         */
#define AUI_SUSPECT     0x0807     /* Suspect the AUI port is down         */
#define MII             0x1000     /* MII on the 21143                     */

#define TIMER_CB        0x80000000 /* Timer callback detection             */

/*
** DE4X5 DEBUG Options
*/
#define DEBUG_NONE      0x0000     /* No DEBUG messages */
#define DEBUG_VERSION   0x0001     /* Print version message */
#define DEBUG_MEDIA     0x0002     /* Print media messages */
#define DEBUG_TX        0x0004     /* Print TX (queue_pkt) messages */
#define DEBUG_RX        0x0008     /* Print RX (de4x5_rx) messages */
#define DEBUG_SROM      0x0010     /* Print SROM messages */
#define DEBUG_MII       0x0020     /* Print MII messages */
#define DEBUG_OPEN      0x0040     /* Print de4x5_open() messages */
#define DEBUG_CLOSE     0x0080     /* Print de4x5_close() messages */
#define DEBUG_PCICFG    0x0100
#define DEBUG_ALL       0x01ff

/*
** Miscellaneous
*/
#define PCI  0
#define EISA 1

#define HASH_TABLE_LEN   512       /* Bits */
#define HASH_BITS        0x01ff    /* 9 LS bits */

#define SETUP_FRAME_LEN  192       /* Bytes */
#define IMPERF_PA_OFFSET 156       /* Bytes */

#define POLL_DEMAND          1

#define LOST_MEDIA_THRESHOLD 3

#define MASK_INTERRUPTS      1
#define UNMASK_INTERRUPTS    0

#define DE4X5_STRLEN         8

#define DE4X5_INIT           0     /* Initialisation time */
#define DE4X5_RUN            1     /* Run time */

#define DE4X5_SAVE_STATE     0
#define DE4X5_RESTORE_STATE  1

/*
** Address Filtering Modes
*/
#define PERFECT              0     /* 16 perfect physical addresses */
#define HASH_PERF            1     /* 1 perfect, 512 multicast addresses */
#define PERFECT_REJ          2     /* Reject 16 perfect physical addresses */
#define ALL_HASH             3     /* Hashes all physical & multicast addrs */

#define ALL                  0     /* Clear out all the setup frame */
#define PHYS_ADDR_ONLY       1     /* Update the physical address only */

/*
** Adapter state
*/
#define INITIALISED          0     /* After h/w initialised and mem alloc'd */
#define CLOSED               1     /* Ready for opening */
#define OPEN                 2     /* Running */

/*
** Various wait times
*/
#define PDET_LINK_WAIT    1200    /* msecs to wait for link detect bits     */
#define ANS_FINISH_WAIT   1000    /* msecs to wait for link detect bits     */

/*
** IEEE OUIs for various PHY vendor/chip combos - Reg 2 values only. Since
** the vendors seem split 50-50 on how to calculate the OUI register values
** anyway, just reading Reg2 seems reasonable for now [see de4x5_get_oui()].
*/
#define NATIONAL_TX 0x2000
#define BROADCOM_T4 0x03e0
#define SEEQ_T4     0x0016
#define CYPRESS_T4  0x0014

/*
** Speed Selection stuff
*/
#define SET_10Mb {\
  if ((lp->phy[lp->active].id) && (!lp->useSROM || lp->useMII)) {\
    omr = inl(DE4X5_OMR) & ~(OMR_TTM | OMR_PCS | OMR_SCR | OMR_FDX);\
    if ((lp->tmp != MII_SR_ASSC) || (lp->autosense != AUTO)) {\
      mii_wr(MII_CR_10|(lp->fdx?MII_CR_FDM:0), MII_CR, lp->phy[lp->active].addr, DE4X5_MII);\
    }\
    omr |= ((lp->fdx ? OMR_FDX : 0) | OMR_TTM);\
    outl(omr, DE4X5_OMR);\
    if (!lp->useSROM) lp->cache.gep = 0;\
  } else if (lp->useSROM && !lp->useMII) {\
    omr = (inl(DE4X5_OMR) & ~(OMR_PS | OMR_HBD | OMR_TTM | OMR_PCS | OMR_SCR | OMR_FDX));\
    omr |= (lp->fdx ? OMR_FDX : 0);\
    outl(omr | (lp->infoblock_csr6 & ~(OMR_SCR | OMR_HBD)), DE4X5_OMR);\
  } else {\
    omr = (inl(DE4X5_OMR) & ~(OMR_PS | OMR_HBD | OMR_TTM | OMR_PCS | OMR_SCR | OMR_FDX));\
    omr |= (lp->fdx ? OMR_FDX : 0);\
    outl(omr | OMR_SDP | OMR_TTM, DE4X5_OMR);\
    lp->cache.gep = (lp->fdx ? 0 : GEP_FDXD);\
    gep_wr(lp->cache.gep, dev);\
  }\
}

#define SET_100Mb {\
  if ((lp->phy[lp->active].id) && (!lp->useSROM || lp->useMII)) {\
    int fdx=0;\
    if (lp->phy[lp->active].id == NATIONAL_TX) {\
        mii_wr(mii_rd(0x18, lp->phy[lp->active].addr, DE4X5_MII) & ~0x2000,\
                      0x18, lp->phy[lp->active].addr, DE4X5_MII);\
    }\
    omr = inl(DE4X5_OMR) & ~(OMR_TTM | OMR_PCS | OMR_SCR | OMR_FDX);\
    sr = mii_rd(MII_SR, lp->phy[lp->active].addr, DE4X5_MII);\
    if (!(sr & MII_ANA_T4AM) && lp->fdx) fdx=1;\
    if ((lp->tmp != MII_SR_ASSC) || (lp->autosense != AUTO)) {\
      mii_wr(MII_CR_100|(fdx?MII_CR_FDM:0), MII_CR, lp->phy[lp->active].addr, DE4X5_MII);\
    }\
    if (fdx) omr |= OMR_FDX;\
    outl(omr, DE4X5_OMR);\
    if (!lp->useSROM) lp->cache.gep = 0;\
  } else if (lp->useSROM && !lp->useMII) {\
    omr = (inl(DE4X5_OMR) & ~(OMR_PS | OMR_HBD | OMR_TTM | OMR_PCS | OMR_SCR | OMR_FDX));\
    omr |= (lp->fdx ? OMR_FDX : 0);\
    outl(omr | lp->infoblock_csr6, DE4X5_OMR);\
  } else {\
    omr = (inl(DE4X5_OMR) & ~(OMR_PS | OMR_HBD | OMR_TTM | OMR_PCS | OMR_SCR | OMR_FDX));\
    omr |= (lp->fdx ? OMR_FDX : 0);\
    outl(omr | OMR_SDP | OMR_PS | OMR_HBD | OMR_PCS | OMR_SCR, DE4X5_OMR);\
    lp->cache.gep = (lp->fdx ? 0 : GEP_FDXD) | GEP_MODE;\
    gep_wr(lp->cache.gep, dev);\
  }\
}

/* FIX ME so I don't jam 10Mb networks */
#define SET_100Mb_PDET {\
  if ((lp->phy[lp->active].id) && (!lp->useSROM || lp->useMII)) {\
    mii_wr(MII_CR_100|MII_CR_ASSE, MII_CR, lp->phy[lp->active].addr, DE4X5_MII);\
    omr = (inl(DE4X5_OMR) & ~(OMR_TTM | OMR_PCS | OMR_SCR | OMR_FDX));\
    outl(omr, DE4X5_OMR);\
  } else if (lp->useSROM && !lp->useMII) {\
    omr = (inl(DE4X5_OMR) & ~(OMR_TTM | OMR_PCS | OMR_SCR | OMR_FDX));\
    outl(omr, DE4X5_OMR);\
  } else {\
    omr = (inl(DE4X5_OMR) & ~(OMR_PS | OMR_HBD | OMR_TTM | OMR_PCS | OMR_SCR | OMR_FDX));\
    outl(omr | OMR_SDP | OMR_PS | OMR_HBD | OMR_PCS, DE4X5_OMR);\
    lp->cache.gep = (GEP_FDXD | GEP_MODE);\
    gep_wr(lp->cache.gep, dev);\
  }\
}

/*
** Include the IOCTL stuff
*/
#include <linux/sockios.h>

#define	DE4X5IOCTL	SIOCDEVPRIVATE

struct de4x5_ioctl {
	unsigned short cmd;                /* Command to run */
	unsigned short len;                /* Length of the data buffer */
	unsigned char  __user *data;       /* Pointer to the data buffer */
};

/*
** Recognised commands for the driver
*/
#define DE4X5_GET_HWADDR	0x01 /* Get the hardware address */
#define DE4X5_SET_HWADDR	0x02 /* Set the hardware address */
#define DE4X5_SET_PROM  	0x03 /* Set Promiscuous Mode */
#define DE4X5_CLR_PROM  	0x04 /* Clear Promiscuous Mode */
#define DE4X5_SAY_BOO	        0x05 /* Say "Boo!" to the kernel log file */
#define DE4X5_GET_MCA   	0x06 /* Get a multicast address */
#define DE4X5_SET_MCA   	0x07 /* Set a multicast address */
#define DE4X5_CLR_MCA    	0x08 /* Clear a multicast address */
#define DE4X5_MCA_EN    	0x09 /* Enable a multicast address group */
#define DE4X5_GET_STATS  	0x0a /* Get the driver statistics */
#define DE4X5_CLR_STATS 	0x0b /* Zero out the driver statistics */
#define DE4X5_GET_OMR           0x0c /* Get the OMR Register contents */
#define DE4X5_SET_OMR           0x0d /* Set the OMR Register contents */
#define DE4X5_GET_REG           0x0e /* Get the DE4X5 Registers */

#define MOTO_SROM_BUG    (lp->active == 8 && (get_unaligned_le32(dev->dev_addr) & 0x00ffffff) == 0x3e0008)
