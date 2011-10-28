/*
 OMAP3430 ZOOM MDK astoria interface defs(cyasmemmap.h)
## ===========================
## Copyright (C) 2010  Cypress Semiconductor
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin Street, Fifth Floor
## Boston, MA  02110-1301, USA.
## ===========================
*/
/* include does not seem to work
 * moving for patch submission
#include <mach/gpmc.h>
#include <mach/mux.h>
*/
#include <linux/../../arch/arm/plat-omap/include/plat/gpmc.h>
#include <linux/../../arch/arm/plat-omap/include/plat/mux.h>

#ifndef _INCLUDED_CYASMEMMAP_H_
#define _INCLUDED_CYASMEMMAP_H_

/* defines copied from OMAP kernel branch */

#define OMAP2_PULL_UP		(1 << 4)
#define OMAP2_PULL_ENA		(1 << 3)
#define	OMAP34XX_MUX_MODE0	0
#define	OMAP34XX_MUX_MODE4	4
#define OMAP3_INPUT_EN		(1 << 8)
#define OMAP34XX_PIN_INPUT_PULLUP	(OMAP2_PULL_ENA | OMAP3_INPUT_EN \
						| OMAP2_PULL_UP)

/*
 * for OMAP3430 <-> astoria :   ADmux mode, 8 bit data path
 * WB Signal-	OMAP3430 signal	    COMMENTS
 *  --------------------------- --------------------
 * CS_L	 -GPMC_nCS4_GPIO_53	ZOOM I SOM board
 *								signal: up_nCS_A_EXT
 * AD[7:0]-upD[7:0]		  	buffered on the
 *								transposer board
 * 							GPMC_ADDR
 *							[A8:A1]->upD[7:0]
 * INT#	-GPMC_nWP_GPIO_62
 * DACK	-N/C				 not connected
 * WAKEUP-GPIO_167
 * RESET-GPIO_126
 * R/B	-GPMC_WAIT2_GPIO_64
 * -------------------------------------------
 * The address range for nCS1B is 0x06000000 - 0x07FF FFFF.
*/

/*
 *OMAP_ZOOM LEDS
 */
#define LED_0 156
#define LED_1 128
#define LED_2 64
#define LED_3 60

#define HIGH 1
#define LOW  1

/*
 *omap GPIO number
 */
#define AST_WAKEUP	 167
#define AST_RESET	 126
#define AST__rn_b	 64

/*
 * NOTE THIS PIN IS USED AS WP for OMAP NAND
 */
#define AST_INT	 62

/*
 * as an I/O, it is actually controlled by GPMC
 */
#define AST_CS	55


/*
 *GPMC prefetch engine
 */

/* register and its bit fields */
#define GPMC_PREFETCH_CONFIG1 0x01E0

	/*32 bytes for 16 bit pnand mode*/
	#define PFE_THRESHOLD 31

	/*
	 * bit fields
	 * PF_ACCESSMODE  - 0 - read mode, 1 - write mode
	 * PF_DMAMODE - 0 - default only intr line signal will be generated
	 * PF_SYNCHROMODE - default 0 - engin will start access as soon as
	 *					ctrl re STARTENGINE is set
	 * PF_WAITPINSEL - FOR synchro mode  selects WAIT pin whch edge
	 *					will be monitored
	 * PF_EN_ENGINE - 1- ENABLES ENGINE, but it needs to be started after
	 *					that C ctrl reg bit 0
	 * PF_FIFO_THRESHOLD - FIFO threshold in number of BUS(8 or 16) words
	 * PF_WEIGHTED_PRIO  - NUM of cycles granted to PFE if RND_ROBIN
	 *					prioritization is enabled
	 * PF_ROUND_ROBIN  - if enabled, gives priority to other CS, but
	 *					reserves NUM of cycles for PFE's turn
	 * PF_ENGIN_CS_SEL  - GPMC CS assotiated with PFE function
	 */
	#define PF_ACCESSMODE  (0 << 0)
	#define PF_DMAMODE	 (0 << 2)
	#define PF_SYNCHROMODE (0 << 3)
	#define PF_WAITPINSEL  (0x0 << 4)
	#define PF_EN_ENGINE   (1 << 7)
	#define PF_FIFO_THRESHOLD (PFE_THRESHOLD << 8)
	#define PF_WEIGHTED_PRIO (0x0 << 16)
	#define PF_ROUND_ROBIN   (0 << 23)
	#define PF_ENGIN_CS_SEL (AST_GPMC_CS << 24)
	#define PF_EN_OPTIM_ACC (0 << 27)
	#define PF_CYCLEOPTIM   (0x0 << 28)

