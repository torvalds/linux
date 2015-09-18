/*
 * Copyright (C) STMicroelectronics SA 2014
 * Author: Vincent Abriou <vincent.abriou@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include "sti_hdmi_tx3g4c28phy.h"

#define HDMI_SRZ_CFG                             0x504
#define HDMI_SRZ_PLL_CFG                         0x510
#define HDMI_SRZ_ICNTL                           0x518
#define HDMI_SRZ_CALCODE_EXT                     0x520

#define HDMI_SRZ_CFG_EN                          BIT(0)
#define HDMI_SRZ_CFG_DISABLE_BYPASS_SINK_CURRENT BIT(1)
#define HDMI_SRZ_CFG_EXTERNAL_DATA               BIT(16)
#define HDMI_SRZ_CFG_RBIAS_EXT                   BIT(17)
#define HDMI_SRZ_CFG_EN_SINK_TERM_DETECTION      BIT(18)
#define HDMI_SRZ_CFG_EN_BIASRES_DETECTION        BIT(19)
#define HDMI_SRZ_CFG_EN_SRC_TERMINATION          BIT(24)

#define HDMI_SRZ_CFG_INTERNAL_MASK  (HDMI_SRZ_CFG_EN     | \
		HDMI_SRZ_CFG_DISABLE_BYPASS_SINK_CURRENT | \
		HDMI_SRZ_CFG_EXTERNAL_DATA               | \
		HDMI_SRZ_CFG_RBIAS_EXT                   | \
		HDMI_SRZ_CFG_EN_SINK_TERM_DETECTION      | \
		HDMI_SRZ_CFG_EN_BIASRES_DETECTION        | \
		HDMI_SRZ_CFG_EN_SRC_TERMINATION)

#define PLL_CFG_EN                               BIT(0)
#define PLL_CFG_NDIV_SHIFT                       (8)
#define PLL_CFG_IDF_SHIFT                        (16)
#define PLL_CFG_ODF_SHIFT                        (24)

#define ODF_DIV_1                                (0)
#define ODF_DIV_2                                (1)
#define ODF_DIV_4                                (2)
#define ODF_DIV_8                                (3)

#define HDMI_TIMEOUT_PLL_LOCK  50  /*milliseconds */

struct plldividers_s {
	uint32_t min;
	uint32_t max;
	uint32_t idf;
	uint32_t odf;
};

/*
 * Functional specification recommended values
 */
#define NB_PLL_MODE 5
static struct plldividers_s plldividers[NB_PLL_MODE] = {
	{0, 20000000, 1, ODF_DIV_8},
	{20000000, 42500000, 2, ODF_DIV_8},
	{42500000, 85000000, 4, ODF_DIV_4},
	{85000000, 170000000, 8, ODF_DIV_2},
	{170000000, 340000000, 16, ODF_DIV_1}
};

#define NB_HDMI_PHY_CONFIG 2
static struct hdmi_phy_config hdmiphy_config[NB_HDMI_PHY_CONFIG] = {
	{0, 250000000, {0x0, 0x0, 0x0, 0x0} },
	{250000000, 300000000, {0x1110, 0x0, 0x0, 0x0} },
};

/**
 * Start hdmi phy macro cell tx3g4c28
 *
 * @hdmi: pointer on the hdmi internal structure
 *
 * Return false if an error occur
 */
