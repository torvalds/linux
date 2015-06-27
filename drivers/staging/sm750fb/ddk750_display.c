#include "ddk750_reg.h"
#include "ddk750_help.h"
#include "ddk750_display.h"
#include "ddk750_power.h"
#include "ddk750_dvi.h"

#define primaryWaitVerticalSync(delay) waitNextVerticalSync(0, delay)

static void setDisplayControl(int ctrl, int dispState)
{
	/* state != 0 means turn on both timing & plane en_bit */
	unsigned long ulDisplayCtrlReg, ulReservedBits;
	int cnt;

	cnt = 0;

	/* Set the primary display control */
	if (!ctrl)
	{
		ulDisplayCtrlReg = PEEK32(PANEL_DISPLAY_CTRL);
		/* Turn on/off the Panel display control */
		if (dispState)
		{
			/* Timing should be enabled first before enabling the plane
			 * because changing at the same time does not guarantee that
			 * the plane will also enabled or disabled.
     	     */
			ulDisplayCtrlReg = FIELD_SET(ulDisplayCtrlReg,
								PANEL_DISPLAY_CTRL, TIMING, ENABLE);
			POKE32(PANEL_DISPLAY_CTRL, ulDisplayCtrlReg);

			ulDisplayCtrlReg = FIELD_SET(ulDisplayCtrlReg,
								PANEL_DISPLAY_CTRL, PLANE, ENABLE);

			/* Added some masks to mask out the reserved bits.
			 * Sometimes, the reserved bits are set/reset randomly when
			 * writing to the PRIMARY_DISPLAY_CTRL, therefore, the register
			 * reserved bits are needed to be masked out.
			 */
			ulReservedBits = FIELD_SET(0, PANEL_DISPLAY_CTRL, RESERVED_1_MASK, ENABLE) |
				FIELD_SET(0, PANEL_DISPLAY_CTRL, RESERVED_2_MASK, ENABLE) |
				FIELD_SET(0, PANEL_DISPLAY_CTRL, RESERVED_3_MASK, ENABLE);

			/* Somehow the register value on the plane is not set
			 * until a few delay. Need to write
			 * and read it a couple times
			 */
			do
			{
				cnt++;
				POKE32(PANEL_DISPLAY_CTRL, ulDisplayCtrlReg);
			} while((PEEK32(PANEL_DISPLAY_CTRL) & ~ulReservedBits) !=
					(ulDisplayCtrlReg & ~ulReservedBits));
			printk("Set Panel Plane enbit:after tried %d times\n", cnt);
		}
		else
		{
			/* When turning off, there is no rule on the programming
			 * sequence since whenever the clock is off, then it does not
			 * matter whether the plane is enabled or disabled.
			 * Note: Modifying the plane bit will take effect on the
			 * next vertical sync. Need to find out if it is necessary to
			 * wait for 1 vsync before modifying the timing enable bit.
			 * */
			ulDisplayCtrlReg = FIELD_SET(ulDisplayCtrlReg,
								PANEL_DISPLAY_CTRL, PLANE, DISABLE);
			POKE32(PANEL_DISPLAY_CTRL, ulDisplayCtrlReg);

			ulDisplayCtrlReg = FIELD_SET(ulDisplayCtrlReg,
								PANEL_DISPLAY_CTRL, TIMING, DISABLE);
			POKE32(PANEL_DISPLAY_CTRL, ulDisplayCtrlReg);
		}

	}
	/* Set the secondary display control */
	else
	{
		ulDisplayCtrlReg = PEEK32(CRT_DISPLAY_CTRL);

		if (dispState)
		{
			/* Timing should be enabled first before enabling the plane because changing at the
			   same time does not guarantee that the plane will also enabled or disabled.
			   */
			ulDisplayCtrlReg = FIELD_SET(ulDisplayCtrlReg,
								CRT_DISPLAY_CTRL, TIMING, ENABLE);
			POKE32(CRT_DISPLAY_CTRL, ulDisplayCtrlReg);

			ulDisplayCtrlReg = FIELD_SET(ulDisplayCtrlReg,
								CRT_DISPLAY_CTRL, PLANE, ENABLE);

			/* Added some masks to mask out the reserved bits.
			 * Sometimes, the reserved bits are set/reset randomly when
			 * writing to the PRIMARY_DISPLAY_CTRL, therefore, the register
			 * reserved bits are needed to be masked out.
			 */

			ulReservedBits = FIELD_SET(0, CRT_DISPLAY_CTRL, RESERVED_1_MASK, ENABLE) |
				FIELD_SET(0, CRT_DISPLAY_CTRL, RESERVED_2_MASK, ENABLE) |
				FIELD_SET(0, CRT_DISPLAY_CTRL, RESERVED_3_MASK, ENABLE) |
				FIELD_SET(0, CRT_DISPLAY_CTRL, RESERVED_4_MASK, ENABLE);

			do
			{
				cnt++;
				POKE32(CRT_DISPLAY_CTRL, ulDisplayCtrlReg);
			} while((PEEK32(CRT_DISPLAY_CTRL) & ~ulReservedBits) !=
					(ulDisplayCtrlReg & ~ulReservedBits));
				printk("Set Crt Plane enbit:after tried %d times\n", cnt);
		}
		else
		{
			/* When turning off, there is no rule on the programming
			 * sequence since whenever the clock is off, then it does not
			 * matter whether the plane is enabled or disabled.
			 * Note: Modifying the plane bit will take effect on the next
			 * vertical sync. Need to find out if it is necessary to
			 * wait for 1 vsync before modifying the timing enable bit.
			 */
			ulDisplayCtrlReg = FIELD_SET(ulDisplayCtrlReg,
								CRT_DISPLAY_CTRL, PLANE, DISABLE);
			POKE32(CRT_DISPLAY_CTRL, ulDisplayCtrlReg);

			ulDisplayCtrlReg = FIELD_SET(ulDisplayCtrlReg,
								CRT_DISPLAY_CTRL, TIMING, DISABLE);
			POKE32(CRT_DISPLAY_CTRL, ulDisplayCtrlReg);
		}
	}
}


