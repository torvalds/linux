/*
 * Copyright (C) STMicroelectronics SA 2014
 * Author: Vincent Abriou <vincent.abriou@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include "sti_hdmi_tx3g0c55phy.h"

#define HDMI_SRZ_PLL_CFG                0x0504
#define HDMI_SRZ_TAP_1                  0x0508
#define HDMI_SRZ_TAP_2                  0x050C
#define HDMI_SRZ_TAP_3                  0x0510
#define HDMI_SRZ_CTRL                   0x0514

#define HDMI_SRZ_PLL_CFG_POWER_DOWN     BIT(0)
#define HDMI_SRZ_PLL_CFG_VCOR_SHIFT     1
#define HDMI_SRZ_PLL_CFG_VCOR_425MHZ    0
#define HDMI_SRZ_PLL_CFG_VCOR_850MHZ    1
#define HDMI_SRZ_PLL_CFG_VCOR_1700MHZ   2
#define HDMI_SRZ_PLL_CFG_VCOR_3000MHZ   3
#define HDMI_SRZ_PLL_CFG_VCOR_MASK      3
#define HDMI_SRZ_PLL_CFG_VCOR(x)        (x << HDMI_SRZ_PLL_CFG_VCOR_SHIFT)
#define HDMI_SRZ_PLL_CFG_NDIV_SHIFT     8
#define HDMI_SRZ_PLL_CFG_NDIV_MASK      (0x1F << HDMI_SRZ_PLL_CFG_NDIV_SHIFT)
#define HDMI_SRZ_PLL_CFG_MODE_SHIFT     16
#define HDMI_SRZ_PLL_CFG_MODE_13_5_MHZ  0x1
#define HDMI_SRZ_PLL_CFG_MODE_25_2_MHZ  0x4
#define HDMI_SRZ_PLL_CFG_MODE_27_MHZ    0x5
#define HDMI_SRZ_PLL_CFG_MODE_33_75_MHZ 0x6
#define HDMI_SRZ_PLL_CFG_MODE_40_5_MHZ  0x7
#define HDMI_SRZ_PLL_CFG_MODE_54_MHZ    0x8
#define HDMI_SRZ_PLL_CFG_MODE_67_5_MHZ  0x9
#define HDMI_SRZ_PLL_CFG_MODE_74_25_MHZ 0xA
#define HDMI_SRZ_PLL_CFG_MODE_81_MHZ    0xB
#define HDMI_SRZ_PLL_CFG_MODE_82_5_MHZ  0xC
#define HDMI_SRZ_PLL_CFG_MODE_108_MHZ   0xD
#define HDMI_SRZ_PLL_CFG_MODE_148_5_MHZ 0xE
#define HDMI_SRZ_PLL_CFG_MODE_165_MHZ   0xF
#define HDMI_SRZ_PLL_CFG_MODE_MASK      0xF
#define HDMI_SRZ_PLL_CFG_MODE(x)        (x << HDMI_SRZ_PLL_CFG_MODE_SHIFT)

#define HDMI_SRZ_CTRL_POWER_DOWN        (1 << 0)
#define HDMI_SRZ_CTRL_EXTERNAL_DATA_EN  (1 << 1)

/* sysconf registers */
#define HDMI_REJECTION_PLL_CONFIGURATION 0x0858	/* SYSTEM_CONFIG2534 */
#define HDMI_REJECTION_PLL_STATUS        0x0948	/* SYSTEM_CONFIG2594 */

#define REJECTION_PLL_HDMI_ENABLE_SHIFT 0
#define REJECTION_PLL_HDMI_ENABLE_MASK  (0x1 << REJECTION_PLL_HDMI_ENABLE_SHIFT)
#define REJECTION_PLL_HDMI_PDIV_SHIFT   24
#define REJECTION_PLL_HDMI_PDIV_MASK    (0x7 << REJECTION_PLL_HDMI_PDIV_SHIFT)
#define REJECTION_PLL_HDMI_NDIV_SHIFT   16
#define REJECTION_PLL_HDMI_NDIV_MASK    (0xFF << REJECTION_PLL_HDMI_NDIV_SHIFT)
#define REJECTION_PLL_HDMI_MDIV_SHIFT   8
#define REJECTION_PLL_HDMI_MDIV_MASK    (0xFF << REJECTION_PLL_HDMI_MDIV_SHIFT)

