# SPDX-License-Identifier: GPL-2.0

obj-y += keymaps/

obj-$(CONFIG_RC_CORE) += rc-core.o
rc-core-y := rc-main.o rc-ir-raw.o
rc-core-$(CONFIG_LIRC) += lirc_dev.o
rc-core-$(CONFIG_MEDIA_CEC_RC) += keymaps/rc-cec.o
rc-core-$(CONFIG_BPF_LIRC_MODE2) += bpf-lirc.o
obj-$(CONFIG_IR_NEC_DECODER) += ir-nec-decoder.o
obj-$(CONFIG_IR_RC5_DECODER) += ir-rc5-decoder.o
obj-$(CONFIG_IR_RC6_DECODER) += ir-rc6-decoder.o
obj-$(CONFIG_IR_JVC_DECODER) += ir-jvc-decoder.o
obj-$(CONFIG_IR_SONY_DECODER) += ir-sony-decoder.o
obj-$(CONFIG_IR_SANYO_DECODER) += ir-sanyo-decoder.o
obj-$(CONFIG_IR_SHARP_DECODER) += ir-sharp-decoder.o
obj-$(CONFIG_IR_MCE_KBD_DECODER) += ir-mce_kbd-decoder.o
obj-$(CONFIG_IR_XMP_DECODER) += ir-xmp-decoder.o
obj-$(CONFIG_IR_IMON_DECODER) += ir-imon-decoder.o
obj-$(CONFIG_IR_RCMM_DECODER) += ir-rcmm-decoder.o

# stand-alone IR receivers/transmitters
obj-$(CONFIG_RC_ATI_REMOTE) += ati_remote.o
obj-$(CONFIG_IR_HIX5HD2) += ir-hix5hd2.o
obj-$(CONFIG_IR_IMON) += imon.o
obj-$(CONFIG_IR_IMON_RAW) += imon_raw.o
obj-$(CONFIG_IR_ITE_CIR) += ite-cir.o
obj-$(CONFIG_IR_MCEUSB) += mceusb.o
obj-$(CONFIG_IR_FINTEK) += fintek-cir.o
obj-$(CONFIG_IR_MESON) += meson-ir.o
obj-$(CONFIG_IR_NUVOTON) += nuvoton-cir.o
obj-$(CONFIG_IR_ENE) += ene_ir.o
obj-$(CONFIG_IR_REDRAT3) += redrat3.o
obj-$(CONFIG_IR_RX51) += ir-rx51.o
obj-$(CONFIG_IR_SPI) += ir-spi.o
obj-$(CONFIG_IR_STREAMZAP) += streamzap.o
obj-$(CONFIG_IR_WINBOND_CIR) += winbond-cir.o
obj-$(CONFIG_RC_LOOPBACK) += rc-loopback.o
obj-$(CONFIG_IR_GPIO_CIR) += gpio-ir-recv.o
obj-$(CONFIG_IR_GPIO_TX) += gpio-ir-tx.o
obj-$(CONFIG_IR_PWM_TX) += pwm-ir-tx.o
obj-$(CONFIG_IR_IGORPLUGUSB) += igorplugusb.o
obj-$(CONFIG_IR_IGUANA) += iguanair.o
obj-$(CONFIG_IR_TTUSBIR) += ttusbir.o
obj-$(CONFIG_RC_ST) += st_rc.o
obj-$(CONFIG_IR_SUNXI) += sunxi-cir.o
obj-$(CONFIG_IR_IMG) += img-ir/
obj-$(CONFIG_IR_SERIAL) += serial_ir.o
obj-$(CONFIG_IR_SIR) += sir_ir.o
obj-$(CONFIG_IR_MTK) += mtk-cir.o
obj-$(CONFIG_IR_ZX) += zx-irdec.o
obj-$(CONFIG_IR_TANGO) += tango-ir.o
obj-$(CONFIG_RC_XBOX_DVD) += xbox_remote.o
obj-$(CONFIG_IR_TOY) += ir_toy.o