static void waitNextVerticalSync(int ctrl, int delay)
{
	unsigned int status;
	if(!ctrl){
		/* primary controller */

        /* Do not wait when the Primary PLL is off or display control is already off.
	           This will prevent the software to wait forever. */
		if ((FIELD_GET(PEEK32(PANEL_PLL_CTRL), PANEL_PLL_CTRL, POWER) ==
			 PANEL_PLL_CTRL_POWER_OFF) ||
			(FIELD_GET(PEEK32(PANEL_DISPLAY_CTRL), PANEL_DISPLAY_CTRL, TIMING) ==
			 PANEL_DISPLAY_CTRL_TIMING_DISABLE))
		{
			return;
		}

        while (delay-- > 0)
        {
            /* Wait for end of vsync. */
            do
            {
                status = FIELD_GET(PEEK32(SYSTEM_CTRL),
                                   SYSTEM_CTRL,
                                   PANEL_VSYNC);
            }
            while (status == SYSTEM_CTRL_PANEL_VSYNC_ACTIVE);

            /* Wait for start of vsync. */
            do
            {
                status = FIELD_GET(PEEK32(SYSTEM_CTRL),
                                   SYSTEM_CTRL,
                                   PANEL_VSYNC);
            }
            while (status == SYSTEM_CTRL_PANEL_VSYNC_INACTIVE);
        }

	}else{

		/* Do not wait when the Primary PLL is off or display control is already off.
			   This will prevent the software to wait forever. */
		if ((FIELD_GET(PEEK32(CRT_PLL_CTRL), CRT_PLL_CTRL, POWER) ==
			 CRT_PLL_CTRL_POWER_OFF) ||
			(FIELD_GET(PEEK32(CRT_DISPLAY_CTRL), CRT_DISPLAY_CTRL, TIMING) ==
			 CRT_DISPLAY_CTRL_TIMING_DISABLE))
		{
			return;
		}

		while (delay-- > 0)
		{
			/* Wait for end of vsync. */
			do
			{
				status = FIELD_GET(PEEK32(SYSTEM_CTRL),
								   SYSTEM_CTRL,
								   CRT_VSYNC);
			}
			while (status == SYSTEM_CTRL_CRT_VSYNC_ACTIVE);

			/* Wait for start of vsync. */
			do
			{
				status = FIELD_GET(PEEK32(SYSTEM_CTRL),
								   SYSTEM_CTRL,
								   CRT_VSYNC);
			}
			while (status == SYSTEM_CTRL_CRT_VSYNC_INACTIVE);
		}
	}
}