static bool sti_hdmi_tx3g4c28phy_start(struct sti_hdmi *hdmi)
{
	u32 ckpxpll = hdmi->mode.clock * 1000;
	u32 val, tmdsck, idf, odf, pllctrl = 0;
	bool foundplldivides = false;
	int i;

	DRM_DEBUG_DRIVER("ckpxpll = %dHz\n", ckpxpll);

	for (i = 0; i < NB_PLL_MODE; i++) {
		if (ckpxpll >= plldividers[i].min &&
		    ckpxpll < plldividers[i].max) {
			idf = plldividers[i].idf;
			odf = plldividers[i].odf;
			foundplldivides = true;
			break;
		}
	}

	if (!foundplldivides) {
		DRM_ERROR("input TMDS clock speed (%d) not supported\n",
			  ckpxpll);
		goto err;
	}

	/* Assuming no pixel repetition and 24bits color */
	tmdsck = ckpxpll;
	pllctrl |= 40 << PLL_CFG_NDIV_SHIFT;

	if (tmdsck > 340000000) {
		DRM_ERROR("output TMDS clock (%d) out of range\n", tmdsck);
		goto err;
	}

	pllctrl |= idf << PLL_CFG_IDF_SHIFT;
	pllctrl |= odf << PLL_CFG_ODF_SHIFT;

	/*
	 * Configure and power up the PHY PLL
	 */
	hdmi->event_received = false;
	DRM_DEBUG_DRIVER("pllctrl = 0x%x\n", pllctrl);
	hdmi_write(hdmi, (pllctrl | PLL_CFG_EN), HDMI_SRZ_PLL_CFG);

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

	val = (HDMI_SRZ_CFG_EN |
	       HDMI_SRZ_CFG_EXTERNAL_DATA |
	       HDMI_SRZ_CFG_EN_BIASRES_DETECTION |
	       HDMI_SRZ_CFG_EN_SINK_TERM_DETECTION);

	if (tmdsck > 165000000)
		val |= HDMI_SRZ_CFG_EN_SRC_TERMINATION;

	/*
	 * To configure the source termination and pre-emphasis appropriately
	 * for different high speed TMDS clock frequencies a phy configuration
	 * table must be provided, tailored to the SoC and board combination.
	 */
	for (i = 0; i < NB_HDMI_PHY_CONFIG; i++) {
		if ((hdmiphy_config[i].min_tmds_freq <= tmdsck) &&
		    (hdmiphy_config[i].max_tmds_freq >= tmdsck)) {
			val |= (hdmiphy_config[i].config[0]
				& ~HDMI_SRZ_CFG_INTERNAL_MASK);
			hdmi_write(hdmi, val, HDMI_SRZ_CFG);

			val = hdmiphy_config[i].config[1];
			hdmi_write(hdmi, val, HDMI_SRZ_ICNTL);

			val = hdmiphy_config[i].config[2];
			hdmi_write(hdmi, val, HDMI_SRZ_CALCODE_EXT);

			DRM_DEBUG_DRIVER("serializer cfg 0x%x 0x%x 0x%x\n",
					 hdmiphy_config[i].config[0],
					 hdmiphy_config[i].config[1],
					 hdmiphy_config[i].config[2]);
			return true;
		}
	}

	/*
	 * Default, power up the serializer with no pre-emphasis or
	 * output swing correction
	 */
	hdmi_write(hdmi, val,  HDMI_SRZ_CFG);
	hdmi_write(hdmi, 0x0, HDMI_SRZ_ICNTL);
	hdmi_write(hdmi, 0x0, HDMI_SRZ_CALCODE_EXT);

	return true;

err:
	return false;
}

/**
 * Stop hdmi phy macro cell tx3g4c28
 *
 * @hdmi: pointer on the hdmi internal structure
 */
static void sti_hdmi_tx3g4c28phy_stop(struct sti_hdmi *hdmi)
{
	int val = 0;

	DRM_DEBUG_DRIVER("\n");

	hdmi->event_received = false;

	val = HDMI_SRZ_CFG_EN_SINK_TERM_DETECTION;
	val |= HDMI_SRZ_CFG_EN_BIASRES_DETECTION;

	hdmi_write(hdmi, val, HDMI_SRZ_CFG);
	hdmi_write(hdmi, 0, HDMI_SRZ_PLL_CFG);

	/* wait PLL interrupt */
	wait_event_interruptible_timeout(hdmi->wait_event,
					 hdmi->event_received == true,
					 msecs_to_jiffies
					 (HDMI_TIMEOUT_PLL_LOCK));

	if (hdmi_read(hdmi, HDMI_STA) & HDMI_STA_DLL_LCK)
		DRM_ERROR("hdmi phy pll not well disabled\n");
}

struct hdmi_phy_ops tx3g4c28phy_ops = {
	.start = sti_hdmi_tx3g4c28phy_start,
	.stop = sti_hdmi_tx3g4c28phy_stop,
};
