/*
 * Copyright (C) 2006 Freescale Semicondutor, Inc. All rights reserved.
 *
 * Authors: 	Shlomi Gridish <gridish@freescale.com>
 * 		Li Yang <leoli@freescale.com>
 *
 * Description:
 * QUICC Engine (QE) external definitions and structure.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef _ASM_POWERPC_QE_H
#define _ASM_POWERPC_QE_H
#ifdef __KERNEL__

#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <asm/cpm.h>
#include <asm/immap_qe.h>

#define QE_NUM_OF_SNUM	256	/* There are 256 serial number in QE */
#define QE_NUM_OF_BRGS	16
#define QE_NUM_OF_PORTS	1024

/* Memory partitions
*/
#define MEM_PART_SYSTEM		0
#define MEM_PART_SECONDARY	1
#define MEM_PART_MURAM		2

/* Clocks and BRGs */
enum qe_clock {
	QE_CLK_NONE = 0,
	QE_BRG1,		/* Baud Rate Generator 1 */
	QE_BRG2,		/* Baud Rate Generator 2 */
	QE_BRG3,		/* Baud Rate Generator 3 */
	QE_BRG4,		/* Baud Rate Generator 4 */
	QE_BRG5,		/* Baud Rate Generator 5 */
	QE_BRG6,		/* Baud Rate Generator 6 */
	QE_BRG7,		/* Baud Rate Generator 7 */
	QE_BRG8,		/* Baud Rate Generator 8 */
	QE_BRG9,		/* Baud Rate Generator 9 */
	QE_BRG10,		/* Baud Rate Generator 10 */
	QE_BRG11,		/* Baud Rate Generator 11 */
	QE_BRG12,		/* Baud Rate Generator 12 */
	QE_BRG13,		/* Baud Rate Generator 13 */
	QE_BRG14,		/* Baud Rate Generator 14 */
	QE_BRG15,		/* Baud Rate Generator 15 */
	QE_BRG16,		/* Baud Rate Generator 16 */
	QE_CLK1,		/* Clock 1 */
	QE_CLK2,		/* Clock 2 */
	QE_CLK3,		/* Clock 3 */
	QE_CLK4,		/* Clock 4 */
	QE_CLK5,		/* Clock 5 */
	QE_CLK6,		/* Clock 6 */
	QE_CLK7,		/* Clock 7 */
	QE_CLK8,		/* Clock 8 */
	QE_CLK9,		/* Clock 9 */
	QE_CLK10,		/* Clock 10 */
	QE_CLK11,		/* Clock 11 */
	QE_CLK12,		/* Clock 12 */
	QE_CLK13,		/* Clock 13 */
	QE_CLK14,		/* Clock 14 */
	QE_CLK15,		/* Clock 15 */
	QE_CLK16,		/* Clock 16 */
	QE_CLK17,		/* Clock 17 */
	QE_CLK18,		/* Clock 18 */
	QE_CLK19,		/* Clock 19 */
	QE_CLK20,		/* Clock 20 */
	QE_CLK21,		/* Clock 21 */
	QE_CLK22,		/* Clock 22 */
	QE_CLK23,		/* Clock 23 */
	QE_CLK24,		/* Clock 24 */
	QE_CLK_DUMMY
};

static inline bool qe_clock_is_brg(enum qe_clock clk)
{
	return clk >= QE_BRG1 && clk <= QE_BRG16;
}

extern spinlock_t cmxgcr_lock;

/* Export QE common operations */
#ifdef CONFIG_QUICC_ENGINE
extern void __init qe_reset(void);
#else
static inline void qe_reset(void) {}
#endif

/* QE PIO */
#define QE_PIO_PINS 32

struct qe_pio_regs {
	__be32	cpodr;		/* Open drain register */
	__be32	cpdata;		/* Data register */
	__be32	cpdir1;		/* Direction register */
	__be32	cpdir2;		/* Direction register */
	__be32	cppar1;		/* Pin assignment register */
	__be32	cppar2;		/* Pin assignment register */
#ifdef CONFIG_PPC_85xx
	u8	pad[8];
#endif
};

#define QE_PIO_DIR_IN	2
#define QE_PIO_DIR_OUT	1
extern void __par_io_config_pin(struct qe_pio_regs __iomem *par_io, u8 pin,
				int dir, int open_drain, int assignment,
				int has_irq);
#ifdef CONFIG_QUICC_ENGINE
extern int par_io_init(struct device_node *np);
extern int par_io_of_config(struct device_node *np);
extern int par_io_config_pin(u8 port, u8 pin, int dir, int open_drain,
			     int assignment, int has_irq);
