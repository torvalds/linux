/* SPDX-License-Identifier: GPL-2.0 */
/*
*Copyright (C) 2004 Konrad Eisele (eiselekd@web.de,konrad@gaisler.com), Gaisler Research
*Copyright (C) 2004 Stefan Holst (mail@s-holst.de), Uni-Stuttgart
*Copyright (C) 2009 Daniel Hellstrom (daniel@gaisler.com),Konrad Eisele (konrad@gaisler.com) Aeroflex Gaisler AB
*/

#ifndef LEON_AMBA_H_INCLUDE
#define LEON_AMBA_H_INCLUDE

#ifndef __ASSEMBLER__

struct amba_prom_registers {
	unsigned int phys_addr;	/* The physical address of this register */
	unsigned int reg_size;	/* How many bytes does this register take up? */
};

#endif

/*
 *  The following defines the bits in the LEON UART Status Registers.
 */

#define LEON_REG_UART_STATUS_DR   0x00000001	/* Data Ready */
#define LEON_REG_UART_STATUS_TSE  0x00000002	/* TX Send Register Empty */
#define LEON_REG_UART_STATUS_THE  0x00000004	/* TX Hold Register Empty */
#define LEON_REG_UART_STATUS_BR   0x00000008	/* Break Error */
#define LEON_REG_UART_STATUS_OE   0x00000010	/* RX Overrun Error */
#define LEON_REG_UART_STATUS_PE   0x00000020	/* RX Parity Error */
#define LEON_REG_UART_STATUS_FE   0x00000040	/* RX Framing Error */
#define LEON_REG_UART_STATUS_ERR  0x00000078	/* Error Mask */

/*
 *  The following defines the bits in the LEON UART Ctrl Registers.
 */

#define LEON_REG_UART_CTRL_RE     0x00000001	/* Receiver enable */
#define LEON_REG_UART_CTRL_TE     0x00000002	/* Transmitter enable */
#define LEON_REG_UART_CTRL_RI     0x00000004	/* Receiver interrupt enable */
#define LEON_REG_UART_CTRL_TI     0x00000008	/* Transmitter irq */
#define LEON_REG_UART_CTRL_PS     0x00000010	/* Parity select */
#define LEON_REG_UART_CTRL_PE     0x00000020	/* Parity enable */
#define LEON_REG_UART_CTRL_FL     0x00000040	/* Flow control enable */
#define LEON_REG_UART_CTRL_LB     0x00000080	/* Loop Back enable */

#define LEON3_GPTIMER_EN 1
#define LEON3_GPTIMER_RL 2
#define LEON3_GPTIMER_LD 4
#define LEON3_GPTIMER_IRQEN 8
#define LEON3_GPTIMER_SEPIRQ 8
#define LEON3_GPTIMER_TIMERS 0x7

#define LEON23_REG_TIMER_CONTROL_EN    0x00000001 /* 1 = enable counting */
/* 0 = hold scalar and counter */
#define LEON23_REG_TIMER_CONTROL_RL    0x00000002 /* 1 = reload at 0 */
						  /* 0 = stop at 0 */
#define LEON23_REG_TIMER_CONTROL_LD    0x00000004 /* 1 = load counter */
						  /* 0 = no function */
#define LEON23_REG_TIMER_CONTROL_IQ    0x00000008 /* 1 = irq enable */
						  /* 0 = no function */

/*
 *  The following defines the bits in the LEON PS/2 Status Registers.
 */

#define LEON_REG_PS2_STATUS_DR   0x00000001	/* Data Ready */
#define LEON_REG_PS2_STATUS_PE   0x00000002	/* Parity error */
#define LEON_REG_PS2_STATUS_FE   0x00000004	/* Framing error */
#define LEON_REG_PS2_STATUS_KI   0x00000008	/* Keyboard inhibit */
#define LEON_REG_PS2_STATUS_RF   0x00000010	/* RX buffer full */
#define LEON_REG_PS2_STATUS_TF   0x00000020	/* TX buffer full */

