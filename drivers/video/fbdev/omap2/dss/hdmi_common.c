
#define DSS_SUBSYS_NAME "HDMI"

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/of.h>
#include <video/omapdss.h>

#include "hdmi.h"

int hdmi_parse_lanes_of(struct platform_device *pdev, struct device_node *ep,
	struct hdmi_phy_data *phy)
{
	struct property *prop;
	int r, len;

	prop = of_find_property(ep, "lanes", &len);
	if (prop) {
		u32 lanes[8];

		if (len / sizeof(u32) != ARRAY_SIZE(lanes)) {
			dev_err(&pdev->dev, "bad number of lanes\n");
			return -EINVAL;
		}

		r = of_property_read_u32_array(ep, "lanes", lanes,
			ARRAY_SIZE(lanes));
		if (r) {
			dev_err(&pdev->dev, "failed to read lane data\n");
			return r;
		}

		r = hdmi_phy_parse_lanes(phy, lanes);
		if (r) {
			dev_err(&pdev->dev, "failed to parse lane data\n");
			return r;
		}
	} else {
		static const u32 default_lanes[] = { 0, 1, 2, 3, 4, 5, 6, 7 };

		r = hdmi_phy_parse_lanes(phy, default_lanes);
		if (WARN_ON(r)) {
			dev_err(&pdev->dev, "failed to parse lane data\n");
			return r;
		}
	}

	return 0;
}

#if defined(CONFIG_OMAP4_DSS_HDMI_AUDIO)
int hdmi_compute_acr(u32 pclk, u32 sample_freq, u32 *n, u32 *cts)
{
	u32 deep_color;
	bool deep_color_correct = false;

	if (n == NULL || cts == NULL)
		return -EINVAL;

	/* TODO: When implemented, query deep color mode here. */
	deep_color = 100;

	/*
	 * When using deep color, the default N value (as in the HDMI
	 * specification) yields to an non-integer CTS. Hence, we
	 * modify it while keeping the restrictions described in
	 * section 7.2.1 of the HDMI 1.4a specification.
	 */
	switch (sample_freq) {
	case 32000:
	case 48000:
	case 96000:
	case 192000:
		if (deep_color == 125)
			if (pclk == 27027000 || pclk == 74250000)
				deep_color_correct = true;
		if (deep_color == 150)
			if (pclk == 27027000)
				deep_color_correct = true;
		break;
	case 44100:
	case 88200:
	case 176400:
		if (deep_color == 125)
			if (pclk == 27027000)
				deep_color_correct = true;
		break;
	default:
		return -EINVAL;
	}

	if (deep_color_correct) {
		switch (sample_freq) {
		case 32000:
			*n = 8192;
			break;
		case 44100:
			*n = 12544;
			break;
		case 48000:
			*n = 8192;
			break;
		case 88200:
			*n = 25088;
			break;
		case 96000:
			*n = 16384;
			break;
		case 176400:
			*n = 50176;
			break;
		case 192000:
			*n = 32768;
			break;
		default:
			return -EINVAL;
		}
	} else {
		switch (sample_freq) {
		case 32000:
			*n = 4096;
			break;
		case 44100:
			*n = 6272;
			break;
		case 48000:
			*n = 6144;
			break;
		case 88200:
			*n = 12544;
			break;
		case 96000:
			*n = 12288;
			break;
		case 176400:
			*n = 25088;
			break;
		case 192000:
			*n = 24576;
			break;
		default:
			return -EINVAL;
		}
	}
	/* Calculate CTS. See HDMI 1.3a or 1.4a specifications */
	*cts = (pclk/1000) * (*n / 128) * deep_color / (sample_freq / 10);

	return 0;
}
#endif