extern int par_io_data_set(u8 port, u8 pin, u8 val);
#else
static inline int par_io_init(struct device_node *np) { return -ENOSYS; }
static inline int par_io_of_config(struct device_node *np) { return -ENOSYS; }
static inline int par_io_config_pin(u8 port, u8 pin, int dir, int open_drain,
		int assignment, int has_irq) { return -ENOSYS; }
static inline int par_io_data_set(u8 port, u8 pin, u8 val) { return -ENOSYS; }
#endif /* CONFIG_QUICC_ENGINE */

/*
 * Pin multiplexing functions.
 */
struct qe_pin;
#ifdef CONFIG_QE_GPIO
extern struct qe_pin *qe_pin_request(struct device_node *np, int index);
extern void qe_pin_free(struct qe_pin *qe_pin);
extern void qe_pin_set_gpio(struct qe_pin *qe_pin);
extern void qe_pin_set_dedicated(struct qe_pin *pin);
#else
static inline struct qe_pin *qe_pin_request(struct device_node *np, int index)
{
	return ERR_PTR(-ENOSYS);
}
static inline void qe_pin_free(struct qe_pin *qe_pin) {}
static inline void qe_pin_set_gpio(struct qe_pin *qe_pin) {}
static inline void qe_pin_set_dedicated(struct qe_pin *pin) {}
#endif /* CONFIG_QE_GPIO */

/* QE internal API */
int qe_issue_cmd(u32 cmd, u32 device, u8 mcn_protocol, u32 cmd_input);
enum qe_clock qe_clock_source(const char *source);
unsigned int qe_get_brg_clk(void);
int qe_setbrg(enum qe_clock brg, unsigned int rate, unsigned int multiplier);
int qe_get_snum(void);
void qe_put_snum(u8 snum);
unsigned int qe_get_num_of_risc(void);
unsigned int qe_get_num_of_snums(void);

/* we actually use cpm_muram implementation, define this for convenience */
#define qe_muram_init cpm_muram_init
#define qe_muram_alloc cpm_muram_alloc
#define qe_muram_alloc_fixed cpm_muram_alloc_fixed
#define qe_muram_free cpm_muram_free
#define qe_muram_addr cpm_muram_addr
#define qe_muram_offset cpm_muram_offset

/* Structure that defines QE firmware binary files.
 *
 * See Documentation/powerpc/qe-firmware.txt for a description of these
 * fields.
 */
struct qe_firmware {
	struct qe_header {
		__be32 length;  /* Length of the entire structure, in bytes */
		u8 magic[3];    /* Set to { 'Q', 'E', 'F' } */
		u8 version;     /* Version of this layout. First ver is '1' */
	} header;
	u8 id[62];      /* Null-terminated identifier string */
	u8 split;	/* 0 = shared I-RAM, 1 = split I-RAM */
	u8 count;       /* Number of microcode[] structures */
	struct {
		__be16 model;   	/* The SOC model  */
		u8 major;       	/* The SOC revision major */
		u8 minor;       	/* The SOC revision minor */
	} __attribute__ ((packed)) soc;
	u8 padding[4];			/* Reserved, for alignment */
	__be64 extended_modes;		/* Extended modes */
	__be32 vtraps[8];		/* Virtual trap addresses */
	u8 reserved[4];			/* Reserved, for future expansion */
	struct qe_microcode {
		u8 id[32];      	/* Null-terminated identifier */
		__be32 traps[16];       /* Trap addresses, 0 == ignore */
		__be32 eccr;    	/* The value for the ECCR register */
		__be32 iram_offset;     /* Offset into I-RAM for the code */
		__be32 count;   	/* Number of 32-bit words of the code */
		__be32 code_offset;     /* Offset of the actual microcode */
		u8 major;       	/* The microcode version major */
		u8 minor;       	/* The microcode version minor */
		u8 revision;		/* The microcode version revision */
		u8 padding;		/* Reserved, for alignment */
		u8 reserved[4];		/* Reserved, for future expansion */
	} __attribute__ ((packed)) microcode[1];
	/* All microcode binaries should be located here */
	/* CRC32 should be located here, after the microcode binaries */
} __attribute__ ((packed));

struct qe_firmware_info {
	char id[64];		/* Firmware name */
	u32 vtraps[8];		/* Virtual trap addresses */
	u64 extended_modes;	/* Extended modes */
};

/* Upload a firmware to the QE */
int qe_upload_firmware(const struct qe_firmware *firmware);

/* Obtain information on the uploaded firmware */
struct qe_firmware_info *qe_get_firmware_info(void);

/* QE USB */
int qe_usb_clock_set(enum qe_clock clk, int rate);

