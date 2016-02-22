ccflags-y := -Iinclude/drm -Idrivers/gpu/drm/msm

obj-y := hdmi.o hdmi_phy.o hdmi_audio.o hdmi_bridge.o hdmi_connector.o \
	 hdmi_hdcp.o hdmi_i2c.o hdmi_phy_8960.o hdmi_phy_8x60.o hdmi_phy_8x74.o

obj-$(CONFIG_COMMON_CLK) += hdmi_pll_8960.o hdmi_phy_8996.o