/*
 *  The following defines the bits in the LEON PS/2 Ctrl Registers.
 */

#define LEON_REG_PS2_CTRL_RE 0x00000001	/* Receiver enable */
#define LEON_REG_PS2_CTRL_TE 0x00000002	/* Transmitter enable */
#define LEON_REG_PS2_CTRL_RI 0x00000004	/* Keyboard receive irq  */
#define LEON_REG_PS2_CTRL_TI 0x00000008	/* Keyboard transmit irq */

#define LEON3_IRQMPSTATUS_CPUNR     28
#define LEON3_IRQMPSTATUS_BROADCAST 27

#define GPTIMER_CONFIG_IRQNT(a)          (((a) >> 3) & 0x1f)
#define GPTIMER_CONFIG_ISSEP(a)          ((a) & (1 << 8))
#define GPTIMER_CONFIG_NTIMERS(a)        ((a) & (0x7))
#define LEON3_GPTIMER_CTRL_PENDING       0x10
#define LEON3_GPTIMER_CONFIG_NRTIMERS(c) ((c)->config & 0x7)
#define LEON3_GPTIMER_CTRL_ISPENDING(r)  (((r)&LEON3_GPTIMER_CTRL_PENDING) ? 1 : 0)

#ifndef __ASSEMBLER__

struct leon3_irqctrl_regs_map {
	u32 ilevel;
	u32 ipend;
	u32 iforce;
	u32 iclear;
	u32 mpstatus;
	u32 mpbroadcast;
	u32 notused02;
	u32 notused03;
	u32 ampctrl;
	u32 icsel[2];
	u32 notused13;
	u32 notused20;
	u32 notused21;
	u32 notused22;
	u32 notused23;
	u32 mask[16];
	u32 force[16];
	/* Extended IRQ registers */
	u32 intid[16];	/* 0xc0 */
	u32 unused[(0x1000-0x100)/4];
};

struct leon3_apbuart_regs_map {
	u32 data;
	u32 status;
	u32 ctrl;
	u32 scaler;
};

struct leon3_gptimerelem_regs_map {
	u32 val;
	u32 rld;
	u32 ctrl;
	u32 unused;
};

struct leon3_gptimer_regs_map {
	u32 scalar;
	u32 scalar_reload;
	u32 config;
	u32 unused;
	struct leon3_gptimerelem_regs_map e[8];
};

/*
 *  Types and structure used for AMBA Plug & Play bus scanning
 */

#define AMBA_MAXAPB_DEVS 64
#define AMBA_MAXAPB_DEVS_PERBUS 16

struct amba_device_table {
	int devnr;		   /* number of devices on AHB or APB bus */
	unsigned int *addr[16];    /* addresses to the devices configuration tables */
	unsigned int allocbits[1]; /* 0=unallocated, 1=allocated driver */
};

struct amba_apbslv_device_table {
	int devnr;		                  /* number of devices on AHB or APB bus */
	unsigned int *addr[AMBA_MAXAPB_DEVS];     /* addresses to the devices configuration tables */
	unsigned int apbmst[AMBA_MAXAPB_DEVS];    /* apb master if a entry is a apb slave */
	unsigned int apbmstidx[AMBA_MAXAPB_DEVS]; /* apb master idx if a entry is a apb slave */
	unsigned int allocbits[4];                /* 0=unallocated, 1=allocated driver */
};

struct amba_confarea_type {
	struct amba_confarea_type *next;/* next bus in chain */
	struct amba_device_table ahbmst;
	struct amba_device_table ahbslv;
	struct amba_apbslv_device_table apbslv;
	unsigned int apbmst;
};

/* collect apb slaves */
struct amba_apb_device {
	unsigned int start, irq, bus_id;
	struct amba_confarea_type *bus;
};