#define GPMC_PREFETCH_CONFIG1_VAL (PF_ACCESSMODE | \
				PF_DMAMODE | PF_SYNCHROMODE | \
				PF_WAITPINSEL | PF_EN_ENGINE | \
				PF_FIFO_THRESHOLD | PF_FIFO_THRESHOLD | \
				PF_WEIGHTED_PRIO | PF_ROUND_ROBIN | \
				PF_ENGIN_CS_SEL | PF_EN_OPTIM_ACC | \
				PF_CYCLEOPTIM)

/* register and its bit fields */
#define GPMC_PREFETCH_CONFIG2 0x01E4
	/*
	 * bit fields
	 * 14 bit field NOTE this counts is also
	 * is in number of BUS(8 or 16) words
	 */
	#define PF_TRANSFERCOUNT (0x000)


/* register and its bit fields */
#define GPMC_PREFETCH_CONTROL 0x01EC
	/*
	 * bit fields , ONLY BIT 0 is implemented
	 * PFWE engin must be programmed with this bit = 0
	 */
	#define PFPW_STARTENGINE (1 << 0)

/* register and its bit fields */
#define GPMC_PREFETCH_STATUS  0x01F0

	/* */
	#define PFE_FIFO_THRESHOLD (1 << 16)

/*
 * GPMC posted write/prefetch engine end
 */


/*
 * chip select number on GPMC ( 0..7 )
 */
#define AST_GPMC_CS 4

/*
 * not connected
 */
#define AST_DACK	00


/*
 * Physical address above the NAND flash
 * we use CS For mapping in OMAP3430 RAM space use 0x0600 0000
 */
#define CYAS_DEV_BASE_ADDR  (0x20000000)

#define CYAS_DEV_MAX_ADDR   (0xFF)
#define CYAS_DEV_ADDR_RANGE (CYAS_DEV_MAX_ADDR << 1)

#ifdef p_s_r_a_m_INTERFACE
 /* in CRAM or PSRAM mode OMAP A1..An wires-> Astoria, there is no A0 line */
 #define CYAS_DEV_CALC_ADDR(cyas_addr) (cyas_addr << 1)
 #define CYAS_DEV_CALC_EP_ADDR(ep) (ep << 1)
#else
 /*
  * For pNAND interface it depends on NAND emulation mode
  * SBD/LBD etc we use NON-LNA_LBD  mode, so it goes like this:
  * forlbd   <CMD><CA0,CA1,RA0,RA1,RA2> <CMD>,
  * where CA1 address must have bits 2,3 = "11"
  * ep is mapped into RA1 bits {4:0}
  */
 #define CYAS_DEV_CALC_ADDR(cyas_addr) (cyas_addr | 0x0c00)
 #define CYAS_DEV_CALC_EP_ADDR(ep) ep
#endif

/*
 *OMAP3430 i/o access macros
 */
#define IORD32(addr) (*(volatile u32  *)(addr))
#define IOWR32(addr, val) (*(volatile u32 *)(addr) = val)

#define IORD16(addr) (*(volatile u16  *)(addr))
#define IOWR16(addr, val) (*(volatile u16 *)(addr) = val)

#define IORD8(addr) (*(volatile u8  *)(addr))
#define IOWR8(addr, val) (*(volatile u8 *)(addr) = val)

/*
 * local defines for accessing to OMAP GPIO ***
 */