/* Buffer descriptors */
struct qe_bd {
	__be16 status;
	__be16 length;
	__be32 buf;
} __attribute__ ((packed));

#define BD_STATUS_MASK	0xffff0000
#define BD_LENGTH_MASK	0x0000ffff

/* Alignment */
#define QE_INTR_TABLE_ALIGN	16	/* ??? */
#define QE_ALIGNMENT_OF_BD	8
#define QE_ALIGNMENT_OF_PRAM	64

/* RISC allocation */
#define QE_RISC_ALLOCATION_RISC1	0x1  /* RISC 1 */
#define QE_RISC_ALLOCATION_RISC2	0x2  /* RISC 2 */
#define QE_RISC_ALLOCATION_RISC3	0x4  /* RISC 3 */
#define QE_RISC_ALLOCATION_RISC4	0x8  /* RISC 4 */
#define QE_RISC_ALLOCATION_RISC1_AND_RISC2	(QE_RISC_ALLOCATION_RISC1 | \
						 QE_RISC_ALLOCATION_RISC2)
#define QE_RISC_ALLOCATION_FOUR_RISCS	(QE_RISC_ALLOCATION_RISC1 | \
					 QE_RISC_ALLOCATION_RISC2 | \
					 QE_RISC_ALLOCATION_RISC3 | \
					 QE_RISC_ALLOCATION_RISC4)

/* QE extended filtering Table Lookup Key Size */
enum qe_fltr_tbl_lookup_key_size {
	QE_FLTR_TABLE_LOOKUP_KEY_SIZE_8_BYTES
		= 0x3f,		/* LookupKey parsed by the Generate LookupKey
				   CMD is truncated to 8 bytes */
	QE_FLTR_TABLE_LOOKUP_KEY_SIZE_16_BYTES
		= 0x5f,		/* LookupKey parsed by the Generate LookupKey
				   CMD is truncated to 16 bytes */
};

/* QE FLTR extended filtering Largest External Table Lookup Key Size */
enum qe_fltr_largest_external_tbl_lookup_key_size {
	QE_FLTR_LARGEST_EXTERNAL_TABLE_LOOKUP_KEY_SIZE_NONE
		= 0x0,/* not used */
	QE_FLTR_LARGEST_EXTERNAL_TABLE_LOOKUP_KEY_SIZE_8_BYTES
		= QE_FLTR_TABLE_LOOKUP_KEY_SIZE_8_BYTES,	/* 8 bytes */
	QE_FLTR_LARGEST_EXTERNAL_TABLE_LOOKUP_KEY_SIZE_16_BYTES
		= QE_FLTR_TABLE_LOOKUP_KEY_SIZE_16_BYTES,	/* 16 bytes */
};

/* structure representing QE parameter RAM */
struct qe_timer_tables {
	u16 tm_base;		/* QE timer table base adr */
	u16 tm_ptr;		/* QE timer table pointer */
	u16 r_tmr;		/* QE timer mode register */
	u16 r_tmv;		/* QE timer valid register */
	u32 tm_cmd;		/* QE timer cmd register */
	u32 tm_cnt;		/* QE timer internal cnt */
} __attribute__ ((packed));

#define QE_FLTR_TAD_SIZE	8

/* QE extended filtering Termination Action Descriptor (TAD) */
struct qe_fltr_tad {
	u8 serialized[QE_FLTR_TAD_SIZE];
} __attribute__ ((packed));

/* Communication Direction */
enum comm_dir {
	COMM_DIR_NONE = 0,
	COMM_DIR_RX = 1,
	COMM_DIR_TX = 2,
	COMM_DIR_RX_AND_TX = 3
};

/* QE CMXUCR Registers.
 * There are two UCCs represented in each of the four CMXUCR registers.
 * These values are for the UCC in the LSBs
 */
#define QE_CMXUCR_MII_ENET_MNG		0x00007000
#define QE_CMXUCR_MII_ENET_MNG_SHIFT	12
#define QE_CMXUCR_GRANT			0x00008000
#define QE_CMXUCR_TSA			0x00004000
#define QE_CMXUCR_BKPT			0x00000100
#define QE_CMXUCR_TX_CLK_SRC_MASK	0x0000000F

/* QE CMXGCR Registers.
*/
#define QE_CMXGCR_MII_ENET_MNG		0x00007000
#define QE_CMXGCR_MII_ENET_MNG_SHIFT	12
#define QE_CMXGCR_USBCS			0x0000000f
#define QE_CMXGCR_USBCS_CLK3		0x1
#define QE_CMXGCR_USBCS_CLK5		0x2
#define QE_CMXGCR_USBCS_CLK7		0x3
#define QE_CMXGCR_USBCS_CLK9		0x4
#define QE_CMXGCR_USBCS_CLK13		0x5
#define QE_CMXGCR_USBCS_CLK17		0x6
#define QE_CMXGCR_USBCS_CLK19		0x7
#define QE_CMXGCR_USBCS_CLK21		0x8
#define QE_CMXGCR_USBCS_BRG9		0x9
#define QE_CMXGCR_USBCS_BRG10		0xa

