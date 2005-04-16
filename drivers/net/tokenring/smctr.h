/* smctr.h: SMC Token Ring driver header for Linux
 *
 * Authors:
 *  - Jay Schulist <jschlst@samba.org>
 */

#ifndef __LINUX_SMCTR_H
#define __LINUX_SMCTR_H

#ifdef __KERNEL__

#define MAX_TX_QUEUE 10

#define SMC_HEADER_SIZE 14

#define SMC_PAGE_OFFSET(X)          (((unsigned long)(X) - tp->ram_access) & tp->page_offset_mask)

#define INIT            0x0D
#define RQ_ATTCH        0x10
#define RQ_STATE        0x0F
#define RQ_ADDR         0x0E
#define CHG_PARM        0x0C
#define RSP             0x00
#define TX_FORWARD      0x09

#define AC_FC_DAT	((3<<13) | 1)
#define      DAT             0x07

#define RPT_NEW_MON     0x25
#define RPT_SUA_CHG     0x26
#define RPT_ACTIVE_ERR  0x28
#define RPT_NN_INCMP    0x27
#define RPT_ERROR       0x29

#define RQ_INIT         0x20
#define RPT_ATTCH       0x24
#define RPT_STATE       0x23
#define RPT_ADDR        0x22

#define POSITIVE_ACK                    0x0001
#define A_FRAME_WAS_FORWARDED           0x8888

#define      GROUP_ADDRESS                   0x2B
#define      PHYSICAL_DROP                   0x0B
#define      AUTHORIZED_ACCESS_PRIORITY      0x07
#define      AUTHORIZED_FUNCTION_CLASS       0x06
#define      FUNCTIONAL_ADDRESS              0x2C
#define      RING_STATION_STATUS             0x29
#define      TRANSMIT_STATUS_CODE            0x2A
#define      IBM_PASS_SOURCE_ADDR    0x01
#define      AC_FC_RPT_TX_FORWARD            ((0<<13) | 0)
#define      AC_FC_RPT_STATE                 ((0<<13) | 0)
#define      AC_FC_RPT_ADDR                  ((0<<13) | 0)
#define      CORRELATOR                      0x09

#define POSITIVE_ACK                    0x0001          /*             */
#define E_MAC_DATA_INCOMPLETE           0x8001          /* not used    */
#define E_VECTOR_LENGTH_ERROR           0x8002          /*             */
#define E_UNRECOGNIZED_VECTOR_ID        0x8003          /*             */
#define E_INAPPROPRIATE_SOURCE_CLASS    0x8004          /*             */
#define E_SUB_VECTOR_LENGTH_ERROR       0x8005          /*             */
#define E_TRANSMIT_FORWARD_INVALID      0x8006          /* def. by IBM */
#define E_MISSING_SUB_VECTOR            0x8007          /*             */
#define E_SUB_VECTOR_UNKNOWN            0x8008          /*             */
#define E_MAC_HEADER_TOO_LONG           0x8009          /*             */
#define E_FUNCTION_DISABLED             0x800A          /* not used    */

#define A_FRAME_WAS_FORWARDED           0x8888          /* used by send_TX_FORWARD */

#define UPSTREAM_NEIGHBOR_ADDRESS       0x02
#define LOCAL_RING_NUMBER               0x03
#define ASSIGN_PHYSICAL_DROP            0x04
#define ERROR_TIMER_VALUE               0x05
#define AUTHORIZED_FUNCTION_CLASS       0x06
#define AUTHORIZED_ACCESS_PRIORITY      0x07
#define CORRELATOR                      0x09
#define PHYSICAL_DROP                   0x0B
#define RESPONSE_CODE                   0x20
#define ADDRESS_MODIFER                 0x21
#define PRODUCT_INSTANCE_ID             0x22
#define RING_STATION_VERSION_NUMBER     0x23
#define WRAP_DATA                       0x26
#define FRAME_FORWARD                   0x27
#define STATION_IDENTIFER               0x28
#define RING_STATION_STATUS             0x29
#define TRANSMIT_STATUS_CODE            0x2A
#define GROUP_ADDRESS                   0x2B
#define FUNCTIONAL_ADDRESS              0x2C

#define F_NO_SUB_VECTORS_FOUND                  0x0000
#define F_UPSTREAM_NEIGHBOR_ADDRESS             0x0001
#define F_LOCAL_RING_NUMBER                     0x0002
#define F_ASSIGN_PHYSICAL_DROP                  0x0004
#define F_ERROR_TIMER_VALUE                     0x0008
#define F_AUTHORIZED_FUNCTION_CLASS             0x0010
#define F_AUTHORIZED_ACCESS_PRIORITY            0x0020
#define F_CORRELATOR                            0x0040
#define F_PHYSICAL_DROP                         0x0080
#define F_RESPONSE_CODE                         0x0100
#define F_PRODUCT_INSTANCE_ID                   0x0200
#define F_RING_STATION_VERSION_NUMBER           0x0400
#define F_STATION_IDENTIFER                     0x0800
#define F_RING_STATION_STATUS                   0x1000
#define F_GROUP_ADDRESS                         0x2000
#define F_FUNCTIONAL_ADDRESS                    0x4000
#define F_FRAME_FORWARD                         0x8000

#define R_INIT                                  0x00
#define R_RQ_ATTCH_STATE_ADDR                   0x00
#define R_CHG_PARM                              0x00
#define R_TX_FORWARD                            F_FRAME_FORWARD


#define      UPSTREAM_NEIGHBOR_ADDRESS       0x02
#define      ADDRESS_MODIFER                 0x21
#define      RING_STATION_VERSION_NUMBER     0x23
#define      PRODUCT_INSTANCE_ID             0x22

#define      RPT_TX_FORWARD  0x2A

#define AC_FC_INIT                      (3<<13) | 0 /*                     */
#define AC_FC_RQ_INIT                   ((3<<13) | 0) /*                     */
#define AC_FC_RQ_ATTCH                  (3<<13) | 0 /* DC = SC of rx frame */
#define AC_FC_RQ_STATE                  (3<<13) | 0 /* DC = SC of rx frame */
#define AC_FC_RQ_ADDR                   (3<<13) | 0 /* DC = SC of rx frame */
#define AC_FC_CHG_PARM                  (3<<13) | 0 /*                     */
#define AC_FC_RSP                       (0<<13) | 0 /* DC = SC of rx frame */
#define AC_FC_RPT_ATTCH                 (0<<13) | 0

#define S_UPSTREAM_NEIGHBOR_ADDRESS               6 + 2
#define S_LOCAL_RING_NUMBER                       2 + 2
#define S_ASSIGN_PHYSICAL_DROP                    4 + 2
#define S_ERROR_TIMER_VALUE                       2 + 2
#define S_AUTHORIZED_FUNCTION_CLASS               2 + 2
#define S_AUTHORIZED_ACCESS_PRIORITY              2 + 2
#define S_CORRELATOR                              2 + 2
#define S_PHYSICAL_DROP                           4 + 2
#define S_RESPONSE_CODE                           4 + 2
#define S_ADDRESS_MODIFER                         2 + 2
#define S_PRODUCT_INSTANCE_ID                    18 + 2
#define S_RING_STATION_VERSION_NUMBER            10 + 2
#define S_STATION_IDENTIFER                       6 + 2
#define S_RING_STATION_STATUS                     6 + 2
#define S_GROUP_ADDRESS                           4 + 2
#define S_FUNCTIONAL_ADDRESS                      4 + 2
#define S_FRAME_FORWARD                         252 + 2
#define S_TRANSMIT_STATUS_CODE                    2 + 2

#define ISB_IMC_RES0                    0x0000  /* */
#define ISB_IMC_MAC_TYPE_3              0x0001  /* MAC_ARC_INDICATE */
#define ISB_IMC_MAC_ERROR_COUNTERS      0x0002  /* */
#define ISB_IMC_RES1                    0x0003  /* */
#define ISB_IMC_MAC_TYPE_2              0x0004  /* QUE_MAC_INDICATE */
#define ISB_IMC_TX_FRAME                0x0005  /* */
#define ISB_IMC_END_OF_TX_QUEUE         0x0006  /* */
#define ISB_IMC_NON_MAC_RX_RESOURCE     0x0007  /* */
#define ISB_IMC_MAC_RX_RESOURCE         0x0008  /* */
#define ISB_IMC_NON_MAC_RX_FRAME        0x0009  /* */
#define ISB_IMC_MAC_RX_FRAME            0x000A  /* */
#define ISB_IMC_TRC_FIFO_STATUS         0x000B  /* */
#define ISB_IMC_COMMAND_STATUS          0x000C  /* */
#define ISB_IMC_MAC_TYPE_1              0x000D  /* Self Removed */
#define ISB_IMC_TRC_INTRNL_TST_STATUS   0x000E  /* */
#define ISB_IMC_RES2                    0x000F  /* */

#define NON_MAC_RX_RESOURCE_BW          0x10    /* shifted right 8 bits */
#define NON_MAC_RX_RESOURCE_FW          0x20    /* shifted right 8 bits */
#define NON_MAC_RX_RESOURCE_BE          0x40    /* shifted right 8 bits */
#define NON_MAC_RX_RESOURCE_FE          0x80    /* shifted right 8 bits */
#define RAW_NON_MAC_RX_RESOURCE_BW      0x1000  /* */
#define RAW_NON_MAC_RX_RESOURCE_FW      0x2000  /* */
#define RAW_NON_MAC_RX_RESOURCE_BE      0x4000  /* */
#define RAW_NON_MAC_RX_RESOURCE_FE      0x8000  /* */