#define CTLPADCONF_BASE_ADDR 0x48002000
#define CTLPADCONF_SIZE 0x1000

#define GPIO1_BASE_ADDR 0x48310000
#define GPIO2_BASE_ADDR 0x49050000
#define GPIO3_BASE_ADDR 0x49052000
#define GPIO4_BASE_ADDR 0x49054000
#define GPIO5_BASE_ADDR 0x49056000
#define GPIO6_BASE_ADDR 0x49058000
#define GPIO_SPACE_SIZE 0x1000


/*
 * OMAP3430 GPMC timing for pNAND interface
 */
#define GPMC_BASE 0x6E000000
#define GPMC_REGION_SIZE 0x1000
#define GPMC_CONFIG_REG (0x50)

/*
 * bit 0 in the GPMC_CONFIG_REG
 */
#define NAND_FORCE_POSTED_WRITE_B 1

/*
 * WAIT2STATUS, must be (1 << 10)
 */
#define AS_WAIT_PIN_MASK (1 << 10)


/*
 * GPMC_CONFIG(reg number [1..7] [for chip sel CS[0..7])
 */
#define GPMC_CFG_REG(N, CS) ((0x60 + (4*(N-1))) + (0x30*CS))

/*
 *gpmc nand registers for CS4
 */
#define AST_GPMC_NAND_CMD		(0x7c + (0x30*AST_GPMC_CS))
#define AST_GPMC_NAND_ADDR		(0x80 + (0x30*AST_GPMC_CS))
#define AST_GPMC_NAND_DATA		(0x84 + (0x30*AST_GPMC_CS))

#define GPMC_STAT_REG		(0x54)
#define GPMC_ERR_TYPE	   (0x48)

/*
 * we get "gpmc_base" from kernel
 */
#define GPMC_VMA(offset) (gpmc_base + offset)

/*
 * GPMC CS space VMA start address
 */
#define GPMC_CS_VMA(offset) (gpmc_data_vma + offset)

/*
 * PAD_CFG mux space VMA
 */
#define PADCFG_VMA(offset) (iomux_vma + offset)

/*
 * CONFIG1: by default, sngle access, async r/w RD_MULTIPLE[30]
 * WR_MULTIPLE[28]; GPMC_FCL_DIV[1:0]
 */
#define GPMC_FCLK_DIV ((0) << 0)

/*
 * ADDITIONAL DIVIDER FOR ALL TIMING PARAMS
 */
#define TIME_GRAN_SCALE ((0) << 4)

/*
 * for use by gpmc_set_timings api, measured in ns, not clocks
 */
#define WB_GPMC_BUSCYC_t  (7 * 6)
#define WB_GPMC_CS_t_o_n	(0)
#define WB_GPMC_ADV_t_o_n   (0)
#define WB_GPMC_OE_t_o_n	(0)
#define WB_GPMC_OE_t_o_f_f   (5 * 6)
#define WB_GPMC_WE_t_o_n	(1 * 6)
#define WB_GPMC_WE_t_o_f_f   (5 * 6)
#define WB_GPMC_RDS_ADJ   (2 * 6)
#define WB_GPMC_RD_t_a_c_c   (WB_GPMC_OE_t_o_f_f + WB_GPMC_RDS_ADJ)
#define WB_GPMC_WR_t_a_c_c  (WB_GPMC_BUSCYC_t)

#define DIR_OUT	0
#define DIR_INP	1
#define DRV_HI	1
#define DRV_LO	0

/*
 * GPMC_CONFIG7[cs] register bit fields
 * AS_CS_MASK - 3 bit mask for  A26,A25,A24,
 * AS_CS_BADDR - 6 BIT VALUE  A29 ...A24
 * CSVALID_B - CSVALID bit on GPMC_CONFIG7[cs] register
 */
#define AS_CS_MASK	(0X7 << 8)
#define AS_CS_BADDR	 0x02
#define CSVALID_B (1 << 6)

/*
 * DEFINE OMAP34XX GPIO OFFSETS (should have been defined in kernel /arch
 * these are offsets from the BASE_ADDRESS of the GPIO BLOCK
 */
