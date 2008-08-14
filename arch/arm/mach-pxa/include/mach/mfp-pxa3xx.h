#ifndef __ASM_ARCH_MFP_PXA3XX_H
#define __ASM_ARCH_MFP_PXA3XX_H

#define MFPR_BASE	(0x40e10000)
#define MFPR_SIZE	(PAGE_SIZE)

/* MFPR register bit definitions */
#define MFPR_PULL_SEL		(0x1 << 15)
#define MFPR_PULLUP_EN		(0x1 << 14)
#define MFPR_PULLDOWN_EN	(0x1 << 13)
#define MFPR_SLEEP_SEL		(0x1 << 9)
#define MFPR_SLEEP_OE_N		(0x1 << 7)
#define MFPR_EDGE_CLEAR		(0x1 << 6)
#define MFPR_EDGE_FALL_EN	(0x1 << 5)
#define MFPR_EDGE_RISE_EN	(0x1 << 4)

#define MFPR_SLEEP_DATA(x)	((x) << 8)
#define MFPR_DRIVE(x)		(((x) & 0x7) << 10)
#define MFPR_AF_SEL(x)		(((x) & 0x7) << 0)

#define MFPR_EDGE_NONE		(0)
#define MFPR_EDGE_RISE		(MFPR_EDGE_RISE_EN)
#define MFPR_EDGE_FALL		(MFPR_EDGE_FALL_EN)
#define MFPR_EDGE_BOTH		(MFPR_EDGE_RISE | MFPR_EDGE_FALL)

/*
 * Table that determines the low power modes outputs, with actual settings
 * used in parentheses for don't-care values. Except for the float output,
 * the configured driven and pulled levels match, so if there is a need for
 * non-LPM pulled output, the same configuration could probably be used.
 *
 * Output value  sleep_oe_n  sleep_data  pullup_en  pulldown_en  pull_sel
 *                 (bit 7)    (bit 8)    (bit 14)     (bit 13)   (bit 15)
 *
 * Input            0          X(0)        X(0)        X(0)       0
 * Drive 0          0          0           0           X(1)       0
 * Drive 1          0          1           X(1)        0	  0
 * Pull hi (1)      1          X(1)        1           0	  0
 * Pull lo (0)      1          X(0)        0           1	  0
 * Z (float)        1          X(0)        0           0	  0
 */
#define MFPR_LPM_INPUT		(0)
#define MFPR_LPM_DRIVE_LOW	(MFPR_SLEEP_DATA(0) | MFPR_PULLDOWN_EN)
#define MFPR_LPM_DRIVE_HIGH    	(MFPR_SLEEP_DATA(1) | MFPR_PULLUP_EN)
#define MFPR_LPM_PULL_LOW      	(MFPR_LPM_DRIVE_LOW  | MFPR_SLEEP_OE_N)
#define MFPR_LPM_PULL_HIGH     	(MFPR_LPM_DRIVE_HIGH | MFPR_SLEEP_OE_N)
#define MFPR_LPM_FLOAT         	(MFPR_SLEEP_OE_N)
#define MFPR_LPM_MASK		(0xe080)

/*
 * The pullup and pulldown state of the MFP pin at run mode is by default
 * determined by the selected alternate function. In case that some buggy
 * devices need to override this default behavior,  the definitions below
 * indicates the setting of corresponding MFPR bits
 *
 * Definition       pull_sel  pullup_en  pulldown_en
 * MFPR_PULL_NONE       0         0        0
 * MFPR_PULL_LOW        1         0        1
 * MFPR_PULL_HIGH       1         1        0
 * MFPR_PULL_BOTH       1         1        1
 */
#define MFPR_PULL_NONE		(0)
#define MFPR_PULL_LOW		(MFPR_PULL_SEL | MFPR_PULLDOWN_EN)
#define MFPR_PULL_BOTH		(MFPR_PULL_LOW | MFPR_PULLUP_EN)
#define MFPR_PULL_HIGH		(MFPR_PULL_SEL | MFPR_PULLUP_EN)