#define MAC_RX_RESOURCE_BW              0x10    /* shifted right 8 bits */
#define MAC_RX_RESOURCE_FW              0x20    /* shifted right 8 bits */
#define MAC_RX_RESOURCE_BE              0x40    /* shifted right 8 bits */
#define MAC_RX_RESOURCE_FE              0x80    /* shifted right 8 bits */
#define RAW_MAC_RX_RESOURCE_BW          0x1000  /* */
#define RAW_MAC_RX_RESOURCE_FW          0x2000  /* */
#define RAW_MAC_RX_RESOURCE_BE          0x4000  /* */
#define RAW_MAC_RX_RESOURCE_FE          0x8000  /* */

#define TRC_FIFO_STATUS_TX_UNDERRUN     0x40    /* shifted right 8 bits */
#define TRC_FIFO_STATUS_RX_OVERRUN      0x80    /* shifted right 8 bits */
#define RAW_TRC_FIFO_STATUS_TX_UNDERRUN 0x4000  /* */
#define RAW_TRC_FIFO_STATUS_RX_OVERRUN  0x8000  /* */

#define       CSR_CLRTINT             0x08

#define MSB(X)                  ((__u8)((__u16) X >> 8))
#define LSB(X)                  ((__u8)((__u16) X &  0xff))

#define AC_FC_LOBE_MEDIA_TEST           ((3<<13) | 0)
#define S_WRAP_DATA                             248 + 2 /* 500 + 2 */
#define      WRAP_DATA                       0x26
#define LOBE_MEDIA_TEST 0x08

/* Destination Class (dc) */

#define DC_MASK         0xF0
#define DC_RS           0x00
#define DC_CRS          0x40
#define DC_RPS          0x50
#define DC_REM          0x60

/* Source Classes (sc) */

#define SC_MASK         0x0F
#define SC_RS           0x00
#define SC_CRS          0x04
#define SC_RPS          0x05
#define SC_REM          0x06

#define PR		0x11
#define PR_PAGE_MASK	0x0C000

#define MICROCHANNEL	0x0008
#define INTERFACE_CHIP	0x0010
#define BOARD_16BIT	0x0040
#define PAGED_RAM	0x0080
#define WD8115TA	(TOKEN_MEDIA | MICROCHANNEL | INTERFACE_CHIP | PAGED_RAM)
#define WD8115T		(TOKEN_MEDIA | INTERFACE_CHIP | BOARD_16BIT | PAGED_RAM)

#define BRD_ID_8316	0x50

#define r587_SER	0x001
#define SER_DIN		0x80
#define SER_DOUT	0x40
#define SER_CLK		0x20
#define SER_ECS		0x10
#define SER_E806	0x08
#define SER_PNP		0x04
#define SER_BIO		0x02
#define SER_16B		0x01

#define r587_IDR	0x004
#define IDR_IRQ_MASK	0x0F0
#define IDR_DCS_MASK	0x007
#define IDR_RWS		0x008


#define r587_BIO	0x003
#define BIO_ENB		0x080
#define BIO_MASK	0x03F

#define r587_PCR	0x005
#define PCR_RAMS	0x040



#define NUM_ADDR_BITS	8

#define ISA_MAX_ADDRESS		0x00ffffff

#define SMCTR_MAX_ADAPTERS	7

#define MC_TABLE_ENTRIES      16

#define MAXFRAGMENTS          32

#define CHIP_REV_MASK         0x3000

#define MAX_TX_QS             8
#define NUM_TX_QS_USED        3

#define MAX_RX_QS             2
#define NUM_RX_QS_USED        2

#define INTEL_DATA_FORMAT	0x4000
#define INTEL_ADDRESS_POINTER_FORMAT	0x8000
#define PAGE_POINTER(X)		((((unsigned long)(X) - tp->ram_access) & tp->page_offset_mask) + tp->ram_access)
#define SWAP_WORDS(X)		(((X & 0xFFFF) << 16) | (X >> 16))

#define INTERFACE_CHIP          0x0010          /* Soft Config Adapter */
#define ADVANCED_FEATURES       0x0020          /* Adv. netw. interface features */
#define BOARD_16BIT             0x0040          /* 16 bit capability */
#define PAGED_RAM               0x0080          /* Adapter has paged RAM */

#define PAGED_ROM               0x0100          /* Adapter has paged ROM */

#define RAM_SIZE_UNKNOWN        0x0000          /* Unknown RAM size */
#define RAM_SIZE_0K             0x0001          /* 0K  RAM */
#define RAM_SIZE_8K             0x0002          /* 8k  RAM */
#define RAM_SIZE_16K            0x0003          /* 16k RAM */
#define RAM_SIZE_32K            0x0004          /* 32k RAM */
#define RAM_SIZE_64K            0x0005          /* 64k RAM */
#define RAM_SIZE_RESERVED_6     0x0006          /* Reserved RAM size */
#define RAM_SIZE_RESERVED_7     0x0007          /* Reserved RAM size */
#define RAM_SIZE_MASK           0x0007          /* Isolates RAM Size */

#define TOKEN_MEDIA           0x0005

#define BID_REG_0       0x00
#define BID_REG_1       0x01
#define BID_REG_2       0x02
#define BID_REG_3       0x03
#define BID_REG_4       0x04
#define BID_REG_5       0x05
#define BID_REG_6       0x06
#define BID_REG_7       0x07
#define BID_LAR_0       0x08
#define BID_LAR_1       0x09
#define BID_LAR_2       0x0A
#define BID_LAR_3       0x0B
#define BID_LAR_4       0x0C
#define BID_LAR_5       0x0D

#define BID_BOARD_ID_BYTE       0x0E
#define BID_CHCKSM_BYTE         0x0F
#define BID_LAR_OFFSET          0x08  

#define BID_MSZ_583_BIT         0x08
#define BID_SIXTEEN_BIT_BIT     0x01

#define BID_BOARD_REV_MASK      0x1E

#define BID_MEDIA_TYPE_BIT      0x01
#define BID_SOFT_CONFIG_BIT     0x20
#define BID_RAM_SIZE_BIT        0x40
#define BID_BUS_TYPE_BIT        0x80

#define BID_CR          0x10

#define BID_TXP         0x04            /* Transmit Packet Command */

#define BID_TCR_DIFF    0x0D    /* Transmit Configuration Register */

#define BID_TCR_VAL     0x18            /* Value to Test 8390 or 690 */
#define BID_PS0         0x00            /* Register Page Select 0 */
#define BID_PS1         0x40            /* Register Page Select 1 */
#define BID_PS2         0x80            /* Register Page Select 2 */
#define BID_PS_MASK     0x3F            /* For Masking Off Page Select Bits */

#define BID_EEPROM_0                    0x08
#define BID_EEPROM_1                    0x09
#define BID_EEPROM_2                    0x0A
#define BID_EEPROM_3                    0x0B
#define BID_EEPROM_4                    0x0C
#define BID_EEPROM_5                    0x0D
#define BID_EEPROM_6                    0x0E
#define BID_EEPROM_7                    0x0F

#define BID_OTHER_BIT                   0x02
#define BID_ICR_MASK                    0x0C
#define BID_EAR_MASK                    0x0F
#define BID_ENGR_PAGE                   0x0A0
#define BID_RLA                         0x10
#define BID_EA6                         0x80
#define BID_RECALL_DONE_MASK            0x10
#define BID_BID_EEPROM_OVERRIDE         0xFFB0
#define BID_EXTRA_EEPROM_OVERRIDE       0xFFD0
#define BID_EEPROM_MEDIA_MASK           0x07
#define BID_STARLAN_TYPE                0x00
#define BID_ETHERNET_TYPE               0x01
#define BID_TP_TYPE                     0x02
#define BID_EW_TYPE                     0x03
#define BID_TOKEN_RING_TYPE             0x04
#define BID_UTP2_TYPE                   0x05
#define BID_EEPROM_IRQ_MASK             0x18
#define BID_PRIMARY_IRQ                 0x00
#define BID_ALTERNATE_IRQ_1             0x08
#define BID_ALTERNATE_IRQ_2             0x10
#define BID_ALTERNATE_IRQ_3             0x18
#define BID_EEPROM_RAM_SIZE_MASK        0xE0
#define BID_EEPROM_RAM_SIZE_RES1        0x00
#define BID_EEPROM_RAM_SIZE_RES2        0x20
#define BID_EEPROM_RAM_SIZE_8K          0x40
#define BID_EEPROM_RAM_SIZE_16K         0x60
#define BID_EEPROM_RAM_SIZE_32K         0x80
#define BID_EEPROM_RAM_SIZE_64K         0xA0
#define BID_EEPROM_RAM_SIZE_RES3        0xC0
#define BID_EEPROM_RAM_SIZE_RES4        0xE0
#define BID_EEPROM_BUS_TYPE_MASK        0x07
#define BID_EEPROM_BUS_TYPE_AT          0x00
#define BID_EEPROM_BUS_TYPE_MCA         0x01
#define BID_EEPROM_BUS_TYPE_EISA        0x02
#define BID_EEPROM_BUS_TYPE_NEC         0x03
#define BID_EEPROM_BUS_SIZE_MASK        0x18
#define BID_EEPROM_BUS_SIZE_8BIT        0x00
#define BID_EEPROM_BUS_SIZE_16BIT       0x08
#define BID_EEPROM_BUS_SIZE_32BIT       0x10
#define BID_EEPROM_BUS_SIZE_64BIT       0x18
#define BID_EEPROM_BUS_MASTER           0x20
#define BID_EEPROM_RAM_PAGING           0x40
#define BID_EEPROM_ROM_PAGING           0x80
#define BID_EEPROM_PAGING_MASK          0xC0
#define BID_EEPROM_LOW_COST             0x08
#define BID_EEPROM_IO_MAPPED            0x10
#define BID_EEPROM_HMI                  0x01
#define BID_EEPROM_AUTO_MEDIA_DETECT    0x01
#define BID_EEPROM_CHIP_REV_MASK        0x0C

