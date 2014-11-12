rc-core-objs	:= rc-main.o rc-ir-raw.o

obj-y += keymaps/

obj-$(CONFIG_RC_CORE) += rc-core.o
obj-$(CONFIG_LIRC) += lirc_dev.o
obj-$(CONFIG_IR_NEC_DECODER) += ir-nec-decoder.o
obj-$(CONFIG_IR_RC5_DECODER) += ir-rc5-decoder.o
obj-$(CONFIG_IR_RC6_DECODER) += ir-rc6-decoder.o
obj-$(CONFIG_IR_JVC_DECODER) += ir-jvc-decoder.o
obj-$(CONFIG_IR_SONY_DECODER) += ir-sony-decoder.o
obj-$(CONFIG_IR_SANYO_DECODER) += ir-sanyo-decoder.o
obj-$(CONFIG_IR_SHARP_DECODER) += ir-sharp-decoder.o
obj-$(CONFIG_IR_MCE_KBD_DECODER) += ir-mce_kbd-decoder.o
obj-$(CONFIG_IR_LIRC_CODEC) += ir-lirc-codec.o
obj-$(CONFIG_IR_XMP_DECODER) += ir-xmp-decoder.o

# stand-alone IR receivers/transmitters
obj-$(CONFIG_RC_ATI_REMOTE) += ati_remote.o
obj-$(CONFIG_IR_HIX5HD2) += ir-hix5hd2.o
obj-$(CONFIG_IR_IMON) += imon.o
obj-$(CONFIG_IR_ITE_CIR) += ite-cir.o
obj-$(CONFIG_IR_MCEUSB) += mceusb.o
obj-$(CONFIG_IR_FINTEK) += fintek-cir.o
obj-$(CONFIG_IR_NUVOTON) += nuvoton-cir.o
obj-$(CONFIG_IR_ENE) += ene_ir.o
obj-$(CONFIG_IR_REDRAT3) += redrat3.o
obj-$(CONFIG_IR_RX51) += ir-rx51.o
obj-$(CONFIG_IR_STREAMZAP) += streamzap.o
obj-$(CONFIG_IR_WINBOND_CIR) += winbond-cir.o
obj-$(CONFIG_RC_LOOPBACK) += rc-loopback.o
obj-$(CONFIG_IR_GPIO_CIR) += gpio-ir-recv.o
obj-$(CONFIG_IR_IGUANA) += iguanair.o
obj-$(CONFIG_IR_TTUSBIR) += ttusbir.o
obj-$(CONFIG_RC_ST) += st_rc.o
obj-$(CONFIG_IR_SUNXI) += sunxi-cir.o
obj-$(CONFIG_IR_IMG) += img-ir/
