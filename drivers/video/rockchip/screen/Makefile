obj-$(CONFIG_LCD_GENERAL)			+= lcd_general.o
obj-$(CONFIG_LCD_MIPI)	+= lcd_mipi.o


quiet_cmd_gen = GEN     $@
      cmd_gen = cmp -s $< $@ || cp $< $@

lcd-obj := $(filter lcd_%.o,$(obj-y))
lcd-cfile := $(patsubst %.o,%.c,$(lcd-obj))
lcd-cpath := $(src)/$(lcd-cfile)

obj-y := $(filter-out $(lcd-obj),$(obj-y))

$(obj)/lcd.h: $(lcd-cpath)  FORCE
	$(call if_changed,gen)

$(obj)/rk_screen.o: $(obj)/lcd.h
obj-y += rk_screen.o

clean-files := lcd.h