#define BID_EEPROM_LAN_ADDR             0x30

#define BID_EEPROM_MEDIA_OPTION         0x54
#define BID_EEPROM_MEDIA_UTP            0x01
#define BID_EEPROM_4MB_RING             0x08
#define BID_EEPROM_16MB_RING            0x10
#define BID_EEPROM_MEDIA_STP            0x40

#define BID_EEPROM_MISC_DATA            0x56
#define BID_EEPROM_EARLY_TOKEN_RELEASE  0x02

#define CNFG_ID_8003E           0x6fc0
#define CNFG_ID_8003S           0x6fc1
#define CNFG_ID_8003W           0x6fc2
#define CNFG_ID_8115TRA         0x6ec6
#define CNFG_ID_8013E           0x61C8
#define CNFG_ID_8013W           0x61C9
#define CNFG_ID_BISTRO03E       0xEFE5
#define CNFG_ID_BISTRO13E       0xEFD5
#define CNFG_ID_BISTRO13W       0xEFD4
#define CNFG_MSR_583    0x0
#define CNFG_ICR_583    0x1
#define CNFG_IAR_583    0x2
#define CNFG_BIO_583    0x3
#define CNFG_EAR_583    0x3
#define CNFG_IRR_583    0x4
#define CNFG_LAAR_584   0x5
#define CNFG_GP2                0x7
#define CNFG_LAAR_MASK          0x1F
#define CNFG_LAAR_ZWS           0x20
#define CNFG_LAAR_L16E          0x40
#define CNFG_ICR_IR2_584        0x04
#define CNFG_ICR_MASK       0x08
#define CNFG_ICR_MSZ        0x08
#define CNFG_ICR_RLA        0x10
#define CNFG_ICR_STO        0x80
#define CNFG_IRR_IRQS           0x60
#define CNFG_IRR_IEN            0x80
#define CNFG_IRR_ZWS            0x01
#define CNFG_GP2_BOOT_NIBBLE    0x0F
#define CNFG_IRR_OUT2       0x04
#define CNFG_IRR_OUT1       0x02

#define CNFG_SIZE_8KB           8
#define CNFG_SIZE_16KB          16
#define CNFG_SIZE_32KB          32
#define CNFG_SIZE_64KB          64
#define CNFG_SIZE_128KB     128
#define CNFG_SIZE_256KB     256
#define ROM_DISABLE             0x0

#define CNFG_SLOT_ENABLE_BIT    0x08

#define CNFG_POS_CONTROL_REG    0x096
#define CNFG_POS_REG0           0x100
#define CNFG_POS_REG1           0x101
#define CNFG_POS_REG2           0x102
#define CNFG_POS_REG3           0x103
#define CNFG_POS_REG4           0x104
#define CNFG_POS_REG5           0x105

#define CNFG_ADAPTER_TYPE_MASK  0x0e

#define SLOT_16BIT              0x0008
#define INTERFACE_5X3_CHIP      0x0000          /* 0000 = 583 or 593 chips */
#define NIC_690_BIT                     0x0010          /* NIC is 690 */
#define ALTERNATE_IRQ_BIT       0x0020          /* Alternate IRQ is used */
#define INTERFACE_584_CHIP      0x0040          /* 0001 = 584 chip */
#define INTERFACE_594_CHIP      0x0080          /* 0010 = 594 chip */
#define INTERFACE_585_CHIP      0x0100          /* 0100 = 585/790 chip */
#define INTERFACE_CHIP_MASK     0x03C0          /* Isolates Intfc Chip Type */

#define BOARD_16BIT             0x0040
#define NODE_ADDR_CKSUM 	0xEE
#define BRD_ID_8115T    	0x04

#define NIC_825_BIT             0x0400          /* TRC 83C825 NIC */
#define NIC_790_BIT             0x0800          /* NIC is 83C790 Ethernet */

#define CHIP_REV_MASK           0x3000

#define HWR_CBUSY			0x02
#define HWR_CA				0x01

#define MAC_QUEUE                       0
#define NON_MAC_QUEUE                   1
#define BUG_QUEUE                       2       /* NO RECEIVE QUEUE, ONLY TX */

#define NUM_MAC_TX_FCBS                 8
#define NUM_MAC_TX_BDBS                 NUM_MAC_TX_FCBS
#define NUM_MAC_RX_FCBS                 7
#define NUM_MAC_RX_BDBS                 8

#define NUM_NON_MAC_TX_FCBS             6
#define NUM_NON_MAC_TX_BDBS             NUM_NON_MAC_TX_FCBS

#define NUM_NON_MAC_RX_BDBS             0       /* CALCULATED DYNAMICALLY */

#define NUM_BUG_TX_FCBS                 8
#define NUM_BUG_TX_BDBS                 NUM_BUG_TX_FCBS

#define MAC_TX_BUFFER_MEMORY            1024
#define NON_MAC_TX_BUFFER_MEMORY        (20 * 1024)
#define BUG_TX_BUFFER_MEMORY            (NUM_BUG_TX_FCBS * 32)

#define RX_BUFFER_MEMORY                0       /* CALCULATED DYNAMICALLY */
#define RX_DATA_BUFFER_SIZE             256
#define RX_BDB_SIZE_SHIFT               3       /* log2(RX_DATA_BUFFER_SIZE)-log2(sizeof(BDBlock)) */
#define RX_BDB_SIZE_MASK                (sizeof(BDBlock) - 1)
#define RX_DATA_BUFFER_SIZE_MASK        (RX_DATA_BUFFER_SIZE-1)

#define NUM_OF_INTERRUPTS               0x20

#define NOT_TRANSMITING                 0
#define TRANSMITING			1

#define TRC_INTERRUPT_ENABLE_MASK       0x7FF6

#define UCODE_VERSION                   0x58

#define UCODE_SIZE_OFFSET               0x0000  /* WORD */
#define UCODE_CHECKSUM_OFFSET           0x0002  /* WORD */
#define UCODE_VERSION_OFFSET            0x0004  /* BYTE */

#define CS_RAM_SIZE                     0X2000
#define CS_RAM_CHECKSUM_OFFSET          0x1FFE  /* WORD 1FFE(MSB)-1FFF(LSB)*/
#define CS_RAM_VERSION_OFFSET           0x1FFC  /* WORD 1FFC(MSB)-1FFD(LSB)*/

#define MISC_DATA_SIZE                  128
#define NUM_OF_ACBS                     1

#define ACB_COMMAND_NOT_DONE            0x0000  /* Init, command not done */
#define ACB_COMMAND_DONE                0x8000  /* TRC says command done */
#define ACB_COMMAND_STATUS_MASK         0x00FF  /* low byte is status */
#define ACB_COMMAND_SUCCESSFUL          0x0000  /* means cmd was successful */
#define ACB_NOT_CHAIN_END               0x0000  /* tell TRC more CBs in chain */
#define ACB_CHAIN_END                   0x8000  /* tell TRC last CB in chain */
#define ACB_COMMAND_NO_INTERRUPT        0x0000  /* tell TRC no INT after CB */
#define ACB_COMMAND_INTERRUPT           0x2000  /* tell TRC to INT after CB */
#define ACB_SUB_CMD_NOP                 0x0000
#define ACB_CMD_HIC_NOP                 0x0080
#define ACB_CMD_MCT_NOP                 0x0000
#define ACB_CMD_MCT_TEST                0x0001
#define ACB_CMD_HIC_TEST                0x0081
#define ACB_CMD_INSERT                  0x0002
#define ACB_CMD_REMOVE                  0x0003
#define ACB_CMD_MCT_WRITE_VALUE         0x0004
#define ACB_CMD_HIC_WRITE_VALUE         0x0084
#define ACB_CMD_MCT_READ_VALUE          0x0005
#define ACB_CMD_HIC_READ_VALUE          0x0085
#define ACB_CMD_INIT_TX_RX              0x0086
#define ACB_CMD_INIT_TRC_TIMERS         0x0006
#define ACB_CMD_READ_TRC_STATUS         0x0007
#define ACB_CMD_CHANGE_JOIN_STATE       0x0008
#define ACB_CMD_RESERVED_9              0x0009
#define ACB_CMD_RESERVED_A              0x000A
#define ACB_CMD_RESERVED_B              0x000B
#define ACB_CMD_RESERVED_C              0x000C
#define ACB_CMD_RESERVED_D              0x000D
#define ACB_CMD_RESERVED_E              0x000E
#define ACB_CMD_RESERVED_F              0x000F

#define TRC_MAC_REGISTERS_TEST          0x0000
#define TRC_INTERNAL_LOOPBACK           0x0001
#define TRC_TRI_LOOPBACK                0x0002
#define TRC_INTERNAL_ROM_TEST           0x0003
#define TRC_LOBE_MEDIA_TEST             0x0004
#define TRC_ANALOG_TEST                 0x0005
#define TRC_HOST_INTERFACE_REG_TEST     0x0003

#define TEST_DMA_1                      0x0000
#define TEST_DMA_2                      0x0001
#define TEST_MCT_ROM                    0x0002
#define HIC_INTERNAL_DIAG               0x0003

