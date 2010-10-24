wl1271-objs		= wl1271_main.o  wl1271_cmd.o wl1271_io.o \
			  wl1271_event.o wl1271_tx.o  wl1271_rx.o   \
			  wl1271_ps.o    wl1271_acx.o wl1271_boot.o \
			  wl1271_init.o  wl1271_debugfs.o wl1271_scan.o

wl1271-$(CONFIG_NL80211_TESTMODE)	+= wl1271_testmode.o
obj-$(CONFIG_WL1271)	+= wl1271.o
obj-$(CONFIG_WL1271_SPI)	+= wl1271_spi.o
obj-$(CONFIG_WL1271_SDIO)	+= wl1271_sdio.o

# small builtin driver bit
obj-$(CONFIG_WL12XX_PLATFORM_DATA)	+= wl12xx_platform_data.o
