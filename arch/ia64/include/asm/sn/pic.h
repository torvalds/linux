/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2003 Silicon Graphics, Inc. All rights reserved.
 */
#ifndef _ASM_IA64_SN_PCI_PIC_H
#define _ASM_IA64_SN_PCI_PIC_H

/*
 * PIC AS DEVICE ZERO
 * ------------------
 *
 * PIC handles PCI/X busses.  PCI/X requires that the 'bridge' (i.e. PIC)
 * be designated as 'device 0'.   That is a departure from earlier SGI
 * PCI bridges.  Because of that we use config space 1 to access the
 * config space of the first actual PCI device on the bus.
 * Here's what the PIC manual says:
 *
 *     The current PCI-X bus specification now defines that the parent
 *     hosts bus bridge (PIC for example) must be device 0 on bus 0. PIC
 *     reduced the total number of devices from 8 to 4 and removed the
 *     device registers and windows, now only supporting devices 0,1,2, and
 *     3. PIC did leave all 8 configuration space windows. The reason was
 *     there was nothing to gain by removing them. Here in lies the problem.
 *     The device numbering we do using 0 through 3 is unrelated to the device
 *     numbering which PCI-X requires in configuration space. In the past we
 *     correlated Configs pace and our device space 0 <-> 0, 1 <-> 1, etc.
 *     PCI-X requires we start a 1, not 0 and currently the PX brick
 *     does associate our:
 *
 *         device 0 with configuration space window 1,
 *         device 1 with configuration space window 2,
 *         device 2 with configuration space window 3,
 *         device 3 with configuration space window 4.
 *
 * The net effect is that all config space access are off-by-one with
 * relation to other per-slot accesses on the PIC.
 * Here is a table that shows some of that:
 *
 *                               Internal Slot#
 *           |
 *           |     0         1        2         3
 * ----------|---------------------------------------
 * config    |  0x21000   0x22000  0x23000   0x24000
 *           |
 * even rrb  |  0[0]      n/a      1[0]      n/a	[] == implied even/odd
 *           |
 * odd rrb   |  n/a       0[1]     n/a       1[1]
 *           |
 * int dev   |  00       01        10        11
 *           |
 * ext slot# |  1        2         3         4
 * ----------|---------------------------------------
 */

#define PIC_ATE_TARGETID_SHFT           8
#define PIC_HOST_INTR_ADDR              0x0000FFFFFFFFFFFFUL
#define PIC_PCI64_ATTR_TARG_SHFT        60


/*****************************************************************************
 *********************** PIC MMR structure mapping ***************************
 *****************************************************************************/

/* NOTE: PIC WAR. PV#854697.  PIC does not allow writes just to [31:0]
 * of a 64-bit register.  When writing PIC registers, always write the
 * entire 64 bits.
 */

struct pic {

    /* 0x000000-0x00FFFF -- Local Registers */

    /* 0x000000-0x000057 -- Standard Widget Configuration */
    u64		p_wid_id;			/* 0x000000 */
    u64		p_wid_stat;			/* 0x000008 */
    u64		p_wid_err_upper;		/* 0x000010 */
    u64		p_wid_err_lower;		/* 0x000018 */
    #define p_wid_err p_wid_err_lower
    u64		p_wid_control;			/* 0x000020 */
    u64		p_wid_req_timeout;		/* 0x000028 */
    u64		p_wid_int_upper;		/* 0x000030 */
    u64		p_wid_int_lower;		/* 0x000038 */
    #define p_wid_int p_wid_int_lower
    u64		p_wid_err_cmdword;		/* 0x000040 */
    u64		p_wid_llp;			/* 0x000048 */
    u64		p_wid_tflush;			/* 0x000050 */

    /* 0x000058-0x00007F -- Bridge-specific Widget Configuration */
    u64		p_wid_aux_err;			/* 0x000058 */
    u64		p_wid_resp_upper;		/* 0x000060 */
    u64		p_wid_resp_lower;		/* 0x000068 */
    #define p_wid_resp p_wid_resp_lower
    u64		p_wid_tst_pin_ctrl;		/* 0x000070 */
    u64		p_wid_addr_lkerr;		/* 0x000078 */