#define ABORT_TRANSMIT_PRIORITY_0       0x0001
#define ABORT_TRANSMIT_PRIORITY_1       0x0002
#define ABORT_TRANSMIT_PRIORITY_2       0x0004
#define ABORT_TRANSMIT_PRIORITY_3       0x0008
#define ABORT_TRANSMIT_PRIORITY_4       0x0010
#define ABORT_TRANSMIT_PRIORITY_5       0x0020
#define ABORT_TRANSMIT_PRIORITY_6       0x0040
#define ABORT_TRANSMIT_PRIORITY_7       0x0080

#define TX_PENDING_PRIORITY_0           0x0001
#define TX_PENDING_PRIORITY_1           0x0002
#define TX_PENDING_PRIORITY_2           0x0004
#define TX_PENDING_PRIORITY_3           0x0008
#define TX_PENDING_PRIORITY_4           0x0010
#define TX_PENDING_PRIORITY_5           0x0020
#define TX_PENDING_PRIORITY_6           0x0040
#define TX_PENDING_PRIORITY_7           0x0080

#define FCB_FRAME_LENGTH                0x100
#define FCB_COMMAND_DONE                0x8000  /* FCB Word 0 */
#define FCB_NOT_CHAIN_END               0x0000  /* FCB Word 1 */
#define FCB_CHAIN_END                   0x8000
#define FCB_NO_WARNING                  0x0000
#define FCB_WARNING                     0x4000
#define FCB_INTERRUPT_DISABLE           0x0000
#define FCB_INTERRUPT_ENABLE            0x2000

#define FCB_ENABLE_IMA                  0x0008
#define FCB_ENABLE_TES                  0x0004  /* Guarantee Tx before Int */
#define FCB_ENABLE_TFS                  0x0002  /* Post Tx Frame Status */
#define FCB_ENABLE_NTC                  0x0001  /* No Tx CRC */

#define FCB_TX_STATUS_CR2               0x0004
#define FCB_TX_STATUS_AR2               0x0008
#define FCB_TX_STATUS_CR1               0x0040
#define FCB_TX_STATUS_AR1               0x0080
#define FCB_TX_AC_BITS                  (FCB_TX_STATUS_AR1+FCB_TX_STATUS_AR2+FCB_TX_STATUS_CR1+FCB_TX_STATUS_CR2)
#define FCB_TX_STATUS_E                 0x0100

#define FCB_RX_STATUS_ANY_ERROR         0x0001
#define FCB_RX_STATUS_FCS_ERROR         0x0002

#define FCB_RX_STATUS_IA_MATCHED        0x0400
#define FCB_RX_STATUS_IGA_BSGA_MATCHED  0x0500
#define FCB_RX_STATUS_FA_MATCHED        0x0600
#define FCB_RX_STATUS_BA_MATCHED        0x0700
#define FCB_RX_STATUS_DA_MATCHED        0x0400
#define FCB_RX_STATUS_SOURCE_ROUTING    0x0800

#define BDB_BUFFER_SIZE                 0x100
#define BDB_NOT_CHAIN_END               0x0000
#define BDB_CHAIN_END                   0x8000
#define BDB_NO_WARNING                  0x0000
#define BDB_WARNING                     0x4000

#define ERROR_COUNTERS_CHANGED          0x0001
#define TI_NDIS_RING_STATUS_CHANGED     0x0002
#define UNA_CHANGED                     0x0004
#define READY_TO_SEND_RQ_INIT           0x0008

#define SCGB_ADDRESS_POINTER_FORMAT     INTEL_ADDRESS_POINTER_FORMAT
#define SCGB_DATA_FORMAT                INTEL_DATA_FORMAT
#define SCGB_MULTI_WORD_CONTROL         0
#define SCGB_BURST_LENGTH               0x000E  /* DMA Burst Length */

#define SCGB_CONFIG                     (INTEL_ADDRESS_POINTER_FORMAT+INTEL_DATA_FORMAT+SCGB_BURST_LENGTH)

#define ISCP_BLOCK_SIZE                 0x0A
#define RAM_SIZE                        0x10000
#define INIT_SYS_CONFIG_PTR_OFFSET      (RAM_SIZE-ISCP_BLOCK_SIZE)
#define SCGP_BLOCK_OFFSET               0

#define SCLB_NOT_VALID                  0x0000  /* Initially, SCLB not valid */
#define SCLB_VALID                      0x8000  /* Host tells TRC SCLB valid */
#define SCLB_PROCESSED                  0x0000  /* TRC says SCLB processed */
#define SCLB_RESUME_CONTROL_NOT_VALID   0x0000  /* Initially, RC not valid */
#define SCLB_RESUME_CONTROL_VALID       0x4000  /* Host tells TRC RC valid */
#define SCLB_IACK_CODE_NOT_VALID        0x0000  /* Initially, IACK not valid */
#define SCLB_IACK_CODE_VALID            0x2000  /* Host tells TRC IACK valid */
#define SCLB_CMD_NOP                    0x0000
#define SCLB_CMD_REMOVE                 0x0001
#define SCLB_CMD_SUSPEND_ACB_CHAIN      0x0002
#define SCLB_CMD_SET_INTERRUPT_MASK     0x0003
#define SCLB_CMD_CLEAR_INTERRUPT_MASK   0x0004
#define SCLB_CMD_RESERVED_5             0x0005
#define SCLB_CMD_RESERVED_6             0x0006
#define SCLB_CMD_RESERVED_7             0x0007
#define SCLB_CMD_RESERVED_8             0x0008
#define SCLB_CMD_RESERVED_9             0x0009
#define SCLB_CMD_RESERVED_A             0x000A
#define SCLB_CMD_RESERVED_B             0x000B
#define SCLB_CMD_RESERVED_C             0x000C
#define SCLB_CMD_RESERVED_D             0x000D
#define SCLB_CMD_RESERVED_E             0x000E
#define SCLB_CMD_RESERVED_F             0x000F

#define SCLB_RC_ACB                     0x0001  /* Action Command Block Chain */
#define SCLB_RC_RES0                    0x0002  /* Always Zero */
#define SCLB_RC_RES1                    0x0004  /* Always Zero */
#define SCLB_RC_RES2                    0x0008  /* Always Zero */
#define SCLB_RC_RX_MAC_FCB              0x0010  /* RX_MAC_FCB Chain */
#define SCLB_RC_RX_MAC_BDB              0x0020  /* RX_MAC_BDB Chain */
#define SCLB_RC_RX_NON_MAC_FCB          0x0040  /* RX_NON_MAC_FCB Chain */
#define SCLB_RC_RX_NON_MAC_BDB          0x0080  /* RX_NON_MAC_BDB Chain */
#define SCLB_RC_TFCB0                   0x0100  /* TX Priority 0 FCB Chain */
#define SCLB_RC_TFCB1                   0x0200  /* TX Priority 1 FCB Chain */
#define SCLB_RC_TFCB2                   0x0400  /* TX Priority 2 FCB Chain */
#define SCLB_RC_TFCB3                   0x0800  /* TX Priority 3 FCB Chain */
#define SCLB_RC_TFCB4                   0x1000  /* TX Priority 4 FCB Chain */
#define SCLB_RC_TFCB5                   0x2000  /* TX Priority 5 FCB Chain */
#define SCLB_RC_TFCB6                   0x4000  /* TX Priority 6 FCB Chain */
#define SCLB_RC_TFCB7                   0x8000  /* TX Priority 7 FCB Chain */

#define SCLB_IMC_RES0                   0x0001  /* */
#define SCLB_IMC_MAC_TYPE_3             0x0002  /* MAC_ARC_INDICATE */
#define SCLB_IMC_MAC_ERROR_COUNTERS     0x0004  /* */
#define SCLB_IMC_RES1                   0x0008  /* */
#define SCLB_IMC_MAC_TYPE_2             0x0010  /* QUE_MAC_INDICATE */
#define SCLB_IMC_TX_FRAME               0x0020  /* */
#define SCLB_IMC_END_OF_TX_QUEUE        0x0040  /* */
#define SCLB_IMC_NON_MAC_RX_RESOURCE    0x0080  /* */
#define SCLB_IMC_MAC_RX_RESOURCE        0x0100  /* */
#define SCLB_IMC_NON_MAC_RX_FRAME       0x0200  /* */
#define SCLB_IMC_MAC_RX_FRAME           0x0400  /* */
#define SCLB_IMC_TRC_FIFO_STATUS        0x0800  /* */
#define SCLB_IMC_COMMAND_STATUS         0x1000  /* */
#define SCLB_IMC_MAC_TYPE_1             0x2000  /* Self Removed */
#define SCLB_IMC_TRC_INTRNL_TST_STATUS  0x4000  /* */
#define SCLB_IMC_RES2                   0x8000  /* */

#define DMA_TRIGGER                     0x0004
#define FREQ_16MB_BIT                   0x0010
#define THDREN                          0x0020
#define CFG0_RSV1                       0x0040
#define CFG0_RSV2                       0x0080
#define ETREN                           0x0100
#define RX_OWN_BIT                      0x0200
#define RXATMAC                         0x0400
#define PROMISCUOUS_BIT                 0x0800
#define USETPT                          0x1000
#define SAVBAD_BIT                      0x2000
#define ONEQUE                          0x4000
#define NO_AUTOREMOVE                   0x8000

#define RX_FCB_AREA_8316        0x00000000
#define RX_BUFF_AREA_8316       0x00000000

