obj-$(CONFIG_AML_HDMI_TX)		+= hdmitx.o

hdmitx-objs := hdmi_tx.o hdmi_tx_video.o hdmi_tx_audio.o hdmi_tx_edid.o hdmi_tx_audio.o hdmi_tx_hdcp.o

ifdef CONFIG_AML_HDMI_TX_NEW_CEC_DRIVER
  hdmitx-objs += amlogic_cec.o
else
  hdmitx-objs += hdmi_tx_cec.o hdmi_cec_key.o
endif

#EXTRA_CFLAGS += -O2
