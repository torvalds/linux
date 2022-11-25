#ifndef DW_HDMI_CEC_H
#define DW_HDMI_CEC_H

struct dw_hdmi;

#define CEC_EN			BIT(0)
#define CEC_WAKE		BIT(1)

struct dw_hdmi_cec_ops {
	void (*write)(struct dw_hdmi *hdmi, u8 val, int offset);
	u8 (*read)(struct dw_hdmi *hdmi, int offset);
	void (*enable)(struct dw_hdmi *hdmi);
	void (*disable)(struct dw_hdmi *hdmi);
	void (*mod)(struct dw_hdmi *hdmi, u8 data, u8 mask, unsigned int reg);
};

struct dw_hdmi_cec_data {
	struct dw_hdmi *hdmi;
	const struct dw_hdmi_cec_ops *ops;
	int irq;
	int wake_irq;
};

void dw_hdmi_hpd_wake_up(struct platform_device *pdev);

#endif