#define TRC_POINTER(X)          ((unsigned long)(X) - tp->ram_access)
#define RX_FCB_TRC_POINTER(X)   ((unsigned long)(X) - tp->ram_access + RX_FCB_AREA_8316)
#define RX_BUFF_TRC_POINTER(X) ((unsigned long)(X) - tp->ram_access + RX_BUFF_AREA_8316)

// Offset 0: MSR - Memory Select Register
//
#define r587_MSR        0x000   // Register Offset
//#define       MSR_RST         0x080   // LAN Controller Reset
#define MSR_MENB        0x040   // Shared Memory Enable
#define MSR_RA18        0x020   // Ram Address bit 18   (583, 584, 587)
#define MSR_RA17        0x010   // Ram Address bit 17   (583, 584, 585/790)
#define MSR_RA16        0x008   // Ram Address bit 16   (583, 584, 585/790)
#define MSR_RA15        0x004   // Ram Address bit 15   (583, 584, 585/790)
#define MSR_RA14        0x002   // Ram Address bit 14   (583, 584, 585/790)
#define MSR_RA13        0x001   // Ram Address bit 13   (583, 584, 585/790)

#define MSR_MASK        0x03F   // Mask for Address bits RA18-RA13 (583, 584, 587)

#define MSR                     0x00
#define IRR                     0x04
#define HWR                     0x04
#define LAAR                    0x05
#define IMCCR                   0x05
#define LAR0                    0x08
#define BDID                    0x0E    // Adapter ID byte register offset
#define CSR                     0x10
#define PR                      0x11

#define MSR_RST                 0x80
#define MSR_MEMB                0x40
#define MSR_0WS                 0x20

#define FORCED_16BIT_MODE       0x0002

#define INTERFRAME_SPACING_16           0x0003  /* 6 bytes */
#define INTERFRAME_SPACING_4            0x0001  /* 2 bytes */
#define MULTICAST_ADDRESS_BIT           0x0010
#define NON_SRC_ROUTING_BIT             0x0020

#define LOOPING_MODE_MASK       0x0007

/*
 * Decode firmware defines.
 */
#define SWAP_BYTES(X)		((X & 0xff) << 8) | (X >> 8)
#define WEIGHT_OFFSET		5
#define TREE_SIZE_OFFSET	9
#define TREE_OFFSET		11

/* The Huffman Encoding Tree is constructed of these nodes. */
typedef struct {
	__u8	llink;	/* Short version of above node. */
	__u8	tag;
	__u8	info;	/* This node is used on decodes. */
	__u8	rlink;
} DECODE_TREE_NODE;

#define ROOT	0	/* Branch value. */
#define LEAF	0	/* Tag field value. */
#define BRANCH	1	/* Tag field value. */

/*
 * Multicast Table Structure
 */
typedef struct {
        __u8    address[6];
        __u8    instance_count;
} McTable;

/*
 * Fragment Descriptor Definition
 */
typedef struct {
        __u8  *fragment_ptr;
        __u32   fragment_length;
} FragmentStructure;

/*
 * Data Buffer Structure Definition
 */
typedef struct {
        __u32 fragment_count;
        FragmentStructure       fragment_list[MAXFRAGMENTS];
} DataBufferStructure;

#pragma pack(1)
typedef struct {
                __u8    IType;
                __u8    ISubtype;
} Interrupt_Status_Word;

#pragma pack(1)
typedef struct BDBlockType {
                __u16                   info;                   /* 02 */
                __u32                   trc_next_ptr;           /* 06 */
                __u32                   trc_data_block_ptr;     /* 10 */
                __u16                   buffer_length;          /* 12 */

                __u16                   *data_block_ptr;        /* 16 */
                struct  BDBlockType     *next_ptr;              /* 20 */
                struct  BDBlockType     *back_ptr;              /* 24 */
                __u8                    filler[8];              /* 32 */
} BDBlock;

#pragma pack(1)
typedef struct FCBlockType {
                __u16                   frame_status;           /* 02 */
                __u16                   info;                   /* 04 */
                __u32                   trc_next_ptr;           /* 08 */
                __u32                   trc_bdb_ptr;            /* 12 */
                __u16                   frame_length;           /* 14 */

                BDBlock                 *bdb_ptr;               /* 18 */
                struct  FCBlockType     *next_ptr;              /* 22 */
                struct  FCBlockType     *back_ptr;              /* 26 */
                __u16                   memory_alloc;           /* 28 */
                __u8                    filler[4];              /* 32 */

} FCBlock;

#pragma pack(1)
typedef struct SBlockType{
                __u8                           Internal_Error_Count;
                __u8                           Line_Error_Count;
                __u8                           AC_Error_Count;
                __u8                           Burst_Error_Count;
                __u8                            RESERVED_COUNTER_0;
                __u8                            AD_TRANS_Count;
                __u8                            RCV_Congestion_Count;
                __u8                            Lost_FR_Error_Count;
                __u8                            FREQ_Error_Count;
                __u8                            FR_Copied_Error_Count;
                __u8                            RESERVED_COUNTER_1;
                __u8                            Token_Error_Count;

                __u16                           TI_NDIS_Ring_Status;
                __u16                           BCN_Type;
                __u16                           Error_Code;
                __u16                           SA_of_Last_AMP_SMP[3];
                __u16                           UNA[3];
                __u16                           Ucode_Version_Number;
                __u16                           Status_CHG_Indicate;
                __u16                           RESERVED_STATUS_0;
} SBlock;

#pragma pack(1)
typedef struct ACBlockType {
                __u16                   cmd_done_status;    /* 02 */
                __u16                   cmd_info;           /* 04 */
                __u32                   trc_next_ptr;           /* 08 */
                __u16                   cmd;                /* 10 */
                __u16                   subcmd;             /* 12 */
                __u16                   data_offset_lo;         /* 14 */
                __u16                   data_offset_hi;         /* 16 */

                struct  ACBlockType     *next_ptr;              /* 20 */

                __u8                    filler[12];             /* 32 */
} ACBlock;

#define NUM_OF_INTERRUPTS               0x20

#pragma pack(1)
typedef struct {
                Interrupt_Status_Word   IStatus[NUM_OF_INTERRUPTS];
} ISBlock;

#pragma pack(1)
typedef struct {
                __u16                   valid_command;          /* 02 */
                __u16                   iack_code;              /* 04 */
                __u16                   resume_control;         /* 06 */
                __u16                   int_mask_control;       /* 08 */
                __u16                   int_mask_state;         /* 10 */

                __u8                    filler[6];              /* 16 */
} SCLBlock;

#pragma pack(1)
typedef struct
{
                __u16                   config;                 /* 02 */
                __u32                   trc_sclb_ptr;           /* 06 */
                __u32                   trc_acb_ptr;            /* 10 */
                __u32                   trc_isb_ptr;            /* 14 */
                __u16                   isbsiz;                 /* 16 */

                SCLBlock                *sclb_ptr;              /* 20 */
                ACBlock                 *acb_ptr;               /* 24 */
                ISBlock                 *isb_ptr;               /* 28 */

                __u16                   Non_Mac_Rx_Bdbs;        /* 30 DEBUG */
                __u8                    filler[2];              /* 32 */

} SCGBlock;

#pragma pack(1)
typedef struct
{
	__u32		trc_scgb_ptr;
	SCGBlock	*scgb_ptr;
} ISCPBlock;
#pragma pack()

typedef struct net_local {
	ISCPBlock       *iscpb_ptr;
        SCGBlock        *scgb_ptr;
        SCLBlock        *sclb_ptr;
        ISBlock         *isb_ptr;

	ACBlock         *acb_head;
        ACBlock         *acb_curr;
        ACBlock         *acb_next;

	__u8		adapter_name[12];

	__u16		num_rx_bdbs	[NUM_RX_QS_USED];
	__u16		num_rx_fcbs	[NUM_RX_QS_USED];

	__u16		num_tx_bdbs	[NUM_TX_QS_USED];
	__u16		num_tx_fcbs	[NUM_TX_QS_USED];

	__u16		num_of_tx_buffs;

	__u16		tx_buff_size	[NUM_TX_QS_USED];
	__u16		tx_buff_used	[NUM_TX_QS_USED];
	__u16		tx_queue_status	[NUM_TX_QS_USED];

	FCBlock		*tx_fcb_head[NUM_TX_QS_USED];
	FCBlock		*tx_fcb_curr[NUM_TX_QS_USED];
	FCBlock		*tx_fcb_end[NUM_TX_QS_USED];
	BDBlock		*tx_bdb_head[NUM_TX_QS_USED];
	__u16		*tx_buff_head[NUM_TX_QS_USED];
	__u16		*tx_buff_end[NUM_TX_QS_USED];
	__u16		*tx_buff_curr[NUM_TX_QS_USED];
	__u16		num_tx_fcbs_used[NUM_TX_QS_USED];

	FCBlock		*rx_fcb_head[NUM_RX_QS_USED];
	FCBlock		*rx_fcb_curr[NUM_RX_QS_USED];
	BDBlock		*rx_bdb_head[NUM_RX_QS_USED];
	BDBlock		*rx_bdb_curr[NUM_RX_QS_USED];
	BDBlock		*rx_bdb_end[NUM_RX_QS_USED];
	__u16		*rx_buff_head[NUM_RX_QS_USED];
	__u16		*rx_buff_end[NUM_RX_QS_USED];

	__u32		*ptr_local_ring_num;

	__u32		sh_mem_used;

	__u16		page_offset_mask;

	__u16		authorized_function_classes;
	__u16		authorized_access_priority;

        __u16            num_acbs;
        __u16            num_acbs_used;
        __u16            acb_pending;

	__u16		current_isb_index;

	__u8            monitor_state;
	__u8		monitor_state_ready;
	__u16		ring_status;
	__u8		ring_status_flags;
	__u8		current_ring_status;
	__u8		state;

	__u8		join_state;

	__u8		slot_num;
	__u16		pos_id;

	__u32		*ptr_una;
	__u32		*ptr_bcn_type;
	__u32		*ptr_tx_fifo_underruns;
	__u32		*ptr_rx_fifo_underruns;
	__u32		*ptr_rx_fifo_overruns;
	__u32		*ptr_tx_fifo_overruns;
	__u32		*ptr_tx_fcb_overruns;
	__u32		*ptr_rx_fcb_overruns;
	__u32		*ptr_tx_bdb_overruns;
	__u32		*ptr_rx_bdb_overruns;

	__u16		receive_queue_number;

	__u8		rx_fifo_overrun_count;
	__u8		tx_fifo_overrun_count;

	__u16            adapter_flags;
	__u16		adapter_flags1;
	__u16            *misc_command_data;
	__u16            max_packet_size;

	__u16            config_word0;
        __u16            config_word1;

	__u8            trc_mask;

	__u16            source_ring_number;
        __u16            target_ring_number;

	__u16		microcode_version;

	__u16            bic_type;
        __u16            nic_type;
        __u16            board_id;

	__u16            rom_size;
	__u32		rom_base;
        __u16            ram_size;
        __u16            ram_usable;
	__u32		ram_base;
	__u32		ram_access;

	__u16            extra_info;
        __u16            mode_bits;
	__u16		media_menu;
	__u16		media_type;
	__u16		adapter_bus;

	__u16		status;
	__u16            receive_mask;

	__u16            group_address_0;
        __u16            group_address[2];
        __u16            functional_address_0;
        __u16            functional_address[2];
        __u16            bitwise_group_address[2];

	__u8            *ptr_ucode;

	__u8		cleanup;

	struct sk_buff_head SendSkbQueue;
        __u16 QueueSkb;

	struct tr_statistics MacStat;   /* MAC statistics structure */
	
	spinlock_t	lock;
} NET_LOCAL;