static void swPanelPowerSequence(int disp, int delay)
{
	unsigned int reg;

	/* disp should be 1 to open sequence */
	reg = PEEK32(PANEL_DISPLAY_CTRL);
	reg = FIELD_VALUE(reg, PANEL_DISPLAY_CTRL, FPEN, disp);
	POKE32(PANEL_DISPLAY_CTRL, reg);
	primaryWaitVerticalSync(delay);


	reg = PEEK32(PANEL_DISPLAY_CTRL);
	reg = FIELD_VALUE(reg, PANEL_DISPLAY_CTRL, DATA, disp);
	POKE32(PANEL_DISPLAY_CTRL, reg);
	primaryWaitVerticalSync(delay);

	reg = PEEK32(PANEL_DISPLAY_CTRL);
	reg = FIELD_VALUE(reg, PANEL_DISPLAY_CTRL, VBIASEN, disp);
	POKE32(PANEL_DISPLAY_CTRL, reg);
	primaryWaitVerticalSync(delay);


	reg = PEEK32(PANEL_DISPLAY_CTRL);
	reg = FIELD_VALUE(reg, PANEL_DISPLAY_CTRL, FPEN, disp);
	POKE32(PANEL_DISPLAY_CTRL, reg);
	primaryWaitVerticalSync(delay);

}

void ddk750_setLogicalDispOut(disp_output_t output)
{
	unsigned int reg;
	if(output & PNL_2_USAGE){
		/* set panel path controller select */
		reg = PEEK32(PANEL_DISPLAY_CTRL);
		reg = FIELD_VALUE(reg, PANEL_DISPLAY_CTRL, SELECT, (output & PNL_2_MASK)>>PNL_2_OFFSET);
		POKE32(PANEL_DISPLAY_CTRL, reg);
	}

	if(output & CRT_2_USAGE){
		/* set crt path controller select */
		reg = PEEK32(CRT_DISPLAY_CTRL);
		reg = FIELD_VALUE(reg, CRT_DISPLAY_CTRL, SELECT, (output & CRT_2_MASK)>>CRT_2_OFFSET);
		/*se blank off */
		reg = FIELD_SET(reg, CRT_DISPLAY_CTRL, BLANK, OFF);
		POKE32(CRT_DISPLAY_CTRL, reg);

	}

	if(output & PRI_TP_USAGE){
		/* set primary timing and plane en_bit */
		setDisplayControl(0, (output&PRI_TP_MASK)>>PRI_TP_OFFSET);
	}

	if(output & SEC_TP_USAGE){
		/* set secondary timing and plane en_bit*/
		setDisplayControl(1, (output&SEC_TP_MASK)>>SEC_TP_OFFSET);
	}

	if(output & PNL_SEQ_USAGE){
		/* set  panel sequence */
		swPanelPowerSequence((output&PNL_SEQ_MASK)>>PNL_SEQ_OFFSET, 4);
	}

	if(output & DAC_USAGE)
		setDAC((output & DAC_MASK)>>DAC_OFFSET);

	if(output & DPMS_USAGE)
		ddk750_setDPMS((output & DPMS_MASK) >> DPMS_OFFSET);
}


int ddk750_initDVIDisp(void)
{
    /* Initialize DVI. If the dviInit fail and the VendorID or the DeviceID are
       not zeroed, then set the failure flag. If it is zeroe, it might mean
       that the system is in Dual CRT Monitor configuration. */

    /* De-skew enabled with default 111b value.
       This will fix some artifacts problem in some mode on board 2.2.
       Somehow this fix does not affect board 2.1.
     */
    if ((dviInit(1,  /* Select Rising Edge */
                1,  /* Select 24-bit bus */
                0,  /* Select Single Edge clock */
                1,  /* Enable HSync as is */
                1,  /* Enable VSync as is */
                1,  /* Enable De-skew */
                7,  /* Set the de-skew setting to maximum setup */
                1,  /* Enable continuous Sync */
                1,  /* Enable PLL Filter */
                4   /* Use the recommended value for PLL Filter value */
        ) != 0) && (dviGetVendorID() != 0x0000) && (dviGetDeviceID() != 0x0000))
    {
        return (-1);
    }

    /* TODO: Initialize other display component */

    /* Success */
    return 0;

}

