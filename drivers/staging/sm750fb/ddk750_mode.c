
#include "ddk750_help.h"
#include "ddk750_reg.h"
#include "ddk750_mode.h"
#include "ddk750_chip.h"

/*
	SM750LE only:
    This function takes care extra registers and bit fields required to set
    up a mode in SM750LE

	Explanation about Display Control register:
    HW only supports 7 predefined pixel clocks, and clock select is
    in bit 29:27 of	Display Control register.
*/
static unsigned long displayControlAdjust_SM750LE(mode_parameter_t *pModeParam, unsigned long dispControl)
{
	unsigned long x, y;

	x = pModeParam->horizontal_display_end;
	y = pModeParam->vertical_display_end;

	/* SM750LE has to set up the top-left and bottom-right
	   registers as well.
	   Note that normal SM750/SM718 only use those two register for
	   auto-centering mode.
	 */
	POKE32(CRT_AUTO_CENTERING_TL, 0);

	POKE32(CRT_AUTO_CENTERING_BR,
		(((y - 1) << CRT_AUTO_CENTERING_BR_BOTTOM_SHIFT) &
			CRT_AUTO_CENTERING_BR_BOTTOM_MASK) |
		((x - 1) & CRT_AUTO_CENTERING_BR_RIGHT_MASK));

	/* Assume common fields in dispControl have been properly set before
	   calling this function.
	   This function only sets the extra fields in dispControl.
	 */

	/* Clear bit 29:27 of display control register */
	dispControl &= ~CRT_DISPLAY_CTRL_CLK_MASK;

	/* Set bit 29:27 of display control register for the right clock */
	/* Note that SM750LE only need to supported 7 resolutions. */
	if (x == 800 && y == 600)
		dispControl |= CRT_DISPLAY_CTRL_CLK_PLL41;
	else if (x == 1024 && y == 768)
		dispControl |= CRT_DISPLAY_CTRL_CLK_PLL65;
	else if (x == 1152 && y == 864)
		dispControl |= CRT_DISPLAY_CTRL_CLK_PLL80;
	else if (x == 1280 && y == 768)
		dispControl |= CRT_DISPLAY_CTRL_CLK_PLL80;
	else if (x == 1280 && y == 720)
		dispControl |= CRT_DISPLAY_CTRL_CLK_PLL74;
	else if (x == 1280 && y == 960)
		dispControl |= CRT_DISPLAY_CTRL_CLK_PLL108;
	else if (x == 1280 && y == 1024)
		dispControl |= CRT_DISPLAY_CTRL_CLK_PLL108;
	else /* default to VGA clock */
		dispControl |= CRT_DISPLAY_CTRL_CLK_PLL25;

	/* Set bit 25:24 of display controller */
	dispControl |= (CRT_DISPLAY_CTRL_CRTSELECT | CRT_DISPLAY_CTRL_RGBBIT);

	/* Set bit 14 of display controller */
	dispControl |= DISPLAY_CTRL_CLOCK_PHASE;

	POKE32(CRT_DISPLAY_CTRL, dispControl);

	return dispControl;
}



