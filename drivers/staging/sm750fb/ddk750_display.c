// SPDX-License-Identifier: GPL-2.0
#include "ddk750_reg.h"
#include "ddk750_chip.h"
#include "ddk750_display.h"
#include "ddk750_power.h"
#include "ddk750_dvi.h"

static void set_display_control(int ctrl, int disp_state)
{
	/* state != 0 means turn on both timing & plane en_bit */
	unsigned long reg, val, reserved;
	int cnt = 0;

	if (!ctrl) {
		reg = PANEL_DISPLAY_CTRL;
		reserved = PANEL_DISPLAY_CTRL_RESERVED_MASK;
	} else {
		reg = CRT_DISPLAY_CTRL;
		reserved = CRT_DISPLAY_CTRL_RESERVED_MASK;
	}

	val = peek32(reg);
	if (disp_state) {
		/*
		 * Timing should be enabled first before enabling the
		 * plane because changing at the same time does not
		 * guarantee that the plane will also enabled or
		 * disabled.
		 */
		val |= DISPLAY_CTRL_TIMING;
		poke32(reg, val);

		val |= DISPLAY_CTRL_PLANE;

		/*
		 * Somehow the register value on the plane is not set
		 * until a few delay. Need to write and read it a
		 * couple times
		 */
		do {
			cnt++;
			poke32(reg, val);
		} while ((peek32(reg) & ~reserved) != (val & ~reserved));
		pr_debug("Set Plane enbit:after tried %d times\n", cnt);
	} else {
		/*
		 * When turning off, there is no rule on the
		 * programming sequence since whenever the clock is
		 * off, then it does not matter whether the plane is
		 * enabled or disabled.  Note: Modifying the plane bit
		 * will take effect on the next vertical sync. Need to
		 * find out if it is necessary to wait for 1 vsync
		 * before modifying the timing enable bit.
		 */
		val &= ~DISPLAY_CTRL_PLANE;
		poke32(reg, val);

		val &= ~DISPLAY_CTRL_TIMING;
		poke32(reg, val);
	}
}

static void primary_wait_vertical_sync(int delay)
{
	unsigned int status;

	/*
	 * Do not wait when the Primary PLL is off or display control is
	 * already off. This will prevent the software to wait forever.
	 */
	if (!(peek32(PANEL_PLL_CTRL) & PLL_CTRL_POWER) ||
	    !(peek32(PANEL_DISPLAY_CTRL) & DISPLAY_CTRL_TIMING))
		return;

	while (delay-- > 0) {
		/* Wait for end of vsync. */
		do {
			status = peek32(SYSTEM_CTRL);
		} while (status & SYSTEM_CTRL_PANEL_VSYNC_ACTIVE);

		/* Wait for start of vsync. */
		do {
			status = peek32(SYSTEM_CTRL);
		} while (!(status & SYSTEM_CTRL_PANEL_VSYNC_ACTIVE));
	}
}

static void swPanelPowerSequence(int disp, int delay)
{
	unsigned int reg;

	/* disp should be 1 to open sequence */
	reg = peek32(PANEL_DISPLAY_CTRL);
	reg |= (disp ? PANEL_DISPLAY_CTRL_FPEN : 0);
	poke32(PANEL_DISPLAY_CTRL, reg);
	primary_wait_vertical_sync(delay);

	reg = peek32(PANEL_DISPLAY_CTRL);
	reg |= (disp ? PANEL_DISPLAY_CTRL_DATA : 0);
	poke32(PANEL_DISPLAY_CTRL, reg);
	primary_wait_vertical_sync(delay);

	reg = peek32(PANEL_DISPLAY_CTRL);
	reg |= (disp ? PANEL_DISPLAY_CTRL_VBIASEN : 0);
	poke32(PANEL_DISPLAY_CTRL, reg);
	primary_wait_vertical_sync(delay);

	reg = peek32(PANEL_DISPLAY_CTRL);
	reg |= (disp ? PANEL_DISPLAY_CTRL_FPEN : 0);
	poke32(PANEL_DISPLAY_CTRL, reg);
	primary_wait_vertical_sync(delay);
}

void ddk750_setLogicalDispOut(enum disp_output output)
{
	unsigned int reg;

	if (output & PNL_2_USAGE) {
		/* set panel path controller select */
		reg = peek32(PANEL_DISPLAY_CTRL);
		reg &= ~PANEL_DISPLAY_CTRL_SELECT_MASK;
		reg |= (((output & PNL_2_MASK) >> PNL_2_OFFSET) <<
			PANEL_DISPLAY_CTRL_SELECT_SHIFT);
		poke32(PANEL_DISPLAY_CTRL, reg);
	}

	if (output & CRT_2_USAGE) {
		/* set crt path controller select */
		reg = peek32(CRT_DISPLAY_CTRL);
		reg &= ~CRT_DISPLAY_CTRL_SELECT_MASK;
		reg |= (((output & CRT_2_MASK) >> CRT_2_OFFSET) <<
			CRT_DISPLAY_CTRL_SELECT_SHIFT);
		/*se blank off */
		reg &= ~CRT_DISPLAY_CTRL_BLANK;
		poke32(CRT_DISPLAY_CTRL, reg);
	}

	if (output & PRI_TP_USAGE) {
		/* set primary timing and plane en_bit */
		set_display_control(0, (output & PRI_TP_MASK) >> PRI_TP_OFFSET);
	}

	if (output & SEC_TP_USAGE) {
		/* set secondary timing and plane en_bit*/
		set_display_control(1, (output & SEC_TP_MASK) >> SEC_TP_OFFSET);
	}

	if (output & PNL_SEQ_USAGE) {
		/* set  panel sequence */
		swPanelPowerSequence((output & PNL_SEQ_MASK) >> PNL_SEQ_OFFSET,
				     4);
	}

	if (output & DAC_USAGE)
		setDAC((output & DAC_MASK) >> DAC_OFFSET);

	if (output & DPMS_USAGE)
		ddk750_set_dpms((output & DPMS_MASK) >> DPMS_OFFSET);
}