/* QE CECR Commands.
*/
#define QE_CR_FLG			0x00010000
#define QE_RESET			0x80000000
#define QE_INIT_TX_RX			0x00000000
#define QE_INIT_RX			0x00000001
#define QE_INIT_TX			0x00000002
#define QE_ENTER_HUNT_MODE		0x00000003
#define QE_STOP_TX			0x00000004
#define QE_GRACEFUL_STOP_TX		0x00000005
#define QE_RESTART_TX			0x00000006
#define QE_CLOSE_RX_BD			0x00000007
#define QE_SWITCH_COMMAND		0x00000007
#define QE_SET_GROUP_ADDRESS		0x00000008
#define QE_START_IDMA			0x00000009
#define QE_MCC_STOP_RX			0x00000009
#define QE_ATM_TRANSMIT			0x0000000a
#define QE_HPAC_CLEAR_ALL		0x0000000b
#define QE_GRACEFUL_STOP_RX		0x0000001a
#define QE_RESTART_RX			0x0000001b
#define QE_HPAC_SET_PRIORITY		0x0000010b
#define QE_HPAC_STOP_TX			0x0000020b
#define QE_HPAC_STOP_RX			0x0000030b
#define QE_HPAC_GRACEFUL_STOP_TX	0x0000040b
#define QE_HPAC_GRACEFUL_STOP_RX	0x0000050b
#define QE_HPAC_START_TX		0x0000060b
#define QE_HPAC_START_RX		0x0000070b
#define QE_USB_STOP_TX			0x0000000a
#define QE_USB_RESTART_TX		0x0000000c
#define QE_QMC_STOP_TX			0x0000000c
#define QE_QMC_STOP_RX			0x0000000d
#define QE_SS7_SU_FIL_RESET		0x0000000e
/* jonathbr added from here down for 83xx */
#define QE_RESET_BCS			0x0000000a
#define QE_MCC_INIT_TX_RX_16		0x00000003
#define QE_MCC_STOP_TX			0x00000004
#define QE_MCC_INIT_TX_1		0x00000005
#define QE_MCC_INIT_RX_1		0x00000006
#define QE_MCC_RESET			0x00000007
#define QE_SET_TIMER			0x00000008
#define QE_RANDOM_NUMBER		0x0000000c
#define QE_ATM_MULTI_THREAD_INIT	0x00000011
#define QE_ASSIGN_PAGE			0x00000012
#define QE_ADD_REMOVE_HASH_ENTRY	0x00000013
#define QE_START_FLOW_CONTROL		0x00000014
#define QE_STOP_FLOW_CONTROL		0x00000015
#define QE_ASSIGN_PAGE_TO_DEVICE	0x00000016

#define QE_ASSIGN_RISC			0x00000010
#define QE_CR_MCN_NORMAL_SHIFT		6
#define QE_CR_MCN_USB_SHIFT		4
#define QE_CR_MCN_RISC_ASSIGN_SHIFT	8
#define QE_CR_SNUM_SHIFT		17