#define REJECTION_PLL_HDMI_REJ_PLL_LOCK BIT(0)

#define HDMI_TIMEOUT_PLL_LOCK  50   /*milliseconds */

/**
 * pll mode structure
 *
 * A pointer to an array of these structures is passed to a TMDS (HDMI) output
 * via the control interface to provide board and SoC specific
 * configurations of the HDMI PHY. Each entry in the array specifies a hardware
 * specific configuration for a given TMDS clock frequency range. The array
 * should be terminated with an entry that has all fields set to zero.
 *
 * @min: Lower bound of TMDS clock frequency this entry applies to
 * @max: Upper bound of TMDS clock frequency this entry applies to
 * @mode: SoC specific register configuration
 */
struct pllmode {
	u32 min;
	u32 max;
	u32 mode;
};

#define NB_PLL_MODE 7
static struct pllmode pllmodes[NB_PLL_MODE] = {
	{13500000, 13513500, HDMI_SRZ_PLL_CFG_MODE_13_5_MHZ},
	{25174800, 25200000, HDMI_SRZ_PLL_CFG_MODE_25_2_MHZ},
	{27000000, 27027000, HDMI_SRZ_PLL_CFG_MODE_27_MHZ},
	{54000000, 54054000, HDMI_SRZ_PLL_CFG_MODE_54_MHZ},
	{72000000, 74250000, HDMI_SRZ_PLL_CFG_MODE_74_25_MHZ},
	{108000000, 108108000, HDMI_SRZ_PLL_CFG_MODE_108_MHZ},
	{148351648, 297000000, HDMI_SRZ_PLL_CFG_MODE_148_5_MHZ}
};

#define NB_HDMI_PHY_CONFIG 5
static struct hdmi_phy_config hdmiphy_config[NB_HDMI_PHY_CONFIG] = {
	{0, 40000000, {0x00101010, 0x00101010, 0x00101010, 0x02} },
	{40000000, 140000000, {0x00111111, 0x00111111, 0x00111111, 0x02} },
	{140000000, 160000000, {0x00131313, 0x00101010, 0x00101010, 0x02} },
	{160000000, 250000000, {0x00131313, 0x00111111, 0x00111111, 0x03FE} },
	{250000000, 300000000, {0x00151515, 0x00101010, 0x00101010, 0x03FE} },
};

#define PLL_CHANGE_DELAY	1 /* ms */

/**
 * Disable the pll rejection
 *
 * @hdmi: pointer on the hdmi internal structure
 *
 * return true if the pll has been disabled
 */
static bool disable_pll_rejection(struct sti_hdmi *hdmi)
{
	u32 val;

	DRM_DEBUG_DRIVER("\n");

	val = readl(hdmi->syscfg + HDMI_REJECTION_PLL_CONFIGURATION);
	val &= ~REJECTION_PLL_HDMI_ENABLE_MASK;
	writel(val, hdmi->syscfg + HDMI_REJECTION_PLL_CONFIGURATION);

	msleep(PLL_CHANGE_DELAY);
	val = readl(hdmi->syscfg + HDMI_REJECTION_PLL_STATUS);

	return !(val & REJECTION_PLL_HDMI_REJ_PLL_LOCK);
}

/**
 * Enable the old BCH/rejection PLL is now reused to provide the CLKPXPLL
 * clock input to the new PHY PLL that generates the serializer clock
 * (TMDS*10) and the TMDS clock which is now fed back into the HDMI
 * formatter instead of the TMDS clock line from ClockGenB.
 *
 * @hdmi: pointer on the hdmi internal structure
 *
 * return true if pll has been correctly set
 */