/* PXA3xx common MFP configurations - processor specific ones defined
 * in mfp-pxa300.h and mfp-pxa320.h
 */
#define GPIO0_GPIO		MFP_CFG(GPIO0, AF0)
#define GPIO1_GPIO		MFP_CFG(GPIO1, AF0)
#define GPIO2_GPIO		MFP_CFG(GPIO2, AF0)
#define GPIO3_GPIO		MFP_CFG(GPIO3, AF0)
#define GPIO4_GPIO		MFP_CFG(GPIO4, AF0)
#define GPIO5_GPIO		MFP_CFG(GPIO5, AF0)
#define GPIO6_GPIO		MFP_CFG(GPIO6, AF0)
#define GPIO7_GPIO		MFP_CFG(GPIO7, AF0)
#define GPIO8_GPIO		MFP_CFG(GPIO8, AF0)
#define GPIO9_GPIO		MFP_CFG(GPIO9, AF0)
#define GPIO10_GPIO		MFP_CFG(GPIO10, AF0)
#define GPIO11_GPIO		MFP_CFG(GPIO11, AF0)
#define GPIO12_GPIO		MFP_CFG(GPIO12, AF0)
#define GPIO13_GPIO		MFP_CFG(GPIO13, AF0)
#define GPIO14_GPIO		MFP_CFG(GPIO14, AF0)
#define GPIO15_GPIO		MFP_CFG(GPIO15, AF0)
#define GPIO16_GPIO		MFP_CFG(GPIO16, AF0)
#define GPIO17_GPIO		MFP_CFG(GPIO17, AF0)
#define GPIO18_GPIO		MFP_CFG(GPIO18, AF0)
#define GPIO19_GPIO		MFP_CFG(GPIO19, AF0)
#define GPIO20_GPIO		MFP_CFG(GPIO20, AF0)
#define GPIO21_GPIO		MFP_CFG(GPIO21, AF0)
#define GPIO22_GPIO		MFP_CFG(GPIO22, AF0)
#define GPIO23_GPIO		MFP_CFG(GPIO23, AF0)
#define GPIO24_GPIO		MFP_CFG(GPIO24, AF0)
#define GPIO25_GPIO		MFP_CFG(GPIO25, AF0)
#define GPIO26_GPIO		MFP_CFG(GPIO26, AF0)
#define GPIO27_GPIO		MFP_CFG(GPIO27, AF0)
#define GPIO28_GPIO		MFP_CFG(GPIO28, AF0)
#define GPIO29_GPIO		MFP_CFG(GPIO29, AF0)
#define GPIO30_GPIO		MFP_CFG(GPIO30, AF0)
#define GPIO31_GPIO		MFP_CFG(GPIO31, AF0)
#define GPIO32_GPIO		MFP_CFG(GPIO32, AF0)
#define GPIO33_GPIO		MFP_CFG(GPIO33, AF0)
#define GPIO34_GPIO		MFP_CFG(GPIO34, AF0)
#define GPIO35_GPIO		MFP_CFG(GPIO35, AF0)
#define GPIO36_GPIO		MFP_CFG(GPIO36, AF0)
#define GPIO37_GPIO		MFP_CFG(GPIO37, AF0)
#define GPIO38_GPIO		MFP_CFG(GPIO38, AF0)
#define GPIO39_GPIO		MFP_CFG(GPIO39, AF0)
#define GPIO40_GPIO		MFP_CFG(GPIO40, AF0)
#define GPIO41_GPIO		MFP_CFG(GPIO41, AF0)
#define GPIO42_GPIO		MFP_CFG(GPIO42, AF0)
#define GPIO43_GPIO		MFP_CFG(GPIO43, AF0)
#define GPIO44_GPIO		MFP_CFG(GPIO44, AF0)
#define GPIO45_GPIO		MFP_CFG(GPIO45, AF0)

#define GPIO47_GPIO		MFP_CFG(GPIO47, AF0)
#define GPIO48_GPIO		MFP_CFG(GPIO48, AF0)

