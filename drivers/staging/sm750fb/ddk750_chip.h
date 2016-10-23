#ifndef DDK750_CHIP_H__
#define DDK750_CHIP_H__
#define DEFAULT_INPUT_CLOCK 14318181 /* Default reference clock */
#ifndef SM750LE_REVISION_ID
#define SM750LE_REVISION_ID ((unsigned char)0xfe)
#endif

#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/uaccess.h>

/* software control endianness */
#define PEEK32(addr) readl(addr + mmio750)
#define POKE32(addr, data) writel(data, addr + mmio750)

extern void __iomem *mmio750;

/* This is all the chips recognized by this library */
typedef enum _logical_chip_type_t {
	SM_UNKNOWN,
	SM718,
	SM750,
	SM750LE,
}
logical_chip_type_t;

typedef enum _clock_type_t {
	MXCLK_PLL,
	PRIMARY_PLL,
	SECONDARY_PLL,
	VGA0_PLL,
	VGA1_PLL,
}
clock_type_t;

struct pll_value {
	clock_type_t clockType;
	unsigned long inputFreq; /* Input clock frequency to the PLL */

	/* Use this when clockType = PANEL_PLL */
	unsigned long M;
	unsigned long N;
	unsigned long OD;
	unsigned long POD;
};

/* input struct to initChipParam() function */
struct initchip_param {
	/* Use power mode 0 or 1 */
	unsigned short powerMode;

	/*
	 * Speed of main chip clock in MHz unit
	 * 0 = keep the current clock setting
	 * Others = the new main chip clock
	 */
	unsigned short chipClock;

	/*
	 * Speed of memory clock in MHz unit
	 * 0 = keep the current clock setting
	 * Others = the new memory clock
	 */
	unsigned short memClock;

	/*
	 * Speed of master clock in MHz unit
	 * 0 = keep the current clock setting
	 * Others = the new master clock
	 */
	unsigned short masterClock;

	/*
	 * 0 = leave all engine state untouched.
	 * 1 = make sure they are off: 2D, Overlay,
	 * video alpha, alpha, hardware cursors
	 */
	unsigned short setAllEngOff;

	/*
	 * 0 = Do not reset the memory controller
	 * 1 = Reset the memory controller
	 */
	unsigned char resetMemory;

	/* More initialization parameter can be added if needed */
};

logical_chip_type_t sm750_get_chip_type(void);
void sm750_set_chip_type(unsigned short devId, char revId);
unsigned int calc_pll_value(unsigned int request, struct  pll_value *pll);
unsigned int format_pll_reg(struct pll_value *pPLL);
unsigned int ddk750_get_vm_size(void);
int ddk750_init_hw(struct initchip_param *);

#endif
