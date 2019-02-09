#ifndef DW_HDMI_AUDIO_H
#define DW_HDMI_AUDIO_H

struct dw_hdmi;

struct dw_hdmi_audio_data {
	phys_addr_t phys;
	void __iomem *base;
	int irq;
	struct dw_hdmi *hdmi;
	u8 *eld;
};

#endif
