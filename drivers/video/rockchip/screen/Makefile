obj-$(CONFIG_LCD_NULL) += lcd_null.o

obj-$(CONFIG_LCD_RK2928) += lcd_rk2928.o

obj-$(CONFIG_LCD_TD043MGEA1) += lcd_td043mgea1.o
obj-$(CONFIG_LCD_HSD070IDW1) += lcd_hsd800x480.o
obj-$(CONFIG_LCD_HL070VM4AU) += lcd_hl070vm4.o
obj-$(CONFIG_LCD_BYD8688FTGF) += lcd_byd1024x600.o
obj-$(CONFIG_LCD_LG_LP097X02)+=	lcd_LG_LP097X02.o
obj-$(CONFIG_LCD_TJ048NC01CA) 		+= lcd_tj048nc01ca.o

obj-$(CONFIG_LCD_A060SE02) += lcd_a060se02.o
obj-$(CONFIG_LCD_S1D13521) += lcd_s1d13521.o
obj-$(CONFIG_LCD_NT35582) += lcd_nt35582.o
obj-$(CONFIG_LCD_NT35580) += lcd_nt35580.o
obj-$(CONFIG_LCD_IPS1P5680_V1_E) += lcd_ips1p5680_v1_e.o
obj-$(CONFIG_LCD_RGB_TFT480800_25_E) += lcd_rgb_tft480800_25_e.o
obj-$(CONFIG_LCD_MCU_TFT480800_25_E) += lcd_mcu_tft480800_25_e.o
obj-$(CONFIG_LCD_LS035Y8DX02A) += lcd_ls035y8dx02a.o
obj-$(CONFIG_LCD_LS035Y8DX04A) += lcd_ls035y8dx04a.o
obj-$(CONFIG_LCD_CPTCLAA038LA31XE) += lcd_CPTclaa038la31xe.o


obj-$(CONFIG_LCD_HX8357) += lcd_hx8357.o
obj-$(CONFIG_LCD_HSD100PXN) += lcd_hsd100pxn.o
obj-$(CONFIG_LCD_HDMI_1366x768) += lcd_hdmi_1366x768.o
obj-$(CONFIG_LCD_HDMI_1280x800) += lcd_hdmi_1280x800.o
obj-$(CONFIG_LCD_HDMI_1024x768) += lcd_hdmi_1024x768.o
obj-$(CONFIG_LCD_HDMI_800x480) += lcd_hdmi_800x480.o
obj-$(CONFIG_LCD_B101AW06) += lcd_B101AW06.o
obj-$(CONFIG_LCD_NT35510) += lcd_nt35510.o
obj-$(CONFIG_LCD_ILI9803_CPT4_3) += lcd_ili9803_cpt4_3.o
obj-$(CONFIG_LCD_RGB_TFT480800_25_E) += lcd_rgb_tft480800_25_e.o
obj-$(CONFIG_LCD_LS035Y8DX02A) += lcd_ls035y8dx02a.o
obj-$(CONFIG_LCD_IPS1P5680_V1_E) += lcd_ips1p5680_v1_e.o
obj-$(CONFIG_LCD_MCU_TFT480800_25_E) += lcd_mcu_tft480800_25_e.o
obj-$(CONFIG_LCD_AT070TNA2)    += lcd_AT070TNA2.o
obj-$(CONFIG_LCD_TX23D88VM) += lcd_tx23d88vm.o
obj-$(CONFIG_LCD_AT070TN93)    += lcd_at070tn93.o
obj-$(CONFIG_LCD_A050VL01)    += lcd_A050VL01.o
obj-$(CONFIG_LCD_B101EW05)    += lcd_b101ew05.o
obj-$(CONFIG_LCD_RK3168M_B101EW05) += lcd_hdmi_rk3168m_b101ew05.o
obj-$(CONFIG_LCD_HJ050NA_06A)    += lcd_hj050na_06a.o
obj-$(CONFIG_LCD_HSD100PXN_FOR_TDW851) += lcd_hsd100pxn_for_tdw851.o
obj-$(CONFIG_LCD_HV070WSA100)    += lcd_hv070wsa.o
obj-$(CONFIG_LCD_COMMON)    += lcd_common.o
obj-$(CONFIG_LCD_RK2928_A720) += lcd_YQ70CPT9160.o
obj-$(CONFIG_LCD_RK3168_AUO_A080SN03) += lcd_AUO_A080SN03.o
obj-$(CONFIG_LCD_RK2926_V86) += lcd_YQ70CPT9160_v86.o
obj-$(CONFIG_LCD_RK3168_86V) += lcd_YQ70CPT9160_rk3168_86v.o
obj-$(CONFIG_LCD_HSD07PFW1) += lcd_hdmi_1024x600.o
obj-$(CONFIG_LCD_HJ080NA)    += lcd_hj080na.o
obj-$(CONFIG_LCD_HJ101NA)   += lcd_hj101na.o
obj-$(CONFIG_LCD_AUTO)   += lcd_auto.o
obj-$(CONFIG_LCD_I30_800X480)   += lcd_I30_800x480.o
obj-$(CONFIG_LCD_TL5001_MIPI)    += lcd_tl5001_mipi.o
obj-$(CONFIG_LCD_LP097QX1)    += lcd_LP097QX1.o
obj-$(CONFIG_LCD_DS1006H)    += lcd_ds1006h.o
obj-$(CONFIG_LCD_B101UANO_1920x1200)	+= lcd_b101uano_1920x1200.o
obj-$(CONFIG_LCD_E242868_1024X600) += lcd_E242868_rk3168_86v.o
obj-$(CONFIG_LCD_WY_800X480)    += lcd_wy_800x480.o
obj-$(CONFIG_LCD_HH070D_LVDS)   += lcd_hh070d_lvds.o
obj-$(CONFIG_LCD_MQ0801D)    += lcd_mq0801d.o
obj-$(CONFIG_LCD_LD089WU1_MIPI)   += lcd_LD089WU1_mipi.o


quiet_cmd_gen = GEN     $@
      cmd_gen = cp -a $< $@

lcd-obj := $(filter lcd_%.o,$(obj-y))
lcd-cfile := $(patsubst %.o,%.c,$(lcd-obj))
lcd-cpath := $(src)/$(lcd-cfile)

obj-y := $(filter-out $(lcd-obj),$(obj-y))

$(obj)/lcd.h: $(lcd-cpath)  FORCE
	$(call if_changed,gen)

$(obj)/rk_screen.o: $(obj)/lcd.h
obj-y += rk_screen.o

clean-files := lcd.h



