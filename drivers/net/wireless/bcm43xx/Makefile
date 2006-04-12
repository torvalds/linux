obj-$(CONFIG_BCM43XX) += bcm43xx.o
bcm43xx-obj-$(CONFIG_BCM43XX_DEBUG) += bcm43xx_debugfs.o

bcm43xx-obj-$(CONFIG_BCM43XX_DMA) += bcm43xx_dma.o
bcm43xx-obj-$(CONFIG_BCM43XX_PIO) += bcm43xx_pio.o

bcm43xx-objs := bcm43xx_main.o bcm43xx_ilt.o \
		bcm43xx_radio.o bcm43xx_phy.o \
		bcm43xx_power.o bcm43xx_wx.o \
		bcm43xx_leds.o bcm43xx_ethtool.o \
		bcm43xx_xmit.o bcm43xx_sysfs.o \
		$(bcm43xx-obj-y)