/* collect ahb slaves */
struct amba_ahb_device {
	unsigned int start[4], irq, bus_id;
	struct amba_confarea_type *bus;
};

struct device_node;
void _amba_init(struct device_node *dp, struct device_node ***nextp);

extern unsigned long amba_system_id;
extern struct leon3_irqctrl_regs_map *leon3_irqctrl_regs;
extern struct leon3_gptimer_regs_map *leon3_gptimer_regs;
extern struct amba_apb_device leon_percpu_timer_dev[16];
extern int leondebug_irq_disable;
extern int leon_debug_irqout;
extern unsigned long leon3_gptimer_irq;
extern unsigned int sparc_leon_eirq;

#endif /* __ASSEMBLER__ */

#define LEON3_IO_AREA 0xfff00000
#define LEON3_CONF_AREA 0xff000
#define LEON3_AHB_SLAVE_CONF_AREA (1 << 11)

#define LEON3_AHB_CONF_WORDS 8
#define LEON3_APB_CONF_WORDS 2
#define LEON3_AHB_MASTERS 16
#define LEON3_AHB_SLAVES 16
#define LEON3_APB_SLAVES 16
#define LEON3_APBUARTS 8

/* Vendor codes */
#define VENDOR_GAISLER   1
#define VENDOR_PENDER    2
#define VENDOR_ESA       4
#define VENDOR_OPENCORES 8

/* Gaisler Research device id's */
#define GAISLER_LEON3    0x003
#define GAISLER_LEON3DSU 0x004
#define GAISLER_ETHAHB   0x005
#define GAISLER_APBMST   0x006
#define GAISLER_AHBUART  0x007
#define GAISLER_SRCTRL   0x008
#define GAISLER_SDCTRL   0x009
#define GAISLER_APBUART  0x00C
#define GAISLER_IRQMP    0x00D
#define GAISLER_AHBRAM   0x00E
#define GAISLER_GPTIMER  0x011
#define GAISLER_PCITRG   0x012
#define GAISLER_PCISBRG  0x013
#define GAISLER_PCIFBRG  0x014
#define GAISLER_PCITRACE 0x015
#define GAISLER_PCIDMA   0x016
#define GAISLER_AHBTRACE 0x017
#define GAISLER_ETHDSU   0x018
#define GAISLER_PIOPORT  0x01A
#define GAISLER_GRGPIO   0x01A
#define GAISLER_AHBJTAG  0x01c
#define GAISLER_ETHMAC   0x01D
#define GAISLER_AHB2AHB  0x020
#define GAISLER_USBDC    0x021
#define GAISLER_ATACTRL  0x024
#define GAISLER_DDRSPA   0x025
#define GAISLER_USBEHC   0x026
#define GAISLER_USBUHC   0x027
#define GAISLER_I2CMST   0x028
#define GAISLER_SPICTRL  0x02D
#define GAISLER_DDR2SPA  0x02E
#define GAISLER_SPIMCTRL 0x045
#define GAISLER_LEON4    0x048
#define GAISLER_LEON4DSU 0x049
#define GAISLER_AHBSTAT  0x052
#define GAISLER_FTMCTRL  0x054
#define GAISLER_KBD      0x060
#define GAISLER_VGA      0x061
#define GAISLER_SVGA     0x063
#define GAISLER_GRSYSMON 0x066
#define GAISLER_GRACECTRL 0x067

#define GAISLER_L2TIME   0xffd	/* internal device: leon2 timer */
#define GAISLER_L2C      0xffe	/* internal device: leon2compat */
#define GAISLER_PLUGPLAY 0xfff	/* internal device: plug & play configarea */

/* Chip IDs */
#define AEROFLEX_UT699    0x0699
#define LEON4_NEXTREME1   0x0102
#define GAISLER_GR712RC   0x0712

#define amba_vendor(x) (((x) >> 24) & 0xff)

#define amba_device(x) (((x) >> 12) & 0xfff)

#endif