static bool enable_pll_rejection(struct sti_hdmi *hdmi)
{
	unsigned int inputclock;
	u32 mdiv, ndiv, pdiv, val;

	DRM_DEBUG_DRIVER("\n");

	if (!disable_pll_rejection(hdmi))
		return false;

	inputclock = hdmi->mode.clock * 1000;

	DRM_DEBUG_DRIVER("hdmi rejection pll input clock = %dHz\n", inputclock);


	/* Power up the HDMI rejection PLL
	 * Note: On this SoC (stiH416) we are forced to have the input clock
	 * be equal to the HDMI pixel clock.
	 *
	 * The values here have been suggested by validation however they are
	 * still provisional and subject to change.
	 *
	 * PLLout = (Fin*Mdiv) / ((2 * Ndiv) / 2^Pdiv)
	 */
	if (inputclock < 50000000) {
		/*
		 * For slower clocks we need to multiply more to keep the
		 * internal VCO frequency within the physical specification
		 * of the PLL.
		 */
		pdiv = 4;
		ndiv = 240;
		mdiv = 30;
	} else {
		pdiv = 2;
		ndiv = 60;
		mdiv = 30;
	}

	val = readl(hdmi->syscfg + HDMI_REJECTION_PLL_CONFIGURATION);

	val &= ~(REJECTION_PLL_HDMI_PDIV_MASK |
		REJECTION_PLL_HDMI_NDIV_MASK |
		REJECTION_PLL_HDMI_MDIV_MASK |
		REJECTION_PLL_HDMI_ENABLE_MASK);

	val |=	(pdiv << REJECTION_PLL_HDMI_PDIV_SHIFT) |
		(ndiv << REJECTION_PLL_HDMI_NDIV_SHIFT) |
		(mdiv << REJECTION_PLL_HDMI_MDIV_SHIFT) |
		(0x1 << REJECTION_PLL_HDMI_ENABLE_SHIFT);

	writel(val, hdmi->syscfg + HDMI_REJECTION_PLL_CONFIGURATION);

	msleep(PLL_CHANGE_DELAY);
	val = readl(hdmi->syscfg + HDMI_REJECTION_PLL_STATUS);

	return (val & REJECTION_PLL_HDMI_REJ_PLL_LOCK);
}

/**
 * Start hdmi phy macro cell tx3g0c55
 *
 * @hdmi: pointer on the hdmi internal structure
 *
 * Return false if an error occur
 */
