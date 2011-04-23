/*
 * General-Purpose Memory Controller for OMAP2
 *
 * Copyright (C) 2005-2006 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __OMAP2_GPMC_H
#define __OMAP2_GPMC_H

/* Maximum Number of Chip Selects */
#define GPMC_CS_NUM		8

#define GPMC_CS_CONFIG1		0x00
#define GPMC_CS_CONFIG2		0x04
#define GPMC_CS_CONFIG3		0x08
#define GPMC_CS_CONFIG4		0x0c
#define GPMC_CS_CONFIG5		0x10
#define GPMC_CS_CONFIG6		0x14
#define GPMC_CS_CONFIG7		0x18
#define GPMC_CS_NAND_COMMAND	0x1c
#define GPMC_CS_NAND_ADDRESS	0x20
#define GPMC_CS_NAND_DATA	0x24

/* Control Commands */
#define GPMC_CONFIG_RDY_BSY	0x00000001
#define GPMC_CONFIG_DEV_SIZE	0x00000002
#define GPMC_CONFIG_DEV_TYPE	0x00000003
#define GPMC_SET_IRQ_STATUS	0x00000004
#define GPMC_CONFIG_WP		0x00000005

#define GPMC_GET_IRQ_STATUS	0x00000006
#define GPMC_PREFETCH_FIFO_CNT	0x00000007 /* bytes available in FIFO for r/w */
#define GPMC_PREFETCH_COUNT	0x00000008 /* remaining bytes to be read/write*/
#define GPMC_STATUS_BUFFER	0x00000009 /* 1: buffer is available to write */

#define GPMC_NAND_COMMAND	0x0000000a
#define GPMC_NAND_ADDRESS	0x0000000b
#define GPMC_NAND_DATA		0x0000000c

#define GPMC_ENABLE_IRQ		0x0000000d

/* ECC commands */
#define GPMC_ECC_READ		0 /* Reset Hardware ECC for read */
#define GPMC_ECC_WRITE		1 /* Reset Hardware ECC for write */
#define GPMC_ECC_READSYN	2 /* Reset before syndrom is read back */

#define GPMC_CONFIG1_WRAPBURST_SUPP     (1 << 31)
#define GPMC_CONFIG1_READMULTIPLE_SUPP  (1 << 30)
#define GPMC_CONFIG1_READTYPE_ASYNC     (0 << 29)
#define GPMC_CONFIG1_READTYPE_SYNC      (1 << 29)
#define GPMC_CONFIG1_WRITEMULTIPLE_SUPP (1 << 28)
#define GPMC_CONFIG1_WRITETYPE_ASYNC    (0 << 27)
#define GPMC_CONFIG1_WRITETYPE_SYNC     (1 << 27)
#define GPMC_CONFIG1_CLKACTIVATIONTIME(val) ((val & 3) << 25)
#define GPMC_CONFIG1_PAGE_LEN(val)      ((val & 3) << 23)
#define GPMC_CONFIG1_WAIT_READ_MON      (1 << 22)
#define GPMC_CONFIG1_WAIT_WRITE_MON     (1 << 21)
#define GPMC_CONFIG1_WAIT_MON_IIME(val) ((val & 3) << 18)
#define GPMC_CONFIG1_WAIT_PIN_SEL(val)  ((val & 3) << 16)
#define GPMC_CONFIG1_DEVICESIZE(val)    ((val & 3) << 12)
#define GPMC_CONFIG1_DEVICESIZE_16      GPMC_CONFIG1_DEVICESIZE(1)
#define GPMC_CONFIG1_DEVICETYPE(val)    ((val & 3) << 10)
#define GPMC_CONFIG1_DEVICETYPE_NOR     GPMC_CONFIG1_DEVICETYPE(0)
#define GPMC_CONFIG1_MUXADDDATA         (1 << 9)
#define GPMC_CONFIG1_TIME_PARA_GRAN     (1 << 4)
#define GPMC_CONFIG1_FCLK_DIV(val)      (val & 3)
#define GPMC_CONFIG1_FCLK_DIV2          (GPMC_CONFIG1_FCLK_DIV(1))
#define GPMC_CONFIG1_FCLK_DIV3          (GPMC_CONFIG1_FCLK_DIV(2))
#define GPMC_CONFIG1_FCLK_DIV4          (GPMC_CONFIG1_FCLK_DIV(3))
#define GPMC_CONFIG7_CSVALID		(1 << 6)

