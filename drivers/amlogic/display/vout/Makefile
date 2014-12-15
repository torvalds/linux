obj-y 	+=display.o
display-objs=vout_notify.o  vout_serve.o
display-objs-$(CONFIG_AM_LCD_OUTPUT)  += vdac_switch.o


obj-$(CONFIG_AM_TV_OUTPUT)  += am_tvout.o
am_tvout-objs :=   tvoutc.o  tvconf.o enc_clk_config.o

obj-$(CONFIG_AM_LCD_OUTPUT)  += lcdoutc.o
obj-$(CONFIG_AM_LCD_OUTPUT)  += lcd_extern/

#EXTRA_CFLAGS += -DCONFIG_PM