static bool sti_hdmi_tx3g0c55phy_start(struct sti_hdmi *hdmi)
{
	u32 ckpxpll = hdmi->mode.clock * 1000;
	u32 val, tmdsck, freqvco, pllctrl = 0;
	unsigned int i;

	if (!enable_pll_rejection(hdmi))
		return false;

	DRM_DEBUG_DRIVER("ckpxpll = %dHz\n", ckpxpll);

	/* Assuming no pixel repetition and 24bits color */
	tmdsck = ckpxpll;
	pllctrl = 2 << HDMI_SRZ_PLL_CFG_NDIV_SHIFT;

	/*
	 * Setup the PLL mode parameter based on the ckpxpll. If we haven't got
	 * a clock frequency supported by one of the specific PLL modes then we
	 * will end up using the generic mode (0) which only supports a 10x
	 * multiplier, hence only 24bit color.
	 */
	for (i = 0; i < NB_PLL_MODE; i++) {
		if (ckpxpll >= pllmodes[i].min && ckpxpll <= pllmodes[i].max)
			pllctrl |= HDMI_SRZ_PLL_CFG_MODE(pllmodes[i].mode);
	}

	freqvco = tmdsck * 10;
	if (freqvco <= 425000000UL)
		pllctrl |= HDMI_SRZ_PLL_CFG_VCOR(HDMI_SRZ_PLL_CFG_VCOR_425MHZ);
	else if (freqvco <= 850000000UL)
		pllctrl |= HDMI_SRZ_PLL_CFG_VCOR(HDMI_SRZ_PLL_CFG_VCOR_850MHZ);
	else if (freqvco <= 1700000000UL)
		pllctrl |= HDMI_SRZ_PLL_CFG_VCOR(HDMI_SRZ_PLL_CFG_VCOR_1700MHZ);
	else if (freqvco <= 2970000000UL)
		pllctrl |= HDMI_SRZ_PLL_CFG_VCOR(HDMI_SRZ_PLL_CFG_VCOR_3000MHZ);
	else {
		DRM_ERROR("PHY serializer clock out of range\n");
		goto err;
	}

	/*
	 * Configure and power up the PHY PLL
	 */
	hdmi->event_received = false;
	DRM_DEBUG_DRIVER("pllctrl = 0x%x\n", pllctrl);
	hdmi_write(hdmi, pllctrl, HDMI_SRZ_PLL_CFG);

	/* wait PLL interrupt */
	wait_event_interruptible_timeout(hdmi->wait_event,
					 hdmi->event_received == true,
					 msecs_to_jiffies
					 (HDMI_TIMEOUT_PLL_LOCK));

	if ((hdmi_read(hdmi, HDMI_STA) & HDMI_STA_DLL_LCK) == 0) {
		DRM_ERROR("hdmi phy pll not locked\n");
		goto err;
	}

	DRM_DEBUG_DRIVER("got PHY PLL Lock\n");

	/*
	 * To configure the source termination and pre-emphasis appropriately
	 * for different high speed TMDS clock frequencies a phy configuration
	 * table must be provided, tailored to the SoC and board combination.
	 */
	for (i = 0; i < NB_HDMI_PHY_CONFIG; i++) {
		if ((hdmiphy_config[i].min_tmds_freq <= tmdsck) &&
		    (hdmiphy_config[i].max_tmds_freq >= tmdsck)) {
			val = hdmiphy_config[i].config[0];
			hdmi_write(hdmi, val, HDMI_SRZ_TAP_1);
			val = hdmiphy_config[i].config[1];
			hdmi_write(hdmi, val, HDMI_SRZ_TAP_2);
			val = hdmiphy_config[i].config[2];
			hdmi_write(hdmi, val, HDMI_SRZ_TAP_3);
			val = hdmiphy_config[i].config[3];
			val |= HDMI_SRZ_CTRL_EXTERNAL_DATA_EN;
			val &= ~HDMI_SRZ_CTRL_POWER_DOWN;
			hdmi_write(hdmi, val, HDMI_SRZ_CTRL);

			DRM_DEBUG_DRIVER("serializer cfg 0x%x 0x%x 0x%x 0x%x\n",
					 hdmiphy_config[i].config[0],
					 hdmiphy_config[i].config[1],
					 hdmiphy_config[i].config[2],
					 hdmiphy_config[i].config[3]);
			return true;
		}
	}

	/*
	 * Default, power up the serializer with no pre-emphasis or source
	 * termination.
	 */
	hdmi_write(hdmi, 0x0, HDMI_SRZ_TAP_1);
	hdmi_write(hdmi, 0x0, HDMI_SRZ_TAP_2);
	hdmi_write(hdmi, 0x0, HDMI_SRZ_TAP_3);
	hdmi_write(hdmi, HDMI_SRZ_CTRL_EXTERNAL_DATA_EN, HDMI_SRZ_CTRL);

	return true;

err:
	disable_pll_rejection(hdmi);

	return false;
}

/**
 * Stop hdmi phy macro cell tx3g0c55
 *
 * @hdmi: pointer on the hdmi internal structure
 */
static void sti_hdmi_tx3g0c55phy_stop(struct sti_hdmi *hdmi)
{
	DRM_DEBUG_DRIVER("\n");

	hdmi->event_received = false;

	hdmi_write(hdmi, HDMI_SRZ_CTRL_POWER_DOWN, HDMI_SRZ_CTRL);
	hdmi_write(hdmi, HDMI_SRZ_PLL_CFG_POWER_DOWN, HDMI_SRZ_PLL_CFG);

	/* wait PLL interrupt */
	wait_event_interruptible_timeout(hdmi->wait_event,
					 hdmi->event_received == true,
					 msecs_to_jiffies
					 (HDMI_TIMEOUT_PLL_LOCK));

	if (hdmi_read(hdmi, HDMI_STA) & HDMI_STA_DLL_LCK)
		DRM_ERROR("hdmi phy pll not well disabled\n");

	disable_pll_rejection(hdmi);
}

struct hdmi_phy_ops tx3g0c55phy_ops = {
	.start = sti_hdmi_tx3g0c55phy_start,
	.stop = sti_hdmi_tx3g0c55phy_stop,
};