/* QE CECR Sub Block - sub block of QE command.
*/
#define QE_CR_SUBBLOCK_INVALID		0x00000000
#define QE_CR_SUBBLOCK_USB		0x03200000
#define QE_CR_SUBBLOCK_UCCFAST1		0x02000000
#define QE_CR_SUBBLOCK_UCCFAST2		0x02200000
#define QE_CR_SUBBLOCK_UCCFAST3		0x02400000
#define QE_CR_SUBBLOCK_UCCFAST4		0x02600000
#define QE_CR_SUBBLOCK_UCCFAST5		0x02800000
#define QE_CR_SUBBLOCK_UCCFAST6		0x02a00000
#define QE_CR_SUBBLOCK_UCCFAST7		0x02c00000
#define QE_CR_SUBBLOCK_UCCFAST8		0x02e00000
#define QE_CR_SUBBLOCK_UCCSLOW1		0x00000000
#define QE_CR_SUBBLOCK_UCCSLOW2		0x00200000
#define QE_CR_SUBBLOCK_UCCSLOW3		0x00400000
#define QE_CR_SUBBLOCK_UCCSLOW4		0x00600000
#define QE_CR_SUBBLOCK_UCCSLOW5		0x00800000
#define QE_CR_SUBBLOCK_UCCSLOW6		0x00a00000
#define QE_CR_SUBBLOCK_UCCSLOW7		0x00c00000
#define QE_CR_SUBBLOCK_UCCSLOW8		0x00e00000
#define QE_CR_SUBBLOCK_MCC1		0x03800000
#define QE_CR_SUBBLOCK_MCC2		0x03a00000
#define QE_CR_SUBBLOCK_MCC3		0x03000000
#define QE_CR_SUBBLOCK_IDMA1		0x02800000
#define QE_CR_SUBBLOCK_IDMA2		0x02a00000
#define QE_CR_SUBBLOCK_IDMA3		0x02c00000
#define QE_CR_SUBBLOCK_IDMA4		0x02e00000
#define QE_CR_SUBBLOCK_HPAC		0x01e00000
#define QE_CR_SUBBLOCK_SPI1		0x01400000
#define QE_CR_SUBBLOCK_SPI2		0x01600000
#define QE_CR_SUBBLOCK_RAND		0x01c00000
#define QE_CR_SUBBLOCK_TIMER		0x01e00000
#define QE_CR_SUBBLOCK_GENERAL		0x03c00000

/* QE CECR Protocol - For non-MCC, specifies mode for QE CECR command */
#define QE_CR_PROTOCOL_UNSPECIFIED	0x00	/* For all other protocols */
#define QE_CR_PROTOCOL_HDLC_TRANSPARENT	0x00
#define QE_CR_PROTOCOL_QMC		0x02
#define QE_CR_PROTOCOL_UART		0x04
#define QE_CR_PROTOCOL_ATM_POS		0x0A
#define QE_CR_PROTOCOL_ETHERNET		0x0C
#define QE_CR_PROTOCOL_L2_SWITCH	0x0D

/* BRG configuration register */
#define QE_BRGC_ENABLE		0x00010000
#define QE_BRGC_DIVISOR_SHIFT	1
#define QE_BRGC_DIVISOR_MAX	0xFFF
#define QE_BRGC_DIV16		1

/* QE Timers registers */
#define QE_GTCFR1_PCAS	0x80
#define QE_GTCFR1_STP2	0x20
#define QE_GTCFR1_RST2	0x10
#define QE_GTCFR1_GM2	0x08
#define QE_GTCFR1_GM1	0x04
#define QE_GTCFR1_STP1	0x02
#define QE_GTCFR1_RST1	0x01

/* SDMA registers */
#define QE_SDSR_BER1	0x02000000
#define QE_SDSR_BER2	0x01000000

#define QE_SDMR_GLB_1_MSK	0x80000000
#define QE_SDMR_ADR_SEL		0x20000000
#define QE_SDMR_BER1_MSK	0x02000000
#define QE_SDMR_BER2_MSK	0x01000000
#define QE_SDMR_EB1_MSK		0x00800000
#define QE_SDMR_ER1_MSK		0x00080000
#define QE_SDMR_ER2_MSK		0x00040000
#define QE_SDMR_CEN_MASK	0x0000E000
#define QE_SDMR_SBER_1		0x00000200
#define QE_SDMR_SBER_2		0x00000200
#define QE_SDMR_EB1_PR_MASK	0x000000C0
#define QE_SDMR_ER1_PR		0x00000008

#define QE_SDMR_CEN_SHIFT	13
#define QE_SDMR_EB1_PR_SHIFT	6

#define QE_SDTM_MSNUM_SHIFT	24

#define QE_SDEBCR_BA_MASK	0x01FFFFFF

/* Communication Processor */
#define QE_CP_CERCR_MEE		0x8000	/* Multi-user RAM ECC enable */
#define QE_CP_CERCR_IEE		0x4000	/* Instruction RAM ECC enable */
#define QE_CP_CERCR_CIR		0x0800	/* Common instruction RAM */

/* I-RAM */
#define QE_IRAM_IADD_AIE	0x80000000	/* Auto Increment Enable */
#define QE_IRAM_IADD_BADDR	0x00080000	/* Base Address */

/* UPC */
#define UPGCR_PROTOCOL	0x80000000	/* protocol ul2 or pl2 */
#define UPGCR_TMS	0x40000000	/* Transmit master/slave mode */
#define UPGCR_RMS	0x20000000	/* Receive master/slave mode */
#define UPGCR_ADDR	0x10000000	/* Master MPHY Addr multiplexing */
#define UPGCR_DIAG	0x01000000	/* Diagnostic mode */