    /* 0x000080-0x00008F -- PMU & MAP */
    u64		p_dir_map;			/* 0x000080 */
    u64		_pad_000088;			/* 0x000088 */

    /* 0x000090-0x00009F -- SSRAM */
    u64		p_map_fault;			/* 0x000090 */
    u64		_pad_000098;			/* 0x000098 */

    /* 0x0000A0-0x0000AF -- Arbitration */
    u64		p_arb;				/* 0x0000A0 */
    u64		_pad_0000A8;			/* 0x0000A8 */

    /* 0x0000B0-0x0000BF -- Number In A Can or ATE Parity Error */
    u64		p_ate_parity_err;		/* 0x0000B0 */
    u64		_pad_0000B8;			/* 0x0000B8 */

    /* 0x0000C0-0x0000FF -- PCI/GIO */
    u64		p_bus_timeout;			/* 0x0000C0 */
    u64		p_pci_cfg;			/* 0x0000C8 */
    u64		p_pci_err_upper;		/* 0x0000D0 */
    u64		p_pci_err_lower;		/* 0x0000D8 */
    #define p_pci_err p_pci_err_lower
    u64		_pad_0000E0[4];			/* 0x0000{E0..F8} */

    /* 0x000100-0x0001FF -- Interrupt */
    u64		p_int_status;			/* 0x000100 */
    u64		p_int_enable;			/* 0x000108 */
    u64		p_int_rst_stat;			/* 0x000110 */
    u64		p_int_mode;			/* 0x000118 */
    u64		p_int_device;			/* 0x000120 */
    u64		p_int_host_err;			/* 0x000128 */
    u64		p_int_addr[8];			/* 0x0001{30,,,68} */
    u64		p_err_int_view;			/* 0x000170 */
    u64		p_mult_int;			/* 0x000178 */
    u64		p_force_always[8];		/* 0x0001{80,,,B8} */
    u64		p_force_pin[8];			/* 0x0001{C0,,,F8} */

    /* 0x000200-0x000298 -- Device */
    u64		p_device[4];			/* 0x0002{00,,,18} */
    u64		_pad_000220[4];			/* 0x0002{20,,,38} */
    u64		p_wr_req_buf[4];		/* 0x0002{40,,,58} */
    u64		_pad_000260[4];			/* 0x0002{60,,,78} */
    u64		p_rrb_map[2];			/* 0x0002{80,,,88} */
    #define p_even_resp p_rrb_map[0]			/* 0x000280 */
    #define p_odd_resp  p_rrb_map[1]			/* 0x000288 */
    u64		p_resp_status;			/* 0x000290 */
    u64		p_resp_clear;			/* 0x000298 */

    u64		_pad_0002A0[12];		/* 0x0002{A0..F8} */

    /* 0x000300-0x0003F8 -- Buffer Address Match Registers */
    struct {
	u64	upper;				/* 0x0003{00,,,F0} */
	u64	lower;				/* 0x0003{08,,,F8} */
    } p_buf_addr_match[16];

    /* 0x000400-0x0005FF -- Performance Monitor Registers (even only) */
    struct {
	u64	flush_w_touch;			/* 0x000{400,,,5C0} */
	u64	flush_wo_touch;			/* 0x000{408,,,5C8} */
	u64	inflight;			/* 0x000{410,,,5D0} */
	u64	prefetch;			/* 0x000{418,,,5D8} */
	u64	total_pci_retry;		/* 0x000{420,,,5E0} */
	u64	max_pci_retry;			/* 0x000{428,,,5E8} */
	u64	max_latency;			/* 0x000{430,,,5F0} */
	u64	clear_all;			/* 0x000{438,,,5F8} */
    } p_buf_count[8];


    /* 0x000600-0x0009FF -- PCI/X registers */
    u64		p_pcix_bus_err_addr;		/* 0x000600 */
    u64		p_pcix_bus_err_attr;		/* 0x000608 */
    u64		p_pcix_bus_err_data;		/* 0x000610 */
    u64		p_pcix_pio_split_addr;		/* 0x000618 */
    u64		p_pcix_pio_split_attr;		/* 0x000620 */
    u64		p_pcix_dma_req_err_attr;	/* 0x000628 */
    u64		p_pcix_dma_req_err_addr;	/* 0x000630 */
    u64		p_pcix_timeout;			/* 0x000638 */

