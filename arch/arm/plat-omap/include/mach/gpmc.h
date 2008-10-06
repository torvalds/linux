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
#define GPMC_CONFIG1_DEVICETYPE_NAND    GPMC_CONFIG1_DEVICETYPE(1)
#define GPMC_CONFIG1_MUXADDDATA         (1 << 9)
#define GPMC_CONFIG1_TIME_PARA_GRAN     (1 << 4)
#define GPMC_CONFIG1_FCLK_DIV(val)      (val & 3)
#define GPMC_CONFIG1_FCLK_DIV2          (GPMC_CONFIG1_FCLK_DIV(1))
#define GPMC_CONFIG1_FCLK_DIV3          (GPMC_CONFIG1_FCLK_DIV(2))
#define GPMC_CONFIG1_FCLK_DIV4          (GPMC_CONFIG1_FCLK_DIV(3))

/*
 * Note that all values in this struct are in nanoseconds, while
 * the register values are in gpmc_fck cycles.
 */
struct gpmc_timings {
	/* Minimum clock period for synchronous mode */
	u16 sync_clk;

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
};

extern unsigned int gpmc_ns_to_ticks(unsigned int time_ns);
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
extern void gpmc_init(void);

#endif