#define GPIO53_GPIO		MFP_CFG(GPIO53, AF0)
#define GPIO54_GPIO		MFP_CFG(GPIO54, AF0)
#define GPIO55_GPIO		MFP_CFG(GPIO55, AF0)

#define GPIO57_GPIO		MFP_CFG(GPIO57, AF0)

#define GPIO63_GPIO		MFP_CFG(GPIO63, AF0)
#define GPIO64_GPIO		MFP_CFG(GPIO64, AF0)
#define GPIO65_GPIO		MFP_CFG(GPIO65, AF0)
#define GPIO66_GPIO		MFP_CFG(GPIO66, AF0)
#define GPIO67_GPIO		MFP_CFG(GPIO67, AF0)
#define GPIO68_GPIO		MFP_CFG(GPIO68, AF0)
#define GPIO69_GPIO		MFP_CFG(GPIO69, AF0)
#define GPIO70_GPIO		MFP_CFG(GPIO70, AF0)
#define GPIO71_GPIO		MFP_CFG(GPIO71, AF0)
#define GPIO72_GPIO		MFP_CFG(GPIO72, AF0)
#define GPIO73_GPIO		MFP_CFG(GPIO73, AF0)
#define GPIO74_GPIO		MFP_CFG(GPIO74, AF0)
#define GPIO75_GPIO		MFP_CFG(GPIO75, AF0)
#define GPIO76_GPIO		MFP_CFG(GPIO76, AF0)
#define GPIO77_GPIO		MFP_CFG(GPIO77, AF0)
#define GPIO78_GPIO		MFP_CFG(GPIO78, AF0)
#define GPIO79_GPIO		MFP_CFG(GPIO79, AF0)
#define GPIO80_GPIO		MFP_CFG(GPIO80, AF0)
#define GPIO81_GPIO		MFP_CFG(GPIO81, AF0)
#define GPIO82_GPIO		MFP_CFG(GPIO82, AF0)
#define GPIO83_GPIO		MFP_CFG(GPIO83, AF0)
#define GPIO84_GPIO		MFP_CFG(GPIO84, AF0)
#define GPIO85_GPIO		MFP_CFG(GPIO85, AF0)
#define GPIO86_GPIO		MFP_CFG(GPIO86, AF0)
#define GPIO87_GPIO		MFP_CFG(GPIO87, AF0)
#define GPIO88_GPIO		MFP_CFG(GPIO88, AF0)
#define GPIO89_GPIO		MFP_CFG(GPIO89, AF0)
#define GPIO90_GPIO		MFP_CFG(GPIO90, AF0)
#define GPIO91_GPIO		MFP_CFG(GPIO91, AF0)
#define GPIO92_GPIO		MFP_CFG(GPIO92, AF0)
#define GPIO93_GPIO		MFP_CFG(GPIO93, AF0)
#define GPIO94_GPIO		MFP_CFG(GPIO94, AF0)
#define GPIO95_GPIO		MFP_CFG(GPIO95, AF0)
#define GPIO96_GPIO		MFP_CFG(GPIO96, AF0)
#define GPIO97_GPIO		MFP_CFG(GPIO97, AF0)
#define GPIO98_GPIO		MFP_CFG(GPIO98, AF0)
#define GPIO99_GPIO		MFP_CFG(GPIO99, AF0)
#define GPIO100_GPIO		MFP_CFG(GPIO100, AF0)
#define GPIO101_GPIO		MFP_CFG(GPIO101, AF0)
#define GPIO102_GPIO		MFP_CFG(GPIO102, AF0)
#define GPIO103_GPIO		MFP_CFG(GPIO103, AF0)
#define GPIO104_GPIO		MFP_CFG(GPIO104, AF0)
#define GPIO105_GPIO		MFP_CFG(GPIO105, AF0)
#define GPIO106_GPIO		MFP_CFG(GPIO106, AF0)
#define GPIO107_GPIO		MFP_CFG(GPIO107, AF0)
#define GPIO108_GPIO		MFP_CFG(GPIO108, AF0)
#define GPIO109_GPIO		MFP_CFG(GPIO109, AF0)
#define GPIO110_GPIO		MFP_CFG(GPIO110, AF0)
#define GPIO111_GPIO		MFP_CFG(GPIO111, AF0)
#define GPIO112_GPIO		MFP_CFG(GPIO112, AF0)
#define GPIO113_GPIO		MFP_CFG(GPIO113, AF0)
#define GPIO114_GPIO		MFP_CFG(GPIO114, AF0)
#define GPIO115_GPIO		MFP_CFG(GPIO115, AF0)
#define GPIO116_GPIO		MFP_CFG(GPIO116, AF0)
#define GPIO117_GPIO		MFP_CFG(GPIO117, AF0)
#define GPIO118_GPIO		MFP_CFG(GPIO118, AF0)
#define GPIO119_GPIO		MFP_CFG(GPIO119, AF0)
#define GPIO120_GPIO		MFP_CFG(GPIO120, AF0)
#define GPIO121_GPIO		MFP_CFG(GPIO121, AF0)
#define GPIO122_GPIO		MFP_CFG(GPIO122, AF0)
#define GPIO123_GPIO		MFP_CFG(GPIO123, AF0)
#define GPIO124_GPIO		MFP_CFG(GPIO124, AF0)
#define GPIO125_GPIO		MFP_CFG(GPIO125, AF0)
#define GPIO126_GPIO		MFP_CFG(GPIO126, AF0)
#define GPIO127_GPIO		MFP_CFG(GPIO127, AF0)