/************************************
 * SNMP-ON-BOARD Agent Link Structure
 ************************************/

typedef struct {
        __u8           LnkSigStr[12]; /* signature string "SmcLinkTable" */
        __u8           LnkDrvTyp;     /* 1=Redbox ODI, 2=ODI DOS, 3=ODI OS/2, 4=NDIS DOS */
        __u8           LnkFlg;        /* 0 if no agent linked, 1 if agent linked */
        void           *LnkNfo;       /* routine which returns pointer to NIC info */
        void           *LnkAgtRcv;    /* pointer to agent receive trap entry */
        void           *LnkAgtXmt;            /* pointer to agent transmit trap
entry  */
void           *LnkGet;                  /* pointer to NIC receive data
copy routine */
        void           *LnkSnd;                  /* pointer to NIC send routine
*/
        void           *LnkRst;                  /* pointer to NIC driver reset
routine */
        void           *LnkMib;                  /* pointer to MIB data base */
        void           *LnkMibAct;            /* pointer to MIB action routine list */
        __u16           LnkCntOffset;  /* offset to error counters */
        __u16           LnkCntNum;     /* number of error counters */
        __u16           LnkCntSize;    /* size of error counters i.e. 32 = 32 bits */
        void           *LnkISR;       /* pointer to interrupt vector */
        __u8           LnkFrmTyp;     /* 1=Ethernet, 2=Token Ring */
        __u8           LnkDrvVer1 ;   /* driver major version */
        __u8           LnkDrvVer2 ;   /* driver minor version */
} AgentLink;

/*
 * Definitions for pcm_card_flags(bit_mapped)
 */
#define REG_COMPLETE   0x0001
#define INSERTED       0x0002
#define PCC_INSERTED   0x0004         /* 1=currently inserted, 0=cur removed */

/*
 * Adapter RAM test patterns
 */
#define RAM_PATTERN_1  0x55AA
#define RAM_PATTERN_2  0x9249
#define RAM_PATTERN_3  0xDB6D

/*
 * definitions for RAM test
 */
#define ROM_SIGNATURE  0xAA55
#define MIN_ROM_SIZE   0x2000

/*
 * Return Codes
 */
#define SUCCESS                 0x0000
#define ADAPTER_AND_CONFIG      0x0001
#define ADAPTER_NO_CONFIG       0x0002
#define NOT_MY_INTERRUPT        0x0003
#define FRAME_REJECTED          0x0004
#define EVENTS_DISABLED         0x0005
#define OUT_OF_RESOURCES        0x0006
#define INVALID_PARAMETER       0x0007
#define INVALID_FUNCTION        0x0008
#define INITIALIZE_FAILED       0x0009
#define CLOSE_FAILED            0x000A
#define MAX_COLLISIONS          0x000B
#define NO_SUCH_DESTINATION     0x000C
#define BUFFER_TOO_SMALL_ERROR  0x000D
#define ADAPTER_CLOSED          0x000E
#define UCODE_NOT_PRESENT       0x000F
#define FIFO_UNDERRUN           0x0010
#define DEST_OUT_OF_RESOURCES   0x0011
#define ADAPTER_NOT_INITIALIZED 0x0012
#define PENDING                 0x0013
#define UCODE_PRESENT           0x0014
#define NOT_INIT_BY_BRIDGE      0x0015

#define OPEN_FAILED             0x0080
#define HARDWARE_FAILED         0x0081
#define SELF_TEST_FAILED        0x0082
#define RAM_TEST_FAILED         0x0083
#define RAM_CONFLICT            0x0084
#define ROM_CONFLICT            0x0085
#define UNKNOWN_ADAPTER         0x0086
#define CONFIG_ERROR            0x0087
#define CONFIG_WARNING          0x0088
#define NO_FIXED_CNFG           0x0089
#define EEROM_CKSUM_ERROR       0x008A
#define ROM_SIGNATURE_ERROR     0x008B
#define ROM_CHECKSUM_ERROR      0x008C
#define ROM_SIZE_ERROR          0x008D
#define UNSUPPORTED_NIC_CHIP    0x008E
#define NIC_REG_ERROR           0x008F
#define BIC_REG_ERROR           0x0090
#define MICROCODE_TEST_ERROR    0x0091
#define LOBE_MEDIA_TEST_FAILED  0x0092

#define ADAPTER_FOUND_LAN_CORRUPT 0x009B

#define ADAPTER_NOT_FOUND       0xFFFF

#define ILLEGAL_FUNCTION        INVALID_FUNCTION

/* Errors */
#define IO_BASE_INVALID         0x0001
#define IO_BASE_RANGE           0x0002
#define IRQ_INVALID             0x0004
#define IRQ_RANGE               0x0008
#define RAM_BASE_INVALID        0x0010
#define RAM_BASE_RANGE          0x0020
#define RAM_SIZE_RANGE          0x0040
#define MEDIA_INVALID           0x0800

/* Warnings */
#define IRQ_MISMATCH            0x0080
#define RAM_BASE_MISMATCH       0x0100
#define RAM_SIZE_MISMATCH       0x0200
#define BUS_MODE_MISMATCH       0x0400

#define RX_CRC_ERROR                            0x01
#define RX_ALIGNMENT_ERROR              0x02
#define RX_HW_FAILED                            0x80

/*
 * Definitions for the field RING_STATUS_FLAGS
 */
#define RING_STATUS_CHANGED                     0X01
#define MONITOR_STATE_CHANGED                   0X02
#define JOIN_STATE_CHANGED                      0X04

/*
 * Definitions for the field JOIN_STATE
 */
#define JS_BYPASS_STATE                         0x00
#define JS_LOBE_TEST_STATE                      0x01
#define JS_DETECT_MONITOR_PRESENT_STATE         0x02
#define JS_AWAIT_NEW_MONITOR_STATE              0x03
#define JS_DUPLICATE_ADDRESS_TEST_STATE         0x04
#define JS_NEIGHBOR_NOTIFICATION_STATE          0x05
#define JS_REQUEST_INITIALIZATION_STATE         0x06
#define JS_JOIN_COMPLETE_STATE                  0x07
#define JS_BYPASS_WAIT_STATE                    0x08

/*
 * Definitions for the field MONITOR_STATE
 */
#define MS_MONITOR_FSM_INACTIVE                 0x00
#define MS_REPEAT_BEACON_STATE                  0x01
#define MS_REPEAT_CLAIM_TOKEN_STATE             0x02
#define MS_TRANSMIT_CLAIM_TOKEN_STATE           0x03
#define MS_STANDBY_MONITOR_STATE                0x04
#define MS_TRANSMIT_BEACON_STATE                0x05
#define MS_ACTIVE_MONITOR_STATE                 0x06
#define MS_TRANSMIT_RING_PURGE_STATE            0x07
#define MS_BEACON_TEST_STATE                    0x09

/*
 * Definitions for the bit-field RING_STATUS
 */
#define SIGNAL_LOSS                             0x8000
#define HARD_ERROR                              0x4000
#define SOFT_ERROR                              0x2000
#define TRANSMIT_BEACON                         0x1000
#define LOBE_WIRE_FAULT                         0x0800
#define AUTO_REMOVAL_ERROR                      0x0400
#define REMOVE_RECEIVED                         0x0100
#define COUNTER_OVERFLOW                        0x0080
#define SINGLE_STATION                          0x0040
#define RING_RECOVERY                           0x0020

/*
 * Definitions for the field BUS_TYPE
 */