/* only timing related registers will be  programed */
static int programModeRegisters(mode_parameter_t *pModeParam, pll_value_t *pll)
{
	int ret = 0;
	int cnt = 0;
	unsigned int tmp, reg;

	if (pll->clockType == SECONDARY_PLL) {
		/* programe secondary pixel clock */
		POKE32(CRT_PLL_CTRL, formatPllReg(pll));
		POKE32(CRT_HORIZONTAL_TOTAL,
			(((pModeParam->horizontal_total - 1) <<
				CRT_HORIZONTAL_TOTAL_TOTAL_SHIFT) &
				CRT_HORIZONTAL_TOTAL_TOTAL_MASK) |
			((pModeParam->horizontal_display_end - 1) &
				CRT_HORIZONTAL_TOTAL_DISPLAY_END_MASK));

		POKE32(CRT_HORIZONTAL_SYNC,
			((pModeParam->horizontal_sync_width <<
				CRT_HORIZONTAL_SYNC_WIDTH_SHIFT) &
				CRT_HORIZONTAL_SYNC_WIDTH_MASK) |
			((pModeParam->horizontal_sync_start - 1) &
				CRT_HORIZONTAL_SYNC_START_MASK));

		POKE32(CRT_VERTICAL_TOTAL,
			(((pModeParam->vertical_total - 1) <<
				CRT_VERTICAL_TOTAL_TOTAL_SHIFT) &
				CRT_VERTICAL_TOTAL_TOTAL_MASK) |
			((pModeParam->vertical_display_end - 1) &
				CRT_VERTICAL_TOTAL_DISPLAY_END_MASK));

		POKE32(CRT_VERTICAL_SYNC,
			((pModeParam->vertical_sync_height <<
				CRT_VERTICAL_SYNC_HEIGHT_SHIFT) &
				CRT_VERTICAL_SYNC_HEIGHT_MASK) |
			((pModeParam->vertical_sync_start - 1) &
				CRT_VERTICAL_SYNC_START_MASK));


		tmp = DISPLAY_CTRL_TIMING | DISPLAY_CTRL_PLANE;
		if (pModeParam->vertical_sync_polarity)
			tmp |= DISPLAY_CTRL_VSYNC_PHASE;
		if (pModeParam->horizontal_sync_polarity)
			tmp |= DISPLAY_CTRL_HSYNC_PHASE;

		if (sm750_get_chip_type() == SM750LE) {
			displayControlAdjust_SM750LE(pModeParam, tmp);
		} else {
			reg = PEEK32(CRT_DISPLAY_CTRL) &
				~(DISPLAY_CTRL_VSYNC_PHASE |
				  DISPLAY_CTRL_HSYNC_PHASE |
				  DISPLAY_CTRL_TIMING | DISPLAY_CTRL_PLANE);

			 POKE32(CRT_DISPLAY_CTRL, tmp | reg);
		}

	} else if (pll->clockType == PRIMARY_PLL) {
		unsigned int reserved;

		POKE32(PANEL_PLL_CTRL, formatPllReg(pll));

		reg = ((pModeParam->horizontal_total - 1) <<
			PANEL_HORIZONTAL_TOTAL_TOTAL_SHIFT) &
			PANEL_HORIZONTAL_TOTAL_TOTAL_MASK;
		reg |= ((pModeParam->horizontal_display_end - 1) &
			PANEL_HORIZONTAL_TOTAL_DISPLAY_END_MASK);
		POKE32(PANEL_HORIZONTAL_TOTAL, reg);

		POKE32(PANEL_HORIZONTAL_SYNC,
			((pModeParam->horizontal_sync_width <<
				PANEL_HORIZONTAL_SYNC_WIDTH_SHIFT) &
				PANEL_HORIZONTAL_SYNC_WIDTH_MASK) |
			((pModeParam->horizontal_sync_start - 1) &
				PANEL_HORIZONTAL_SYNC_START_MASK));

		POKE32(PANEL_VERTICAL_TOTAL,
			(((pModeParam->vertical_total - 1) <<
				PANEL_VERTICAL_TOTAL_TOTAL_SHIFT) &
				PANEL_VERTICAL_TOTAL_TOTAL_MASK) |
			((pModeParam->vertical_display_end - 1) &
				PANEL_VERTICAL_TOTAL_DISPLAY_END_MASK));

		POKE32(PANEL_VERTICAL_SYNC,
			((pModeParam->vertical_sync_height <<
				PANEL_VERTICAL_SYNC_HEIGHT_SHIFT) &
				PANEL_VERTICAL_SYNC_HEIGHT_MASK) |
			((pModeParam->vertical_sync_start - 1) &
				PANEL_VERTICAL_SYNC_START_MASK));

		tmp = DISPLAY_CTRL_TIMING | DISPLAY_CTRL_PLANE;
		if (pModeParam->vertical_sync_polarity)
			tmp |= DISPLAY_CTRL_VSYNC_PHASE;
		if (pModeParam->horizontal_sync_polarity)
			tmp |= DISPLAY_CTRL_HSYNC_PHASE;
		if (pModeParam->clock_phase_polarity)
			tmp |= DISPLAY_CTRL_CLOCK_PHASE;

		reserved = PANEL_DISPLAY_CTRL_RESERVED_MASK |
			PANEL_DISPLAY_CTRL_VSYNC;

		reg = (PEEK32(PANEL_DISPLAY_CTRL) & ~reserved) &
			~(DISPLAY_CTRL_CLOCK_PHASE | DISPLAY_CTRL_VSYNC_PHASE |
			  DISPLAY_CTRL_HSYNC_PHASE | DISPLAY_CTRL_TIMING |
			  DISPLAY_CTRL_PLANE);

		/* May a hardware bug or just my test chip (not confirmed).
		* PANEL_DISPLAY_CTRL register seems requiring few writes
		* before a value can be successfully written in.
		* Added some masks to mask out the reserved bits.
		* Note: This problem happens by design. The hardware will wait for the
		*       next vertical sync to turn on/off the plane.
		*/

		POKE32(PANEL_DISPLAY_CTRL, tmp | reg);

		while ((PEEK32(PANEL_DISPLAY_CTRL) & ~reserved) !=
			(tmp | reg)) {
			cnt++;
			if (cnt > 1000)
				break;
			POKE32(PANEL_DISPLAY_CTRL, tmp | reg);
		}
	} else {
		ret = -1;
	}
	return ret;
}

int ddk750_setModeTiming(mode_parameter_t *parm, clock_type_t clock)
{
	pll_value_t pll;
	unsigned int uiActualPixelClk;

	pll.inputFreq = DEFAULT_INPUT_CLOCK;
	pll.clockType = clock;

	uiActualPixelClk = calcPllValue(parm->pixel_clock, &pll);
	if (sm750_get_chip_type() == SM750LE) {
		/* set graphic mode via IO method */
		outb_p(0x88, 0x3d4);
		outb_p(0x06, 0x3d5);
	}
	programModeRegisters(parm, &pll);
	return 0;
}