#define GPIO0_2_GPIO		MFP_CFG(GPIO0_2, AF0)
#define GPIO1_2_GPIO		MFP_CFG(GPIO1_2, AF0)
#define GPIO2_2_GPIO		MFP_CFG(GPIO2_2, AF0)
#define GPIO3_2_GPIO		MFP_CFG(GPIO3_2, AF0)
#define GPIO4_2_GPIO		MFP_CFG(GPIO4_2, AF0)
#define GPIO5_2_GPIO		MFP_CFG(GPIO5_2, AF0)
#define GPIO6_2_GPIO		MFP_CFG(GPIO6_2, AF0)

/*
 * each MFP pin will have a MFPR register, since the offset of the
 * register varies between processors, the processor specific code
 * should initialize the pin offsets by pxa3xx_mfp_init_addr()
 *
 * pxa3xx_mfp_init_addr - accepts a table of "pxa3xx_mfp_addr_map"
 * structure, which represents a range of MFP pins from "start" to
 * "end", with the offset begining at "offset", to define a single
 * pin, let "end" = -1
 *
 * use
 *
 * MFP_ADDR_X() to define a range of pins
 * MFP_ADDR()   to define a single pin
 * MFP_ADDR_END to signal the end of pin offset definitions
 */
struct pxa3xx_mfp_addr_map {
	unsigned int	start;
	unsigned int	end;
	unsigned long	offset;
};

#define MFP_ADDR_X(start, end, offset) \
	{ MFP_PIN_##start, MFP_PIN_##end, offset }

#define MFP_ADDR(pin, offset) \
	{ MFP_PIN_##pin, -1, offset }

#define MFP_ADDR_END	{ MFP_PIN_INVALID, 0 }

/*
 * pxa3xx_mfp_read()/pxa3xx_mfp_write() - for direct read/write access
 * to the MFPR register
 */
unsigned long pxa3xx_mfp_read(int mfp);
void pxa3xx_mfp_write(int mfp, unsigned long mfpr_val);

/*
 * pxa3xx_mfp_config - configure the MFPR registers
 *
 * used by board specific initialization code
 */
void pxa3xx_mfp_config(unsigned long *mfp_cfgs, int num);

/*
 * pxa3xx_mfp_init_addr() - initialize the mapping between mfp pin
 * index and MFPR register offset
 *
 * used by processor specific code
 */
void __init pxa3xx_mfp_init_addr(struct pxa3xx_mfp_addr_map *);
void __init pxa3xx_init_mfp(void);
#endif /* __ASM_ARCH_MFP_PXA3XX_H */