#define GPIO_REVISION		0x000
#define GPIO_SYSCONFIG		0x010
#define GPIO_SYSSTATUS1		0x014
#define GPIO_IRQSTATUS1		0x018
#define GPIO_IRQENABLE1		0x01C
#define GPIO_IRQSTATUS2		0x028
#define GPIO_CTRL		0x030
#define GPIO_OE			0x034
#define GPIO_DATA_IN		0x038
#define GPIO_DATA_OUT		0x03C
#define GPIO_LEVELDETECT0	   0x040
#define GPIO_LEVELDETECT1	   0x044
#define GPIO_RISINGDETECT	   0x048
#define GPIO_FALLINGDETECT	  0x04c
#define GPIO_CLEAR_DATAOUT	0x090
#define GPIO_SET_DATAOUT	0x094

typedef struct  {
	char	*name;
	u32		phy_addr;
	u32		virt_addr;
	u32		size;
} io2vma_tab_t;

/*
 * GPIO phy to translation VMA table
 */
static  io2vma_tab_t gpio_vma_tab[6] = {
		{"GPIO1_BASE_ADDR", GPIO1_BASE_ADDR , 0 , GPIO_SPACE_SIZE},
		{"GPIO2_BASE_ADDR", GPIO2_BASE_ADDR , 0 , GPIO_SPACE_SIZE},
		{"GPIO3_BASE_ADDR", GPIO3_BASE_ADDR , 0 , GPIO_SPACE_SIZE},
		{"GPIO4_BASE_ADDR", GPIO4_BASE_ADDR , 0 , GPIO_SPACE_SIZE},
		{"GPIO5_BASE_ADDR", GPIO5_BASE_ADDR , 0 , GPIO_SPACE_SIZE},
		{"GPIO6_BASE_ADDR", GPIO6_BASE_ADDR , 0 , GPIO_SPACE_SIZE}
};
/*
 * name - USER signal name assigned to the pin ( for printks)
 * mux_func -  enum index NAME for the pad_cfg function
 * pin_num - pin_number if mux_func is GPIO, if not a GPIO it is -1
 * mux_ptr - pointer to the corresponding pad_cfg_reg
 *			(used for pad release )
 * mux_save - preserve here original PAD_CNF value for this
 *			pin (used for pad release)
 * dir - if GPIO: 0 - OUT , 1 - IN
 * dir_save - save original pin direction
 * drv - initial drive level "0" or "1"
 * drv_save - save original pin drive level
 * valid - 1 if successfuly configured
*/
typedef struct  {
	char *name;
	u32 mux_func;
	int pin_num;
	u16 *mux_ptr;
	u16 mux_save;
	u8 dir;
	u8 dir_save;
	u8 drv;
	u8 drv_save;
	u8 valid;
} user_pad_cfg_t;

/*
 * need to ensure that enums are in sync with the
 * omap_mux_pin_cfg table, these enums designate
 * functions that OMAP pads can be configured to
 */
enum {
	B23_OMAP3430_GPIO_167,
	D23_OMAP3430_GPIO_126,
	H1_OMAP3430_GPIO_62,
	H1_OMAP3430_GPMC_n_w_p,
	T8_OMAP3430_GPMC_n_c_s4,
	T8_OMAP3430_GPIO_55,
	R25_OMAP3430_GPIO_156,
	R27_OMAP3430_GPIO_128,
	K8_OMAP3430_GPIO_64,
	K8_GPMC_WAIT2,
	G3_OMAP3430_GPIO_60,
	G3_OMAP3430_n_b_e0_CLE,
	C6_GPMC_WAIT3,
	J1_OMAP3430_GPIO_61,
	C6_OMAP3430_GPIO_65,

	END_OF_TABLE
};

/*
 * number of GPIOS we plan to grab
 */
#define GPIO_SLOTS 8

/*
 *  user_pads_init() reads(and saves) from/to this table
 *  used in conjunction with omap_3430_mux_t table in .h file
 *  because the way it's done in the kernel code
 *  TODO: implement restore of the the original cfg and i/o regs
 */