#define AT_BUS                  0x00
#define MCA_BUS                 0x01
#define EISA_BUS                0x02
#define PCI_BUS                 0x03
#define PCMCIA_BUS              0x04

/*
 * Definitions for adapter_flags
 */
#define RX_VALID_LOOKAHEAD      0x0001
#define FORCED_16BIT_MODE       0x0002
#define ADAPTER_DISABLED        0x0004
#define TRANSMIT_CHAIN_INT      0x0008
#define EARLY_RX_FRAME          0x0010
#define EARLY_TX                0x0020
#define EARLY_RX_COPY           0x0040
#define USES_PHYSICAL_ADDR      0x0080		/* Rsvd for DEC PCI and 9232 */
#define NEEDS_PHYSICAL_ADDR  	0x0100       	/* Reserved*/
#define RX_STATUS_PENDING       0x0200
#define ERX_DISABLED         	0x0400       	/* EARLY_RX_ENABLE rcv_mask */
#define ENABLE_TX_PENDING       0x0800
#define ENABLE_RX_PENDING       0x1000
#define PERM_CLOSE              0x2000  
#define IO_MAPPED               0x4000  	/* IOmapped bus interface 795 */
#define ETX_DISABLED            0x8000


/*
 * Definitions for adapter_flags1
 */
#define TX_PHY_RX_VIRT          0x0001 
#define NEEDS_HOST_RAM          0x0002
#define NEEDS_MEDIA_TYPE        0x0004
#define EARLY_RX_DONE           0x0008
#define PNP_BOOT_BIT            0x0010  /* activates PnP & config on power-up */
                                        /* clear => regular PnP operation */
#define PNP_ENABLE              0x0020  /* regular PnP operation clear => */
                                        /* no PnP, overrides PNP_BOOT_BIT */
#define SATURN_ENABLE           0x0040

#define ADAPTER_REMOVABLE       0x0080 	/* adapter is hot swappable */
#define TX_PHY                  0x0100  /* Uses physical address for tx bufs */
#define RX_PHY                  0x0200  /* Uses physical address for rx bufs */
#define TX_VIRT                 0x0400  /* Uses virtual addr for tx bufs */
#define RX_VIRT                 0x0800 
#define NEEDS_SERVICE           0x1000 

/*
 * Adapter Status Codes
 */
#define OPEN                    0x0001
#define INITIALIZED             0x0002
#define CLOSED                  0x0003
#define FAILED                  0x0005
#define NOT_INITIALIZED         0x0006
#define IO_CONFLICT             0x0007
#define CARD_REMOVED            0x0008
#define CARD_INSERTED           0x0009

/*
 * Mode Bit Definitions
 */
#define INTERRUPT_STATUS_BIT    0x8000  /* PC Interrupt Line: 0 = Not Enabled */
#define BOOT_STATUS_MASK        0x6000  /* Mask to isolate BOOT_STATUS */
#define BOOT_INHIBIT            0x0000  /* BOOT_STATUS is 'inhibited' */
#define BOOT_TYPE_1             0x2000  /* Unused BOOT_STATUS value */
#define BOOT_TYPE_2             0x4000  /* Unused BOOT_STATUS value */
#define BOOT_TYPE_3             0x6000  /* Unused BOOT_STATUS value */
#define ZERO_WAIT_STATE_MASK    0x1800  /* Mask to isolate Wait State flags */
#define ZERO_WAIT_STATE_8_BIT   0x1000  /* 0 = Disabled (Inserts Wait States) */
#define ZERO_WAIT_STATE_16_BIT  0x0800  /* 0 = Disabled (Inserts Wait States) */
#define LOOPING_MODE_MASK       0x0007
#define LOOPBACK_MODE_0         0x0000
#define LOOPBACK_MODE_1         0x0001
#define LOOPBACK_MODE_2         0x0002
#define LOOPBACK_MODE_3         0x0003
#define LOOPBACK_MODE_4         0x0004
#define LOOPBACK_MODE_5         0x0005
#define LOOPBACK_MODE_6         0x0006
#define LOOPBACK_MODE_7         0x0007
#define AUTO_MEDIA_DETECT       0x0008
#define MANUAL_CRC              0x0010
#define EARLY_TOKEN_REL         0x0020  /* Early Token Release for Token Ring */
#define UMAC               0x0040 
#define UTP2_PORT               0x0080  /* For 8216T2, 0=port A, 1=Port B. */
#define BNC_10BT_INTERFACE      0x0600  /* BNC and UTP current media set */
#define UTP_INTERFACE           0x0500  /* Ethernet UTP Only. */
#define BNC_INTERFACE           0x0400
#define AUI_INTERFACE           0x0300
#define AUI_10BT_INTERFACE      0x0200
#define STARLAN_10_INTERFACE    0x0100
#define INTERFACE_TYPE_MASK     0x0700

/*
 * Media Type Bit Definitions
 *
 * legend:      TP = Twisted Pair
 *              STP = Shielded twisted pair
 *              UTP = Unshielded twisted pair
 */

#define CNFG_MEDIA_TYPE_MASK    0x001e  /* POS Register 3 Mask         */

#define MEDIA_S10               0x0000  /* Ethernet adapter, TP.        */
#define MEDIA_AUI_UTP           0x0001  /* Ethernet adapter, AUI/UTP media */
#define MEDIA_BNC               0x0002  /* Ethernet adapter, BNC media. */
#define MEDIA_AUI               0x0003  /* Ethernet Adapter, AUI media. */
#define MEDIA_STP_16            0x0004  /* TokenRing adap, 16Mbit STP.  */
#define MEDIA_STP_4             0x0005  /* TokenRing adap, 4Mbit STP.   */
#define MEDIA_UTP_16            0x0006  /* TokenRing adap, 16Mbit UTP.  */
#define MEDIA_UTP_4             0x0007  /* TokenRing adap, 4Mbit UTP.   */
#define MEDIA_UTP               0x0008  /* Ethernet adapter, UTP media (no AUI)
*/
#define MEDIA_BNC_UTP           0x0010  /* Ethernet adapter, BNC/UTP media */
#define MEDIA_UTPFD             0x0011  /* Ethernet adapter, TP full duplex */
#define MEDIA_UTPNL             0x0012  /* Ethernet adapter, TP with link integrity test disabled */
#define MEDIA_AUI_BNC           0x0013  /* Ethernet adapter, AUI/BNC media */
#define MEDIA_AUI_BNC_UTP       0x0014  /* Ethernet adapter, AUI_BNC/UTP */
#define MEDIA_UTPA              0x0015  /* Ethernet UTP-10Mbps Ports A */
#define MEDIA_UTPB              0x0016  /* Ethernet UTP-10Mbps Ports B */
#define MEDIA_STP_16_UTP_16     0x0017  /* Token Ring STP-16Mbps/UTP-16Mbps */
#define MEDIA_STP_4_UTP_4       0x0018  /* Token Ring STP-4Mbps/UTP-4Mbps */

#define MEDIA_STP100_UTP100     0x0020  /* Ethernet STP-100Mbps/UTP-100Mbps */
#define MEDIA_UTP100FD          0x0021  /* Ethernet UTP-100Mbps, full duplex */
#define MEDIA_UTP100            0x0022  /* Ethernet UTP-100Mbps */


#define MEDIA_UNKNOWN           0xFFFF  /* Unknown adapter/media type   */

/*
 * Definitions for the field:
 * media_type2
 */
#define MEDIA_TYPE_MII              0x0001
#define MEDIA_TYPE_UTP              0x0002
#define MEDIA_TYPE_BNC              0x0004
#define MEDIA_TYPE_AUI              0x0008
#define MEDIA_TYPE_S10              0x0010
#define MEDIA_TYPE_AUTO_SENSE       0x1000
#define MEDIA_TYPE_AUTO_DETECT      0x4000
#define MEDIA_TYPE_AUTO_NEGOTIATE   0x8000

/*
 * Definitions for the field:
 * line_speed
 */
#define LINE_SPEED_UNKNOWN          0x0000
#define LINE_SPEED_4                0x0001
#define LINE_SPEED_10               0x0002
#define LINE_SPEED_16               0x0004
#define LINE_SPEED_100              0x0008
#define LINE_SPEED_T4               0x0008  /* 100BaseT4 aliased for 9332BVT */
#define LINE_SPEED_FULL_DUPLEX      0x8000

/*
 * Definitions for the field:
 * bic_type (Bus interface chip type)
 */
#define BIC_NO_CHIP             0x0000  /* Bus interface chip not implemented */
#define BIC_583_CHIP            0x0001  /* 83C583 bus interface chip */
#define BIC_584_CHIP            0x0002  /* 83C584 bus interface chip */
#define BIC_585_CHIP            0x0003  /* 83C585 bus interface chip */
#define BIC_593_CHIP            0x0004  /* 83C593 bus interface chip */
#define BIC_594_CHIP            0x0005  /* 83C594 bus interface chip */
#define BIC_564_CHIP            0x0006  /* PCMCIA Bus interface chip */
#define BIC_790_CHIP            0x0007  /* 83C790 bus i-face/Ethernet NIC chip */
#define BIC_571_CHIP            0x0008  /* 83C571 EISA bus master i-face */
#define BIC_587_CHIP            0x0009  /* Token Ring AT bus master i-face */
#define BIC_574_CHIP            0x0010  /* FEAST bus interface chip */
#define BIC_8432_CHIP           0x0011  /* 8432 bus i-face/Ethernet NIC(DEC PCI) */
#define BIC_9332_CHIP           0x0012  /* 9332 bus i-face/100Mbps Ether NIC(DEC PCI) */
#define BIC_8432E_CHIP          0x0013  /* 8432 Enhanced bus iface/Ethernet NIC(DEC) */
#define BIC_EPIC100_CHIP        0x0014  /* EPIC/100 10/100 Mbps Ethernet BIC/NIC */
#define BIC_C94_CHIP            0x0015  /* 91C94 bus i-face in PCMCIA mode */
#define BIC_X8020_CHIP          0x0016  /* Xilinx PCMCIA multi-func i-face */

