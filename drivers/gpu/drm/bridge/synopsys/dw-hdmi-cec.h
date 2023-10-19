#ifndef DW_HDMI_CEC_H
#define DW_HDMI_CEC_H

struct dw_hdmi;

struct dw_hdmi_cec_ops {
	void (*write)(struct dw_hdmi *hdmi, u8 val, int offset);
	u8 (*read)(struct dw_hdmi *hdmi, int offset);
	void (*enable)(struct dw_hdmi *hdmi);
	void (*disable)(struct dw_hdmi *hdmi);
};

struct dw_hdmi_cec_data {
	struct dw_hdmi *hdmi;
	const struct dw_hdmi_cec_ops *ops;
	int irq;
};

#endif