/* UCC GUEMR register */
#define UCC_GUEMR_MODE_MASK_RX	0x02
#define UCC_GUEMR_MODE_FAST_RX	0x02
#define UCC_GUEMR_MODE_SLOW_RX	0x00
#define UCC_GUEMR_MODE_MASK_TX	0x01
#define UCC_GUEMR_MODE_FAST_TX	0x01
#define UCC_GUEMR_MODE_SLOW_TX	0x00
#define UCC_GUEMR_MODE_MASK (UCC_GUEMR_MODE_MASK_RX | UCC_GUEMR_MODE_MASK_TX)
#define UCC_GUEMR_SET_RESERVED3	0x10	/* Bit 3 in the guemr is reserved but
					   must be set 1 */

/* structure representing UCC SLOW parameter RAM */
struct ucc_slow_pram {
	__be16 rbase;		/* RX BD base address */
	__be16 tbase;		/* TX BD base address */
	u8 rbmr;		/* RX bus mode register (same as CPM's RFCR) */
	u8 tbmr;		/* TX bus mode register (same as CPM's TFCR) */
	__be16 mrblr;		/* Rx buffer length */
	__be32 rstate;		/* Rx internal state */
	__be32 rptr;		/* Rx internal data pointer */
	__be16 rbptr;		/* rb BD Pointer */
	__be16 rcount;		/* Rx internal byte count */
	__be32 rtemp;		/* Rx temp */
	__be32 tstate;		/* Tx internal state */
	__be32 tptr;		/* Tx internal data pointer */
	__be16 tbptr;		/* Tx BD pointer */
	__be16 tcount;		/* Tx byte count */
	__be32 ttemp;		/* Tx temp */
	__be32 rcrc;		/* temp receive CRC */
	__be32 tcrc;		/* temp transmit CRC */
} __attribute__ ((packed));

/* General UCC SLOW Mode Register (GUMRH & GUMRL) */
#define UCC_SLOW_GUMR_H_SAM_QMC		0x00000000
#define UCC_SLOW_GUMR_H_SAM_SATM	0x00008000
#define UCC_SLOW_GUMR_H_REVD		0x00002000
#define UCC_SLOW_GUMR_H_TRX		0x00001000
#define UCC_SLOW_GUMR_H_TTX		0x00000800
#define UCC_SLOW_GUMR_H_CDP		0x00000400
#define UCC_SLOW_GUMR_H_CTSP		0x00000200
#define UCC_SLOW_GUMR_H_CDS		0x00000100
#define UCC_SLOW_GUMR_H_CTSS		0x00000080
#define UCC_SLOW_GUMR_H_TFL		0x00000040
#define UCC_SLOW_GUMR_H_RFW		0x00000020
#define UCC_SLOW_GUMR_H_TXSY		0x00000010
#define UCC_SLOW_GUMR_H_4SYNC		0x00000004
#define UCC_SLOW_GUMR_H_8SYNC		0x00000008
#define UCC_SLOW_GUMR_H_16SYNC		0x0000000c
#define UCC_SLOW_GUMR_H_RTSM		0x00000002
#define UCC_SLOW_GUMR_H_RSYN		0x00000001

#define UCC_SLOW_GUMR_L_TCI		0x10000000
#define UCC_SLOW_GUMR_L_RINV		0x02000000
#define UCC_SLOW_GUMR_L_TINV		0x01000000
#define UCC_SLOW_GUMR_L_TEND		0x00040000
#define UCC_SLOW_GUMR_L_TDCR_MASK	0x00030000
#define UCC_SLOW_GUMR_L_TDCR_32	        0x00030000
#define UCC_SLOW_GUMR_L_TDCR_16	        0x00020000
#define UCC_SLOW_GUMR_L_TDCR_8	        0x00010000
#define UCC_SLOW_GUMR_L_TDCR_1	        0x00000000
#define UCC_SLOW_GUMR_L_RDCR_MASK	0x0000c000
#define UCC_SLOW_GUMR_L_RDCR_32		0x0000c000
#define UCC_SLOW_GUMR_L_RDCR_16	        0x00008000
#define UCC_SLOW_GUMR_L_RDCR_8	        0x00004000
#define UCC_SLOW_GUMR_L_RDCR_1		0x00000000
#define UCC_SLOW_GUMR_L_RENC_NRZI	0x00000800
#define UCC_SLOW_GUMR_L_RENC_NRZ	0x00000000
#define UCC_SLOW_GUMR_L_TENC_NRZI	0x00000100
#define UCC_SLOW_GUMR_L_TENC_NRZ	0x00000000
#define UCC_SLOW_GUMR_L_DIAG_MASK	0x000000c0
#define UCC_SLOW_GUMR_L_DIAG_LE	        0x000000c0
#define UCC_SLOW_GUMR_L_DIAG_ECHO	0x00000080
#define UCC_SLOW_GUMR_L_DIAG_LOOP	0x00000040
#define UCC_SLOW_GUMR_L_DIAG_NORM	0x00000000
#define UCC_SLOW_GUMR_L_ENR		0x00000020
#define UCC_SLOW_GUMR_L_ENT		0x00000010
#define UCC_SLOW_GUMR_L_MODE_MASK	0x0000000F
#define UCC_SLOW_GUMR_L_MODE_BISYNC	0x00000008
#define UCC_SLOW_GUMR_L_MODE_AHDLC	0x00000006
#define UCC_SLOW_GUMR_L_MODE_UART	0x00000004
#define UCC_SLOW_GUMR_L_MODE_QMC	0x00000002