/*
 * Definitions for the field:
 * nic_type (Bus interface chip type)
 */
#define NIC_UNK_CHIP            0x0000  /* Unknown NIC chip      */
#define NIC_8390_CHIP           0x0001  /* DP8390 Ethernet NIC   */
#define NIC_690_CHIP            0x0002  /* 83C690 Ethernet NIC   */
#define NIC_825_CHIP            0x0003  /* 83C825 Token Ring NIC */
/*      #define NIC_???_CHIP    0x0004  */ /* Not used           */
/*      #define NIC_???_CHIP    0x0005  */ /* Not used           */
/*      #define NIC_???_CHIP    0x0006  */ /* Not used           */
#define NIC_790_CHIP            0x0007  /* 83C790 bus i-face/Ethernet NIC chip */
#define NIC_C100_CHIP           0x0010  /* FEAST 100Mbps Ethernet NIC */
#define NIC_8432_CHIP           0x0011  /* 8432 bus i-face/Ethernet NIC(DEC PCI) */
#define NIC_9332_CHIP           0x0012  /* 9332 bus i-face/100Mbps Ether NIC(DEC PCI) */
#define NIC_8432E_CHIP          0x0013  /* 8432 enhanced bus iface/Ethernet NIC(DEC) */
#define NIC_EPIC100_CHIP        0x0014   /* EPIC/100 10/100 Mbps Ethernet BIC/NIC */
#define NIC_C94_CHIP            0x0015  /* 91C94 PC Card with multi func */

/*
 * Definitions for the field:
 * adapter_type The adapter_type field describes the adapter/bus
 *              configuration.
 */
#define BUS_ISA16_TYPE          0x0001  /* 16 bit adap in 16 bit (E)ISA slot  */
#define BUS_ISA8_TYPE           0x0002  /* 8/16b adap in 8 bit XT/(E)ISA slot */
#define BUS_MCA_TYPE            0x0003  /* Micro Channel adapter              */

/*
 * Receive Mask definitions
 */
#define ACCEPT_MULTICAST                0x0001
#define ACCEPT_BROADCAST                0x0002
#define PROMISCUOUS_MODE                0x0004
#define ACCEPT_SOURCE_ROUTING           0x0008
#define ACCEPT_ERR_PACKETS              0x0010
#define ACCEPT_ATT_MAC_FRAMES           0x0020
#define ACCEPT_MULTI_PROM               0x0040
#define TRANSMIT_ONLY                   0x0080
#define ACCEPT_EXT_MAC_FRAMES           0x0100
#define EARLY_RX_ENABLE                 0x0200
#define PKT_SIZE_NOT_NEEDED             0x0400
#define ACCEPT_SOURCE_ROUTING_SPANNING  0x0808

#define ACCEPT_ALL_MAC_FRAMES           0x0120

/*
 * config_mode defs
 */
#define STORE_EEROM             0x0001  /* Store config in EEROM. */
#define STORE_REGS              0x0002  /* Store config in register set. */

/*
 * equates for lmac_flags in adapter structure (Ethernet)
 */
#define         MEM_DISABLE     0x0001
#define         RX_STATUS_POLL  0x0002
#define         USE_RE_BIT      0x0004
/*#define       RESERVED        0x0008 */
/*#define       RESERVED        0x0010 */
/*#define       RESERVED        0x0020 */
/*#define       RESERVED        0x0040 */
/*#define       RESERVED        0x0080 */
/*#define       RESERVED        0x0100 */
/*#define       RESERVED        0x0200 */
/*#define       RESERVED        0x0400 */
/*#define       RESERVED        0x0800 */
/*#define       RESERVED        0x1000 */
/*#define       RESERVED        0x2000 */
/*#define       RESERVED        0x4000 */
/*#define       RESERVED        0x8000 */

/* media_opts & media_set Fields bit defs for Ethernet ... */
#define         MED_OPT_BNC     0x01
#define         MED_OPT_UTP     0x02
#define         MED_OPT_AUI     0x04
#define         MED_OPT_10MB    0x08
#define         MED_OPT_100MB   0x10
#define         MED_OPT_S10     0x20

/* media_opts & media_set Fields bit defs for Token Ring ... */
#define         MED_OPT_4MB     0x08
#define         MED_OPT_16MB    0x10
#define         MED_OPT_STP     0x40

#define MAX_8023_SIZE           1500    /* Max 802.3 size of frame. */
#define DEFAULT_ERX_VALUE       4       /* Number of 16-byte blocks for 790B early Rx. */
#define DEFAULT_ETX_VALUE       32      /* Number of bytes for 790B early Tx. */
#define DEFAULT_TX_RETRIES      3       /* Number of transmit retries */
#define LPBK_FRAME_SIZE         1024    /* Default loopback frame for Rx calibration test. */
#define MAX_LOOKAHEAD_SIZE      252     /* Max lookahead size for ethernet. */

#define RW_MAC_STATE                    0x1101
#define RW_SA_OF_LAST_AMP_OR_SMP        0x2803
#define RW_PHYSICAL_DROP_NUMBER         0x3B02
#define RW_UPSTREAM_NEIGHBOR_ADDRESS    0x3E03
#define RW_PRODUCT_INSTANCE_ID          0x4B09

#define RW_TRC_STATUS_BLOCK             0x5412

#define RW_MAC_ERROR_COUNTERS_NO_CLEAR  0x8006
#define RW_MAC_ERROR_COUNTER_CLEAR      0x7A06
#define RW_CONFIG_REGISTER_0            0xA001
#define RW_CONFIG_REGISTER_1            0xA101
#define RW_PRESCALE_TIMER_THRESHOLD     0xA201
#define RW_TPT_THRESHOLD                0xA301
#define RW_TQP_THRESHOLD                0xA401
#define RW_TNT_THRESHOLD                0xA501
#define RW_TBT_THRESHOLD                0xA601
#define RW_TSM_THRESHOLD                0xA701
#define RW_TAM_THRESHOLD                0xA801
#define RW_TBR_THRESHOLD                0xA901
#define RW_TER_THRESHOLD                0xAA01
#define RW_TGT_THRESHOLD                0xAB01
#define RW_THT_THRESHOLD                0xAC01
#define RW_TRR_THRESHOLD                0xAD01
#define RW_TVX_THRESHOLD                0xAE01
#define RW_INDIVIDUAL_MAC_ADDRESS       0xB003

#define RW_INDIVIDUAL_GROUP_ADDRESS     0xB303  /* all of group addr */
#define RW_INDIVIDUAL_GROUP_ADDR_WORD_0 0xB301  /* 1st word of group addr */
#define RW_INDIVIDUAL_GROUP_ADDR        0xB402  /* 2nd-3rd word of group addr */
#define RW_FUNCTIONAL_ADDRESS           0xB603  /* all of functional addr */
#define RW_FUNCTIONAL_ADDR_WORD_0       0xB601  /* 1st word of func  addr */
#define RW_FUNCTIONAL_ADDR              0xB702  /* 2nd-3rd word func addr */

#define RW_BIT_SIGNIFICANT_GROUP_ADDR   0xB902
#define RW_SOURCE_RING_BRIDGE_NUMBER    0xBB01
#define RW_TARGET_RING_NUMBER           0xBC01

#define RW_HIC_INTERRUPT_MASK           0xC601

#define SOURCE_ROUTING_SPANNING_BITS    0x00C0  /* Spanning Tree Frames */
#define SOURCE_ROUTING_EXPLORER_BIT     0x0040  /* Explorer and Single Route */

        /* write */

#define CSR_MSK_ALL             0x80    // Bic 587 Only
#define CSR_MSKTINT             0x20
#define CSR_MSKCBUSY            0x10
#define CSR_CLRTINT             0x08
#define CSR_CLRCBUSY            0x04
#define CSR_WCSS                0x02
#define CSR_CA                  0x01

        /* read */

#define CSR_TINT                0x20
#define CSR_CINT                0x10
#define CSR_TSTAT               0x08
#define CSR_CSTAT               0x04
#define CSR_FAULT               0x02
#define CSR_CBUSY               0x01

#define LAAR_MEM16ENB           0x80
#define Zws16                   0x20

#define IRR_IEN                 0x80
#define Zws8                    0x01

#define IMCCR_EIL               0x04

typedef struct {
        __u8            ac;                             /* Access Control */
        __u8            fc;                             /* Frame Control */
        __u8            da[6];                          /* Dest Addr */
        __u8            sa[6];                          /* Source Addr */

        __u16            vl;                             /* Vector Length */
        __u8            dc_sc;                          /* Dest/Source Class */
        __u8            vc;                             /* Vector Code */
        } MAC_HEADER;

#define MAX_SUB_VECTOR_INFO     (RX_DATA_BUFFER_SIZE - sizeof(MAC_HEADER) - 2)

typedef struct
        {
        __u8            svl;                            /* Sub-vector Length */
        __u8            svi;                            /* Sub-vector Code */
        __u8            svv[MAX_SUB_VECTOR_INFO];       /* Sub-vector Info */
        } MAC_SUB_VECTOR;

#endif	/* __KERNEL__ */
#endif	/* __LINUX_SMCTR_H */