static user_pad_cfg_t user_pad_cfg[] = {
		 /*
		 * name,pad_func,pin_num, mux_ptr, mux_sav, dir,
		 *    dir_sav, drv, drv_save, valid
		 */
		{"AST_WAKEUP", B23_OMAP3430_GPIO_167, 167, NULL, 0,
				DIR_OUT, 0, DRV_HI, 0, 0},
		{"AST_RESET", D23_OMAP3430_GPIO_126, 126, NULL,	0,
				DIR_OUT, 0, DRV_HI, 0, 0},
		{"AST__rn_b", K8_GPMC_WAIT2, 64, NULL, 0,
				DIR_INP, 0,	0, 0, 0},
		{"AST_INTR", H1_OMAP3430_GPIO_62, 62, NULL, 0,
				DIR_INP, 0,	DRV_HI, 0, 0},
		{"AST_CS", T8_OMAP3430_GPMC_n_c_s4, 55, NULL, 0,
				DIR_OUT, 0,	DRV_HI, 0, 0},
		{"LED_0", R25_OMAP3430_GPIO_156, 156, NULL, 0,
				DIR_OUT, 0,	DRV_LO, 0, 0},
		{"LED_1", R27_OMAP3430_GPIO_128, 128, NULL, 0,
				DIR_OUT, 0,	DRV_LO, 0, 0},
		{"AST_CLE", G3_OMAP3430_n_b_e0_CLE , 60, NULL, 0,
				DIR_OUT, 0,	DRV_LO, 0, 0},
		/*
		 * Z terminator, must always be present
		 * for sanity check, don't remove
		 */
		{NULL}
};

#define GPIO_BANK(pin) (pin >> 5)
#define REG_WIDTH 32
#define GPIO_REG_VMA(pin_num, offset) \
	(gpio_vma_tab[GPIO_BANK(pin_num)].virt_addr + offset)

/*
 * OMAP GPIO_REG 32 BIT MASK for a bit or
 * flag in gpio_No[0..191]  apply it to a 32 bit
 * location to set clear or check on a corresponding
 * gpio bit or flag
 */
#define GPIO_REG_MASK(pin_num) (1 << \
		(pin_num - (GPIO_BANK(pin_num) * REG_WIDTH)))

/*
 * OMAP GPIO registers bitwise access macros
 */

#define OMAP_GPIO_BIT(pin_num, reg) \
	((*((u32 *)GPIO_REG_VMA(pin_num, reg)) \
	& GPIO_REG_MASK(pin_num)) ? 1 : 0)

#define RD_OMAP_GPIO_BIT(pin_num, v) OMAP_GPIO_BIT(pin_num, reg)

/*
 *these are superfast set/clr bitbang macro, 48ns cyc tyme
 */
#define OMAP_SET_GPIO(pin_num) \
	(*(u32 *)GPIO_REG_VMA(pin_num, GPIO_SET_DATAOUT) \
	= GPIO_REG_MASK(pin_num))
#define OMAP_CLR_GPIO(pin_num) \
	(*(u32 *)GPIO_REG_VMA(pin_num, GPIO_CLEAR_DATAOUT) \
	= GPIO_REG_MASK(pin_num))

#define WR_OMAP_GPIO_BIT(pin_num, v) \
	(v ? (*(u32 *)GPIO_REG_VMA(pin_num, \
	GPIO_SET_DATAOUT) = GPIO_REG_MASK(pin_num)) \
	: (*(u32 *)GPIO_REG_VMA(pin_num, \
	GPIO_CLEAR_DATAOUT) = GPIO_REG_MASK(pin_num)))

/*
 * Note this pin cfg mimicks similar implementation
 * in linux kernel, which unfortunately doesn't allow
 * us to dynamically insert new custom GPIO mux
 * configurations all REG definitions used in this
 * applications. to add a new pad_cfg function, insert
 * a new ENUM and new pin_cfg entry in omap_mux_pin_cfg[]
 * table below
 *
 * offset - note this is a word offset since the
 *		SCM regs are 16 bit packed in one 32 bit word
 * mux_val - just enough to describe pins used
 */