#define GPMC_DEVICETYPE_NOR		0
#define GPMC_DEVICETYPE_NAND		2
#define GPMC_CONFIG_WRITEPROTECT	0x00000010
#define GPMC_STATUS_BUFF_EMPTY		0x00000001
#define WR_RD_PIN_MONITORING		0x00600000
#define GPMC_PREFETCH_STATUS_FIFO_CNT(val)	((val >> 24) & 0x7F)
#define GPMC_PREFETCH_STATUS_COUNT(val)	(val & 0x00003fff)
#define GPMC_IRQ_FIFOEVENTENABLE	0x01
#define GPMC_IRQ_COUNT_EVENT		0x02

#define PREFETCH_FIFOTHRESHOLD_MAX	0x40
#define PREFETCH_FIFOTHRESHOLD(val)	((val) << 8)

enum omap_ecc {
		/* 1-bit ecc: stored at end of spare area */
	OMAP_ECC_HAMMING_CODE_DEFAULT = 0, /* Default, s/w method */
	OMAP_ECC_HAMMING_CODE_HW, /* gpmc to detect the error */
		/* 1-bit ecc: stored at beginning of spare area as romcode */
	OMAP_ECC_HAMMING_CODE_HW_ROMCODE, /* gpmc method & romcode layout */
};

/*
 * Note that all values in this struct are in nanoseconds except sync_clk
 * (which is in picoseconds), while the register values are in gpmc_fck cycles.
 */
struct gpmc_timings {
	/* Minimum clock period for synchronous mode (in picoseconds) */
	u32 sync_clk;

	/* Chip-select signal timings corresponding to GPMC_CS_CONFIG2 */
	u16 cs_on;		/* Assertion time */
	u16 cs_rd_off;		/* Read deassertion time */
	u16 cs_wr_off;		/* Write deassertion time */

	/* ADV signal timings corresponding to GPMC_CONFIG3 */
	u16 adv_on;		/* Assertion time */
	u16 adv_rd_off;		/* Read deassertion time */
	u16 adv_wr_off;		/* Write deassertion time */

	/* WE signals timings corresponding to GPMC_CONFIG4 */
	u16 we_on;		/* WE assertion time */
	u16 we_off;		/* WE deassertion time */

	/* OE signals timings corresponding to GPMC_CONFIG4 */
	u16 oe_on;		/* OE assertion time */
	u16 oe_off;		/* OE deassertion time */

	/* Access time and cycle time timings corresponding to GPMC_CONFIG5 */
	u16 page_burst_access;	/* Multiple access word delay */
	u16 access;		/* Start-cycle to first data valid delay */
	u16 rd_cycle;		/* Total read cycle time */
	u16 wr_cycle;		/* Total write cycle time */

	/* The following are only on OMAP3430 */
	u16 wr_access;		/* WRACCESSTIME */
	u16 wr_data_mux_bus;	/* WRDATAONADMUXBUS */
};

extern unsigned int gpmc_ns_to_ticks(unsigned int time_ns);
extern unsigned int gpmc_ps_to_ticks(unsigned int time_ps);
extern unsigned int gpmc_ticks_to_ns(unsigned int ticks);
extern unsigned int gpmc_round_ns_to_ticks(unsigned int time_ns);
extern unsigned long gpmc_get_fclk_period(void);

extern void gpmc_cs_write_reg(int cs, int idx, u32 val);
extern u32 gpmc_cs_read_reg(int cs, int idx);
extern int gpmc_cs_calc_divider(int cs, unsigned int sync_clk);
extern int gpmc_cs_set_timings(int cs, const struct gpmc_timings *t);
extern int gpmc_cs_request(int cs, unsigned long size, unsigned long *base);
extern void gpmc_cs_free(int cs);
extern int gpmc_cs_set_reserved(int cs, int reserved);
extern int gpmc_cs_reserved(int cs);
extern int gpmc_prefetch_enable(int cs, int fifo_th, int dma_mode,
					unsigned int u32_count, int is_write);
extern int gpmc_prefetch_reset(int cs);
extern void omap3_gpmc_save_context(void);
extern void omap3_gpmc_restore_context(void);
extern int gpmc_read_status(int cmd);
extern int gpmc_cs_configure(int cs, int cmd, int wval);
extern int gpmc_nand_read(int cs, int cmd);
extern int gpmc_nand_write(int cs, int cmd, int wval);

int gpmc_enable_hwecc(int cs, int mode, int dev_width, int ecc_size);
int gpmc_calculate_ecc(int cs, const u_char *dat, u_char *ecc_code);
#endif