/* General UCC FAST Mode Register */
#define UCC_FAST_GUMR_TCI	0x20000000
#define UCC_FAST_GUMR_TRX	0x10000000
#define UCC_FAST_GUMR_TTX	0x08000000
#define UCC_FAST_GUMR_CDP	0x04000000
#define UCC_FAST_GUMR_CTSP	0x02000000
#define UCC_FAST_GUMR_CDS	0x01000000
#define UCC_FAST_GUMR_CTSS	0x00800000
#define UCC_FAST_GUMR_TXSY	0x00020000
#define UCC_FAST_GUMR_RSYN	0x00010000
#define UCC_FAST_GUMR_RTSM	0x00002000
#define UCC_FAST_GUMR_REVD	0x00000400
#define UCC_FAST_GUMR_ENR	0x00000020
#define UCC_FAST_GUMR_ENT	0x00000010

/* UART Slow UCC Event Register (UCCE) */
#define UCC_UART_UCCE_AB	0x0200
#define UCC_UART_UCCE_IDLE	0x0100
#define UCC_UART_UCCE_GRA	0x0080
#define UCC_UART_UCCE_BRKE	0x0040
#define UCC_UART_UCCE_BRKS	0x0020
#define UCC_UART_UCCE_CCR	0x0008
#define UCC_UART_UCCE_BSY	0x0004
#define UCC_UART_UCCE_TX	0x0002
#define UCC_UART_UCCE_RX	0x0001

/* HDLC Slow UCC Event Register (UCCE) */
#define UCC_HDLC_UCCE_GLR	0x1000
#define UCC_HDLC_UCCE_GLT	0x0800
#define UCC_HDLC_UCCE_IDLE	0x0100
#define UCC_HDLC_UCCE_BRKE	0x0040
#define UCC_HDLC_UCCE_BRKS	0x0020
#define UCC_HDLC_UCCE_TXE	0x0010
#define UCC_HDLC_UCCE_RXF	0x0008
#define UCC_HDLC_UCCE_BSY	0x0004
#define UCC_HDLC_UCCE_TXB	0x0002
#define UCC_HDLC_UCCE_RXB	0x0001

/* BISYNC Slow UCC Event Register (UCCE) */
#define UCC_BISYNC_UCCE_GRA	0x0080
#define UCC_BISYNC_UCCE_TXE	0x0010
#define UCC_BISYNC_UCCE_RCH	0x0008
#define UCC_BISYNC_UCCE_BSY	0x0004
#define UCC_BISYNC_UCCE_TXB	0x0002
#define UCC_BISYNC_UCCE_RXB	0x0001

/* Gigabit Ethernet Fast UCC Event Register (UCCE) */
#define UCC_GETH_UCCE_MPD       0x80000000
#define UCC_GETH_UCCE_SCAR      0x40000000
#define UCC_GETH_UCCE_GRA       0x20000000
#define UCC_GETH_UCCE_CBPR      0x10000000
#define UCC_GETH_UCCE_BSY       0x08000000
#define UCC_GETH_UCCE_RXC       0x04000000
#define UCC_GETH_UCCE_TXC       0x02000000
#define UCC_GETH_UCCE_TXE       0x01000000
#define UCC_GETH_UCCE_TXB7      0x00800000
#define UCC_GETH_UCCE_TXB6      0x00400000
#define UCC_GETH_UCCE_TXB5      0x00200000
#define UCC_GETH_UCCE_TXB4      0x00100000
#define UCC_GETH_UCCE_TXB3      0x00080000
#define UCC_GETH_UCCE_TXB2      0x00040000
#define UCC_GETH_UCCE_TXB1      0x00020000
#define UCC_GETH_UCCE_TXB0      0x00010000
#define UCC_GETH_UCCE_RXB7      0x00008000
#define UCC_GETH_UCCE_RXB6      0x00004000
#define UCC_GETH_UCCE_RXB5      0x00002000
#define UCC_GETH_UCCE_RXB4      0x00001000
#define UCC_GETH_UCCE_RXB3      0x00000800
#define UCC_GETH_UCCE_RXB2      0x00000400
#define UCC_GETH_UCCE_RXB1      0x00000200
#define UCC_GETH_UCCE_RXB0      0x00000100
#define UCC_GETH_UCCE_RXF7      0x00000080
#define UCC_GETH_UCCE_RXF6      0x00000040
#define UCC_GETH_UCCE_RXF5      0x00000020
#define UCC_GETH_UCCE_RXF4      0x00000010
#define UCC_GETH_UCCE_RXF3      0x00000008
#define UCC_GETH_UCCE_RXF2      0x00000004
#define UCC_GETH_UCCE_RXF1      0x00000002
#define UCC_GETH_UCCE_RXF0      0x00000001