typedef struct  {
	char	*name;
	u16		offset;
	u16	 mux_val;
} omap_3430_mux_t;

/*
 * "OUTIN" is configuration when DATA reg drives the
 * pin but the level at the pin can be sensed
 */
#define PAD_AS_OUTIN (OMAP34XX_MUX_MODE4 | \
		OMAP34XX_PIN_OUTPUT | OMAP34XX_PIN_INPUT)

omap_3430_mux_t omap_mux_pin_cfg[] = {
	/*
	 * B23_OMAP3430_GPIO_167 - GPIO func to PAD 167 WB wakeup
	 * D23_OMAP3430_GPIO_126 - drive GPIO_126 ( AST RESET)
	 * H1_OMAP3430_GPIO_62 - need a pullup on this pin
	 * H1_OMAP3430_GPMC_n_w_p -  GPMC NAND CTRL n_w_p out
	 * T8_OMAP3430_GPMC_n_c_s4" - T8 is controlled b_y GPMC NAND ctrl
	 * R25_OMAP3430_GPIO_156 - OMAPZOOM drive LED_0
	 * R27_OMAP3430_GPIO_128 - OMAPZOOM drive LED_1
	 * K8_OMAP3430_GPIO_64 - OMAPZOOM drive LED_2
	 * K8_GPMC_WAIT2 - GPMC WAIT2 function on PAD K8
	 * G3_OMAP3430_GPIO_60 - OMAPZOOM drive LED_3
	 * G3_OMAP3430_n_b_e0_CLE -GPMC NAND ctrl CLE signal
	*/

	{"B23_OMAP3430_GPIO_167", 0x0130, (OMAP34XX_MUX_MODE4)},
	{"D23_OMAP3430_GPIO_126", 0x0132, (OMAP34XX_MUX_MODE4)},
	{"H1_OMAP3430_GPIO_62",   0x00CA, (OMAP34XX_MUX_MODE4 |
				OMAP3_INPUT_EN | OMAP34XX_PIN_INPUT_PULLUP) },
	{"H1_OMAP3430_GPMC_n_w_p",  0x00CA, (OMAP34XX_MUX_MODE0)},
	{"T8_OMAP3430_GPMC_n_c_s4", 0x00B6, (OMAP34XX_MUX_MODE0) },
	{"T8_OMAP3430_GPIO_55",   0x00B6, (OMAP34XX_MUX_MODE4) },
	{"R25_OMAP3430_GPIO_156", 0x018C, (OMAP34XX_MUX_MODE4) },
	{"R27_OMAP3430_GPIO_128", 0x0154, (OMAP34XX_MUX_MODE4) },
	{"K8_OMAP3430_GPIO_64",   0x00d0, (OMAP34XX_MUX_MODE4) },
	{"K8_GPMC_WAIT2",		  0x00d0, (OMAP34XX_MUX_MODE0) },
	{"G3_OMAP3430_GPIO_60",   0x00C6, (OMAP34XX_MUX_MODE4 |
				OMAP3_INPUT_EN)},
	{"G3_OMAP3430_n_b_e0_CLE",  0x00C6, (OMAP34XX_MUX_MODE0)},
	{"C6_GPMC_WAIT3", 0x00d2, (OMAP34XX_MUX_MODE0)},
	{"C6_OMAP3430_GPIO_65", 0x00d2, (OMAP34XX_MUX_MODE4 |
				OMAP3_INPUT_EN)},
	{"J1_OMAP3430_GPIO_61", 0x00C8, (OMAP34XX_MUX_MODE4 |
				OMAP3_INPUT_EN | OMAP34XX_PIN_INPUT_PULLUP)},
	/*
	 * don't remove, used for sanity check.
	 */
	{"END_OF_TABLE"}
};


#endif /* _INCLUDED_CYASMEMMAP_H_ */

/*[]*/