    u64		_pad_000640[120];		/* 0x000{640,,,9F8} */

    /* 0x000A00-0x000BFF -- PCI/X Read&Write Buffer */
    struct {
	u64	p_buf_addr;			/* 0x000{A00,,,AF0} */
	u64	p_buf_attr;			/* 0X000{A08,,,AF8} */
    } p_pcix_read_buf_64[16];

    struct {
	u64	p_buf_addr;			/* 0x000{B00,,,BE0} */
	u64	p_buf_attr;			/* 0x000{B08,,,BE8} */
	u64	p_buf_valid;			/* 0x000{B10,,,BF0} */
	u64	__pad1;				/* 0x000{B18,,,BF8} */
    } p_pcix_write_buf_64[8];

    /* End of Local Registers -- Start of Address Map space */

    char		_pad_000c00[0x010000 - 0x000c00];

    /* 0x010000-0x011fff -- Internal ATE RAM (Auto Parity Generation) */
    u64		p_int_ate_ram[1024];		/* 0x010000-0x011fff */

    /* 0x012000-0x013fff -- Internal ATE RAM (Manual Parity Generation) */
    u64		p_int_ate_ram_mp[1024];		/* 0x012000-0x013fff */

    char		_pad_014000[0x18000 - 0x014000];

    /* 0x18000-0x197F8 -- PIC Write Request Ram */
    u64		p_wr_req_lower[256];		/* 0x18000 - 0x187F8 */
    u64		p_wr_req_upper[256];		/* 0x18800 - 0x18FF8 */
    u64		p_wr_req_parity[256];		/* 0x19000 - 0x197F8 */

    char		_pad_019800[0x20000 - 0x019800];

    /* 0x020000-0x027FFF -- PCI Device Configuration Spaces */
    union {
	u8		c[0x1000 / 1];			/* 0x02{0000,,,7FFF} */
	u16	s[0x1000 / 2];			/* 0x02{0000,,,7FFF} */
	u32	l[0x1000 / 4];			/* 0x02{0000,,,7FFF} */
	u64	d[0x1000 / 8];			/* 0x02{0000,,,7FFF} */
	union {
	    u8	c[0x100 / 1];
	    u16	s[0x100 / 2];
	    u32	l[0x100 / 4];
	    u64	d[0x100 / 8];
	} f[8];
    } p_type0_cfg_dev[8];				/* 0x02{0000,,,7FFF} */

    /* 0x028000-0x028FFF -- PCI Type 1 Configuration Space */
    union {
	u8		c[0x1000 / 1];			/* 0x028000-0x029000 */
	u16	s[0x1000 / 2];			/* 0x028000-0x029000 */
	u32	l[0x1000 / 4];			/* 0x028000-0x029000 */
	u64	d[0x1000 / 8];			/* 0x028000-0x029000 */
	union {
	    u8	c[0x100 / 1];
	    u16	s[0x100 / 2];
	    u32	l[0x100 / 4];
	    u64	d[0x100 / 8];
	} f[8];
    } p_type1_cfg;					/* 0x028000-0x029000 */

    char		_pad_029000[0x030000-0x029000];

    /* 0x030000-0x030007 -- PCI Interrupt Acknowledge Cycle */
    union {
	u8		c[8 / 1];
	u16	s[8 / 2];
	u32	l[8 / 4];
	u64	d[8 / 8];
    } p_pci_iack;					/* 0x030000-0x030007 */

    char		_pad_030007[0x040000-0x030008];

    /* 0x040000-0x030007 -- PCIX Special Cycle */
    union {
	u8		c[8 / 1];
	u16	s[8 / 2];
	u32	l[8 / 4];
	u64	d[8 / 8];
    } p_pcix_cycle;					/* 0x040000-0x040007 */
};

#endif                          /* _ASM_IA64_SN_PCI_PIC_H */