/* UCC Protocol Specific Mode Register (UPSMR), when used for UART */
#define UCC_UART_UPSMR_FLC		0x8000
#define UCC_UART_UPSMR_SL		0x4000
#define UCC_UART_UPSMR_CL_MASK		0x3000
#define UCC_UART_UPSMR_CL_8		0x3000
#define UCC_UART_UPSMR_CL_7		0x2000
#define UCC_UART_UPSMR_CL_6		0x1000
#define UCC_UART_UPSMR_CL_5		0x0000
#define UCC_UART_UPSMR_UM_MASK		0x0c00
#define UCC_UART_UPSMR_UM_NORMAL	0x0000
#define UCC_UART_UPSMR_UM_MAN_MULTI	0x0400
#define UCC_UART_UPSMR_UM_AUTO_MULTI	0x0c00
#define UCC_UART_UPSMR_FRZ		0x0200
#define UCC_UART_UPSMR_RZS		0x0100
#define UCC_UART_UPSMR_SYN		0x0080
#define UCC_UART_UPSMR_DRT		0x0040
#define UCC_UART_UPSMR_PEN		0x0010
#define UCC_UART_UPSMR_RPM_MASK		0x000c
#define UCC_UART_UPSMR_RPM_ODD		0x0000
#define UCC_UART_UPSMR_RPM_LOW		0x0004
#define UCC_UART_UPSMR_RPM_EVEN		0x0008
#define UCC_UART_UPSMR_RPM_HIGH		0x000C
#define UCC_UART_UPSMR_TPM_MASK		0x0003
#define UCC_UART_UPSMR_TPM_ODD		0x0000
#define UCC_UART_UPSMR_TPM_LOW		0x0001
#define UCC_UART_UPSMR_TPM_EVEN		0x0002
#define UCC_UART_UPSMR_TPM_HIGH		0x0003

/* UCC Protocol Specific Mode Register (UPSMR), when used for Ethernet */
#define UCC_GETH_UPSMR_FTFE     0x80000000
#define UCC_GETH_UPSMR_PTPE     0x40000000
#define UCC_GETH_UPSMR_ECM      0x04000000
#define UCC_GETH_UPSMR_HSE      0x02000000
#define UCC_GETH_UPSMR_PRO      0x00400000
#define UCC_GETH_UPSMR_CAP      0x00200000
#define UCC_GETH_UPSMR_RSH      0x00100000
#define UCC_GETH_UPSMR_RPM      0x00080000
#define UCC_GETH_UPSMR_R10M     0x00040000
#define UCC_GETH_UPSMR_RLPB     0x00020000
#define UCC_GETH_UPSMR_TBIM     0x00010000
#define UCC_GETH_UPSMR_RES1     0x00002000
#define UCC_GETH_UPSMR_RMM      0x00001000
#define UCC_GETH_UPSMR_CAM      0x00000400
#define UCC_GETH_UPSMR_BRO      0x00000200

/* UCC Transmit On Demand Register (UTODR) */
#define UCC_SLOW_TOD	0x8000
#define UCC_FAST_TOD	0x8000

/* UCC Bus Mode Register masks */
/* Not to be confused with the Bundle Mode Register */
#define UCC_BMR_GBL		0x20
#define UCC_BMR_BO_BE		0x10
#define UCC_BMR_CETM		0x04
#define UCC_BMR_DTB		0x02
#define UCC_BMR_BDB		0x01

/* Function code masks */
#define FC_GBL				0x20
#define FC_DTB_LCL			0x02
#define UCC_FAST_FUNCTION_CODE_GBL	0x20
#define UCC_FAST_FUNCTION_CODE_DTB_LCL	0x02
#define UCC_FAST_FUNCTION_CODE_BDB_LCL	0x01

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_QE_H */
